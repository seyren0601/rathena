// Copyright (c) rAthena Dev Teams - Licensed under GNU GPL
// For more information, see LICENCE in the main folder

/**
 * Custom Atcommands
 * Place the body of custom atcommands in this file.
 * Format:
 *	ACMD_FUNC(command_name)
 *	{
 *		<code>;
 *	}
 **/

//ACMD_FUNC(newcommand)
//{
//	clif_displaymessage(fd, "It works!");
//	clif_specialeffect(&sd->bl, EF_HEARTCASTING, AREA);
//	return 0;
//}

/**
 * [Job Record] Debug: shows the loaded job records and the write queue.
 * Usage: @jobrecordinfo
 * Development aid; access is controlled by conf/groups.yml (GM only by default).
 **/
ACMD_FUNC(jobrecordinfo)
{
	nullpo_retr(-1, sd);

	jobrecord_show_info(*sd, fd);
	return 0;
}

/**
 * [Job Record] Test command until the item menu exists (phase 8).
 * Usage:
 *   @jobrecord new <human|doram> <name>
 *   @jobrecord switch <record id>
 *   @jobrecord preview <record id>
 *   @jobrecord rename <record id> <name>
 *   @jobrecord delete <record id>
 *   @jobrecord vars      (job-quest progress the whitelist keeps per record)
 *   @jobrecord reload    (reloads db/job_record_vars.yml)
 * Access is controlled by conf/groups.yml (GM only by default).
 **/
ACMD_FUNC(jobrecord)
{
	char action[16] = {}, arg1[NAME_LENGTH] = {}, arg2[NAME_LENGTH] = {};
	const char* error = nullptr;
	int32 args;

	nullpo_retr(-1, sd);

	if (!message || (args = sscanf(message, "%15s %23s %23[^\n]", action, arg1, arg2)) < 1) {
		clif_displaymessage(fd, "Usage: @jobrecord new <human|doram> <name> | switch <id> | preview <id> | rename <id> <name> | delete <id> | vars | reload");
		return -1;
	}

	if (!strcmpi(action, "vars")) {
		jobrecord_show_vars(*sd, fd);
		return 0;
	}
	if (!strcmpi(action, "reload")) {
		jobrecord_reload();
		clif_displaymessage(fd, "Job record: db/job_record_vars.yml reloaded.");
		return 0;
	}
	if (args < 2) {
		clif_displaymessage(fd, "Usage: @jobrecord new <human|doram> <name> | switch <id> | preview <id> | rename <id> <name> | delete <id> | vars | reload");
		return -1;
	}

	if (!strcmpi(action, "new")) {
		if (arg2[0] == '\0' || (strcmpi(arg1, "human") && strcmpi(arg1, "doram"))) {
			clif_displaymessage(fd, "Usage: @jobrecord new <human|doram> <name>");
			return -1;
		}
		error = jobrecord_create(*sd, arg2, !strcmpi(arg1, "doram"));
		if (error == nullptr)
			clif_displaymessage(fd, "Job record: new record created.");
	} else if (!strcmpi(action, "switch")) {
		error = jobrecord_start_switch(*sd, static_cast<uint32>(strtoul(arg1, nullptr, 10)));
		if (error == nullptr)
			clif_displaymessage(fd, "Job record: loading the record...");
	} else if (!strcmpi(action, "preview")) {
		s_job_record_preview preview;
		char output[CHAT_SIZE_MAX];

		error = jobrecord_preview(*sd, static_cast<uint32>(strtoul(arg1, nullptr, 10)), preview);
		if (error == nullptr) {
			safesnprintf(output, sizeof(output), "Job record preview: weight %u / %u (%u%%, warning from %u%%), equipment needs %u slot(s), %u free.",
				preview.weight / 10, preview.max_weight / 10, preview.rate, preview.warn_rate, preview.needed_slots, preview.free_slots);
			clif_displaymessage(fd, output);
		}
	} else if (!strcmpi(action, "rename")) {
		if (arg2[0] == '\0') {
			clif_displaymessage(fd, "Usage: @jobrecord rename <id> <name>");
			return -1;
		}
		error = jobrecord_rename(*sd, static_cast<uint32>(strtoul(arg1, nullptr, 10)), arg2);
		if (error == nullptr)
			clif_displaymessage(fd, "Job record: renamed.");
	} else if (!strcmpi(action, "delete")) {
		error = jobrecord_delete(*sd, static_cast<uint32>(strtoul(arg1, nullptr, 10)));
		if (error == nullptr)
			clif_displaymessage(fd, "Job record: deleting the record...");
	} else {
		clif_displaymessage(fd, "Usage: @jobrecord new <human|doram> <name> | switch <id> | preview <id> | rename <id> <name> | delete <id> | vars | reload");
		return -1;
	}

	if (error != nullptr) {
		clif_displaymessage(fd, error);
		return -1;
	}

	return 0;
}
