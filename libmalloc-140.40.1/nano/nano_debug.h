//
//  nano_debug.h
//  libsystem_malloc
//
//  Created by 王举范 on 2018/12/17.
//

#ifndef nano_debug_h
#define nano_debug_h

#include <stdio.h>
/*********************             PROTOTYPES		***********************/
// msg prints after fmt, ...
static MALLOC_NOINLINE void
nanozone_error(nanozone_t *nanozone, int is_corruption, const char *msg, const void *ptr, const char *fmt, ...) __printflike(5, 6);

static void nano_statistics(nanozone_t *nanozone, malloc_statistics_t *stats);

#endif /* nano_debug_h */
