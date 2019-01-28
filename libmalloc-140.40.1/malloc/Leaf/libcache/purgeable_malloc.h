
#ifndef __PURGEABLE_MALLOC_H
#define __PURGEABLE_MALLOC_H

/* Create a new zone that supports malloc_make{un}purgeable() discipline. */
MALLOC_NOEXPORT
malloc_zone_t *
create_purgeable_zone(size_t initial_size, malloc_zone_t *malloc_default_zone, unsigned debug_flags);//可清除的，可净化的，可泻下的

#endif // __PURGEABLE_MALLOC_H
