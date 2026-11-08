// Copyright (c) rAthena Dev Teams - Licensed under GNU GPL
// For more information, see LICENCE in the main folder

/**
 * Custom Script Commands
 * Place the body of custom script commands in this file.
 * Format:
 *	BUILDIN_FUNC(command_name)
 *	{
 *		<code>;
 *	}
 **/

//BUILDIN_FUNC(example)
//{
//	ShowInfo("buildin_example: It works!\n");
//	script_pushint(st,1);
//	return 0;
//}

/*==========================================
 * [Job Record] Script commands, see doc in npc/custom/job_record.txt
 * and docs/subsystems/map/job-record-system-design.md
 *------------------------------------------*/

/// Stores the reason of a failed job record command in @jobrecord_error$ and pushes 1 (success) or 0
static void jobrecord_script_result( struct script_state* st, map_session_data* sd, const char* error ){
	pc_setregstr( sd, add_str( "@jobrecord_error$" ), error != nullptr ? error : "" );
	script_pushint( st, error == nullptr ? 1 : 0 );
}

/// Stores a preview in @jobrecord_weight_rate, @jobrecord_warn_rate, @jobrecord_needed_slots, @jobrecord_free_slots
static void jobrecord_script_preview( map_session_data* sd, const s_job_record_preview& preview ){
	pc_setreg( sd, add_str( "@jobrecord_weight_rate" ), preview.rate );
	pc_setreg( sd, add_str( "@jobrecord_warn_rate" ), preview.warn_rate );
	pc_setreg( sd, add_str( "@jobrecord_needed_slots" ), preview.needed_slots );
	pc_setreg( sd, add_str( "@jobrecord_free_slots" ), preview.free_slots );
}

/// getjobrecords;
/// Fills @jobrecord_id[], @jobrecord_name$[], @jobrecord_class[], @jobrecord_baselv[], @jobrecord_joblv[],
/// @jobrecord_count and @jobrecord_active (0 while the character has no record yet). The active record
/// shows the live job and levels. Returns the number of records, or -1 while they are still loading.
BUILDIN_FUNC(getjobrecords)
{
	map_session_data* sd;

	if( !script_rid2sd( sd ) ){
		return SCRIPT_CMD_FAILURE;
	}

	if( sd->job_record == nullptr || sd->job_record->state == JOBRECORD_STATE_UNLOADED ){
		pc_setreg( sd, add_str( "@jobrecord_count" ), 0 );
		pc_setreg( sd, add_str( "@jobrecord_active" ), 0 );
		script_pushint( st, -1 );
		return SCRIPT_CMD_SUCCESS;
	}

	const s_job_record_session& jr = *sd->job_record;
	int32 i = 0;

	for( const s_job_record_summary& s : jr.records ){
		bool active = s.base.record_id == jr.active_record;

		pc_setreg( sd, reference_uid( add_str( "@jobrecord_id" ), i ), s.base.record_id );
		pc_setregstr( sd, reference_uid( add_str( "@jobrecord_name$" ), i ), s.base.name );
		pc_setreg( sd, reference_uid( add_str( "@jobrecord_class" ), i ), active ? sd->status.class_ : s.base.class_ );
		pc_setreg( sd, reference_uid( add_str( "@jobrecord_baselv" ), i ), active ? sd->status.base_level : s.base.base_level );
		pc_setreg( sd, reference_uid( add_str( "@jobrecord_joblv" ), i ), active ? sd->status.job_level : s.base.job_level );
		i++;
	}

	pc_setreg( sd, add_str( "@jobrecord_count" ), i );
	pc_setreg( sd, add_str( "@jobrecord_active" ), jr.active_record );
	script_pushint( st, i );
	return SCRIPT_CMD_SUCCESS;
}

/// jobrecord_canact()
/// Returns 1 when a create, switch or delete is possible now, otherwise 0 and the reason in @jobrecord_error$.
BUILDIN_FUNC(jobrecord_canact)
{
	map_session_data* sd;

	if( !script_rid2sd( sd ) ){
		return SCRIPT_CMD_FAILURE;
	}

	jobrecord_script_result( st, sd, jobrecord_can_act( *sd ) );
	return SCRIPT_CMD_SUCCESS;
}

/// jobrecord_cooldown()
/// Returns the seconds left before the next create, switch or delete.
BUILDIN_FUNC(jobrecord_cooldown)
{
	map_session_data* sd;

	if( !script_rid2sd( sd ) ){
		return SCRIPT_CMD_FAILURE;
	}

	script_pushint( st, jobrecord_get_cooldown( *sd ) );
	return SCRIPT_CMD_SUCCESS;
}

/// jobrecord_preview(<record id>)
/// jobrecord_preview_new(<doram>)
/// Predicts the weight after switching to a record or creating a new one. Returns the weight rate in percent
/// (also in @jobrecord_weight_rate) and sets @jobrecord_warn_rate, @jobrecord_needed_slots, @jobrecord_free_slots.
/// Returns -1 and the reason in @jobrecord_error$ when the record does not exist.
BUILDIN_FUNC(jobrecord_preview)
{
	map_session_data* sd;
	s_job_record_preview preview;
	const char* error;

	if( !script_rid2sd( sd ) ){
		return SCRIPT_CMD_FAILURE;
	}

	if( !strcmp( script_getfuncname( st ), "jobrecord_preview_new" ) ){
		error = jobrecord_preview_new( *sd, script_getnum( st, 2 ) != 0, preview );
	}else{
		error = jobrecord_preview( *sd, static_cast<uint32>( script_getnum( st, 2 ) ), preview );
	}

	pc_setregstr( sd, add_str( "@jobrecord_error$" ), error != nullptr ? error : "" );

	if( error != nullptr ){
		script_pushint( st, -1 );
		return SCRIPT_CMD_SUCCESS;
	}

	jobrecord_script_preview( sd, preview );
	script_pushint( st, preview.rate );
	return SCRIPT_CMD_SUCCESS;
}

/// jobrecord_switch(<record id>)
/// Starts switching to a record. The switch completes a moment later (the record is loaded first) and the
/// player is told the result. Returns 1 when started, otherwise 0 and the reason in @jobrecord_error$.
BUILDIN_FUNC(jobrecord_switch)
{
	map_session_data* sd;

	if( !script_rid2sd( sd ) ){
		return SCRIPT_CMD_FAILURE;
	}

	jobrecord_script_result( st, sd, jobrecord_start_switch( *sd, static_cast<uint32>( script_getnum( st, 2 ) ) ) );
	return SCRIPT_CMD_SUCCESS;
}

/// jobrecord_create("<name>",<doram>)
/// Saves the current record and starts a new one at base level 1 (Novice, or Summoner when <doram> is 1).
/// Returns 1 on success, otherwise 0 and the reason in @jobrecord_error$.
BUILDIN_FUNC(jobrecord_create)
{
	map_session_data* sd;

	if( !script_rid2sd( sd ) ){
		return SCRIPT_CMD_FAILURE;
	}

	jobrecord_script_result( st, sd, jobrecord_create( *sd, script_getstr( st, 2 ), script_getnum( st, 3 ) != 0 ) );
	return SCRIPT_CMD_SUCCESS;
}

/// jobrecord_delete(<record id>)
/// Deletes a saved record (not the active one). Its equipment is mailed to the character. The result
/// arrives a moment later and is shown to the player. Returns 1 when requested, otherwise 0 and the
/// reason in @jobrecord_error$.
BUILDIN_FUNC(jobrecord_delete)
{
	map_session_data* sd;

	if( !script_rid2sd( sd ) ){
		return SCRIPT_CMD_FAILURE;
	}

	jobrecord_script_result( st, sd, jobrecord_delete( *sd, static_cast<uint32>( script_getnum( st, 2 ) ) ) );
	return SCRIPT_CMD_SUCCESS;
}

/// jobrecord_rename(<record id>,"<name>")
/// Returns 1 on success, otherwise 0 and the reason in @jobrecord_error$.
BUILDIN_FUNC(jobrecord_rename)
{
	map_session_data* sd;

	if( !script_rid2sd( sd ) ){
		return SCRIPT_CMD_FAILURE;
	}

	jobrecord_script_result( st, sd, jobrecord_rename( *sd, static_cast<uint32>( script_getnum( st, 2 ) ), script_getstr( st, 3 ) ) );
	return SCRIPT_CMD_SUCCESS;
}
