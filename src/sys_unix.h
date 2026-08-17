/*
 * Copyright (C) 2026 Mrokkk
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 *
 * See the GNU General Public License for more details.
 */

#ifndef __SYS_UNIX_H__
#define __SYS_UNIX_H__

#include <stdint.h>
#include <time.h>

typedef struct timespec timespec_t;

#define NSEC_IN_SEC 1000000000

static inline uint64_t ts_to_ns(timespec_t *t)
{
	return t->tv_sec * NSEC_IN_SEC + t->tv_nsec;
}

static inline uint64_t gettime_ns(void)
{
	timespec_t ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return ts_to_ns(&ts);
}

#endif // __SYS_UNIX_H__

// vim: set noexpandtab tabstop=4 shiftwidth=4 :
