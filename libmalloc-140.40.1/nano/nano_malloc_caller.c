//
//  nano_malloc_caller.c
//  libsystem_malloc
//
//  Created by 王举范 on 2018/12/17.
//

#include "nano_malloc_caller.h"

//in malloc.c wjf
// Called in the child process after fork() to resume normal operation.
void
_malloc_fork_child(void)
{
#if CONFIG_NANOZONE
	if (_malloc_initialize_pred && _malloc_engaged_nano) {
		nano_forked_zone((nanozone_t *)inline_malloc_default_zone());
	}
#endif
	return _malloc_reinit_lock_all(&__stack_logging_fork_child);
}


////////////////////////public in nano_malloc

void
nano_init(const char *envp[], const char *apple[])
{
	const char *flag = _simple_getenv(apple, "MallocNanoZone");
	if (flag && flag[0] == '1') {
		_malloc_engaged_nano = 1;
	}
#if CONFIG_NANO_SMALLMEM_DYNAMIC_DISABLE_35305995
	// Disable nano malloc on <=1gb configurations rdar://problem/35305995
	uint64_t memsize = platform_hw_memsize();
	if (memsize <= (1ull << 30)) {
		_malloc_engaged_nano = 0;
	}
#endif // CONFIG_NANO_SMALLMEM_DYNAMIC_DISABLE_35305995
	/* Explicit overrides from the environment */
	flag = _simple_getenv(envp, "MallocNanoZone");
	if (flag && flag[0] == '1') {
		_malloc_engaged_nano = 1;
	} else if (flag && flag[0] == '0') {
		_malloc_engaged_nano = 0;
	}
#if NANO_PREALLOCATE_BAND_VM
	// Unconditionally preallocate the VA space set aside for nano malloc to
	// reserve it in all configurations. rdar://problem/33392283
	boolean_t preallocated = nano_preallocate_band_vm();
	if (!preallocated && _malloc_engaged_nano) {
		_malloc_printf(ASL_LEVEL_NOTICE, "nano zone abandoned due to inability to preallocate reserved vm space.\n");
		_malloc_engaged_nano = 0;
	}
#endif
}

malloc_zone_t *
create_nano_zone(size_t initial_size,
				 malloc_zone_t *helper_zone,
				 unsigned debug_flags)
{
	nanozone_t *nanozone;
	int i, j;
	
	/* Note: It is important that create_nano_zone clears _malloc_engaged_nano
	 * if it is unable to enable the nanozone (and chooses not to abort). As
	 * several functions rely on _malloc_engaged_nano to determine if they
	 * should manipulate the nanozone, and these should not run if we failed
	 * to create the zone.
	 */
	if (!_malloc_engaged_nano) {
		return NULL;
	}
	
	if (_COMM_PAGE_VERSION_REQD > (*((uint16_t *)_COMM_PAGE_VERSION))) {
		MALLOC_PRINTF_FATAL_ERROR((*((uint16_t *)_COMM_PAGE_VERSION)), "comm page version mismatch");
	}
	
	/* get memory for the zone. */
	nanozone = nano_allocate_pages(NULL, NANOZONE_PAGED_SIZE, 0, 0, VM_MEMORY_MALLOC);
	if (!nanozone) {
		_malloc_engaged_nano = false;
		return NULL;
	}
	
	/* set up the basic_zone portion of the nanozone structure */
	nanozone->basic_zone.version = 9;
	nanozone->basic_zone.size = (void *)nano_size;
	nanozone->basic_zone.malloc = (debug_flags & MALLOC_DO_SCRIBBLE) ? (void *)nano_malloc_scribble : (void *)nano_malloc;
	nanozone->basic_zone.calloc = (void *)nano_calloc;
	nanozone->basic_zone.valloc = (void *)nano_valloc;
	nanozone->basic_zone.free = (debug_flags & MALLOC_DO_SCRIBBLE) ? (void *)nano_free_scribble : (void *)nano_free;
	nanozone->basic_zone.realloc = (void *)nano_realloc;
	nanozone->basic_zone.destroy = (void *)nano_destroy;
	nanozone->basic_zone.batch_malloc = (void *)nano_batch_malloc;
	nanozone->basic_zone.batch_free = (void *)nano_batch_free;
	nanozone->basic_zone.introspect = (struct malloc_introspection_t *)&nano_introspect;
	nanozone->basic_zone.memalign = (void *)nano_memalign;
	nanozone->basic_zone.free_definite_size = (debug_flags & MALLOC_DO_SCRIBBLE) ? (void *)nano_free_definite_size_scribble
	: (void *)nano_free_definite_size;
	
	nanozone->basic_zone.pressure_relief = (void *)nano_pressure_relief;
	
	nanozone->basic_zone.reserved1 = 0; /* Set to zero once and for all as required by CFAllocator. */
	nanozone->basic_zone.reserved2 = 0; /* Set to zero once and for all as required by CFAllocator. */
	
	mprotect(nanozone, sizeof(nanozone->basic_zone), PROT_READ); /* Prevent overwriting the function pointers in basic_zone. */
	
	/* set up the remainder of the nanozone structure */
	nanozone->debug_flags = debug_flags;
	nanozone->our_signature = NANOZONE_SIGNATURE;
	
	/* Query the number of configured processors. */
	nanozone->phys_ncpus = *(uint8_t *)(uintptr_t)_COMM_PAGE_PHYSICAL_CPUS;
	nanozone->logical_ncpus = *(uint8_t *)(uintptr_t)_COMM_PAGE_LOGICAL_CPUS;
	
	if (nanozone->phys_ncpus > sizeof(nanozone->core_mapped_size) /
		sizeof(nanozone->core_mapped_size[0])) {
		MALLOC_PRINTF_FATAL_ERROR(nanozone->phys_ncpus,
								  "nanozone abandoned because NCPUS > max magazines.\n");
	}
	
	if (0 != (nanozone->logical_ncpus % nanozone->phys_ncpus)) {
		MALLOC_PRINTF_FATAL_ERROR(nanozone->logical_ncpus % nanozone->phys_ncpus,
								  "logical_ncpus % phys_ncpus != 0");
	}
	
	switch (nanozone->logical_ncpus / nanozone->phys_ncpus) {
		case 1:
			nanozone->hyper_shift = 0;
			break;
		case 2:
			nanozone->hyper_shift = 1;
			break;
		case 4:
			nanozone->hyper_shift = 2;
			break;
		default:
			MALLOC_PRINTF_FATAL_ERROR(nanozone->logical_ncpus / nanozone->phys_ncpus, "logical_ncpus / phys_ncpus not 1, 2, or 4");
	}
	
	/* Initialize slot queue heads and resupply locks. */
	OSQueueHead q0 = OS_ATOMIC_QUEUE_INIT;
	for (i = 0; i < nanozone->phys_ncpus; ++i) {
		_malloc_lock_init(&nanozone->band_resupply_lock[i]);
		
		for (j = 0; j < NANO_SLOT_SIZE; ++j) {
			nanozone->meta_data[i][j].slot_LIFO = q0;
		}
	}
	
	/* Initialize the security token. */
	if (0 == _dyld_get_image_slide((const struct mach_header *)_NSGetMachExecuteHeader())) {
		// zero slide when ASLR has been disabled by boot-arg. Eliminate cloaking.
		malloc_entropy[0] = 0;
		malloc_entropy[1] = 0;
	}
	nanozone->cookie = (uintptr_t)malloc_entropy[0] & 0x0000ffffffff0000ULL; // scramble central 32bits with this cookie
	
	/* Nano zone does not support MALLOC_ADD_GUARD_PAGES. */
	if (nanozone->debug_flags & MALLOC_ADD_GUARD_PAGES) {
		_malloc_printf(ASL_LEVEL_INFO, "nano zone does not support guard pages\n");
		nanozone->debug_flags &= ~MALLOC_ADD_GUARD_PAGES;
	}
	
	nanozone->helper_zone = helper_zone;
	
	return (malloc_zone_t *)nanozone;
}

void
nano_forked_zone(nanozone_t *nanozone)
{
	/*
	 * Hobble the nano zone in the child of a fork prior to an exec since
	 * the state of the zone can be made inconsistent by a parent thread while the
	 * fork is underway.
	 * All new allocations will be referred to the helper zone (which is more stable.)
	 * All free()'s of existing nano objects will be leaked.
	 */
	
	mprotect(nanozone, sizeof(nanozone->basic_zone), PROT_READ | PROT_WRITE);
	
	nanozone->basic_zone.size = (void *)nano_size; /* Unchanged. */
	nanozone->basic_zone.malloc = (void *)nano_forked_malloc;
	nanozone->basic_zone.calloc = (void *)nano_forked_calloc;
	nanozone->basic_zone.valloc = (void *)nano_valloc; /* Unchanged, already always obtained from helper zone. */
	nanozone->basic_zone.free = (void *)nano_forked_free;
	nanozone->basic_zone.realloc = (void *)nano_forked_realloc;
	nanozone->basic_zone.destroy = (void *)nano_destroy; /* Unchanged. */
	nanozone->basic_zone.batch_malloc = (void *)nano_forked_batch_malloc;
	nanozone->basic_zone.batch_free = (void *)nano_forked_batch_free;
	nanozone->basic_zone.introspect = (struct malloc_introspection_t *)&nano_introspect; /* Unchanged. */
	nanozone->basic_zone.memalign = (void *)nano_memalign;								 /* Unchanged. */
	nanozone->basic_zone.free_definite_size = (void *)nano_forked_free_definite_size;
	
	mprotect(nanozone, sizeof(nanozone->basic_zone), PROT_READ);
}

