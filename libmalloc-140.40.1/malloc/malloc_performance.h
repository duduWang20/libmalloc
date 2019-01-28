//
//  malloc_performance.h
//  libmalloc
//
//  Created by 王举范 on 2018/12/17.
//

#ifndef malloc_performance_h
#define malloc_performance_h


/*********	Functions for performance tools	************/

extern kern_return_t malloc_get_all_zones(task_t task,
										  memory_reader_t reader,
										  vm_address_t **addresses,
										  unsigned *count);
/* Fills addresses and count with the addresses of the zones in task;
 Note that the validity of the addresses returned correspond to the validity of the memory returned by reader */

#endif /* malloc_performance_h */
