//
//  malloc_zone_batch.c
//  libsystem_malloc
//
//  Created by 王举范 on 2018/12/19.
//

#include "malloc_zone_batch.h"

/*********	Batch methods	************/

unsigned
malloc_zone_batch_malloc(malloc_zone_t *zone, size_t size, void **results, unsigned num_requested)
{
	if (!zone->batch_malloc) {
		return 0;
	}
	if (malloc_check_start && (malloc_check_counter++ >= malloc_check_start)) {
		internal_check();
	}
	unsigned batched = zone->batch_malloc(zone, size, results, num_requested);
	
	if (malloc_logger) {
		unsigned index = 0;
		while (index < batched) {
			malloc_logger(MALLOC_LOG_TYPE_ALLOCATE | MALLOC_LOG_TYPE_HAS_ZONE, (uintptr_t)zone, (uintptr_t)size, 0,
						  (uintptr_t)results[index], 0);
			index++;
		}
	}
	return batched;
}

void
malloc_zone_batch_free(malloc_zone_t *zone, void **to_be_freed, unsigned num)
{
	if (malloc_check_start && (malloc_check_counter++ >= malloc_check_start)) {
		internal_check();
	}
	if (malloc_logger) {
		unsigned index = 0;
		while (index < num) {
			malloc_logger(
						  MALLOC_LOG_TYPE_DEALLOCATE | MALLOC_LOG_TYPE_HAS_ZONE, (uintptr_t)zone, (uintptr_t)to_be_freed[index], 0, 0, 0);
			index++;
		}
	}
	
	if (zone->batch_free) {
		zone->batch_free(zone, to_be_freed, num);
	} else {
		void (*free_fun)(malloc_zone_t *, void *) = zone->free;
		
		while (num--) {
			void *ptr = *to_be_freed++;
			free_fun(zone, ptr);
		}
	}
}
