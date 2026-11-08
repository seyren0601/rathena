// Copyright (c) rAthena Dev Teams - Licensed under GNU GPL
// For more information, see LICENCE in the main folder

// [Job Record] map-server side of the job record system.
// See docs/subsystems/map/job-record-system-design.md
//
// Phase 3: record list at login, char-server packets, and the write queue that is
// re-sent with every character save until the char-server acknowledges it.

#include "jobrecord.hpp"

#include <algorithm>
#include <cstring>

#include <common/showmsg.hpp>
#include <common/socket.hpp>
#include <common/strlib.hpp>
#include <common/timer.hpp>

#include "chrif.hpp"
#include "clif.hpp"
#include "intif.hpp"
#include "map.hpp"
#include "pc.hpp"
#include "skill.hpp"

extern int32 char_fd; // inter-server link, see intif.cpp

/// Re-send an unacknowledged write after this long (covers writes lost in a dead socket)
#define JOBRECORD_RESEND_TIMEOUT 30000

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

/// Called when a requested record has been fully received (or failed to load)
void jobrecord_on_loaded( map_session_data& sd, s_job_record_session& jr, e_job_record_load_result result ){
	if( result == JOBRECORD_LOAD_OK && jr.loading != nullptr ){
		const s_job_record_full& r = *jr.loading;

		ShowInfo( "Job record %u of character %u loaded: %s, %u skills, %u variables, %u quests, %u hotkeys, %u items.\n",
			jr.loading_id, sd.status.char_id, job_name( r.base.class_ ), static_cast<uint32>( r.skills.size() ), static_cast<uint32>( r.regs.size() ),
			static_cast<uint32>( r.quests.size() ), static_cast<uint32>( r.hotkeys.size() ), static_cast<uint32>( r.items.size() ) );
	}else{
		ShowWarning( "Job record %u of character %u could not be loaded (result %d).\n", jr.loading_id, sd.status.char_id, result );
	}

	// Phase 3 only loads; applying the record is added by the switch engine (phase 4).
	jr.loading = nullptr;
	jr.loading_id = 0;
	jr.state = JOBRECORD_STATE_IDLE;
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
		return;
	}

	// The new name is part of the next list; refresh it so the menu shows the confirmed name
	if( sd->job_record->state == JOBRECORD_STATE_IDLE ){
		jobrecord_request_list( *sd );
	}
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
