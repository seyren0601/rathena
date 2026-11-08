// Copyright (c) rAthena Dev Teams - Licensed under GNU GPL
// For more information, see LICENCE in the main folder

// [Job Record] Shared data structures and inter-server packet layout.
// See docs/subsystems/map/job-record-system-design.md

#ifndef JOB_RECORD_HPP
#define JOB_RECORD_HPP

#include <config/core.hpp> // t_exp (via config/const.hpp)

#include "cbasetypes.hpp"
#include "mmo.hpp"

/// Length of a registry key, matches `char_reg_num`.`key` varchar(32) (+1 for the terminator)
#define JOB_RECORD_REG_KEY_LENGTH 33
/// Length of a string registry value, matches `char_job_record_reg`.`value_str` varchar(254) (+1)
#define JOB_RECORD_REG_STR_LENGTH 255
/// Maximum number of skill ids the map-server can ask about in a record list request (weight preview)
#define JOB_RECORD_MAX_SUMMARY_SKILLS 4
/// Maximum payload of one chunked packet, keeps every packet below the 16-bit length limit
#define JOB_RECORD_MAX_CHUNK_PAYLOAD 60000

/// Inter-server packet ids (map -> char)
enum e_job_record_packet_frommap : uint16 {
	JOBRECORD_LIST_REQ = 0x30B0,   ///< W cmd, W len, L char_id, B skill_count, W skill_id[skill_count]
	JOBRECORD_LOAD_REQ = 0x30B1,   ///< W cmd, L char_id, L record_id (10)
	JOBRECORD_UPSERT = 0x30B2,     ///< W cmd, W len, L char_id, L record_id, B section, W count, data[] (chunked, ends with JOBRECORD_SECTION_END)
	JOBRECORD_COMMIT = 0x30B3,     ///< W cmd, L char_id, L active_record, L clear_stash_record (14)
	JOBRECORD_RENAME = 0x30B4,     ///< W cmd, L char_id, L record_id, NAME_LENGTH name (34)
};

/// Inter-server packet ids (char -> map)
enum e_job_record_packet_tomap : uint16 {
	JOBRECORD_LIST_ACK = 0x38B0,   ///< W cmd, W len, L char_id, L active, L pending_to, L next_record_id, B is_last, B skill_count, W count, s_job_record_summary[count]
	JOBRECORD_LOAD_ACK = 0x38B1,   ///< W cmd, W len, L char_id, L record_id, B section, B result, W count, data[] (chunked, ends with JOBRECORD_SECTION_END)
	JOBRECORD_UPSERT_ACK = 0x38B2, ///< W cmd, L char_id, L record_id, B success (11)
	JOBRECORD_COMMIT_ACK = 0x38B3, ///< W cmd, L char_id, L active_record, B success (11)
	JOBRECORD_RENAME_ACK = 0x38B4, ///< W cmd, L char_id, L record_id, B success (11)
};

/// Header sizes of the chunked packets
#define JOBRECORD_LIST_REQ_HEADER 9
#define JOBRECORD_LIST_ACK_HEADER 24
#define JOBRECORD_UPSERT_HEADER 15
#define JOBRECORD_LOAD_ACK_HEADER 16

/// Sections of a chunked record transfer (JOBRECORD_UPSERT and JOBRECORD_LOAD_ACK)
enum e_job_record_section : uint8 {
	JOBRECORD_SECTION_BASE = 0,   ///< 1x s_job_record_base, always the first chunk
	JOBRECORD_SECTION_SKILL,      ///< s_skill[]
	JOBRECORD_SECTION_REG,        ///< s_job_record_reg[]
	JOBRECORD_SECTION_QUEST,      ///< quest[]
	JOBRECORD_SECTION_HOTKEY,     ///< s_job_record_hotkey[]
	JOBRECORD_SECTION_ITEM,       ///< item[] (stashed equipment)
	JOBRECORD_SECTION_END = 0xFF, ///< UPSERT: 1x s_job_record_upsert_end. LOAD_ACK: no data
};

/// Result codes of JOBRECORD_LOAD_ACK
enum e_job_record_load_result : uint8 {
	JOBRECORD_LOAD_OK = 0,
	JOBRECORD_LOAD_NOT_FOUND,
	JOBRECORD_LOAD_ERROR,
};

/// Scalar data of one record (`char_job_record` row)
struct s_job_record_base {
	uint32 record_id;
	char name[NAME_LENGTH];
	uint16 class_;
	uint32 base_level;
	t_exp base_exp;
	uint32 job_level;
	t_exp job_exp;
	uint32 status_point, skill_point, trait_point;
	uint16 str, agi, vit, int_, dex, luk;
	uint16 pow, sta, wis, spl, con, crt;
	uint8 chg_2nd, chg_3rd, chg_4th;
	uint16 hair, hair_color, clothes_color;
	uint32 equip_weight;
	uint16 equip_count;
	uint32 created, last_used;
};

/// One entry of the record list sent at login
struct s_job_record_summary {
	struct s_job_record_base base;
	/// Learned levels of the skills asked for in JOBRECORD_LIST_REQ, in request order
	uint8 skill_lv[JOB_RECORD_MAX_SUMMARY_SKILLS];
};

/// One saved registry entry (whitelisted job-quest variable)
struct s_job_record_reg {
	char key[JOB_RECORD_REG_KEY_LENGTH];
	uint32 index;
	uint8 is_str;
	int64 value_num;
	char value_str[JOB_RECORD_REG_STR_LENGTH];
};

/// One saved hotkey slot
struct s_job_record_hotkey {
	uint8 slot;
	struct hotkey hotkey;
};

/// Last chunk of JOBRECORD_UPSERT
struct s_job_record_upsert_end {
	uint32 next_record_id; ///< New value of `char_job_record_state`.`next_record_id`
	uint32 pending_to;     ///< New value of `char_job_record_state`.`pending_to` when set_pending is true
	uint8 set_pending;     ///< Whether pending_to should be written (switch journal)
};

#endif /* JOB_RECORD_HPP */
