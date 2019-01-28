//
//  malloc_libcache.h
//  libmalloc
//
//  Created by 王举范 on 2018/12/17.
//

#ifndef malloc_libcache_h
#define malloc_libcache_h

/*********	Functions for libcache	************/

extern malloc_zone_t *malloc_default_purgeable_zone(void) __OSX_AVAILABLE_STARTING(__MAC_10_6, __IPHONE_3_0);
/* Returns a pointer to the default purgeable_zone. */

extern void malloc_make_purgeable(void *ptr) __OSX_AVAILABLE_STARTING(__MAC_10_6, __IPHONE_3_0);
/* Make an allocation from the purgeable zone purgeable if possible.  */

extern int malloc_make_nonpurgeable(void *ptr) __OSX_AVAILABLE_STARTING(__MAC_10_6, __IPHONE_3_0);
/* Makes an allocation from the purgeable zone nonpurgeable.
 * Returns zero if the contents were not purged since the last
 * call to malloc_make_purgeable, else returns non-zero. */

#endif /* malloc_libcache_h */
