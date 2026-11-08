// Copyright (c) rAthena Dev Teams - Licensed under GNU GPL
// For more information, see LICENCE in the main folder

// [Job Record] map-server side of the job record system.
// See docs/subsystems/map/job-record-system-design.md

#ifndef JOBRECORD_HPP
#define JOBRECORD_HPP

#include <memory>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

#include <common/cbasetypes.hpp>
#include <common/database.hpp>
#include <common/job_record.hpp>
#include <common/mmo.hpp>
#include <common/timer.hpp> // t_tick

class map_session_data;

/// What the job record system of one player is doing
enum e_job_record_state : uint8 {
	JOBRECORD_STATE_UNLOADED = 0, ///< Record list not received yet
	JOBRECORD_STATE_IDLE,         ///< Ready for an action
	JOBRECORD_STATE_LOADING,      ///< Waiting for JOBRECORD_LOAD_ACK
	JOBRECORD_STATE_APPLYING,     ///< Switching (local changes in progress)
	JOBRECORD_STATE_DELETING,     ///< Waiting for a delete acknowledgement
};

/// Recovery of a switch that was interrupted (switch journal still set at login, §9.2 of the design)
enum e_job_record_recovery : uint8 {
	JOBRECORD_RECOVERY_NONE = 0,
	JOBRECORD_RECOVERY_TARGET, ///< Loading the target record to see whether the switch had completed
	JOBRECORD_RECOVERY_SOURCE, ///< Loading the source record to put it back
};

/// One complete record (inactive record data, as stored by the char-server)
struct s_job_record_full {
	s_job_record_base base{};
	std::vector<s_skill> skills;
	std::vector<s_job_record_reg> regs;
	std::vector<quest> quests;
	std::vector<s_job_record_hotkey> hotkeys;
	std::vector<item> items;
};

/// A record save that is kept until the char-server acknowledges it.
/// It is sent with the next character save, and again if no acknowledgement
/// arrives in time (a write lost in a dead socket) or if it failed.
struct s_job_record_pending_upsert {
	s_job_record_full data;
	s_job_record_upsert_end end{};
	bool in_flight = false; ///< Sent, waiting for JOBRECORD_UPSERT_ACK
	bool dirty = false;     ///< Replaced by newer data while in flight
	t_tick sent_tick = 0;
};

/// Per-player job record state, owned by map_session_data
struct s_job_record_session {
	e_job_record_state state = JOBRECORD_STATE_UNLOADED;

	// From JOBRECORD_LIST_ACK
	uint32 active_record = 0;
	uint32 pending_to = 0;
	uint32 next_record_id = 1;
	uint8 summary_skill_count = 0;
	std::vector<s_job_record_summary> records;
	std::vector<s_job_record_summary> list_buffer; ///< Chunks received so far

	// JOBRECORD_LOAD_REQ in progress
	uint32 loading_id = 0;
	std::shared_ptr<s_job_record_full> loading;
	int32 load_timer = INVALID_TIMER; ///< Gives up on a load (or a delete) that gets no answer
	uint32 deleting_id = 0;           ///< JOBRECORD_DELETE in progress

	// Interrupted switch recovery
	e_job_record_recovery recovery = JOBRECORD_RECOVERY_NONE;
	uint32 recovery_from = 0;              ///< Record that was active before the interrupted switch
	std::vector<item> recovery_target_items; ///< Stash of the target, removed from the inventory when rolling back

	// Writes waiting for an acknowledgement (§9.2 of the design: resend until ACK)
	std::vector<s_job_record_pending_upsert> pending_upserts; ///< Sent before the inventory save
	bool commit_pending = false;                              ///< Sent after the status save
	bool commit_in_flight = false;
	bool commit_dirty = false;
	t_tick commit_sent_tick = 0;
	uint32 commit_active = 0;
	uint32 commit_clear_stash = 0;
};

/// Predicted state after a switch (§2.2 of the design)
struct s_job_record_preview {
	uint32 weight = 0;       ///< Predicted weight (x10, like map_session_data::weight)
	uint32 max_weight = 0;   ///< Predicted maximum weight, without item bonuses
	uint32 rate = 0;         ///< weight * 100 / max_weight
	uint32 warn_rate = 0;    ///< Warn from this rate on (natural_heal_weight_rate)
	uint16 needed_slots = 0; ///< Inventory slots the target record's equipment needs
	uint16 free_slots = 0;   ///< Free slots after the current equipment has been stashed
};

// Skills whose learned level is reported in the record list (weight preview)
extern const uint16 job_record_summary_skills[];
extern const uint8 job_record_summary_skill_count;

/// Which character variables and quests are job-quest progress kept per record (db/job_record_vars.yml)
class JobRecordVarDatabase : public YamlDatabase {
public:
	std::unordered_set<std::string> variables;
	std::vector<std::string> prefixes;
	std::vector<std::pair<int32, int32>> quests; ///< Inclusive ranges

	JobRecordVarDatabase() : YamlDatabase( "JOB_RECORD_VARS_DB", 1 ){
	}

	void clear() override;
	const std::string getDefaultLocation() override;
	uint64 parseBodyNode( const ryml::NodeRef& node ) override;

	bool matchesVariable( const char* name ) const;
	bool matchesQuest( int32 quest_id ) const;
};

extern JobRecordVarDatabase job_record_var_db;

void do_init_jobrecord();
void jobrecord_reload();

// Lifecycle
void jobrecord_request_list( map_session_data& sd );

// Actions (return nullptr on success, otherwise a message for the player)
const char* jobrecord_start_switch( map_session_data& sd, uint32 record_id );
const char* jobrecord_create( map_session_data& sd, const char* name, bool doram );
const char* jobrecord_rename( map_session_data& sd, uint32 record_id, const char* name );
const char* jobrecord_delete( map_session_data& sd, uint32 record_id );
const char* jobrecord_can_act( map_session_data& sd );
uint32 jobrecord_get_cooldown( map_session_data& sd );
const char* jobrecord_preview( map_session_data& sd, uint32 record_id, s_job_record_preview& out );
const char* jobrecord_preview_new( map_session_data& sd, bool doram, s_job_record_preview& out );

// Hooks in chrif_save (called only while the char-server is connected)
void jobrecord_save_before_inventory( map_session_data& sd );
void jobrecord_save_after_status( map_session_data& sd );

// Queueing writes (used by later phases)
void jobrecord_queue_upsert( map_session_data& sd, const s_job_record_full& record, const s_job_record_upsert_end& end );
void jobrecord_queue_commit( map_session_data& sd, uint32 active_record, uint32 clear_stash_record );
bool jobrecord_request_load( map_session_data& sd, uint32 record_id );
bool jobrecord_request_rename( map_session_data& sd, uint32 record_id, const char* name );

// Packets from the char-server (dispatched by intif_parse)
void jobrecord_parse_list_ack( int32 fd );
void jobrecord_parse_load_ack( int32 fd );
void jobrecord_parse_upsert_ack( int32 fd );
void jobrecord_parse_commit_ack( int32 fd );
void jobrecord_parse_rename_ack( int32 fd );
void jobrecord_parse_delete_ack( int32 fd );

// Debug output for @jobrecordinfo and @jobrecord vars
void jobrecord_show_info( map_session_data& sd, int32 fd );
void jobrecord_show_vars( map_session_data& sd, int32 fd );

#endif /* JOBRECORD_HPP */
