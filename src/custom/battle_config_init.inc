// Copyright (c) rAthena Dev Teams - Licensed under GNU GPL
// For more information, see LICENCE in the main folder

/**
 * Custom battle config initializations
 * Place the definition of custom battle configuration initializations in this file.
 *
 * Do not forget to add a comma at the end of the line, if you want to add another config
 *
 * Format:
 *	 { "name", &battle_config.<variable name>, <default value>, <minimum value>, <maximum value> },
 **/

	// [Job Record] conf/battle/job_record.conf
	{ "job_record_max",                     &battle_config.job_record_max,                  20,     0,      INT_MAX,        },
	{ "job_record_cooldown",                &battle_config.job_record_cooldown,             30000,  0,      INT_MAX,        },
	{ "job_record_combat_delay",            &battle_config.job_record_combat_delay,         10000,  0,      INT_MAX,        },

