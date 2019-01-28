//
//  malloc_debug.c
//  libsystem_malloc
//
//  Created by 王举范 on 2018/12/19.
//

#include "malloc_debug.h"

/*********	Functions for performance tools	************/

static kern_return_t
_malloc_default_reader(task_t task, vm_address_t address, vm_size_t size, void **ptr)
{
	*ptr = (void *)address;
	return 0;
}

kern_return_t
malloc_get_all_zones(task_t task, memory_reader_t reader, vm_address_t **addresses, unsigned *count)
{
	// Note that the 2 following addresses are not correct if the address of the target is different from your own.  This notably
	// occurs if the address of System.framework is slid (e.g. different than at B & I )
	vm_address_t remote_malloc_zones = (vm_address_t)&malloc_zones;
	vm_address_t remote_malloc_num_zones = (vm_address_t)&malloc_num_zones;
	kern_return_t err;
	vm_address_t zones_address;
	vm_address_t *zones_address_ref;
	unsigned num_zones;
	unsigned *num_zones_ref;
	if (!reader) {
		reader = _malloc_default_reader;
	}
	// printf("Read malloc_zones at address %p should be %p\n", &malloc_zones, malloc_zones);
	err = reader(task, remote_malloc_zones, sizeof(void *), (void **)&zones_address_ref);
	// printf("Read malloc_zones[%p]=%p\n", remote_malloc_zones, *zones_address_ref);
	if (err) {
		malloc_printf("*** malloc_get_all_zones: error reading zones_address at %p\n", (unsigned)remote_malloc_zones);
		return err;
	}
	zones_address = *zones_address_ref;
	// printf("Reading num_zones at address %p\n", remote_malloc_num_zones);
	err = reader(task, remote_malloc_num_zones, sizeof(unsigned), (void **)&num_zones_ref);
	if (err) {
		malloc_printf("*** malloc_get_all_zones: error reading num_zones at %p\n", (unsigned)remote_malloc_num_zones);
		return err;
	}
	num_zones = *num_zones_ref;
	// printf("Read malloc_num_zones[%p]=%d\n", remote_malloc_num_zones, num_zones);
	*count = num_zones;
	// printf("malloc_get_all_zones succesfully found %d zones\n", num_zones);
	err = reader(task, zones_address, sizeof(malloc_zone_t *) * num_zones, (void **)addresses);
	if (err) {
		malloc_printf("*** malloc_get_all_zones: error reading zones at %p\n", &zones_address);
		return err;
	}
	// printf("malloc_get_all_zones succesfully read %d zones\n", num_zones);
	return err;
}

/*********	Debug helpers	************/

void
malloc_zone_print_ptr_info(void *ptr)
{
	malloc_zone_t *zone;
	if (!ptr) {
		return;
	}
	zone = malloc_zone_from_ptr(ptr);
	if (zone) {
		printf("ptr %p in registered zone %p\n", ptr, zone);
	} else {
		printf("ptr %p not in heap\n", ptr);
	}
}

boolean_t
malloc_zone_check(malloc_zone_t *zone)
{
	boolean_t ok = 1;
	if (!zone) {
		unsigned index = 0;
		while (index < malloc_num_zones) {
			zone = malloc_zones[index++];
			if (!zone->introspect->check(zone)) {
				ok = 0;
			}
		}
	} else {
		ok = zone->introspect->check(zone);
	}
	return ok;
}

void
malloc_zone_print(malloc_zone_t *zone, boolean_t verbose)
{
	if (!zone) {
		unsigned index = 0;
		while (index < malloc_num_zones) {
			zone = malloc_zones[index++];
			zone->introspect->print(zone, verbose);
		}
	} else {
		zone->introspect->print(zone, verbose);
	}
}

void
malloc_zone_statistics(malloc_zone_t *zone, malloc_statistics_t *stats)
{
	if (!zone) {
		memset(stats, 0, sizeof(*stats));
		unsigned index = 0;
		while (index < malloc_num_zones) {
			zone = malloc_zones[index++];
			malloc_statistics_t this_stats;
			zone->introspect->statistics(zone, &this_stats);
			stats->blocks_in_use += this_stats.blocks_in_use;
			stats->size_in_use += this_stats.size_in_use;
			stats->max_size_in_use += this_stats.max_size_in_use;
			stats->size_allocated += this_stats.size_allocated;
		}
	} else {
		zone->introspect->statistics(zone, stats);
	}
}

void
malloc_zone_log(malloc_zone_t *zone, void *address)
{
	if (!zone) {
		unsigned index = 0;
		while (index < malloc_num_zones) {
			zone = malloc_zones[index++];
			zone->introspect->log(zone, address);
		}
	} else {
		zone->introspect->log(zone, address);
	}
}

/*********	Misc other entry points	************/

static void
DefaultMallocError(int x)
{
#if USE_SLEEP_RATHER_THAN_ABORT
	malloc_printf("*** error %d\n", x);
	sleep(3600);
#else
	_SIMPLE_STRING b = _simple_salloc();
	if (b) {
		_simple_sprintf(b, "*** error %d", x);
		malloc_printf("%s\n", _simple_string(b));
		_os_set_crash_log_message_dynamic(_simple_string(b));
	} else {
		_malloc_printf(MALLOC_PRINTF_NOLOG, "*** error %d", x);
		_os_set_crash_log_message("*** DefaultMallocError called");
	}
	abort();
#endif
}

void (*malloc_error(void (*func)(int)))(int)
{
	return DefaultMallocError;
}

static void
_malloc_lock_all(void (*callout)(void))
{
	unsigned index = 0;
	MALLOC_LOCK();
	while (index < malloc_num_zones) {
		malloc_zone_t *zone = malloc_zones[index++];
		zone->introspect->force_lock(zone);
	}
	callout();
}

static void
_malloc_unlock_all(void (*callout)(void))
{
	unsigned index = 0;
	callout();
	while (index < malloc_num_zones) {
		malloc_zone_t *zone = malloc_zones[index++];
		zone->introspect->force_unlock(zone);
	}
	MALLOC_UNLOCK();
}

static void
_malloc_reinit_lock_all(void (*callout)(void))
{
	unsigned index = 0;
	callout();
	while (index < malloc_num_zones) {
		malloc_zone_t *zone = malloc_zones[index++];
		if (zone->version < 9) { // Version must be >= 9 to look at reinit_lock
			zone->introspect->force_unlock(zone);
		} else {
			zone->introspect->reinit_lock(zone);
		}
	}
	MALLOC_REINIT_LOCK();
}


// Called prior to fork() to guarantee that malloc is not in any critical
// sections during the fork(); prevent any locks from being held by non-
// surviving threads after the fork.
void
_malloc_fork_prepare(void)
{
	return _malloc_lock_all(&__stack_logging_fork_prepare);
}

// Called in the parent process after fork() to resume normal operation.
void
_malloc_fork_parent(void)
{
	return _malloc_unlock_all(&__stack_logging_fork_parent);
}

/*
 * A Glibc-like mstats() interface.
 *
 * Note that this interface really isn't very good, as it doesn't understand
 * that we may have multiple allocators running at once.  We just massage
 * the result from malloc_zone_statistics in any case.
 */
struct mstats
mstats(void)
{
	malloc_statistics_t s;
	struct mstats m;
	
	malloc_zone_statistics(NULL, &s);
	m.bytes_total = s.size_allocated;
	m.chunks_used = s.blocks_in_use;
	m.bytes_used = s.size_in_use;
	m.chunks_free = 0;
	m.bytes_free = m.bytes_total - m.bytes_used; /* isn't this somewhat obvious? */
	
	return (m);
}

boolean_t
malloc_zone_enable_discharge_checking(malloc_zone_t *zone)
{
	if (zone->version < 7) { // Version must be >= 7 to look at the new discharge checking fields.
		return FALSE;
	}
	if (NULL == zone->introspect->enable_discharge_checking) {
		return FALSE;
	}
	return zone->introspect->enable_discharge_checking(zone);
}

void
malloc_zone_disable_discharge_checking(malloc_zone_t *zone)
{
	if (zone->version < 7) { // Version must be >= 7 to look at the new discharge checking fields.
		return;
	}
	if (NULL == zone->introspect->disable_discharge_checking) {
		return;
	}
	zone->introspect->disable_discharge_checking(zone);
}

void
malloc_zone_discharge(malloc_zone_t *zone, void *memory)
{
	if (NULL == zone) {
		zone = malloc_zone_from_ptr(memory);
	}
	if (NULL == zone) {
		return;
	}
	if (zone->version < 7) { // Version must be >= 7 to look at the new discharge checking fields.
		return;
	}
	if (NULL == zone->introspect->discharge) {
		return;
	}
	zone->introspect->discharge(zone, memory);
}

void
malloc_zone_enumerate_discharged_pointers(malloc_zone_t *zone, void (^report_discharged)(void *memory, void *info))
{
	if (!zone) {
		unsigned index = 0;
		while (index < malloc_num_zones) {
			zone = malloc_zones[index++];
			if (zone->version < 7) {
				continue;
			}
			if (NULL == zone->introspect->enumerate_discharged_pointers) {
				continue;
			}
			zone->introspect->enumerate_discharged_pointers(zone, report_discharged);
		}
	} else {
		if (zone->version < 7) {
			return;
		}
		if (NULL == zone->introspect->enumerate_discharged_pointers) {
			return;
		}
		zone->introspect->enumerate_discharged_pointers(zone, report_discharged);
	}
}



/*****************	OBSOLETE ENTRY POINTS	********************/

#if PHASE_OUT_OLD_MALLOC
#error PHASE OUT THE FOLLOWING FUNCTIONS
#endif

void
set_malloc_singlethreaded(boolean_t single)
{
	static boolean_t warned = 0;
	if (!warned) {
#if PHASE_OUT_OLD_MALLOC
		malloc_printf("*** OBSOLETE: set_malloc_singlethreaded(%d)\n", single);
#endif
		warned = 1;
	}
}

void
malloc_singlethreaded(void)
{
	static boolean_t warned = 0;
	if (!warned) {
		malloc_printf("*** OBSOLETE: malloc_singlethreaded()\n");
		warned = 1;
	}
}

int
malloc_debug(int level)
{
	malloc_printf("*** OBSOLETE: malloc_debug()\n");
	return 0;
}

/* vim: set noet:ts=4:sw=4:cindent: */
