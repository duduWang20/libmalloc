
#include "malloc_zone_public.h"

/*********	Generic ANSI callouts	************/

void *
malloc(size_t size)
{
	void *retval;
	retval = malloc_zone_malloc(default_zone, size);
	if (retval == NULL) {
		errno = ENOMEM;
	}
	return retval;
}

void *
calloc(size_t num_items, size_t size)
{
	void *retval;
	retval = malloc_zone_calloc(default_zone, num_items, size);
	if (retval == NULL) {
		errno = ENOMEM;
	}
	return retval;
}

void
free(void *ptr)
{
	malloc_zone_t *zone;
	size_t size;
	if (!ptr) {
		return;
	}
	zone = find_registered_zone(ptr, &size);
	if (!zone) {
		malloc_printf(
					  "*** error for object %p: pointer being freed was not allocated\n"
					  "*** set a breakpoint in malloc_error_break to debug\n",
					  ptr);
		malloc_error_break();
		if ((malloc_debug_flags & (MALLOC_ABORT_ON_CORRUPTION | MALLOC_ABORT_ON_ERROR))) {
			_SIMPLE_STRING b = _simple_salloc();
			if (b) {
				_simple_sprintf(b, "*** error for object %p: pointer being freed was not allocated\n", ptr);
				_os_set_crash_log_message_dynamic(_simple_string(b));
			} else {
				_os_set_crash_log_message("*** error: pointer being freed was not allocated\n");
			}
			abort();
		}
	} else if (zone->version >= 6 && zone->free_definite_size) {
		malloc_zone_free_definite_size(zone, ptr, size);
	} else {
		malloc_zone_free(zone, ptr);
	}
}

void *
realloc(void *in_ptr, size_t new_size)
{
	void *retval = NULL;
	void *old_ptr;
	malloc_zone_t *zone;
	
	// SUSv3: "If size is 0 and ptr is not a null pointer, the object
	// pointed to is freed. If the space cannot be allocated, the object
	// shall remain unchanged."  Also "If size is 0, either a null pointer
	// or a unique pointer that can be successfully passed to free() shall
	// be returned."  We choose to allocate a minimum size object by calling
	// malloc_zone_malloc with zero size, which matches "If ptr is a null
	// pointer, realloc() shall be equivalent to malloc() for the specified
	// size."  So we only free the original memory if the allocation succeeds.
	old_ptr = (new_size == 0) ? NULL : in_ptr;
	if (!old_ptr) {
		retval = malloc_zone_malloc(default_zone, new_size);
	} else {
		zone = find_registered_zone(old_ptr, NULL);
		if (!zone) {
			malloc_printf(
						  "*** error for object %p: pointer being realloc'd was not allocated\n"
						  "*** set a breakpoint in malloc_error_break to debug\n",
						  old_ptr);
			malloc_error_break();
			if ((malloc_debug_flags & (MALLOC_ABORT_ON_CORRUPTION | MALLOC_ABORT_ON_ERROR))) {
				_SIMPLE_STRING b = _simple_salloc();
				if (b) {
					_simple_sprintf(b, "*** error for object %p: pointer being realloc'd was not allocated\n", old_ptr);
					_os_set_crash_log_message_dynamic(_simple_string(b));
				} else {
					_os_set_crash_log_message("*** error: pointer being realloc'd was not allocated\n");
				}
				abort();
			}
		} else {
			retval = malloc_zone_realloc(zone, old_ptr, new_size);
		}
	}
	if (retval == NULL) {
		errno = ENOMEM;
	} else if (new_size == 0) {
		free(in_ptr);
	}
	return retval;
}

void *
valloc(size_t size)
{
	void *retval;
	malloc_zone_t *zone = default_zone;
	retval = malloc_zone_valloc(zone, size);
	if (retval == NULL) {
		errno = ENOMEM;
	}
	return retval;
}

extern void
vfree(void *ptr)
{
	free(ptr);
}

size_t
malloc_size(const void *ptr)
{
	size_t size = 0;
	
	if (!ptr) {
		return size;
	}
	
	(void)find_registered_zone(ptr, &size);
	return size;
}

size_t
malloc_good_size(size_t size)
{
	malloc_zone_t *zone = default_zone;
	return zone->introspect->good_size(zone, size);
}

/*
 * The posix_memalign() function shall allocate size bytes aligned on a boundary specified by alignment,
 * and shall return a pointer to the allocated memory in memptr.
 * The value of alignment shall be a multiple of sizeof( void *), that is also a power of two.
 * Upon successful completion, the value pointed to by memptr shall be a multiple of alignment.
 *
 * Upon successful completion, posix_memalign() shall return zero; otherwise,
 * an error number shall be returned to indicate the error.
 *
 * The posix_memalign() function shall fail if:
 * EINVAL
 *	The value of the alignment parameter is not a power of two multiple of sizeof( void *).
 * ENOMEM
 *	There is insufficient memory available with the requested alignment.
 */

int
posix_memalign(void **memptr, size_t alignment, size_t size)
{
	void *retval;
	
	/* POSIX is silent on NULL == memptr !?! */
	
	retval = malloc_zone_memalign(default_zone, alignment, size);
	if (retval == NULL) {
		// To avoid testing the alignment constraints redundantly, we'll rely on the
		// test made in malloc_zone_memalign to vet each request. Only if that test fails
		// and returns NULL, do we arrive here to detect the bogus alignment and give the
		// required EINVAL return.
		if (alignment < sizeof(void *) ||			  // excludes 0 == alignment
			0 != (alignment & (alignment - 1))) { // relies on sizeof(void *) being a power of two.
			return EINVAL;
		}
		return ENOMEM;
	} else {
		*memptr = retval; // Set iff allocation succeeded
		return 0;
	}
}

void *
reallocarray(void * in_ptr, size_t nmemb, size_t size){
	size_t alloc_size;
	if (os_mul_overflow(nmemb, size, &alloc_size)){
		errno = ENOMEM;
		return NULL;
	}
	return realloc(in_ptr, alloc_size);
}

void *
reallocarrayf(void * in_ptr, size_t nmemb, size_t size){
	size_t alloc_size;
	if (os_mul_overflow(nmemb, size, &alloc_size)){
		errno = ENOMEM;
		return NULL;
	}
	return reallocf(in_ptr, alloc_size);
}

static malloc_zone_t *
find_registered_purgeable_zone(void *ptr)
{
	if (!ptr) {
		return NULL;
	}
	
	/*
	 * Look for a zone which contains ptr.  If that zone does not have the purgeable malloc flag
	 * set, or the allocation is too small, do nothing.  Otherwise, set the allocation volatile.
	 * FIXME: for performance reasons, we should probably keep a separate list of purgeable zones
	 * and only search those.
	 */
	size_t size = 0;
	malloc_zone_t *zone = find_registered_zone(ptr, &size);
	
	/* FIXME: would really like a zone->introspect->flags->purgeable check, but haven't determined
	 * binary compatibility impact of changing the introspect struct yet. */
	if (!zone) {
		return NULL;
	}
	
	/* Check to make sure pointer is page aligned and size is multiple of page size */
	if ((size < vm_page_size) || ((size % vm_page_size) != 0)) {
		return NULL;
	}
	
	return zone;
}


void
malloc_enter_process_memory_limit_warn_mode(void)
{
	// <rdar://problem/25063714>
}

#if ENABLE_MEMORY_RESOURCE_EXCEPTION_HANDLING

// Is the system elible to turn on/off MSL lite in response to memory resource exceptions
//
// Return true if
// - The user has not explicitly opted out
//     and
// - Either the user has explicitly opted in or this is an Apple Internal enabled build

static boolean_t
check_is_eligible_for_lite_mode_mre_handling(void)
{
	struct stat stat_buf;
	
	// User opted out
	if (stat("/var/db/disableLiteModeMemoryResourceExceptionHandling", &stat_buf) == 0) {
		return false;
	}
	
	// User opted in
	if (stat("/var/db/enableLiteModeMemoryResourceExceptionHandling", &stat_buf) == 0) {
		return true;
	}
	
	
	// Not enabled for everything else
	return false;
}

// Not thread-safe, but it's called from malloc_memory_event_handler which already assumes
// single thread execution.
static boolean_t
is_eligible_for_lite_mode_mre_handling(void)
{
	static boolean_t is_eligible = false;
	static boolean_t needs_check = true;
	
	if (needs_check) {
		is_eligible = check_is_eligible_for_lite_mode_mre_handling();
		needs_check = false;
	}
	
	return is_eligible;
}

#endif

static void
handle_msl_memory_event(unsigned long event)
{
	// don't mix and match enabling mechanisms
	if (warn_mode_entered) {
		return;
	}
	
	event &= NOTE_MEMORYSTATUS_MSL_STATUS;
	
	// sanity check
	if (event == 0) {
		return;
	}
	
	// first check if the disable bit is set
	if (event & MEMORYSTATUS_DISABLE_MSL) {
		turn_off_stack_logging();
		return;
	}
	
	boolean_t msl_malloc = (event & MEMORYSTATUS_ENABLE_MSL_MALLOC);
	boolean_t msl_vm = (event & MEMORYSTATUS_ENABLE_MSL_VM);
	boolean_t msl_lite = (event & MEMORYSTATUS_ENABLE_MSL_LITE);
	
	// The following always checks to make it's not possible to enable two different modes
	// For instance this would not be allowed:
	// Enable lite
	// Disable
	// Enable full
	
	// Currently there is no separation of malloc/vm in lite mode
	if (msl_lite) {
		if (msl_type_enabled_at_runtime == stack_logging_mode_none || msl_type_enabled_at_runtime == stack_logging_mode_lite) {
			msl_type_enabled_at_runtime = stack_logging_mode_lite;
			turn_on_stack_logging(stack_logging_mode_lite);
		}
		return;
	} else if (msl_malloc && msl_vm) {
		if (msl_type_enabled_at_runtime == stack_logging_mode_none || msl_type_enabled_at_runtime == stack_logging_mode_all) {
			msl_type_enabled_at_runtime = stack_logging_mode_all;
			turn_on_stack_logging(stack_logging_mode_all);
		}
		return;
	} else if (msl_malloc) {
		if (msl_type_enabled_at_runtime == stack_logging_mode_none || msl_type_enabled_at_runtime == stack_logging_mode_malloc) {
			msl_type_enabled_at_runtime = stack_logging_mode_malloc;
			turn_on_stack_logging(stack_logging_mode_malloc);
		}
		return;
	} else if (msl_vm) {
		if (msl_type_enabled_at_runtime == stack_logging_mode_none || msl_type_enabled_at_runtime == stack_logging_mode_vm) {
			msl_type_enabled_at_runtime = stack_logging_mode_vm;
			turn_on_stack_logging(stack_logging_mode_vm);
		}
		return;
	}
}

// Note that malloc_memory_event_handler is not thread-safe, and we are relying on the callers of this for synchronization
void
malloc_memory_event_handler(unsigned long event)
{
	if (event & NOTE_MEMORYSTATUS_PRESSURE_WARN) {
		malloc_zone_pressure_relief(0, 0);
	}
	
	// First check for enable/disable MSL - only recognize if all other bits are 0
	// Don't attempt this if we've either entered or exited MRE mode
	if ((event & NOTE_MEMORYSTATUS_MSL_STATUS) != 0 && (event & ~NOTE_MEMORYSTATUS_MSL_STATUS) == 0 && !warn_mode_entered && !warn_mode_disable_retries) {
		handle_msl_memory_event(event);
		return;
	}
	
#if ENABLE_MEMORY_RESOURCE_EXCEPTION_HANDLING
	// If we have reached EXC_RESOURCE, we no longer need stack log data.
	// If we are under system-wide memory pressure, we should jettison stack log data.
	if ((event & (NOTE_MEMORYSTATUS_PROC_LIMIT_CRITICAL | NOTE_MEMORYSTATUS_PRESSURE_CRITICAL)) &&
		!warn_mode_disable_retries) {
		// If we have crossed the EXC_RESOURCE limit once already, there is no point in
		// collecting stack logs in the future, even if we missed a previous chance to
		// collect data because nobody is going to ask us for it again.
		warn_mode_disable_retries = true;
		
		// Only try to clean up stack log data if it was enabled through a proc limit warning.
		// User initiated stack logging should proceed unimpeded.
		if (warn_mode_entered) {
			malloc_printf("malloc_memory_event_handler: stopping stack-logging\n");
			turn_off_stack_logging();
			__malloc_lock_stack_logging();
			__delete_uniquing_table_memory_while_locked();
			__malloc_unlock_stack_logging();
			
			warn_mode_entered = false;
		}
	}
	
	// Enable stack logging if we are approaching the process limit, provided
	// we aren't under system wide memory pressure and we're allowed to try again.
	if ((event & NOTE_MEMORYSTATUS_PROC_LIMIT_WARN) &&
		!(event & NOTE_MEMORYSTATUS_PRESSURE_CRITICAL) &&
		!warn_mode_entered && !warn_mode_disable_retries &&
		is_eligible_for_lite_mode_mre_handling()) {
		malloc_printf("malloc_memory_event_handler: approaching memory limit. Starting stack-logging.\n");
		if (turn_on_stack_logging(stack_logging_mode_lite)) {
			warn_mode_entered = true;
			
			// set the maximum allocation threshold
			max_lite_mallocs = MAX_LITE_MALLOCS;
		}
	}
#endif
}

size_t
malloc_zone_pressure_relief(malloc_zone_t *zone, size_t goal)
{
	if (!zone) {
		unsigned index = 0;
		size_t total = 0;
		
		// Take lock to defend against malloc_destroy_zone()
		MALLOC_LOCK();
		while (index < malloc_num_zones) {
			zone = malloc_zones[index++];
			if (zone->version < 8) {
				continue;
			}
			if (NULL == zone->pressure_relief) {
				continue;
			}
			if (0 == goal) { /* Greedy */
				total += zone->pressure_relief(zone, 0);
			} else if (goal > total) {
				total += zone->pressure_relief(zone, goal - total);
			} else { /* total >= goal */
				break;
			}
		}
		MALLOC_UNLOCK();
		return total;
	} else {
		// Assumes zone is not destroyed for the duration of this call
		if (zone->version < 8) {
			return 0;
		}
		if (NULL == zone->pressure_relief) {
			return 0;
		}
		return zone->pressure_relief(zone, goal);
	}
}
