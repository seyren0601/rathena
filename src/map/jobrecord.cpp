// Copyright (c) rAthena Dev Teams - Licensed under GNU GPL
// For more information, see LICENCE in the main folder

// [Job Record] map-server side of the job record system.
// See docs/subsystems/map/job-record-system-design.md
//
// Phase 3: record list at login, char-server packets, and the write queue that is
// re-sent with every character save until the char-server acknowledges it.
// Phase 4: switch engine (levels, exp, stats, points, skills, job change levels, hotkeys, look).

#include "jobrecord.hpp"

#include <algorithm>
#include <cstring>
#include <ctime>

#include <common/showmsg.hpp>
#include <common/socket.hpp>
#include <common/strlib.hpp>
#include <common/timer.hpp>
#include <common/utils.hpp> // cap_value

#include "battle.hpp"
#include "chrif.hpp"
#include "clif.hpp"
#include "elemental.hpp"
#include "guild.hpp"
#include "homunculus.hpp"
#include "intif.hpp"
#include "map.hpp"
#include "party.hpp"
#include "pc.hpp"
#include "script.hpp"
#include "skill.hpp"
#include "status.hpp"

extern int32 char_fd; // inter-server link, see intif.cpp

/// Re-send an unacknowledged write after this long (covers writes lost in a dead socket)
#define JOBRECORD_RESEND_TIMEOUT 30000
/// Maximum number of records per character (made configurable in phase 7)
#define JOBRECORD_MAX_RECORDS 20
/// Name given to the character's state the first time it is saved as a record
#define JOBRECORD_DEFAULT_NAME "Default"

const uint16 job_record_summary_skills[] = {
	MC_INCCARRY,
	ALL_INCCARRY,
};
const uint8 job_record_summary_skill_count = static_cast<uint8>( ARRAYLENGTH( job_record_summary_skills ) );

namespace {

/// Returns the job record session of a player, creating it if needed
s_job_record_session& jobrecord_session( map_session_data& sd ){
	if( sd.job_record == nullptr ){
		sd.job_record = std::make_shared<s_job_record_session>();
	}

	return *sd.job_record;
}

const char* jobrecord_state_name( e_job_record_state state ){
	switch( state ){
		case JOBRECORD_STATE_UNLOADED: return "unloaded";
		case JOBRECORD_STATE_IDLE: return "idle";
		case JOBRECORD_STATE_LOADING: return "loading";
		case JOBRECORD_STATE_APPLYING: return "applying";
		case JOBRECORD_STATE_DELETING: return "deleting";
		default: return "unknown";
	}
}

/// Whether a queued write should be sent now: never sent, or sent but unacknowledged for too long
bool jobrecord_should_send( bool in_flight, t_tick sent_tick ){
	return !in_flight || DIFF_TICK( gettick(), sent_tick ) > JOBRECORD_RESEND_TIMEOUT;
}

/// Sends one section of a record as as many JOBRECORD_UPSERT chunks as needed
template <typename T>
void jobrecord_send_upsert_section( uint32 char_id, uint32 record_id, e_job_record_section section, const T* data, size_t count ){
	const size_t per_chunk = JOB_RECORD_MAX_CHUNK_PAYLOAD / sizeof( T );

	for( size_t sent = 0; sent < count; ){
		size_t n = std::min( per_chunk, count - sent );
		uint16 len = static_cast<uint16>( JOBRECORD_UPSERT_HEADER + n * sizeof( T ) );

		WFIFOHEAD( char_fd, len );
		WFIFOW( char_fd, 0 ) = JOBRECORD_UPSERT;
		WFIFOW( char_fd, 2 ) = len;
		WFIFOL( char_fd, 4 ) = char_id;
		WFIFOL( char_fd, 8 ) = record_id;
		WFIFOB( char_fd, 12 ) = section;
		WFIFOW( char_fd, 13 ) = static_cast<uint16>( n );
		memcpy( WFIFOP( char_fd, JOBRECORD_UPSERT_HEADER ), data + sent, n * sizeof( T ) );
		WFIFOSET( char_fd, len );

		sent += n;
	}
}

void jobrecord_send_upsert( uint32 char_id, const s_job_record_pending_upsert& p ){
	const s_job_record_full& r = p.data;
	uint32 record_id = r.base.record_id;

	jobrecord_send_upsert_section( char_id, record_id, JOBRECORD_SECTION_BASE, &r.base, 1 );
	jobrecord_send_upsert_section( char_id, record_id, JOBRECORD_SECTION_SKILL, r.skills.data(), r.skills.size() );
	jobrecord_send_upsert_section( char_id, record_id, JOBRECORD_SECTION_REG, r.regs.data(), r.regs.size() );
	jobrecord_send_upsert_section( char_id, record_id, JOBRECORD_SECTION_QUEST, r.quests.data(), r.quests.size() );
	jobrecord_send_upsert_section( char_id, record_id, JOBRECORD_SECTION_HOTKEY, r.hotkeys.data(), r.hotkeys.size() );
	jobrecord_send_upsert_section( char_id, record_id, JOBRECORD_SECTION_ITEM, r.items.data(), r.items.size() );
	jobrecord_send_upsert_section( char_id, record_id, JOBRECORD_SECTION_END, &p.end, 1 );
}

void jobrecord_send_commit( uint32 char_id, uint32 active_record, uint32 clear_stash_record ){
	WFIFOHEAD( char_fd, 14 );
	WFIFOW( char_fd, 0 ) = JOBRECORD_COMMIT;
	WFIFOL( char_fd, 2 ) = char_id;
	WFIFOL( char_fd, 6 ) = active_record;
	WFIFOL( char_fd, 10 ) = clear_stash_record;
	WFIFOSET( char_fd, 14 );
}

/// Appends the payload of a chunk to a vector, false if the payload size doesn't match the count
template <typename T>
bool jobrecord_append( std::vector<T>& vec, const uint8* data, uint16 count, size_t payload ){
	if( payload != count * sizeof( T ) ){
		return false;
	}

	size_t old_size = vec.size();

	vec.resize( old_size + count );
	memcpy( vec.data() + old_size, data, payload );

	return true;
}

/*==========================================
 * Switch engine (phase 4)
 *------------------------------------------*/

uint32 jobrecord_now(){
	return static_cast<uint32>( time( nullptr ) );
}

bool jobrecord_is_doram( const map_session_data& sd ){
	return ( sd.class_ & MAPID_FIRSTMASK ) == MAPID_SUMMONER;
}

const s_job_record_summary* jobrecord_find_summary( const s_job_record_session& jr, uint32 record_id ){
	for( const s_job_record_summary& s : jr.records ){
		if( s.base.record_id == record_id ){
			return &s;
		}
	}

	return nullptr;
}

/// Adds or replaces the list entry of a record (keeps the list ordered by record id)
void jobrecord_update_summary( s_job_record_session& jr, const s_job_record_full& r ){
	s_job_record_summary s = {};

	s.base = r.base;

	for( uint8 i = 0; i < job_record_summary_skill_count && i < JOB_RECORD_MAX_SUMMARY_SKILLS; i++ ){
		for( const s_skill& skill : r.skills ){
			if( skill.id == job_record_summary_skills[i] ){
				s.skill_lv[i] = skill.lv;
			}
		}
	}

	for( s_job_record_summary& existing : jr.records ){
		if( existing.base.record_id == r.base.record_id ){
			existing = s;
			return;
		}
	}

	jr.records.push_back( s );
	std::sort( jr.records.begin(), jr.records.end(), []( const s_job_record_summary& a, const s_job_record_summary& b ){
		return a.base.record_id < b.base.record_id;
	} );
}

/// Learned skills that belong to a record: permanent and script-granted ones.
/// Item-granted levels are saved as the learned level underneath; temporary, plagiarized
/// and wedding skills (marriage belongs to the character, not to a record) are skipped.
void jobrecord_collect_skills( const map_session_data& sd, std::vector<s_skill>& out ){
	for( uint16 idx = 1; idx < MAX_SKILL; idx++ ){
		const s_skill& s = sd.status.skill[idx];
		uint8 lv, flag;

		if( s.id == 0 ){
			continue;
		}

		if( s.flag == SKILL_FLAG_PERMANENT || s.flag == SKILL_FLAG_PERM_GRANTED ){
			lv = s.lv;
			flag = s.flag;
		}else if( s.flag >= SKILL_FLAG_REPLACED_LV_0 ){
			lv = static_cast<uint8>( s.flag - SKILL_FLAG_REPLACED_LV_0 );
			flag = SKILL_FLAG_PERMANENT;
		}else{
			continue;
		}

		if( lv == 0 || skill_get_inf2( s.id, INF2_ISWEDDING ) ){
			continue;
		}

		out.push_back( { s.id, lv, flag } );
	}
}

/// Takes a full copy of the player's current progress as a record
s_job_record_full jobrecord_snapshot( const map_session_data& sd, uint32 record_id, const char* name, uint32 created ){
	s_job_record_full r;
	s_job_record_base& b = r.base;
	const mmo_charstatus& st = sd.status;

	b.record_id = record_id;
	safestrncpy( b.name, name, sizeof( b.name ) );
	b.class_ = static_cast<uint16>( st.class_ );
	b.base_level = st.base_level;
	b.base_exp = st.base_exp;
	b.job_level = st.job_level;
	b.job_exp = st.job_exp;
	b.status_point = st.status_point;
	b.skill_point = st.skill_point;
	b.trait_point = st.trait_point;
	b.str = st.str; b.agi = st.agi; b.vit = st.vit; b.int_ = st.int_; b.dex = st.dex; b.luk = st.luk;
	b.pow = st.pow; b.sta = st.sta; b.wis = st.wis; b.spl = st.spl; b.con = st.con; b.crt = st.crt;
	// Job levels fit in a byte (pc_reg_received reads these variables as unsigned char too)
	b.chg_2nd = static_cast<uint8>( sd.change_level_2nd );
	b.chg_3rd = static_cast<uint8>( sd.change_level_3rd );
	b.chg_4th = static_cast<uint8>( sd.change_level_4th );
	b.hair = st.hair;
	b.hair_color = st.hair_color;
	b.clothes_color = st.clothes_color;
	b.equip_weight = 0; // Equipment stash: phase 5
	b.equip_count = 0;
	b.created = created != 0 ? created : jobrecord_now();
	b.last_used = jobrecord_now();

	jobrecord_collect_skills( sd, r.skills );

	for( uint8 slot = 0; slot < MAX_HOTKEYS_DB; slot++ ){
		if( st.hotkeys[slot].id != 0 ){
			r.hotkeys.push_back( { slot, st.hotkeys[slot] } );
		}
	}

	return r;
}

/// Removes every learned skill except plagiarized and wedding ones
void jobrecord_clear_skills( map_session_data& sd ){
	for( const auto& entry : skill_db ){
		uint16 idx = skill_get_index( entry.second->nameid );

		if( idx == 0 || entry.second->inf2[INF2_ISWEDDING] || sd.status.skill[idx].flag == SKILL_FLAG_PLAGIARIZED ){
			continue;
		}

		sd.status.skill[idx].id = 0;
		sd.status.skill[idx].lv = 0;
		sd.status.skill[idx].flag = SKILL_FLAG_PERMANENT;
	}
}

void jobrecord_set_change_levels( map_session_data& sd, uint8 chg_2nd, uint8 chg_3rd, uint8 chg_4th ){
	// Both the cache read by pc_calc_skilltree_normalize_job and the variable read at login (DD §2.5)
	sd.change_level_2nd = chg_2nd;
	sd.change_level_3rd = chg_3rd;
	sd.change_level_4th = chg_4th;
	pc_setglobalreg( &sd, add_str( JOBCHANGE2ND_VAR ), chg_2nd );
	pc_setglobalreg( &sd, add_str( JOBCHANGE3RD_VAR ), chg_3rd );
	pc_setglobalreg( &sd, add_str( JOBCHANGE4TH_VAR ), chg_4th );
}

void jobrecord_send_status_updates( map_session_data& sd ){
	static const _sp types[] = {
		SP_BASELEVEL, SP_JOBLEVEL, SP_BASEEXP, SP_NEXTBASEEXP, SP_JOBEXP, SP_NEXTJOBEXP,
		SP_STATUSPOINT, SP_SKILLPOINT, SP_TRAITPOINT,
		SP_STR, SP_AGI, SP_VIT, SP_INT, SP_DEX, SP_LUK,
		SP_USTR, SP_UAGI, SP_UVIT, SP_UINT, SP_UDEX, SP_ULUK,
		SP_POW, SP_STA, SP_WIS, SP_SPL, SP_CON, SP_CRT,
		SP_UPOW, SP_USTA, SP_UWIS, SP_USPL, SP_UCON, SP_UCRT,
	};

	for( _sp type : types ){
		clif_updatestatus( sd, type );
	}
}

/// Replaces the player's progress with the record's.
/// The order matters, see §4.1 of the design and DD §2.3 / §2.5 / §3.1.
bool jobrecord_apply( map_session_data& sd, const s_job_record_full& r ){
	const s_job_record_base& b = r.base;

	if( !job_db.exists( b.class_ ) ){
		ShowError( "jobrecord_apply: Record %u of character %u has an unknown job %d.\n", b.record_id, sd.status.char_id, b.class_ );
		return false;
	}

	// 1. Leave states that belong to the previous record
	if( sd.cloneskill_idx > 0 ){
		pc_skill_plagiarism_reset( sd, 1 );
	}
	if( sd.reproduceskill_idx > 0 ){
		pc_skill_plagiarism_reset( sd, 2 );
	}

	int32 option = sd.sc.option & ~( OPTION_RIDING | OPTION_FALCON | OPTION_DRAGON | OPTION_WUG | OPTION_WUGRIDER | OPTION_MADOGEAR );

#ifndef NEW_CARTS
	option &= ~OPTION_CART;
#else
	if( sd.sc.getSCE( SC_PUSH_CART ) ){
		pc_setcart( &sd, 0 );
	}
#endif
	if( option != sd.sc.option ){
		pc_setoption( &sd, option );
	}
	if( hom_is_active( sd.hd ) ){
		hom_vaporize( &sd, HOM_ST_ACTIVE );
	}
	if( sd.ed != nullptr ){
		elemental_delete( sd.ed );
	}

	// 2. Look and base level before the job change: pc_jobchange refreshes the head once with the
	//    right race, and only truncates the base level if it exceeds the target job's cap
	sd.status.hair = cap_value( b.hair, MIN_HAIR_STYLE, MAX_HAIR_STYLE );
	sd.status.hair_color = cap_value( b.hair_color, MIN_HAIR_COLOR, MAX_HAIR_COLOR );
	sd.status.clothes_color = cap_value( b.clothes_color, MIN_CLOTH_COLOR, MAX_CLOTH_COLOR );
	sd.status.base_level = std::max<uint32>( b.base_level, 1 );
	sd.status.base_exp = b.base_exp;

	// 3. Job (returns false when the job does not change, which is fine)
	pc_jobchange( &sd, b.class_, 0 );

	// 4. Skills: wipe what pc_jobchange left behind, then restore the record's own
	jobrecord_clear_skills( sd );

	for( const s_skill& skill : r.skills ){
		uint16 idx = skill_get_index( skill.id );

		if( idx == 0 ){
			continue;
		}

		sd.status.skill[idx].id = skill.id;
		sd.status.skill[idx].lv = skill.lv;
		sd.status.skill[idx].flag = ( skill.flag == SKILL_FLAG_PERM_GRANTED ) ? SKILL_FLAG_PERM_GRANTED : SKILL_FLAG_PERMANENT;
	}

	jobrecord_set_change_levels( sd, b.chg_2nd, b.chg_3rd, b.chg_4th );

	// 5. Levels, exp, points and stats (overwrite what pc_jobchange computed)
	const uint32 max_base_level = pc_maxbaselv( &sd );
	const uint32 max_job_level = pc_maxjoblv( &sd );

	sd.status.base_level = cap_value( b.base_level, 1u, max_base_level );
	sd.status.base_exp = b.base_exp;
	sd.status.job_level = cap_value( b.job_level, 1u, max_job_level );
	sd.status.job_exp = b.job_exp;
	sd.status.skill_point = b.skill_point;
	sd.status.status_point = b.status_point;
	sd.status.trait_point = b.trait_point;
	sd.status.str = b.str; sd.status.agi = b.agi; sd.status.vit = b.vit;
	sd.status.int_ = b.int_; sd.status.dex = b.dex; sd.status.luk = b.luk;
	sd.status.pow = b.pow; sd.status.sta = b.sta; sd.status.wis = b.wis;
	sd.status.spl = b.spl; sd.status.con = b.con; sd.status.crt = b.crt;

	// 6. Hotkeys
	memset( sd.status.hotkeys, 0, sizeof( sd.status.hotkeys ) );
	for( const s_job_record_hotkey& hk : r.hotkeys ){
		if( hk.slot < MAX_HOTKEYS_DB ){
			sd.status.hotkeys[hk.slot] = hk.hotkey;
		}
	}

	// 7. Refresh the server state and the client
	pc_calc_skilltree( &sd );
	clif_skillinfoblock( sd );

	status_set_viewdata( &sd, sd.status.class_ );
	clif_changelook( &sd, LOOK_BASE, sd.vd.look[LOOK_BASE] );
	clif_changelook( &sd, LOOK_HAIR, sd.vd.look[LOOK_HAIR] );
	clif_changelook( &sd, LOOK_HAIR_COLOR, sd.vd.look[LOOK_HAIR_COLOR] );
	clif_changelook( &sd, LOOK_CLOTHES_COLOR, sd.vd.look[LOOK_CLOTHES_COLOR] );
	clif_changelook( &sd, LOOK_BODY2, sd.vd.look[LOOK_BODY2] );

	status_calc_pc( &sd, SCO_FORCE );
	jobrecord_send_status_updates( sd );

	clif_hotkeys_send( &sd, 0 );
#if PACKETVER_MAIN_NUM >= 20190522 || PACKETVER_RE_NUM >= 20190508 || PACKETVER_ZERO_NUM >= 20190605
	clif_hotkeys_send( &sd, 1 );
#endif

	if( sd.status.party_id != 0 ){
		party_send_levelup( &sd );
	}
	if( sd.status.guild_id != 0 ){
		intif_guild_change_memberinfo( sd.status.guild_id, sd.status.account_id, sd.status.char_id, GMI_HAIR, &sd.status.hair, sizeof( sd.status.hair ) );
		intif_guild_change_memberinfo( sd.status.guild_id, sd.status.account_id, sd.status.char_id, GMI_HAIR_COLOR, &sd.status.hair_color, sizeof( sd.status.hair_color ) );
		guild_send_memberinfoshort( &sd, 1 );
	}

	return true;
}

/// Builds the data of a brand new record: base level 1, Novice (human) or Summoner (doram)
s_job_record_full jobrecord_build_new( const map_session_data& sd, uint32 record_id, const char* name, bool doram ){
	s_job_record_full r;
	s_job_record_base& b = r.base;

	b.record_id = record_id;
	safestrncpy( b.name, name, sizeof( b.name ) );
	b.class_ = doram ? JOB_SUMMONER : JOB_NOVICE;
	b.base_level = 1;
	b.job_level = 1;
	b.str = b.agi = b.vit = b.int_ = b.dex = b.luk = 1;
	b.created = b.last_used = jobrecord_now();

	// Look (§9.1 of the design): keep it within the same race, otherwise start from a safe default
	if( doram == jobrecord_is_doram( sd ) ){
		b.hair = sd.status.hair;
		b.hair_color = sd.status.hair_color;
		b.clothes_color = sd.status.clothes_color;
	}else{
		b.hair = 1;
		b.hair_color = sd.status.hair_color <= 8 ? sd.status.hair_color : 0;
		b.clothes_color = 0;
	}

	return r;
}

/// Saves the character's current progress as the outgoing record and marks the switch journal.
/// The first time, the current progress becomes a new record named JOBRECORD_DEFAULT_NAME.
void jobrecord_save_outgoing( map_session_data& sd, s_job_record_session& jr, uint32 target_id ){
	uint32 outgoing_id = jr.active_record;
	const char* name = JOBRECORD_DEFAULT_NAME;
	uint32 created = 0;

	if( outgoing_id == 0 ){
		outgoing_id = jr.next_record_id++;
	}else if( const s_job_record_summary* s = jobrecord_find_summary( jr, outgoing_id ); s != nullptr ){
		name = s->base.name;
		created = s->base.created;
	}

	s_job_record_full outgoing = jobrecord_snapshot( sd, outgoing_id, name, created );
	s_job_record_upsert_end end = {};

	end.next_record_id = jr.next_record_id;
	end.pending_to = target_id;
	end.set_pending = 1;

	jobrecord_update_summary( jr, outgoing );
	jobrecord_queue_upsert( sd, outgoing, end );
	jr.active_record = outgoing_id;
	jr.pending_to = target_id;

	// Persist the outgoing record and the journal before anything changes (§9.2 of the design)
	chrif_save( &sd, CSAVE_INVENTORY );
}

/// Makes a record active: local pointer, commit queued, saved with the new state
void jobrecord_activate( map_session_data& sd, s_job_record_session& jr, uint32 record_id ){
	jr.active_record = record_id;
	jobrecord_queue_commit( sd, record_id, 0 ); // Stash clearing: phase 5
	chrif_save( &sd, CSAVE_INVENTORY );
}

/// Completes a switch once the target record has been loaded
void jobrecord_finish_switch( map_session_data& sd, s_job_record_session& jr, const s_job_record_full& target ){
	char output[CHAT_SIZE_MAX];

	jr.state = JOBRECORD_STATE_APPLYING;

	if( !chrif_isconnected() || pc_isdead( &sd ) ){
		clif_displaymessage( sd.fd, "Job record: switch cancelled." );
		jr.state = JOBRECORD_STATE_IDLE;
		return;
	}

	jobrecord_save_outgoing( sd, jr, target.base.record_id );

	if( !jobrecord_apply( sd, target ) ){
		// Nothing changed yet: the journal is cleared again by committing the outgoing record
		jobrecord_activate( sd, jr, jr.active_record );
		clif_displaymessage( sd.fd, "Job record: this record could not be applied." );
		jr.state = JOBRECORD_STATE_IDLE;
		return;
	}

	s_job_record_full used = target;

	used.base.last_used = jobrecord_now();
	jobrecord_update_summary( jr, used );
	jobrecord_activate( sd, jr, target.base.record_id );
	jr.state = JOBRECORD_STATE_IDLE;

	safesnprintf( output, sizeof( output ), "Job record: switched to '%s' (%s, base %u, job %u).",
		target.base.name, job_name( sd.status.class_ ), sd.status.base_level, sd.status.job_level );
	clif_displaymessage( sd.fd, output );
}

/// Called when a requested record has been fully received (or failed to load)
void jobrecord_on_loaded( map_session_data& sd, s_job_record_session& jr, e_job_record_load_result result ){
	std::shared_ptr<s_job_record_full> loaded = jr.loading;

	jr.loading = nullptr;
	jr.loading_id = 0;
	jr.state = JOBRECORD_STATE_IDLE;

	if( result != JOBRECORD_LOAD_OK || loaded == nullptr ){
		ShowWarning( "Job record of character %u could not be loaded (result %d).\n", sd.status.char_id, result );
		clif_displaymessage( sd.fd, "Job record: the record could not be loaded." );
		return;
	}

	jobrecord_finish_switch( sd, jr, *loaded );
}

/// Name rules: 1 to NAME_LENGTH-1 printable characters, unique among the character's records
const char* jobrecord_check_name( const s_job_record_session& jr, const char* name, uint32 ignore_record ){
	size_t len = strnlen( name, NAME_LENGTH );

	if( len == 0 || len >= NAME_LENGTH ){
		return "The name must be 1 to 23 characters long.";
	}

	for( size_t i = 0; i < len; i++ ){
		if( static_cast<unsigned char>( name[i] ) < 0x20 ){
			return "The name contains invalid characters.";
		}
	}

	for( const s_job_record_summary& s : jr.records ){
		if( s.base.record_id != ignore_record && strncmp( s.base.name, name, NAME_LENGTH ) == 0 ){
			return "Another record already has this name.";
		}
	}

	return nullptr;
}

/// Basic guards (phase 4). Cooldown, combat and map restrictions are added in phase 7.
const char* jobrecord_check_can_act( map_session_data& sd ){
	if( sd.job_record == nullptr || sd.job_record->state == JOBRECORD_STATE_UNLOADED ){
		return "Your job records are still loading.";
	}
	if( sd.job_record->state != JOBRECORD_STATE_IDLE ){
		return "A job record action is already in progress.";
	}
	if( !chrif_isconnected() ){
		return "The character server is unavailable, try again later.";
	}
	if( pc_isdead( &sd ) ){
		return "You cannot do this while dead.";
	}
	if( sd.state.vending || sd.state.buyingstore || sd.state.trading || sd.state.storage_flag || sd.chatID ){
		return "Close your shop, trade, storage or chat first.";
	}

	return nullptr;
}

/// Finds the online character of a char-server reply that belongs to this session's player
map_session_data* jobrecord_reply_owner( uint32 char_id ){
	map_session_data* sd = map_charid2sd( char_id );

	if( sd == nullptr || sd->job_record == nullptr ){
		return nullptr; // Not online anymore
	}

	return sd;
}

} // namespace

/*==========================================
 * Requests
 *------------------------------------------*/

/// Requests the record list. Called once the character's registry has been received.
void jobrecord_request_list( map_session_data& sd ){
	s_job_record_session& jr = jobrecord_session( sd );

	jr.state = JOBRECORD_STATE_UNLOADED;
	jr.list_buffer.clear();

	if( CheckForCharServer() ){
		return;
	}

	uint16 len = static_cast<uint16>( JOBRECORD_LIST_REQ_HEADER + job_record_summary_skill_count * 2 );

	WFIFOHEAD( char_fd, len );
	WFIFOW( char_fd, 0 ) = JOBRECORD_LIST_REQ;
	WFIFOW( char_fd, 2 ) = len;
	WFIFOL( char_fd, 4 ) = sd.status.char_id;
	WFIFOB( char_fd, 8 ) = job_record_summary_skill_count;
	for( uint8 i = 0; i < job_record_summary_skill_count; i++ ){
		WFIFOW( char_fd, JOBRECORD_LIST_REQ_HEADER + i * 2 ) = job_record_summary_skills[i];
	}
	WFIFOSET( char_fd, len );
}

/// Requests one full record. Only possible while idle.
bool jobrecord_request_load( map_session_data& sd, uint32 record_id ){
	s_job_record_session& jr = jobrecord_session( sd );

	if( jr.state != JOBRECORD_STATE_IDLE || !chrif_isconnected() ){
		return false;
	}

	jr.state = JOBRECORD_STATE_LOADING;
	jr.loading_id = record_id;
	jr.loading = std::make_shared<s_job_record_full>();

	WFIFOHEAD( char_fd, 10 );
	WFIFOW( char_fd, 0 ) = JOBRECORD_LOAD_REQ;
	WFIFOL( char_fd, 2 ) = sd.status.char_id;
	WFIFOL( char_fd, 6 ) = record_id;
	WFIFOSET( char_fd, 10 );

	return true;
}

/// Renames a record. The local list is updated when the char-server confirms.
bool jobrecord_request_rename( map_session_data& sd, uint32 record_id, const char* name ){
	if( !chrif_isconnected() ){
		return false;
	}

	WFIFOHEAD( char_fd, 10 + NAME_LENGTH );
	WFIFOW( char_fd, 0 ) = JOBRECORD_RENAME;
	WFIFOL( char_fd, 2 ) = sd.status.char_id;
	WFIFOL( char_fd, 6 ) = record_id;
	safestrncpy( WFIFOCP( char_fd, 10 ), name, NAME_LENGTH );
	WFIFOSET( char_fd, 10 + NAME_LENGTH );

	return true;
}

/*==========================================
 * Actions (phase 4)
 *------------------------------------------*/

/// Starts a switch to a saved record. The switch completes when the record has been loaded.
/// @return nullptr when started, otherwise a message for the player
const char* jobrecord_start_switch( map_session_data& sd, uint32 record_id ){
	if( const char* error = jobrecord_check_can_act( sd ); error != nullptr ){
		return error;
	}

	s_job_record_session& jr = *sd.job_record;

	if( record_id == jr.active_record ){
		return "This record is already active.";
	}
	if( jobrecord_find_summary( jr, record_id ) == nullptr ){
		return "This record does not exist.";
	}
	if( !jobrecord_request_load( sd, record_id ) ){
		return "The record could not be requested, try again later.";
	}

	return nullptr;
}

/// Saves the current progress and starts a new record: base level 1, Novice or Summoner (doram).
/// @return nullptr on success, otherwise a message for the player
const char* jobrecord_create( map_session_data& sd, const char* name, bool doram ){
	if( const char* error = jobrecord_check_can_act( sd ); error != nullptr ){
		return error;
	}

	s_job_record_session& jr = *sd.job_record;

	if( const char* error = jobrecord_check_name( jr, name, 0 ); error != nullptr ){
		return error;
	}

	// The current progress becomes a record too when there is none yet
	size_t total = jr.records.size() + 1 + ( jr.active_record == 0 ? 1 : 0 );

	if( total > JOBRECORD_MAX_RECORDS ){
		return "You already have the maximum number of records.";
	}

	jr.state = JOBRECORD_STATE_APPLYING;

	// The id of the outgoing record is allocated first (when it has none yet), then the new one
	uint32 new_id = ( jr.active_record == 0 ) ? jr.next_record_id + 1 : jr.next_record_id;

	jobrecord_save_outgoing( sd, jr, new_id );
	jr.next_record_id = std::max( jr.next_record_id, new_id + 1 );

	s_job_record_full fresh = jobrecord_build_new( sd, new_id, name, doram );

	if( !jobrecord_apply( sd, fresh ) ){
		jobrecord_activate( sd, jr, jr.active_record );
		jr.state = JOBRECORD_STATE_IDLE;
		return "The new record could not be created.";
	}

	// Status points for base level 1 of the new job, stats back to 1, no skill points
	pc_resetstate( &sd );
	sd.status.skill_point = 0;
	clif_updatestatus( sd, SP_SKILLPOINT );

	s_job_record_full created = jobrecord_snapshot( sd, new_id, name, 0 );
	s_job_record_upsert_end end = {};

	end.next_record_id = jr.next_record_id;

	jobrecord_update_summary( jr, created );
	jobrecord_queue_upsert( sd, created, end ); // So the record exists in the list from now on
	jobrecord_activate( sd, jr, new_id );
	jr.state = JOBRECORD_STATE_IDLE;

	return nullptr;
}

/// Renames a record (the active one included)
/// @return nullptr when requested, otherwise a message for the player
const char* jobrecord_rename( map_session_data& sd, uint32 record_id, const char* name ){
	if( sd.job_record == nullptr || sd.job_record->state == JOBRECORD_STATE_UNLOADED ){
		return "Your job records are still loading.";
	}

	s_job_record_session& jr = *sd.job_record;

	for( s_job_record_summary& s : jr.records ){
		if( s.base.record_id != record_id ){
			continue;
		}

		if( const char* error = jobrecord_check_name( jr, name, record_id ); error != nullptr ){
			return error;
		}
		if( !jobrecord_request_rename( sd, record_id, name ) ){
			return "The character server is unavailable, try again later.";
		}

		// Update the local copy now, so a save of the active record keeps the new name
		safestrncpy( s.base.name, name, sizeof( s.base.name ) );
		return nullptr;
	}

	return "This record does not exist.";
}

/*==========================================
 * Write queue (resend until acknowledged)
 *------------------------------------------*/

/// Queues a full-state save of a record. A newer save of the same record replaces the queued one.
void jobrecord_queue_upsert( map_session_data& sd, const s_job_record_full& record, const s_job_record_upsert_end& end ){
	s_job_record_session& jr = jobrecord_session( sd );

	for( s_job_record_pending_upsert& p : jr.pending_upserts ){
		if( p.data.base.record_id == record.base.record_id ){
			p.data = record;
			p.end = end;
			p.dirty = true; // If it is in flight, send again once acknowledged
			return;
		}
	}

	s_job_record_pending_upsert p;

	p.data = record;
	p.end = end;
	jr.pending_upserts.push_back( std::move( p ) );
}

/// Queues the commit of a switch (active record, stash clear, journal reset).
void jobrecord_queue_commit( map_session_data& sd, uint32 active_record, uint32 clear_stash_record ){
	s_job_record_session& jr = jobrecord_session( sd );

	jr.commit_pending = true;
	jr.commit_dirty = jr.commit_in_flight;
	jr.commit_active = active_record;
	jr.commit_clear_stash = clear_stash_record;
}

/// chrif_save hook, before the inventory: record saves (stash rows are only inserted here)
void jobrecord_save_before_inventory( map_session_data& sd ){
	if( sd.job_record == nullptr ){
		return;
	}

	for( s_job_record_pending_upsert& p : sd.job_record->pending_upserts ){
		if( !jobrecord_should_send( p.in_flight, p.sent_tick ) ){
			continue;
		}

		jobrecord_send_upsert( sd.status.char_id, p );
		p.in_flight = true;
		p.dirty = false;
		p.sent_tick = gettick();
	}
}

/// chrif_save hook, after the status: the commit (stash rows are only removed here)
void jobrecord_save_after_status( map_session_data& sd ){
	if( sd.job_record == nullptr ){
		return;
	}

	s_job_record_session& jr = *sd.job_record;

	if( !jr.commit_pending || !jobrecord_should_send( jr.commit_in_flight, jr.commit_sent_tick ) ){
		return;
	}

	jobrecord_send_commit( sd.status.char_id, jr.commit_active, jr.commit_clear_stash );
	jr.commit_in_flight = true;
	jr.commit_dirty = false;
	jr.commit_sent_tick = gettick();
}

/*==========================================
 * Replies from the char-server
 *------------------------------------------*/

/// JOBRECORD_LIST_ACK
void jobrecord_parse_list_ack( int32 fd ){
	uint16 len = RFIFOW( fd, 2 );
	uint32 char_id = RFIFOL( fd, 4 );
	map_session_data* sd = jobrecord_reply_owner( char_id );

	if( sd == nullptr ){
		return;
	}

	s_job_record_session& jr = *sd->job_record;
	uint16 count = RFIFOW( fd, 22 );

	if( len != JOBRECORD_LIST_ACK_HEADER + count * sizeof( s_job_record_summary ) ){
		ShowError( "jobrecord_parse_list_ack: Invalid packet length %d for character %u.\n", len, char_id );
		return;
	}

	jobrecord_append( jr.list_buffer, RFIFOP( fd, JOBRECORD_LIST_ACK_HEADER ), count, count * sizeof( s_job_record_summary ) );

	if( RFIFOB( fd, 20 ) == 0 ){
		return; // More chunks follow
	}

	jr.active_record = RFIFOL( fd, 8 );
	jr.pending_to = RFIFOL( fd, 12 );
	jr.next_record_id = std::max<uint32>( RFIFOL( fd, 16 ), 1 );
	jr.summary_skill_count = RFIFOB( fd, 21 );
	jr.records = std::move( jr.list_buffer );
	jr.list_buffer.clear();

	if( jr.state == JOBRECORD_STATE_UNLOADED ){
		jr.state = JOBRECORD_STATE_IDLE;
	}

	ShowInfo( "Job records of character %u loaded: %u record(s), active %u%s.\n", char_id, static_cast<uint32>( jr.records.size() ), jr.active_record,
		jr.pending_to != 0 ? " (interrupted switch pending)" : "" );
}

/// JOBRECORD_LOAD_ACK
void jobrecord_parse_load_ack( int32 fd ){
	uint16 len = RFIFOW( fd, 2 );
	uint32 char_id = RFIFOL( fd, 4 );
	uint32 record_id = RFIFOL( fd, 8 );
	uint8 section = RFIFOB( fd, 12 );
	uint8 result = RFIFOB( fd, 13 );
	uint16 count = RFIFOW( fd, 14 );
	map_session_data* sd = jobrecord_reply_owner( char_id );

	if( sd == nullptr ){
		return;
	}

	s_job_record_session& jr = *sd->job_record;

	if( jr.state != JOBRECORD_STATE_LOADING || jr.loading == nullptr || jr.loading_id != record_id ){
		return; // Stale reply (e.g. after a timeout)
	}

	const uint8* data = RFIFOP( fd, JOBRECORD_LOAD_ACK_HEADER );
	size_t payload = ( len >= JOBRECORD_LOAD_ACK_HEADER ) ? len - JOBRECORD_LOAD_ACK_HEADER : 0;
	s_job_record_full& r = *jr.loading;
	bool ok = true;

	switch( section ){
		case JOBRECORD_SECTION_BASE:
			ok = ( count == 1 && payload == sizeof( s_job_record_base ) );
			if( ok ){
				memcpy( &r.base, data, sizeof( s_job_record_base ) );
			}
			break;
		case JOBRECORD_SECTION_SKILL: ok = jobrecord_append( r.skills, data, count, payload ); break;
		case JOBRECORD_SECTION_REG: ok = jobrecord_append( r.regs, data, count, payload ); break;
		case JOBRECORD_SECTION_QUEST: ok = jobrecord_append( r.quests, data, count, payload ); break;
		case JOBRECORD_SECTION_HOTKEY: ok = jobrecord_append( r.hotkeys, data, count, payload ); break;
		case JOBRECORD_SECTION_ITEM: ok = jobrecord_append( r.items, data, count, payload ); break;
		case JOBRECORD_SECTION_END:
			jobrecord_on_loaded( *sd, jr, static_cast<e_job_record_load_result>( result ) );
			return;
		default:
			ok = false;
			break;
	}

	if( !ok ){
		ShowError( "jobrecord_parse_load_ack: Invalid section %d for character %u record %u.\n", section, char_id, record_id );
		jobrecord_on_loaded( *sd, jr, JOBRECORD_LOAD_ERROR );
	}
}

/// JOBRECORD_UPSERT_ACK
void jobrecord_parse_upsert_ack( int32 fd ){
	uint32 char_id = RFIFOL( fd, 2 );
	uint32 record_id = RFIFOL( fd, 6 );
	bool success = RFIFOB( fd, 10 ) != 0;
	map_session_data* sd = jobrecord_reply_owner( char_id );

	if( sd == nullptr ){
		return;
	}

	std::vector<s_job_record_pending_upsert>& pending = sd->job_record->pending_upserts;

	for( auto it = pending.begin(); it != pending.end(); ++it ){
		if( it->data.base.record_id != record_id ){
			continue;
		}

		if( !success ){
			ShowError( "Job record %u of character %u could not be saved, retrying with the next save.\n", record_id, char_id );
			it->in_flight = false;
		}else if( it->dirty ){
			it->in_flight = false; // Changed while in flight: send the newer data
		}else{
			pending.erase( it );
		}
		return;
	}
}

/// JOBRECORD_COMMIT_ACK
void jobrecord_parse_commit_ack( int32 fd ){
	uint32 char_id = RFIFOL( fd, 2 );
	uint32 active = RFIFOL( fd, 6 );
	bool success = RFIFOB( fd, 10 ) != 0;
	map_session_data* sd = jobrecord_reply_owner( char_id );

	if( sd == nullptr ){
		return;
	}

	s_job_record_session& jr = *sd->job_record;

	if( !jr.commit_pending ){
		return;
	}

	jr.commit_in_flight = false;

	if( !success ){
		ShowError( "Job record commit (active %u) of character %u failed, retrying with the next save.\n", active, char_id );
		return;
	}

	if( !jr.commit_dirty ){
		jr.commit_pending = false;
		jr.active_record = jr.commit_active;
		jr.pending_to = 0;
	}
}

/// JOBRECORD_RENAME_ACK
void jobrecord_parse_rename_ack( int32 fd ){
	uint32 char_id = RFIFOL( fd, 2 );
	uint32 record_id = RFIFOL( fd, 6 );
	bool success = RFIFOB( fd, 10 ) != 0;
	map_session_data* sd = jobrecord_reply_owner( char_id );

	if( sd == nullptr ){
		return;
	}

	if( !success ){
		ShowError( "Job record %u of character %u could not be renamed.\n", record_id, char_id );
		clif_displaymessage( sd->fd, "Job record: the record could not be renamed." );
		return;
	}

	// The local list was already updated when the rename was requested (jobrecord_rename)
}

/*==========================================
 * Debug
 *------------------------------------------*/

/// Prints the record list and the queue state (@jobrecordinfo)
void jobrecord_show_info( map_session_data& sd, int32 fd ){
	char output[CHAT_SIZE_MAX];

	if( sd.job_record == nullptr ){
		clif_displaymessage( fd, "Job records: not loaded." );
		return;
	}

	const s_job_record_session& jr = *sd.job_record;

	safesnprintf( output, sizeof( output ), "Job records: %u, state %s, active %u, pending switch %u, next id %u.",
		static_cast<uint32>( jr.records.size() ), jobrecord_state_name( jr.state ), jr.active_record, jr.pending_to, jr.next_record_id );
	clif_displaymessage( fd, output );

	for( const s_job_record_summary& s : jr.records ){
		const s_job_record_base& b = s.base;

		safesnprintf( output, sizeof( output ), "%s#%u %s: %s, base %u, job %u, gear %u item(s) / weight %u",
			b.record_id == jr.active_record ? "* " : "  ", b.record_id, b.name, job_name( b.class_ ), b.base_level, b.job_level, b.equip_count, b.equip_weight / 10 );
		clif_displaymessage( fd, output );
	}

	safesnprintf( output, sizeof( output ), "Queued writes: %u record save(s), commit %s.",
		static_cast<uint32>( jr.pending_upserts.size() ), jr.commit_pending ? ( jr.commit_in_flight ? "in flight" : "queued" ) : "none" );
	clif_displaymessage( fd, output );
}
