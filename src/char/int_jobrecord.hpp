// Copyright (c) rAthena Dev Teams - Licensed under GNU GPL
// For more information, see LICENCE in the main folder

// [Job Record] char-server side storage of job records.
// See docs/subsystems/map/job-record-system-design.md

#ifndef INT_JOBRECORD_HPP
#define INT_JOBRECORD_HPP

#include <common/cbasetypes.hpp>

int32 inter_jobrecord_parse_frommap(int32 fd);

#endif /* INT_JOBRECORD_HPP */
