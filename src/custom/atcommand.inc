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
