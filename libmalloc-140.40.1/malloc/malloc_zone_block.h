
#ifndef malloc_zone_block_h
#define malloc_zone_block_h

#include <stdio.h>

/*********	Block creation and manipulation	************/

extern void *malloc_zone_malloc(malloc_zone_t *zone, size_t size);
/* Allocates a new pointer of size size; zone must be non-NULL */

extern void *malloc_zone_calloc(malloc_zone_t *zone, size_t num_items, size_t size);
/* Allocates a new pointer of size num_items * size; block is cleared; zone must be non-NULL */

extern void *malloc_zone_valloc(malloc_zone_t *zone, size_t size);
/* Allocates a new pointer of size size; zone must be non-NULL; Pointer is guaranteed to be page-aligned and block is cleared */

extern void malloc_zone_free(malloc_zone_t *zone, void *ptr);
/* Frees pointer in zone; zone must be non-NULL */

extern void *malloc_zone_realloc(malloc_zone_t *zone, void *ptr, size_t size);
/* Enlarges block if necessary; zone must be non-NULL */

extern malloc_zone_t *malloc_zone_from_ptr(const void *ptr);
/* Returns the zone for a pointer, or NULL if not in any zone.
 The ptr must have been returned from a malloc or realloc call. */

extern size_t malloc_size(const void *ptr);
/* Returns size of given ptr */

extern size_t malloc_good_size(size_t size);
/* Returns number of bytes greater than or equal to size that can be allocated without padding */

extern void *malloc_zone_memalign(malloc_zone_t *zone, size_t alignment, size_t size) __OSX_AVAILABLE_STARTING(__MAC_10_6, __IPHONE_3_0);
/*
 * Allocates a new pointer of size size whose address is an exact multiple of alignment.
 * alignment must be a power of two and at least as large as sizeof(void *).
 * zone must be non-NULL.
 */

#endif /* malloc_zone_block_h */
