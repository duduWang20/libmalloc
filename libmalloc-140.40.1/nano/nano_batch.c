//
//  nano_batch.c
//  libsystem_malloc
//
//  Created by 王举范 on 2018/12/17.
//

#include "nano_batch.h"


static unsigned
nano_batch_malloc(nanozone_t *nanozone, size_t size, void **results, unsigned count)
{
	unsigned found = 0;
	
	if (size <= NANO_MAX_SIZE) {
		while (found < count) {
			void *ptr = _nano_malloc_check_clear(nanozone, size, 0);
			if (!ptr) {
				break;
			}
			
			*results++ = ptr;
			found++;
		}
		if (found == count) {
			return found;
		} else {
			/* FALLTHROUGH to mop-up in the helper zone */
		}
	}
	
	malloc_zone_t *zone = (malloc_zone_t *)(nanozone->helper_zone);
	return found + zone->batch_malloc(zone, size, results, count - found);
}

static unsigned
nano_forked_batch_malloc(nanozone_t *nanozone, size_t size, void **results, unsigned count)
{
	malloc_zone_t *zone = (malloc_zone_t *)(nanozone->helper_zone);
	return zone->batch_malloc(zone, size, results, count);
}

static void
nano_batch_free(nanozone_t *nanozone, void **to_be_freed, unsigned count)
{
	void *ptr;
	
	// frees all the pointers in to_be_freed
	// note that to_be_freed may be overwritten during the process
	if (!count) {
		return;
	}
	
	while (count--) {
		ptr = to_be_freed[count];
		if (ptr) {
			nano_free(nanozone, ptr);
		}
	}
}

static void
nano_forked_batch_free(nanozone_t *nanozone, void **to_be_freed, unsigned count)
{
	void *ptr;
	
	// frees all the pointers in to_be_freed
	// note that to_be_freed may be overwritten during the process
	if (!count) {
		return;
	}
	
	while (count--) {
		ptr = to_be_freed[count];
		if (ptr) {
			nano_forked_free(nanozone, ptr);
		}
	}
}
