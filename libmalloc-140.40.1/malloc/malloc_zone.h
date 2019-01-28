//
//  malloc_zone.h
//  libmalloc
//
//  Created by 王举范 on 2018/12/17.
//

#ifndef malloc_zone_h
#define malloc_zone_h
/*********	Functions for zone implementors	************/

extern void malloc_zone_register(malloc_zone_t *zone);
/* Registers a custom malloc zone; Should typically be called after a
 * malloc_zone_t has been filled in with custom methods by a client.  See
 * malloc_create_zone for creating additional malloc zones with the
 * default allocation and free behavior. */

extern void malloc_zone_unregister(malloc_zone_t *zone);
/* De-registers a zone
 Should typically be called before calling the zone destruction routine */

extern void malloc_set_zone_name(malloc_zone_t *zone, const char *name);
/* Sets the name of a zone */

extern const char *malloc_get_zone_name(malloc_zone_t *zone);
/* Returns the name of a zone */

size_t malloc_zone_pressure_relief(malloc_zone_t *zone, size_t goal) __OSX_AVAILABLE_STARTING(__MAC_10_7, __IPHONE_4_3);
/* malloc_zone_pressure_relief() advises the malloc subsystem that the process is under memory pressure and
 * that the subsystem should make its best effort towards releasing (i.e. munmap()-ing) "goal" bytes from "zone".
 * If "goal" is passed as zero, the malloc subsystem will attempt to achieve maximal pressure relief in "zone".
 * If "zone" is passed as NULL, all zones are examined for pressure relief opportunities.
 * malloc_zone_pressure_relief() returns the number of bytes released.
 */



typedef struct {
	vm_address_t	address;
	vm_size_t		size;
} vm_range_t;

typedef struct malloc_statistics_t {
	unsigned	blocks_in_use;
	size_t	size_in_use;
	size_t	max_size_in_use;	/* high water mark of touched memory */
	size_t	size_allocated;		/* reserved in memory */
} malloc_statistics_t;

typedef kern_return_t memory_reader_t(task_t remote_task, vm_address_t remote_address, vm_size_t size, void **local_memory);
/* given a task, "reads" the memory at the given address and size
 local_memory: set to a contiguous chunk of memory; validity of local_memory is assumed to be limited (until next call) */

#define MALLOC_PTR_IN_USE_RANGE_TYPE	1	/* for allocated pointers */
#define MALLOC_PTR_REGION_RANGE_TYPE	2	/* for region containing pointers */
#define MALLOC_ADMIN_REGION_RANGE_TYPE	4	/* for region used internally */
#define MALLOC_ZONE_SPECIFIC_FLAGS	0xff00	/* bits reserved for zone-specific purposes */

typedef void vm_range_recorder_t(task_t, void *, unsigned type, vm_range_t *, unsigned);
/* given a task and context, "records" the specified addresses */
