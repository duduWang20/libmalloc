//
//  malloc_batch.h
//  libmalloc
//
//  Created by 王举范 on 2018/12/17.
//

#ifndef malloc_batch_h
#define malloc_batch_h

/*********	Batch methods	************/

extern unsigned malloc_zone_batch_malloc(malloc_zone_t *zone, size_t size, void **results, unsigned num_requested);
/* Allocates num blocks of the same size; Returns the number truly allocated (may be 0) */

extern void malloc_zone_batch_free(malloc_zone_t *zone, void **to_be_freed, unsigned num);
/* frees all the pointers in to_be_freed; note that to_be_freed may be overwritten during the process; This function will always free even if the zone has no batch callback */



#endif /* malloc_batch_h */
