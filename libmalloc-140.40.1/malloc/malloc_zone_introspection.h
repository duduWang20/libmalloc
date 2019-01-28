//
//  malloc_zone_introspection.h
//  libsystem_malloc
//
//  Created by 王举范 on 2018/12/17.
//

#ifndef malloc_zone_introspection_h
#define malloc_zone_introspection_h

#include <stdio.h>

/////////////////////////////////////////////////////////////////////

typedef struct malloc_introspection_t {
	kern_return_t (*enumerator)(task_t task, void *,
								unsigned type_mask,
								vm_address_t zone_address,
								memory_reader_t reader,
								vm_range_recorder_t recorder);
	
	/* enumerates all the malloc pointers in use */
	
	size_t	(*good_size)(malloc_zone_t *zone, size_t size);
	
	boolean_t 	(*check)(malloc_zone_t *zone); /* Consistency checker */
	void 	(*print)(malloc_zone_t *zone, boolean_t verbose); /* Prints zone  */
	void	(*log)(malloc_zone_t *zone, void *address); /* Enables logging of activity */
	void	(*force_lock)(malloc_zone_t *zone); /* Forces locking zone */
	void	(*force_unlock)(malloc_zone_t *zone); /* Forces unlocking zone */
	void	(*statistics)(malloc_zone_t *zone,
						  malloc_statistics_t *stats); /* Fills statistics */
	boolean_t   (*zone_locked)(malloc_zone_t *zone); /* Are any zone locks held */
	
	/* Discharge checking. Present in version >= 7. */
	boolean_t	(*enable_discharge_checking)(malloc_zone_t *zone);
	void	(*disable_discharge_checking)(malloc_zone_t *zone);
	void	(*discharge)(malloc_zone_t *zone, void *memory);
#ifdef __BLOCKS__
	void        (*enumerate_discharged_pointers)(malloc_zone_t *zone, void (^report_discharged)(void *memory, void *info));
#else
	void	*enumerate_unavailable_without_blocks;
#endif /* __BLOCKS__ */
	void	(*reinit_lock)(malloc_zone_t *zone); /* Reinitialize zone locks, called only from atfork_child handler. Present in version >= 9. */
} malloc_introspection_t;


extern void malloc_printf(const char *format, ...);
/* Convenience for logging errors and warnings;
 No allocation is performed during execution of this function;
 Only understands usual %p %d %s formats, and %y that expresses a number of bytes (5b,10KB,1MB...)
 */


#endif /* malloc_zone_introspection_h */
