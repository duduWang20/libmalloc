//
//  malloc_Debug.h
//  libmalloc
//
//  Created by 王举范 on 2018/12/17.
//

#ifndef malloc_Debug_h
#define malloc_Debug_h

/*********	Debug helpers	************/

extern void malloc_zone_print_ptr_info(void *ptr);
/* print to stdout if this pointer is in the malloc heap, free status, and size */

extern boolean_t malloc_zone_check(malloc_zone_t *zone);
/* Checks zone is well formed; if !zone, checks all zones */

extern void malloc_zone_print(malloc_zone_t *zone, boolean_t verbose);
/* Prints summary on zone; if !zone, prints all zones */

extern void malloc_zone_statistics(malloc_zone_t *zone, malloc_statistics_t *stats);
/* Fills statistics for zone; if !zone, sums up all zones */

extern void malloc_zone_log(malloc_zone_t *zone, void *address);
/* Controls logging of all activity; if !zone, for all zones;
 If address==0 nothing is logged;
 If address==-1 all activity is logged;
 Else only the activity regarding address is logged */

struct mstats {
	size_t	bytes_total;
	size_t	chunks_used;
	size_t	bytes_used;
	size_t	chunks_free;
	size_t	bytes_free;
};

extern struct mstats mstats(void);

extern boolean_t malloc_zone_enable_discharge_checking(malloc_zone_t *zone) __OSX_AVAILABLE_STARTING(__MAC_10_7, __IPHONE_4_3);
/* Increment the discharge checking enabled counter for a zone. Returns true if the zone supports checking, false if it does not. */

extern void malloc_zone_disable_discharge_checking(malloc_zone_t *zone) __OSX_AVAILABLE_STARTING(__MAC_10_7, __IPHONE_4_3);
/* Decrement the discharge checking enabled counter for a zone. */

extern void malloc_zone_discharge(malloc_zone_t *zone, void *memory) __OSX_AVAILABLE_STARTING(__MAC_10_7, __IPHONE_4_3);
/* Register memory that the programmer expects to be freed soon.
 zone may be NULL in which case the zone is determined using malloc_zone_from_ptr().
 If discharge checking is off for the zone this function is a no-op. */

#ifdef __BLOCKS__
extern void malloc_zone_enumerate_discharged_pointers(malloc_zone_t *zone, void (^report_discharged)(void *memory, void *info)) __OSX_AVAILABLE_STARTING(__MAC_10_7, __IPHONE_4_3);
/* Calls report_discharged for each block that was registered using malloc_zone_discharge() but has not yet been freed.
 info is used to provide zone defined information about the memory block.
 If zone is NULL then the enumeration covers all zones. */
#else
extern void malloc_zone_enumerate_discharged_pointers(malloc_zone_t *zone, void *) __OSX_AVAILABLE_STARTING(__MAC_10_7, __IPHONE_4_3);
#endif /* __BLOCKS__ */

__END_DECLS


#endif /* malloc_Debug_h */
