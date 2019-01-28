//
//  malloc_zone_block.c
//  libsystem_malloc
//
//  Created by 王举范 on 2018/12/19.
//

#include "malloc_zone_block.h"



/*********	Block creation and manipulation	************/

static void
internal_check(void)
{
	static vm_address_t *frames = NULL;
	static unsigned num_frames;
	if (malloc_zone_check(NULL)) {
		if (!frames) {
			vm_allocate(mach_task_self(), (void *)&frames, vm_page_size, 1);
		}
		thread_stack_pcs(frames, (unsigned)(vm_page_size / sizeof(vm_address_t) - 1), &num_frames);
	} else {
		_SIMPLE_STRING b = _simple_salloc();
		if (b) {
			_simple_sprintf(b, "*** MallocCheckHeap: FAILED check at %dth operation\n", malloc_check_counter - 1);
		} else {
			_malloc_printf(MALLOC_PRINTF_NOLOG, "*** MallocCheckHeap: FAILED check at %dth operation\n", malloc_check_counter - 1);
		}
		malloc_printf("*** MallocCheckHeap: FAILED check at %dth operation\n", malloc_check_counter - 1);
		if (frames) {
			unsigned index = 1;
			if (b) {
				_simple_sappend(b, "Stack for last operation where the malloc check succeeded: ");
				while (index < num_frames)
					_simple_sprintf(b, "%p ", (void*)frames[index++]);
				malloc_printf("%s\n(Use 'atos' for a symbolic stack)\n", _simple_string(b));
			} else {
				/*
				 * Should only get here if vm_allocate() can't get a single page of
				 * memory, implying _simple_asl_log() would also fail.  So we just
				 * print to the file descriptor.
				 */
				_malloc_printf(MALLOC_PRINTF_NOLOG, "Stack for last operation where the malloc check succeeded: ");
				while (index < num_frames)
					_malloc_printf(MALLOC_PRINTF_NOLOG, "%p ", frames[index++]);
				_malloc_printf(MALLOC_PRINTF_NOLOG, "\n(Use 'atos' for a symbolic stack)\n");
			}
		}
		if (malloc_check_each > 1) {
			unsigned recomm_each = (malloc_check_each > 10) ? malloc_check_each / 10 : 1;
			unsigned recomm_start =
			(malloc_check_counter > malloc_check_each + 1) ? malloc_check_counter - 1 - malloc_check_each : 1;
			malloc_printf(
						  "*** Recommend using 'setenv MallocCheckHeapStart %d; setenv MallocCheckHeapEach %d' to narrow down failure\n",
						  recomm_start, recomm_each);
		}
		if (malloc_check_abort) {
			if (b) {
				_os_set_crash_log_message_dynamic(_simple_string(b));
			} else {
				_os_set_crash_log_message("*** MallocCheckHeap: FAILED check");
			}
			abort();
		} else if (b) {
			_simple_sfree(b);
		}
		if (malloc_check_sleep > 0) {
			_malloc_printf(ASL_LEVEL_NOTICE, "*** Sleeping for %d seconds to leave time to attach\n", malloc_check_sleep);
			sleep(malloc_check_sleep);
		} else if (malloc_check_sleep < 0) {
			_malloc_printf(ASL_LEVEL_NOTICE, "*** Sleeping once for %d seconds to leave time to attach\n", -malloc_check_sleep);
			sleep(-malloc_check_sleep);
			malloc_check_sleep = 0;
		}
	}
	malloc_check_start += malloc_check_each;
}

void *
malloc_zone_malloc(malloc_zone_t *zone, size_t size)
{
	MALLOC_TRACE(TRACE_malloc | DBG_FUNC_START, (uintptr_t)zone, size, 0, 0);
	
	void *ptr;
	if (malloc_check_start && (malloc_check_counter++ >= malloc_check_start)) {
		internal_check();
	}
	if (size > MALLOC_ABSOLUTE_MAX_SIZE) {
		return NULL;
	}
	
	ptr = zone->malloc(zone, size);
	// if lite zone is passed in then we still call the lite methods
	
	
	if (malloc_logger) {
		malloc_logger(MALLOC_LOG_TYPE_ALLOCATE | MALLOC_LOG_TYPE_HAS_ZONE, (uintptr_t)zone, (uintptr_t)size, 0, (uintptr_t)ptr, 0);
	}
	
	MALLOC_TRACE(TRACE_malloc | DBG_FUNC_END, (uintptr_t)zone, size, (uintptr_t)ptr, 0);
	return ptr;
}

void *
malloc_zone_calloc(malloc_zone_t *zone, size_t num_items, size_t size)
{
	void *ptr;
	size_t alloc_size;
	if (malloc_check_start && (malloc_check_counter++ >= malloc_check_start)) {
		internal_check();
	}
	if (os_mul_overflow(num_items, size, &alloc_size) || alloc_size > MALLOC_ABSOLUTE_MAX_SIZE){
		errno = ENOMEM;
		return NULL;
	}
	
	ptr = zone->calloc(zone, num_items, size);
	
	if (malloc_logger) {
		malloc_logger(MALLOC_LOG_TYPE_ALLOCATE | MALLOC_LOG_TYPE_HAS_ZONE | MALLOC_LOG_TYPE_CLEARED, (uintptr_t)zone,
					  (uintptr_t)(num_items * size), 0, (uintptr_t)ptr, 0);
	}
	return ptr;
}

void *
malloc_zone_valloc(malloc_zone_t *zone, size_t size)
{
	void *ptr;
	if (malloc_check_start && (malloc_check_counter++ >= malloc_check_start)) {
		internal_check();
	}
	if (size > MALLOC_ABSOLUTE_MAX_SIZE) {
		return NULL;
	}
	
	ptr = zone->valloc(zone, size);
	
	if (malloc_logger) {
		malloc_logger(MALLOC_LOG_TYPE_ALLOCATE | MALLOC_LOG_TYPE_HAS_ZONE, (uintptr_t)zone, (uintptr_t)size, 0, (uintptr_t)ptr, 0);
	}
	return ptr;
}

void *
malloc_zone_realloc(malloc_zone_t *zone, void *ptr, size_t size)
{
	MALLOC_TRACE(TRACE_realloc | DBG_FUNC_START, (uintptr_t)zone, (uintptr_t)ptr, size, 0);
	
	void *new_ptr;
	if (malloc_check_start && (malloc_check_counter++ >= malloc_check_start)) {
		internal_check();
	}
	if (size > MALLOC_ABSOLUTE_MAX_SIZE) {
		return NULL;
	}
	
	new_ptr = zone->realloc(zone, ptr, size);
	
	if (malloc_logger) {
		malloc_logger(MALLOC_LOG_TYPE_ALLOCATE | MALLOC_LOG_TYPE_DEALLOCATE | MALLOC_LOG_TYPE_HAS_ZONE, (uintptr_t)zone,
					  (uintptr_t)ptr, (uintptr_t)size, (uintptr_t)new_ptr, 0);
	}
	MALLOC_TRACE(TRACE_realloc | DBG_FUNC_END, (uintptr_t)zone, (uintptr_t)ptr, size, (uintptr_t)new_ptr);
	return new_ptr;
}

void
malloc_zone_free(malloc_zone_t *zone, void *ptr)
{
	MALLOC_TRACE(TRACE_free, (uintptr_t)zone, (uintptr_t)ptr, 0, 0);
	
	if (malloc_logger) {
		malloc_logger(MALLOC_LOG_TYPE_DEALLOCATE | MALLOC_LOG_TYPE_HAS_ZONE, (uintptr_t)zone, (uintptr_t)ptr, 0, 0, 0);
	}
	if (malloc_check_start && (malloc_check_counter++ >= malloc_check_start)) {
		internal_check();
	}
	
	zone->free(zone, ptr);
}

static void
malloc_zone_free_definite_size(malloc_zone_t *zone, void *ptr, size_t size)
{
	MALLOC_TRACE(TRACE_free, (uintptr_t)zone, (uintptr_t)ptr, size, 0);
	
	if (malloc_logger) {
		malloc_logger(MALLOC_LOG_TYPE_DEALLOCATE | MALLOC_LOG_TYPE_HAS_ZONE, (uintptr_t)zone, (uintptr_t)ptr, 0, 0, 0);
	}
	if (malloc_check_start && (malloc_check_counter++ >= malloc_check_start)) {
		internal_check();
	}
	
	zone->free_definite_size(zone, ptr, size);
}

malloc_zone_t *
malloc_zone_from_ptr(const void *ptr)
{
	if (!ptr) {
		return NULL;
	} else {
		return find_registered_zone(ptr, NULL);
	}
}

void *
malloc_zone_memalign(malloc_zone_t *zone, size_t alignment, size_t size)
{
	MALLOC_TRACE(TRACE_memalign | DBG_FUNC_START, (uintptr_t)zone, alignment, size, 0);
	
	void *ptr;
	if (zone->version < 5) { // Version must be >= 5 to look at the new memalign field.
		return NULL;
	}
	if (malloc_check_start && (malloc_check_counter++ >= malloc_check_start)) {
		internal_check();
	}
	if (size > MALLOC_ABSOLUTE_MAX_SIZE) {
		return NULL;
	}
	if (alignment < sizeof(void *) ||			  // excludes 0 == alignment
		0 != (alignment & (alignment - 1))) { // relies on sizeof(void *) being a power of two.
		return NULL;
	}
	
	if (!(zone->memalign)) {
		return NULL;
	}
	ptr = zone->memalign(zone, alignment, size);
	
	if (malloc_logger) {
		malloc_logger(MALLOC_LOG_TYPE_ALLOCATE | MALLOC_LOG_TYPE_HAS_ZONE, (uintptr_t)zone, (uintptr_t)size, 0, (uintptr_t)ptr, 0);
	}
	
	MALLOC_TRACE(TRACE_memalign | DBG_FUNC_END, (uintptr_t)zone, alignment, size, (uintptr_t)ptr);
	return ptr;
}

