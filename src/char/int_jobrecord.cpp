// Copyright (c) rAthena Dev Teams - Licensed under GNU GPL
// For more information, see LICENCE in the main folder

// [Job Record] char-server side storage of job records.
// See docs/subsystems/map/job-record-system-design.md
//
// The map-server owns the live data of the active record. This file only persists
// inactive records and the per-character state (active record, switch journal).
// Every write runs inside one SQL transaction (the job record tables are InnoDB).

#include "int_jobrecord.hpp"

#include <algorithm>
#include <cinttypes>
#include <cstdlib>
#include <cstring>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include <common/job_record.hpp>
#include <common/mmo.hpp>
#include <common/showmsg.hpp>
#include <common/socket.hpp>
#include <common/sql.hpp>
#include <common/strlib.hpp>

#include "char.hpp"
#include "inter.hpp"
#include "int_mail.hpp" // mapif_Mail_new

namespace {

/// Chunks of one JOBRECORD_UPSERT, collected until JOBRECORD_SECTION_END arrives
struct s_job_record_upsert_buffer {
	int32 fd = 0;
	uint32 record_id = 0;
	bool invalid = false;
	s_job_record_base base{};
	std::vector<s_skill> skills;
	std::vector<s_job_record_reg> regs;
	std::vector<quest> quests;
	std::vector<s_job_record_hotkey> hotkeys;
	std::vector<item> items;
};

/// Pending upserts by char_id. Only one map-server serves a character at a time.
std::unordered_map<uint32, s_job_record_upsert_buffer> jobrecord_upsert_buffers;

bool jobrecord_query( const char* query ){
	if( SQL_ERROR == Sql_QueryStr( sql_handle, query ) ){
		Sql_ShowDebug( sql_handle );
		return false;
	}

	return true;
}

bool jobrecord_transaction_begin(){
	return jobrecord_query( "START TRANSACTION" );
}

bool jobrecord_transaction_commit(){
	return jobrecord_query( "COMMIT" );
}

void jobrecord_transaction_rollback(){
	jobrecord_query( "ROLLBACK" );
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
 * Loading
 *------------------------------------------*/

/// Loads the scalar data of all records of a character (record_id == 0) or of one record
bool jobrecord_bases_fromsql( uint32 char_id, uint32 record_id, std::vector<s_job_record_base>& out ){
	SqlStmt stmt{ *sql_handle };
	StringBuf buf;
	s_job_record_base tmp{};

	StringBuf_Init( &buf );
	StringBuf_Printf( &buf, "SELECT `record_id`,`name`,`class`,`base_level`,`base_exp`,`job_level`,`job_exp`,"
		"`status_point`,`skill_point`,`trait_point`,`str`,`agi`,`vit`,`int`,`dex`,`luk`,`pow`,`sta`,`wis`,`spl`,`con`,`crt`,"
		"`chg_2nd`,`chg_3rd`,`chg_4th`,`hair`,`hair_color`,`clothes_color`,`equip_weight`,`equip_count`,`created`,`last_used`"
		" FROM `%s` WHERE `char_id` = '%u'", schema_config.job_record_table, char_id );

	if( record_id != 0 ){
		StringBuf_Printf( &buf, " AND `record_id` = '%u'", record_id );
	}

	StringBuf_AppendStr( &buf, " ORDER BY `record_id`" );

	if( SQL_ERROR == stmt.PrepareStr( StringBuf_Value( &buf ) ) || SQL_ERROR == stmt.Execute() ){
		SqlStmt_ShowDebug( stmt );
		return false;
	}

	stmt.BindColumn( 0, SQLDT_UINT32, &tmp.record_id );
	stmt.BindColumn( 1, SQLDT_STRING, &tmp.name, sizeof( tmp.name ) );
	stmt.BindColumn( 2, SQLDT_UINT16, &tmp.class_ );
	stmt.BindColumn( 3, SQLDT_UINT32, &tmp.base_level );
	stmt.BindColumn( 4, SQLDT_UINT64, &tmp.base_exp );
	stmt.BindColumn( 5, SQLDT_UINT32, &tmp.job_level );
	stmt.BindColumn( 6, SQLDT_UINT64, &tmp.job_exp );
	stmt.BindColumn( 7, SQLDT_UINT32, &tmp.status_point );
	stmt.BindColumn( 8, SQLDT_UINT32, &tmp.skill_point );
	stmt.BindColumn( 9, SQLDT_UINT32, &tmp.trait_point );
	stmt.BindColumn( 10, SQLDT_UINT16, &tmp.str );
	stmt.BindColumn( 11, SQLDT_UINT16, &tmp.agi );
	stmt.BindColumn( 12, SQLDT_UINT16, &tmp.vit );
	stmt.BindColumn( 13, SQLDT_UINT16, &tmp.int_ );
	stmt.BindColumn( 14, SQLDT_UINT16, &tmp.dex );
	stmt.BindColumn( 15, SQLDT_UINT16, &tmp.luk );
	stmt.BindColumn( 16, SQLDT_UINT16, &tmp.pow );
	stmt.BindColumn( 17, SQLDT_UINT16, &tmp.sta );
	stmt.BindColumn( 18, SQLDT_UINT16, &tmp.wis );
	stmt.BindColumn( 19, SQLDT_UINT16, &tmp.spl );
	stmt.BindColumn( 20, SQLDT_UINT16, &tmp.con );
	stmt.BindColumn( 21, SQLDT_UINT16, &tmp.crt );
	stmt.BindColumn( 22, SQLDT_UINT8, &tmp.chg_2nd );
	stmt.BindColumn( 23, SQLDT_UINT8, &tmp.chg_3rd );
	stmt.BindColumn( 24, SQLDT_UINT8, &tmp.chg_4th );
	stmt.BindColumn( 25, SQLDT_UINT16, &tmp.hair );
	stmt.BindColumn( 26, SQLDT_UINT16, &tmp.hair_color );
	stmt.BindColumn( 27, SQLDT_UINT16, &tmp.clothes_color );
	stmt.BindColumn( 28, SQLDT_UINT32, &tmp.equip_weight );
	stmt.BindColumn( 29, SQLDT_UINT16, &tmp.equip_count );
	stmt.BindColumn( 30, SQLDT_UINT32, &tmp.created );
	stmt.BindColumn( 31, SQLDT_UINT32, &tmp.last_used );

	while( SQL_SUCCESS == stmt.NextRow() ){
		out.push_back( tmp );
	}

	return true;
}

bool jobrecord_skills_fromsql( uint32 char_id, uint32 record_id, std::vector<s_skill>& out ){
	SqlStmt stmt{ *sql_handle };
	s_skill tmp{};

	if( SQL_ERROR == stmt.Prepare( "SELECT `id`,`lv`,`flag` FROM `%s` WHERE `char_id` = '%u' AND `record_id` = '%u'", schema_config.job_record_skill_table, char_id, record_id )
		|| SQL_ERROR == stmt.Execute() ){
		SqlStmt_ShowDebug( stmt );
		return false;
	}

	stmt.BindColumn( 0, SQLDT_UINT16, &tmp.id );
	stmt.BindColumn( 1, SQLDT_UINT8, &tmp.lv );
	stmt.BindColumn( 2, SQLDT_UINT8, &tmp.flag );

	while( SQL_SUCCESS == stmt.NextRow() ){
		out.push_back( tmp );
	}

	return true;
}

bool jobrecord_regs_fromsql( uint32 char_id, uint32 record_id, std::vector<s_job_record_reg>& out ){
	SqlStmt stmt{ *sql_handle };
	s_job_record_reg tmp{};

	if( SQL_ERROR == stmt.Prepare( "SELECT `key`,`index`,`is_str`,`value_num`,`value_str` FROM `%s` WHERE `char_id` = '%u' AND `record_id` = '%u'", schema_config.job_record_reg_table, char_id, record_id )
		|| SQL_ERROR == stmt.Execute() ){
		SqlStmt_ShowDebug( stmt );
		return false;
	}

	stmt.BindColumn( 0, SQLDT_STRING, &tmp.key, sizeof( tmp.key ) );
	stmt.BindColumn( 1, SQLDT_UINT32, &tmp.index );
	stmt.BindColumn( 2, SQLDT_UINT8, &tmp.is_str );
	stmt.BindColumn( 3, SQLDT_INT64, &tmp.value_num );
	stmt.BindColumn( 4, SQLDT_STRING, &tmp.value_str, sizeof( tmp.value_str ) );

	while( SQL_SUCCESS == stmt.NextRow() ){
		out.push_back( tmp );
	}

	return true;
}

bool jobrecord_quests_fromsql( uint32 char_id, uint32 record_id, std::vector<quest>& out ){
	SqlStmt stmt{ *sql_handle };
	quest tmp{};
	int32 state = 0; // bound separately: e_quest_state is a single byte

	if( SQL_ERROR == stmt.Prepare( "SELECT `quest_id`,`state`,`time`,`count1`,`count2`,`count3` FROM `%s` WHERE `char_id` = '%u' AND `record_id` = '%u'", schema_config.job_record_quest_table, char_id, record_id )
		|| SQL_ERROR == stmt.Execute() ){
		SqlStmt_ShowDebug( stmt );
		return false;
	}

	stmt.BindColumn( 0, SQLDT_INT32, &tmp.quest_id );
	stmt.BindColumn( 1, SQLDT_INT32, &state );
	stmt.BindColumn( 2, SQLDT_UINT32, &tmp.time );
	stmt.BindColumn( 3, SQLDT_INT32, &tmp.count[0] );
	stmt.BindColumn( 4, SQLDT_INT32, &tmp.count[1] );
	stmt.BindColumn( 5, SQLDT_INT32, &tmp.count[2] );

	while( SQL_SUCCESS == stmt.NextRow() ){
		tmp.state = static_cast<e_quest_state>( state );
		out.push_back( tmp );
	}

	return true;
}

bool jobrecord_hotkeys_fromsql( uint32 char_id, uint32 record_id, std::vector<s_job_record_hotkey>& out ){
	SqlStmt stmt{ *sql_handle };
	s_job_record_hotkey tmp{};

	if( SQL_ERROR == stmt.Prepare( "SELECT `hotkey`,`type`,`itemskill_id`,`skill_lvl` FROM `%s` WHERE `char_id` = '%u' AND `record_id` = '%u'", schema_config.job_record_hotkey_table, char_id, record_id )
		|| SQL_ERROR == stmt.Execute() ){
		SqlStmt_ShowDebug( stmt );
		return false;
	}

	stmt.BindColumn( 0, SQLDT_UINT8, &tmp.slot );
	stmt.BindColumn( 1, SQLDT_UCHAR, &tmp.hotkey.type );
	stmt.BindColumn( 2, SQLDT_UINT32, &tmp.hotkey.id );
	stmt.BindColumn( 3, SQLDT_UINT16, &tmp.hotkey.lv );

	while( SQL_SUCCESS == stmt.NextRow() ){
		out.push_back( tmp );
	}

	return true;
}

bool jobrecord_items_fromsql( uint32 char_id, uint32 record_id, std::vector<item>& out ){
	SqlStmt stmt{ *sql_handle };
	StringBuf buf;
	item tmp{};
	int32 i;

	StringBuf_Init( &buf );
	StringBuf_AppendStr( &buf, "SELECT `id`,`nameid`,`amount`,`equip`,`identify`,`refine`,`attribute`,`expire_time`,`bound`,`unique_id`,`enchantgrade`,`favorite`" );
	for( i = 0; i < MAX_SLOTS; ++i ){
		StringBuf_Printf( &buf, ",`card%d`", i );
	}
	for( i = 0; i < MAX_ITEM_RDM_OPT; ++i ){
		StringBuf_Printf( &buf, ",`option_id%d`,`option_val%d`,`option_parm%d`", i, i, i );
	}
	StringBuf_Printf( &buf, " FROM `%s` WHERE `char_id` = '%u' AND `record_id` = '%u' ORDER BY `id`", schema_config.job_record_item_table, char_id, record_id );

	if( SQL_ERROR == stmt.PrepareStr( StringBuf_Value( &buf ) ) || SQL_ERROR == stmt.Execute() ){
		SqlStmt_ShowDebug( stmt );
		return false;
	}

	stmt.BindColumn( 0, SQLDT_INT32, &tmp.id );
	stmt.BindColumn( 1, SQLDT_UINT32, &tmp.nameid );
	stmt.BindColumn( 2, SQLDT_INT16, &tmp.amount );
	stmt.BindColumn( 3, SQLDT_UINT32, &tmp.equip );
	stmt.BindColumn( 4, SQLDT_CHAR, &tmp.identify );
	stmt.BindColumn( 5, SQLDT_CHAR, &tmp.refine );
	stmt.BindColumn( 6, SQLDT_CHAR, &tmp.attribute );
	stmt.BindColumn( 7, SQLDT_UINT32, &tmp.expire_time );
	stmt.BindColumn( 8, SQLDT_CHAR, &tmp.bound );
	stmt.BindColumn( 9, SQLDT_ULONGLONG, &tmp.unique_id );
	stmt.BindColumn( 10, SQLDT_INT8, &tmp.enchantgrade );
	stmt.BindColumn( 11, SQLDT_CHAR, &tmp.favorite );
	for( i = 0; i < MAX_SLOTS; ++i ){
		stmt.BindColumn( 12 + i, SQLDT_UINT32, &tmp.card[i] );
	}
	for( i = 0; i < MAX_ITEM_RDM_OPT; ++i ){
		stmt.BindColumn( 12 + MAX_SLOTS + i * 3, SQLDT_INT16, &tmp.option[i].id );
		stmt.BindColumn( 13 + MAX_SLOTS + i * 3, SQLDT_INT16, &tmp.option[i].value );
		stmt.BindColumn( 14 + MAX_SLOTS + i * 3, SQLDT_CHAR, &tmp.option[i].param );
	}

	while( SQL_SUCCESS == stmt.NextRow() ){
		out.push_back( tmp );
	}

	return true;
}

/*==========================================
 * Saving (always called inside a transaction)
 *------------------------------------------*/

bool jobrecord_base_tosql( uint32 char_id, const s_job_record_base& b ){
	SqlStmt stmt{ *sql_handle };
	StringBuf buf;

	StringBuf_Init( &buf );
	StringBuf_Printf( &buf, "REPLACE INTO `%s` (`char_id`,`record_id`,`name`,`class`,`base_level`,`base_exp`,`job_level`,`job_exp`,"
		"`status_point`,`skill_point`,`trait_point`,`str`,`agi`,`vit`,`int`,`dex`,`luk`,`pow`,`sta`,`wis`,`spl`,`con`,`crt`,"
		"`chg_2nd`,`chg_3rd`,`chg_4th`,`hair`,`hair_color`,`clothes_color`,`equip_weight`,`equip_count`,`created`,`last_used`) VALUES "
		"('%u','%u',?,'%u','%u','%" PRIu64 "','%u','%" PRIu64 "','%u','%u','%u',"
		"'%u','%u','%u','%u','%u','%u','%u','%u','%u','%u','%u','%u',"
		"'%u','%u','%u','%u','%u','%u','%u','%u','%u','%u')",
		schema_config.job_record_table, char_id, b.record_id, b.class_, b.base_level, b.base_exp, b.job_level, b.job_exp,
		b.status_point, b.skill_point, b.trait_point,
		b.str, b.agi, b.vit, b.int_, b.dex, b.luk, b.pow, b.sta, b.wis, b.spl, b.con, b.crt,
		b.chg_2nd, b.chg_3rd, b.chg_4th, b.hair, b.hair_color, b.clothes_color, b.equip_weight, b.equip_count, b.created, b.last_used );

	if( SQL_ERROR == stmt.PrepareStr( StringBuf_Value( &buf ) )
		|| SQL_ERROR == stmt.BindParam( 0, SQLDT_STRING, (void*)b.name, strnlen( b.name, NAME_LENGTH ) )
		|| SQL_ERROR == stmt.Execute() ){
		SqlStmt_ShowDebug( stmt );
		return false;
	}

	return true;
}

/// Deletes all rows of one record from a child table
bool jobrecord_clear_rows( const char* table, uint32 char_id, uint32 record_id ){
	if( SQL_ERROR == Sql_Query( sql_handle, "DELETE FROM `%s` WHERE `char_id` = '%u' AND `record_id` = '%u'", table, char_id, record_id ) ){
		Sql_ShowDebug( sql_handle );
		return false;
	}

	return true;
}

bool jobrecord_skills_tosql( uint32 char_id, uint32 record_id, const std::vector<s_skill>& skills ){
	if( !jobrecord_clear_rows( schema_config.job_record_skill_table, char_id, record_id ) ){
		return false;
	}

	StringBuf buf;
	bool first = true;

	StringBuf_Init( &buf );
	StringBuf_Printf( &buf, "INSERT INTO `%s` (`char_id`,`record_id`,`id`,`lv`,`flag`) VALUES ", schema_config.job_record_skill_table );

	for( const s_skill& skill : skills ){
		if( skill.id == 0 || skill.lv == 0 ){
			continue;
		}

		StringBuf_Printf( &buf, "%s('%u','%u','%u','%u','%u')", first ? "" : ",", char_id, record_id, skill.id, skill.lv, skill.flag );
		first = false;
	}

	return first || jobrecord_query( StringBuf_Value( &buf ) );
}

bool jobrecord_regs_tosql( uint32 char_id, uint32 record_id, const std::vector<s_job_record_reg>& regs ){
	if( !jobrecord_clear_rows( schema_config.job_record_reg_table, char_id, record_id ) ){
		return false;
	}

	StringBuf buf;
	bool first = true;
	char esc_key[JOB_RECORD_REG_KEY_LENGTH * 2 + 1];
	char esc_str[JOB_RECORD_REG_STR_LENGTH * 2 + 1];

	StringBuf_Init( &buf );
	StringBuf_Printf( &buf, "INSERT INTO `%s` (`char_id`,`record_id`,`key`,`index`,`is_str`,`value_num`,`value_str`) VALUES ", schema_config.job_record_reg_table );

	for( const s_job_record_reg& reg : regs ){
		if( reg.key[0] == '\0' ){
			continue;
		}

		Sql_EscapeStringLen( sql_handle, esc_key, reg.key, strnlen( reg.key, JOB_RECORD_REG_KEY_LENGTH - 1 ) );
		Sql_EscapeStringLen( sql_handle, esc_str, reg.is_str ? reg.value_str : "", reg.is_str ? strnlen( reg.value_str, JOB_RECORD_REG_STR_LENGTH - 1 ) : 0 );

		StringBuf_Printf( &buf, "%s('%u','%u','%s','%u','%u','%" PRId64 "','%s')", first ? "" : ",",
			char_id, record_id, esc_key, reg.index, reg.is_str ? 1 : 0, reg.is_str ? 0 : reg.value_num, esc_str );
		first = false;
	}

	return first || jobrecord_query( StringBuf_Value( &buf ) );
}

bool jobrecord_quests_tosql( uint32 char_id, uint32 record_id, const std::vector<quest>& quests ){
	if( !jobrecord_clear_rows( schema_config.job_record_quest_table, char_id, record_id ) ){
		return false;
	}

	StringBuf buf;
	bool first = true;

	StringBuf_Init( &buf );
	StringBuf_Printf( &buf, "INSERT INTO `%s` (`char_id`,`record_id`,`quest_id`,`state`,`time`,`count1`,`count2`,`count3`) VALUES ", schema_config.job_record_quest_table );

	for( const quest& q : quests ){
		if( q.quest_id <= 0 ){
			continue;
		}

		StringBuf_Printf( &buf, "%s('%u','%u','%d','%d','%u','%d','%d','%d')", first ? "" : ",",
			char_id, record_id, q.quest_id, static_cast<int32>( q.state ), q.time, q.count[0], q.count[1], q.count[2] );
		first = false;
	}

	return first || jobrecord_query( StringBuf_Value( &buf ) );
}

bool jobrecord_hotkeys_tosql( uint32 char_id, uint32 record_id, const std::vector<s_job_record_hotkey>& hotkeys ){
	if( !jobrecord_clear_rows( schema_config.job_record_hotkey_table, char_id, record_id ) ){
		return false;
	}

	StringBuf buf;
	bool first = true;

	StringBuf_Init( &buf );
	StringBuf_Printf( &buf, "INSERT INTO `%s` (`char_id`,`record_id`,`hotkey`,`type`,`itemskill_id`,`skill_lvl`) VALUES ", schema_config.job_record_hotkey_table );

	for( const s_job_record_hotkey& hk : hotkeys ){
		if( hk.slot >= MAX_HOTKEYS_DB || hk.hotkey.id == 0 ){
			continue;
		}

		StringBuf_Printf( &buf, "%s('%u','%u','%u','%u','%u','%u')", first ? "" : ",",
			char_id, record_id, hk.slot, hk.hotkey.type, hk.hotkey.id, hk.hotkey.lv );
		first = false;
	}

	return first || jobrecord_query( StringBuf_Value( &buf ) );
}

/// Inserts stashed items. Insert-only: rows are never deleted here, so a crash between
/// this save and the inventory save can only duplicate an item, never lose it.
/// Items whose unique_id is already stashed for this record are skipped.
bool jobrecord_items_tosql( uint32 char_id, uint32 record_id, const std::vector<item>& items ){
	if( items.empty() ){
		return true;
	}

	std::unordered_set<uint64> stashed;

	if( SQL_ERROR == Sql_Query( sql_handle, "SELECT `unique_id` FROM `%s` WHERE `char_id` = '%u' AND `record_id` = '%u'", schema_config.job_record_item_table, char_id, record_id ) ){
		Sql_ShowDebug( sql_handle );
		return false;
	}

	while( SQL_SUCCESS == Sql_NextRow( sql_handle ) ){
		char* data;

		Sql_GetData( sql_handle, 0, &data, nullptr );
		stashed.insert( strtoull( data, nullptr, 10 ) );
	}

	Sql_FreeResult( sql_handle );

	StringBuf buf;
	bool first = true;
	int32 i;

	StringBuf_Init( &buf );
	StringBuf_Printf( &buf, "INSERT INTO `%s` (`char_id`,`record_id`,`nameid`,`amount`,`equip`,`identify`,`refine`,`attribute`,`expire_time`,`favorite`,`bound`,`unique_id`,`enchantgrade`", schema_config.job_record_item_table );
	for( i = 0; i < MAX_SLOTS; ++i ){
		StringBuf_Printf( &buf, ",`card%d`", i );
	}
	for( i = 0; i < MAX_ITEM_RDM_OPT; ++i ){
		StringBuf_Printf( &buf, ",`option_id%d`,`option_val%d`,`option_parm%d`", i, i, i );
	}
	StringBuf_AppendStr( &buf, ") VALUES " );

	for( const item& it : items ){
		if( it.nameid == 0 || it.amount <= 0 ){
			continue;
		}

		if( it.unique_id != 0 && !stashed.insert( it.unique_id ).second ){
			continue; // Already stashed (re-sent save)
		}

		StringBuf_Printf( &buf, "%s('%u','%u','%u','%d','%u','%d','%d','%d','%u','%d','%d','%" PRIu64 "','%d'", first ? "" : ",",
			char_id, record_id, it.nameid, it.amount, it.equip, it.identify, it.refine, it.attribute, it.expire_time,
			it.favorite, it.bound, it.unique_id, it.enchantgrade );
		for( i = 0; i < MAX_SLOTS; ++i ){
			StringBuf_Printf( &buf, ",'%u'", it.card[i] );
		}
		for( i = 0; i < MAX_ITEM_RDM_OPT; ++i ){
			StringBuf_Printf( &buf, ",'%d','%d','%d'", it.option[i].id, it.option[i].value, it.option[i].param );
		}
		StringBuf_AppendStr( &buf, ")" );
		first = false;
	}

	return first || jobrecord_query( StringBuf_Value( &buf ) );
}

bool jobrecord_state_tosql( uint32 char_id, const s_job_record_upsert_end& end ){
	if( SQL_ERROR == Sql_Query( sql_handle, "INSERT INTO `%s` (`char_id`,`active_record`,`pending_to`,`next_record_id`) VALUES ('%u','0','%u','%u')"
		" ON DUPLICATE KEY UPDATE `next_record_id` = GREATEST(`next_record_id`, VALUES(`next_record_id`))%s",
		schema_config.job_record_state_table, char_id, end.set_pending ? end.pending_to : 0, end.next_record_id,
		end.set_pending ? ", `pending_to` = VALUES(`pending_to`)" : "" ) ){
		Sql_ShowDebug( sql_handle );
		return false;
	}

	return true;
}

bool jobrecord_upsert_tosql( uint32 char_id, const s_job_record_upsert_buffer& b, const s_job_record_upsert_end& end ){
	if( !jobrecord_transaction_begin() ){
		return false;
	}

	if( !jobrecord_base_tosql( char_id, b.base )
		|| !jobrecord_skills_tosql( char_id, b.record_id, b.skills )
		|| !jobrecord_regs_tosql( char_id, b.record_id, b.regs )
		|| !jobrecord_quests_tosql( char_id, b.record_id, b.quests )
		|| !jobrecord_hotkeys_tosql( char_id, b.record_id, b.hotkeys )
		|| !jobrecord_items_tosql( char_id, b.record_id, b.items )
		|| !jobrecord_state_tosql( char_id, end ) ){
		jobrecord_transaction_rollback();
		return false;
	}

	return jobrecord_transaction_commit();
}

/*==========================================
 * Sending to the map-server
 *------------------------------------------*/

void mapif_jobrecord_ack( int32 fd, uint16 cmd, uint32 char_id, uint32 id, bool success ){
	WFIFOHEAD( fd, 11 );
	WFIFOW( fd, 0 ) = cmd;
	WFIFOL( fd, 2 ) = char_id;
	WFIFOL( fd, 6 ) = id;
	WFIFOB( fd, 10 ) = success ? 1 : 0;
	WFIFOSET( fd, 11 );
}

/// Sends one section of a record, split into as many JOBRECORD_LOAD_ACK packets as needed
template <typename T>
void mapif_jobrecord_load_section( int32 fd, uint32 char_id, uint32 record_id, e_job_record_section section, const T* data, size_t count ){
	const size_t per_chunk = JOB_RECORD_MAX_CHUNK_PAYLOAD / sizeof( T );

	for( size_t sent = 0; sent < count; ){
		size_t n = std::min( per_chunk, count - sent );
		uint16 len = static_cast<uint16>( JOBRECORD_LOAD_ACK_HEADER + n * sizeof( T ) );

		WFIFOHEAD( fd, len );
		WFIFOW( fd, 0 ) = JOBRECORD_LOAD_ACK;
		WFIFOW( fd, 2 ) = len;
		WFIFOL( fd, 4 ) = char_id;
		WFIFOL( fd, 8 ) = record_id;
		WFIFOB( fd, 12 ) = section;
		WFIFOB( fd, 13 ) = JOBRECORD_LOAD_OK;
		WFIFOW( fd, 14 ) = static_cast<uint16>( n );
		memcpy( WFIFOP( fd, JOBRECORD_LOAD_ACK_HEADER ), data + sent, n * sizeof( T ) );
		WFIFOSET( fd, len );

		sent += n;
	}
}

void mapif_jobrecord_load_end( int32 fd, uint32 char_id, uint32 record_id, e_job_record_load_result result ){
	WFIFOHEAD( fd, JOBRECORD_LOAD_ACK_HEADER );
	WFIFOW( fd, 0 ) = JOBRECORD_LOAD_ACK;
	WFIFOW( fd, 2 ) = JOBRECORD_LOAD_ACK_HEADER;
	WFIFOL( fd, 4 ) = char_id;
	WFIFOL( fd, 8 ) = record_id;
	WFIFOB( fd, 12 ) = JOBRECORD_SECTION_END;
	WFIFOB( fd, 13 ) = result;
	WFIFOW( fd, 14 ) = 0;
	WFIFOSET( fd, JOBRECORD_LOAD_ACK_HEADER );
}

/*==========================================
 * Parsing requests from the map-server
 *------------------------------------------*/

/// JOBRECORD_LIST_REQ: sends all record summaries and the state row
void mapif_parse_jobrecord_list( int32 fd ){
	uint16 len = RFIFOW( fd, 2 );
	uint32 char_id = RFIFOL( fd, 4 );
	uint8 skill_count = std::min<uint8>( RFIFOB( fd, 8 ), JOB_RECORD_MAX_SUMMARY_SKILLS );
	uint16 skill_ids[JOB_RECORD_MAX_SUMMARY_SKILLS] = {};

	if( len < JOBRECORD_LIST_REQ_HEADER + skill_count * 2 ){
		ShowError( "mapif_parse_jobrecord_list: Invalid packet length %d for character %u.\n", len, char_id );
		skill_count = 0;
	}

	for( uint8 i = 0; i < skill_count; i++ ){
		skill_ids[i] = RFIFOW( fd, JOBRECORD_LIST_REQ_HEADER + i * 2 );
	}

	uint32 active = 0, pending_to = 0, next_record_id = 1;

	if( SQL_ERROR == Sql_Query( sql_handle, "SELECT `active_record`,`pending_to`,`next_record_id` FROM `%s` WHERE `char_id` = '%u'", schema_config.job_record_state_table, char_id ) ){
		Sql_ShowDebug( sql_handle );
	}else if( SQL_SUCCESS == Sql_NextRow( sql_handle ) ){
		char* data;

		Sql_GetData( sql_handle, 0, &data, nullptr ); active = strtoul( data, nullptr, 10 );
		Sql_GetData( sql_handle, 1, &data, nullptr ); pending_to = strtoul( data, nullptr, 10 );
		Sql_GetData( sql_handle, 2, &data, nullptr ); next_record_id = strtoul( data, nullptr, 10 );
	}
	Sql_FreeResult( sql_handle );

	std::vector<s_job_record_base> bases;

	jobrecord_bases_fromsql( char_id, 0, bases );

	std::vector<s_job_record_summary> summaries( bases.size() );
	std::unordered_map<uint32, size_t> index_of;

	for( size_t i = 0; i < bases.size(); i++ ){
		summaries[i] = {};
		summaries[i].base = bases[i];
		index_of[bases[i].record_id] = i;
	}

	if( skill_count > 0 && !summaries.empty() ){
		StringBuf buf;

		StringBuf_Init( &buf );
		StringBuf_Printf( &buf, "SELECT `record_id`,`id`,`lv` FROM `%s` WHERE `char_id` = '%u' AND `id` IN (", schema_config.job_record_skill_table, char_id );
		for( uint8 i = 0; i < skill_count; i++ ){
			StringBuf_Printf( &buf, "%s'%u'", i ? "," : "", skill_ids[i] );
		}
		StringBuf_AppendStr( &buf, ")" );

		if( SQL_ERROR == Sql_QueryStr( sql_handle, StringBuf_Value( &buf ) ) ){
			Sql_ShowDebug( sql_handle );
		}else{
			while( SQL_SUCCESS == Sql_NextRow( sql_handle ) ){
				char* data;

				Sql_GetData( sql_handle, 0, &data, nullptr ); uint32 record_id = strtoul( data, nullptr, 10 );
				Sql_GetData( sql_handle, 1, &data, nullptr ); uint16 skill_id = static_cast<uint16>( strtoul( data, nullptr, 10 ) );
				Sql_GetData( sql_handle, 2, &data, nullptr ); uint8 lv = static_cast<uint8>( strtoul( data, nullptr, 10 ) );

				auto it = index_of.find( record_id );

				if( it == index_of.end() ){
					continue;
				}

				for( uint8 i = 0; i < skill_count; i++ ){
					if( skill_ids[i] == skill_id ){
						summaries[it->second].skill_lv[i] = lv;
					}
				}
			}
		}
		Sql_FreeResult( sql_handle );
	}

	const size_t per_chunk = JOB_RECORD_MAX_CHUNK_PAYLOAD / sizeof( s_job_record_summary );
	size_t sent = 0;

	do{
		size_t n = std::min( per_chunk, summaries.size() - sent );
		uint16 plen = static_cast<uint16>( JOBRECORD_LIST_ACK_HEADER + n * sizeof( s_job_record_summary ) );

		WFIFOHEAD( fd, plen );
		WFIFOW( fd, 0 ) = JOBRECORD_LIST_ACK;
		WFIFOW( fd, 2 ) = plen;
		WFIFOL( fd, 4 ) = char_id;
		WFIFOL( fd, 8 ) = active;
		WFIFOL( fd, 12 ) = pending_to;
		WFIFOL( fd, 16 ) = next_record_id;
		WFIFOB( fd, 20 ) = ( sent + n >= summaries.size() ) ? 1 : 0;
		WFIFOB( fd, 21 ) = skill_count;
		WFIFOW( fd, 22 ) = static_cast<uint16>( n );
		if( n > 0 ){
			memcpy( WFIFOP( fd, JOBRECORD_LIST_ACK_HEADER ), summaries.data() + sent, n * sizeof( s_job_record_summary ) );
		}
		WFIFOSET( fd, plen );

		sent += n;
	}while( sent < summaries.size() );
}

/// JOBRECORD_LOAD_REQ: sends one full record
void mapif_parse_jobrecord_load( int32 fd ){
	uint32 char_id = RFIFOL( fd, 2 );
	uint32 record_id = RFIFOL( fd, 6 );
	std::vector<s_job_record_base> bases;

	if( !jobrecord_bases_fromsql( char_id, record_id, bases ) ){
		mapif_jobrecord_load_end( fd, char_id, record_id, JOBRECORD_LOAD_ERROR );
		return;
	}

	if( bases.empty() ){
		mapif_jobrecord_load_end( fd, char_id, record_id, JOBRECORD_LOAD_NOT_FOUND );
		return;
	}

	std::vector<s_skill> skills;
	std::vector<s_job_record_reg> regs;
	std::vector<quest> quests;
	std::vector<s_job_record_hotkey> hotkeys;
	std::vector<item> items;

	if( !jobrecord_skills_fromsql( char_id, record_id, skills )
		|| !jobrecord_regs_fromsql( char_id, record_id, regs )
		|| !jobrecord_quests_fromsql( char_id, record_id, quests )
		|| !jobrecord_hotkeys_fromsql( char_id, record_id, hotkeys )
		|| !jobrecord_items_fromsql( char_id, record_id, items ) ){
		mapif_jobrecord_load_end( fd, char_id, record_id, JOBRECORD_LOAD_ERROR );
		return;
	}

	mapif_jobrecord_load_section( fd, char_id, record_id, JOBRECORD_SECTION_BASE, bases.data(), 1 );
	mapif_jobrecord_load_section( fd, char_id, record_id, JOBRECORD_SECTION_SKILL, skills.data(), skills.size() );
	mapif_jobrecord_load_section( fd, char_id, record_id, JOBRECORD_SECTION_REG, regs.data(), regs.size() );
	mapif_jobrecord_load_section( fd, char_id, record_id, JOBRECORD_SECTION_QUEST, quests.data(), quests.size() );
	mapif_jobrecord_load_section( fd, char_id, record_id, JOBRECORD_SECTION_HOTKEY, hotkeys.data(), hotkeys.size() );
	mapif_jobrecord_load_section( fd, char_id, record_id, JOBRECORD_SECTION_ITEM, items.data(), items.size() );
	mapif_jobrecord_load_end( fd, char_id, record_id, JOBRECORD_LOAD_OK );
}

/// JOBRECORD_UPSERT: collects chunks, saves the record in one transaction on JOBRECORD_SECTION_END
void mapif_parse_jobrecord_upsert( int32 fd ){
	uint16 len = RFIFOW( fd, 2 );
	uint32 char_id = RFIFOL( fd, 4 );
	uint32 record_id = RFIFOL( fd, 8 );
	uint8 section = RFIFOB( fd, 12 );
	uint16 count = RFIFOW( fd, 13 );
	const uint8* data = RFIFOP( fd, JOBRECORD_UPSERT_HEADER );
	size_t payload = ( len >= JOBRECORD_UPSERT_HEADER ) ? len - JOBRECORD_UPSERT_HEADER : 0;

	if( section == JOBRECORD_SECTION_BASE ){
		s_job_record_upsert_buffer& b = jobrecord_upsert_buffers[char_id];

		b = {};
		b.fd = fd;
		b.record_id = record_id;

		if( count != 1 || payload != sizeof( s_job_record_base ) ){
			ShowError( "mapif_parse_jobrecord_upsert: Invalid base chunk for character %u record %u.\n", char_id, record_id );
			b.invalid = true;
		}else{
			memcpy( &b.base, data, sizeof( s_job_record_base ) );
			b.base.record_id = record_id;
		}
		return;
	}

	auto it = jobrecord_upsert_buffers.find( char_id );

	if( it == jobrecord_upsert_buffers.end() || it->second.record_id != record_id ){
		ShowError( "mapif_parse_jobrecord_upsert: Chunk %d without base chunk for character %u record %u.\n", section, char_id, record_id );

		if( section == JOBRECORD_SECTION_END ){
			mapif_jobrecord_ack( fd, JOBRECORD_UPSERT_ACK, char_id, record_id, false );
		}
		return;
	}

	s_job_record_upsert_buffer& b = it->second;
	bool ok = true;

	switch( section ){
		case JOBRECORD_SECTION_SKILL: ok = jobrecord_append( b.skills, data, count, payload ); break;
		case JOBRECORD_SECTION_REG: ok = jobrecord_append( b.regs, data, count, payload ); break;
		case JOBRECORD_SECTION_QUEST: ok = jobrecord_append( b.quests, data, count, payload ); break;
		case JOBRECORD_SECTION_HOTKEY: ok = jobrecord_append( b.hotkeys, data, count, payload ); break;
		case JOBRECORD_SECTION_ITEM: ok = jobrecord_append( b.items, data, count, payload ); break;
		case JOBRECORD_SECTION_END: {
			bool success = false;

			if( b.invalid || payload != sizeof( s_job_record_upsert_end ) ){
				ShowError( "mapif_parse_jobrecord_upsert: Rejecting invalid upsert for character %u record %u.\n", char_id, record_id );
			}else{
				s_job_record_upsert_end end;

				memcpy( &end, data, sizeof( end ) );
				success = jobrecord_upsert_tosql( char_id, b, end );
			}

			mapif_jobrecord_ack( fd, JOBRECORD_UPSERT_ACK, char_id, record_id, success );
			jobrecord_upsert_buffers.erase( it );
			return;
		}
		default:
			ShowError( "mapif_parse_jobrecord_upsert: Unknown section %d for character %u record %u.\n", section, char_id, record_id );
			ok = false;
			break;
	}

	if( !ok ){
		ShowError( "mapif_parse_jobrecord_upsert: Invalid chunk size for section %d, character %u record %u.\n", section, char_id, record_id );
		b.invalid = true;
	}
}

/// JOBRECORD_COMMIT: sets the active record, clears the incoming record's stash and the switch journal.
/// Sent after the inventory and status saves, so stashed rows are only removed once the inventory holds them.
void mapif_parse_jobrecord_commit( int32 fd ){
	uint32 char_id = RFIFOL( fd, 2 );
	uint32 active = RFIFOL( fd, 6 );
	uint32 clear_stash = RFIFOL( fd, 10 );
	bool success = jobrecord_transaction_begin();

	if( success && SQL_ERROR == Sql_Query( sql_handle, "INSERT INTO `%s` (`char_id`,`active_record`,`pending_to`,`next_record_id`) VALUES ('%u','%u','0','1')"
		" ON DUPLICATE KEY UPDATE `active_record` = VALUES(`active_record`), `pending_to` = '0'", schema_config.job_record_state_table, char_id, active ) ){
		Sql_ShowDebug( sql_handle );
		success = false;
	}

	if( success && clear_stash != 0 ){
		if( !jobrecord_clear_rows( schema_config.job_record_item_table, char_id, clear_stash )
			|| SQL_ERROR == Sql_Query( sql_handle, "UPDATE `%s` SET `equip_weight` = '0', `equip_count` = '0' WHERE `char_id` = '%u' AND `record_id` = '%u'", schema_config.job_record_table, char_id, clear_stash ) ){
			Sql_ShowDebug( sql_handle );
			success = false;
		}
	}

	if( success ){
		success = jobrecord_transaction_commit();
	}else{
		jobrecord_transaction_rollback();
	}

	mapif_jobrecord_ack( fd, JOBRECORD_COMMIT_ACK, char_id, active, success );
}

/// JOBRECORD_RENAME: renames one record
void mapif_parse_jobrecord_rename( int32 fd ){
	uint32 char_id = RFIFOL( fd, 2 );
	uint32 record_id = RFIFOL( fd, 6 );
	char name[NAME_LENGTH];
	SqlStmt stmt{ *sql_handle };
	bool success = true;

	safestrncpy( name, RFIFOCP( fd, 10 ), NAME_LENGTH );

	if( SQL_ERROR == stmt.Prepare( "UPDATE `%s` SET `name` = ? WHERE `char_id` = '%u' AND `record_id` = '%u'", schema_config.job_record_table, char_id, record_id )
		|| SQL_ERROR == stmt.BindParam( 0, SQLDT_STRING, name, strnlen( name, NAME_LENGTH ) )
		|| SQL_ERROR == stmt.Execute() ){
		SqlStmt_ShowDebug( stmt );
		success = false;
	}

	mapif_jobrecord_ack( fd, JOBRECORD_RENAME_ACK, char_id, record_id, success );
}

/*==========================================
 * Deleting a record (phase 9)
 *------------------------------------------*/

/// Unique ids of the items the character already owns: inventory, cart and the account's storages
bool jobrecord_owned_unique_ids( uint32 char_id, std::unordered_set<uint64>& out ){
	uint32 account_id = 0;

	if( SQL_ERROR == Sql_Query( sql_handle, "SELECT `account_id` FROM `%s` WHERE `char_id` = '%u'", schema_config.char_db, char_id ) ){
		Sql_ShowDebug( sql_handle );
		return false;
	}
	if( SQL_SUCCESS == Sql_NextRow( sql_handle ) ){
		char* data;

		Sql_GetData( sql_handle, 0, &data, nullptr );
		account_id = strtoul( data, nullptr, 10 );
	}
	Sql_FreeResult( sql_handle );

	auto collect = [&out]( const char* table, const char* owner_column, uint32 owner ) -> bool {
		if( SQL_ERROR == Sql_Query( sql_handle, "SELECT `unique_id` FROM `%s` WHERE `%s` = '%u' AND `unique_id` <> '0'", table, owner_column, owner ) ){
			Sql_ShowDebug( sql_handle );
			return false;
		}

		while( SQL_SUCCESS == Sql_NextRow( sql_handle ) ){
			char* data;

			Sql_GetData( sql_handle, 0, &data, nullptr );
			out.insert( strtoull( data, nullptr, 10 ) );
		}

		Sql_FreeResult( sql_handle );
		return true;
	};

	if( !collect( schema_config.inventory_db, "char_id", char_id ) || !collect( schema_config.cart_db, "char_id", char_id ) ){
		return false;
	}

	if( account_id != 0 ){
		for( const auto& storage : interServerDb ){
			if( !collect( storage.second->table, "account_id", account_id ) ){
				return false;
			}
		}
	}

	return true;
}

/// Inserts one mail with its attachments. Same SQL as mail_savemessage, but without its own
/// transaction: the caller's transaction must cover the mail and the deletion of the stash rows.
bool jobrecord_mail_tosql( mail_message& msg ){
	SqlStmt stmt{ *sql_handle };
	StringBuf buf;
	int32 j;

	StringBuf_Init( &buf );
	StringBuf_Printf( &buf, "INSERT INTO `%s` (`send_name`, `send_id`, `dest_name`, `dest_id`, `title`, `message`, `time`, `status`, `zeny`,`type`)"
		" VALUES (?, '%d', ?, '%d', ?, ?, '%lu', '%d', '%d', '%d')", schema_config.mail_db,
		msg.send_id, msg.dest_id, (unsigned long)msg.timestamp, msg.status, msg.zeny, msg.type );

	if( SQL_SUCCESS != stmt.PrepareStr( StringBuf_Value( &buf ) )
		|| SQL_SUCCESS != stmt.BindParam( 0, SQLDT_STRING, msg.send_name, strnlen( msg.send_name, NAME_LENGTH ) )
		|| SQL_SUCCESS != stmt.BindParam( 1, SQLDT_STRING, msg.dest_name, strnlen( msg.dest_name, NAME_LENGTH ) )
		|| SQL_SUCCESS != stmt.BindParam( 2, SQLDT_STRING, msg.title, strnlen( msg.title, MAIL_TITLE_LENGTH ) )
		|| SQL_SUCCESS != stmt.BindParam( 3, SQLDT_STRING, msg.body, strnlen( msg.body, MAIL_BODY_LENGTH ) )
		|| SQL_SUCCESS != stmt.Execute() ){
		SqlStmt_ShowDebug( stmt );
		msg.id = 0;
		return false;
	}

	msg.id = static_cast<int32>( stmt.LastInsertId() );

	StringBuf_Clear( &buf );
	StringBuf_Printf( &buf, "INSERT INTO `%s` (`id`, `index`, `amount`, `nameid`, `refine`, `attribute`, `identify`, `unique_id`, `bound`, `enchantgrade`", schema_config.mail_attachment_db );
	for( j = 0; j < MAX_SLOTS; j++ ){
		StringBuf_Printf( &buf, ", `card%d`", j );
	}
	for( j = 0; j < MAX_ITEM_RDM_OPT; ++j ){
		StringBuf_Printf( &buf, ", `option_id%d`, `option_val%d`, `option_parm%d`", j, j, j );
	}
	StringBuf_AppendStr( &buf, ") VALUES " );

	bool found = false;

	for( int32 i = 0; i < MAIL_MAX_ITEM; i++ ){
		const item& it = msg.item[i];

		if( it.nameid == 0 ){
			continue;
		}

		StringBuf_Printf( &buf, "%s('%d', '%d', '%d', '%u', '%d', '%d', '%d', '%" PRIu64 "', '%d', '%d'", found ? "," : "",
			msg.id, i, it.amount, it.nameid, it.refine, it.attribute, it.identify, it.unique_id, it.bound, it.enchantgrade );
		for( j = 0; j < MAX_SLOTS; j++ ){
			StringBuf_Printf( &buf, ", '%u'", it.card[j] );
		}
		for( j = 0; j < MAX_ITEM_RDM_OPT; ++j ){
			StringBuf_Printf( &buf, ", '%d', '%d', '%d'", it.option[j].id, it.option[j].value, it.option[j].param );
		}
		StringBuf_AppendStr( &buf, ")" );
		found = true;
	}

	return !found || jobrecord_query( StringBuf_Value( &buf ) );
}

/// JOBRECORD_DELETE: mails the record's stashed equipment to the character and deletes the record,
/// all in one transaction. Items the character already owns (duplicates left by an interrupted
/// save) are not mailed. The mails have sender id 0, so they never expire (§2.4 of the design).
void mapif_parse_jobrecord_delete( int32 fd ){
	uint32 char_id = RFIFOL( fd, 2 );
	uint32 record_id = RFIFOL( fd, 6 );
	char char_name[NAME_LENGTH];
	std::vector<s_job_record_base> bases;
	std::vector<item> items;
	std::unordered_set<uint64> owned;
	std::vector<mail_message> mails;
	uint32 active = 0;
	bool success = false;

	safestrncpy( char_name, RFIFOCP( fd, 10 ), NAME_LENGTH );

	if( SQL_ERROR == Sql_Query( sql_handle, "SELECT `active_record` FROM `%s` WHERE `char_id` = '%u'", schema_config.job_record_state_table, char_id ) ){
		Sql_ShowDebug( sql_handle );
	}else if( SQL_SUCCESS == Sql_NextRow( sql_handle ) ){
		char* data;

		Sql_GetData( sql_handle, 0, &data, nullptr );
		active = strtoul( data, nullptr, 10 );
	}
	Sql_FreeResult( sql_handle );

	if( record_id == 0 || record_id == active ){
		ShowError( "mapif_parse_jobrecord_delete: Refusing to delete the active record %u of character %u.\n", record_id, char_id );
	}else if( jobrecord_bases_fromsql( char_id, record_id, bases ) && !bases.empty()
		&& jobrecord_items_fromsql( char_id, record_id, items )
		&& jobrecord_owned_unique_ids( char_id, owned ) ){
		// Group the remaining items by MAIL_MAX_ITEM
		for( const item& it : items ){
			if( it.unique_id != 0 && owned.count( it.unique_id ) > 0 ){
				ShowWarning( "Job record: character %u already owns item %u (unique id %" PRIu64 "), not mailing the stashed copy.\n", char_id, it.nameid, it.unique_id );
				continue;
			}

			if( mails.empty() || mails.back().item[MAIL_MAX_ITEM - 1].nameid != 0 ){
				mail_message msg = {};

				msg.send_id = 0; // System mail: never returned or deleted
				safestrncpy( msg.send_name, "Job Record", NAME_LENGTH );
				msg.dest_id = char_id;
				safestrncpy( msg.dest_name, char_name, NAME_LENGTH );
				safesnprintf( msg.title, MAIL_TITLE_LENGTH, "Equipment of %s", bases[0].name );
				safesnprintf( msg.body, MAIL_BODY_LENGTH, "The job record \"%s\" was deleted. This is the equipment it was wearing.", bases[0].name );
				msg.status = MAIL_NEW;
				msg.type = MAIL_INBOX_NORMAL;
				msg.timestamp = time( nullptr );
				mails.push_back( msg );
			}

			item* slot = mails.back().item;
			int32 i = 0;

			while( slot[i].nameid != 0 ){
				i++;
			}

			slot[i] = it;
			slot[i].id = 0;
			slot[i].equip = 0;
			slot[i].equipSwitch = 0;
		}

		if( jobrecord_transaction_begin() ){
			success = true;

			for( mail_message& msg : mails ){
				if( !jobrecord_mail_tosql( msg ) ){
					success = false;
					break;
				}
			}

			const char* tables[] = {
				schema_config.job_record_table, schema_config.job_record_skill_table, schema_config.job_record_reg_table,
				schema_config.job_record_quest_table, schema_config.job_record_hotkey_table, schema_config.job_record_item_table,
			};

			for( size_t t = 0; success && t < ARRAYLENGTH( tables ); t++ ){
				success = jobrecord_clear_rows( tables[t], char_id, record_id );
			}

			if( success ){
				success = jobrecord_transaction_commit();
			}else{
				jobrecord_transaction_rollback();
			}
		}
	}

	if( success ){
		// Notify the recipient only now that the mails are committed
		for( mail_message& msg : mails ){
			mapif_Mail_new( &msg );
		}

		ShowInfo( "Job record %u of character %u deleted, %u mail(s) sent.\n", record_id, char_id, static_cast<uint32>( mails.size() ) );
	}

	WFIFOHEAD( fd, 13 );
	WFIFOW( fd, 0 ) = JOBRECORD_DELETE_ACK;
	WFIFOL( fd, 2 ) = char_id;
	WFIFOL( fd, 6 ) = record_id;
	WFIFOB( fd, 10 ) = success ? 1 : 0;
	WFIFOW( fd, 11 ) = static_cast<uint16>( success ? mails.size() : 0 );
	WFIFOSET( fd, 13 );
}

} // namespace

/**
 * Parses job record packets from the map-server.
 * @see inter_parse_frommap
 */
int32 inter_jobrecord_parse_frommap( int32 fd ){
	switch( RFIFOW( fd, 0 ) ){
		case JOBRECORD_LIST_REQ: mapif_parse_jobrecord_list( fd ); break;
		case JOBRECORD_LOAD_REQ: mapif_parse_jobrecord_load( fd ); break;
		case JOBRECORD_UPSERT: mapif_parse_jobrecord_upsert( fd ); break;
		case JOBRECORD_COMMIT: mapif_parse_jobrecord_commit( fd ); break;
		case JOBRECORD_RENAME: mapif_parse_jobrecord_rename( fd ); break;
		case JOBRECORD_DELETE: mapif_parse_jobrecord_delete( fd ); break;
		default:
			return 0;
	}

	return 1;
}
