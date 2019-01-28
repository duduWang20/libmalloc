
#ifndef nano_malloc_caller_h
#define nano_malloc_caller_h

#include <stdio.h>

MALLOC_NOEXPORT
malloc_zone_t *
create_nano_zone(size_t initial_size,
				 malloc_zone_t *helper_zone,
				 unsigned debug_flags);

MALLOC_NOEXPORT
void nano_forked_zone(nanozone_t *nanozone);

MALLOC_NOEXPORT
void nano_init(const char *envp[], const char *apple[]);

#endif /* nano_malloc_caller_h */
