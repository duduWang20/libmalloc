//
//  nano_introspection.h
//  libsystem_malloc
//
//  Created by 王举范 on 2018/12/17.
//

#ifndef nano_introspection_h
#define nano_introspection_h

#include <stdio.h>

static const struct malloc_introspection_t nano_introspect = {
	(void *)nano_ptr_in_use_enumerator,
	(void *)nano_good_size,
	(void *)nanozone_check,
	(void *)nano_print, nano_log,
	(void *)nano_force_lock,
	(void *)nano_force_unlock,
	(void *)nano_statistics,
	(void *)nano_locked, NULL, NULL, NULL,
	NULL, /* Zone enumeration version 7 and forward. */
	(void *)nano_reinit_lock, // reinit_lock version 9 and foward
}; // marked as const to spare the DATA section

#endif /* nano_introspection_h */
