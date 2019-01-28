//
//  nano_debug.c
//  libsystem_malloc
//
//  Created by 王举范 on 2018/12/17.
//

#include "nano_debug.h"
// msg prints after fmt, ...

static MALLOC_NOINLINE void
nanozone_error(nanozone_t *nanozone, int is_corruption, const char *msg, const void *ptr, const char *fmt, ...)
{
	va_list ap;
	_SIMPLE_STRING b = _simple_salloc();
	
	if (b) {
		if (fmt) {
			va_start(ap, fmt);
			_simple_vsprintf(b, fmt, ap);
			va_end(ap);
		}
		if (ptr) {
			_simple_sprintf(b, "*** error for object %p: %s\n", ptr, msg);
		} else {
			_simple_sprintf(b, "*** error: %s\n", msg);
		}
		malloc_printf("%s*** set a breakpoint in malloc_error_break to debug\n", _simple_string(b));
	} else {
		/*
		 * Should only get here if vm_allocate() can't get a single page of
		 * memory, implying _simple_asl_log() would also fail.  So we just
		 * print to the file descriptor.
		 */
		if (fmt) {
			va_start(ap, fmt);
			_malloc_vprintf(MALLOC_PRINTF_NOLOG, fmt, ap);
			va_end(ap);
		}
		if (ptr) {
			_malloc_printf(MALLOC_PRINTF_NOLOG, "*** error for object %p: %s\n", ptr, msg);
		} else {
			_malloc_printf(MALLOC_PRINTF_NOLOG, "*** error: %s\n", msg);
		}
		_malloc_printf(MALLOC_PRINTF_NOLOG, "*** set a breakpoint in malloc_error_break to debug\n");
	}
	malloc_error_break();
	
	// Call abort() if this is a memory corruption error and the abort on
	// corruption flag is set, or if any error should abort.
	if ((is_corruption && (nanozone->debug_flags & MALLOC_ABORT_ON_CORRUPTION))
		||(nanozone->debug_flags & MALLOC_ABORT_ON_ERROR)) {
		_os_set_crash_log_message_dynamic(b ? _simple_string(b) : msg);
		abort();
	} else if (b) {
		_simple_sfree(b);
	}
}

static void
nano_statistics(nanozone_t *nanozone, malloc_statistics_t *stats)
{
	int i, j;
	
	bzero(stats, sizeof(*stats));
	
	for (i = 0; i < nanozone->phys_ncpus; ++i) {
		nano_blk_addr_t p;
		
		// Establish p as base address for slot 0 in this CPU magazine
		p.fields.nano_signature = NANOZONE_SIGNATURE;
		p.fields.nano_mag_index = i;
		p.fields.nano_band = 0;
		p.fields.nano_slot = 0;
		p.fields.nano_offset = 0;
		
		for (j = 0; j < NANO_SLOT_SIZE; p.addr += SLOT_IN_BAND_SIZE, // Advance to next slot base
			 ++j) {
			nano_meta_admin_t pMeta = &nanozone->meta_data[i][j];
			uintptr_t offset = pMeta->slot_bump_addr - p.addr;
			
			if (0 == pMeta->slot_current_base_addr) { // Nothing allocated in this magazine for this slot?
				continue;
			} else {
				unsigned blocks_touched = offset_to_index(nanozone, pMeta, offset) - (unsigned)pMeta->slot_objects_skipped;
				unsigned blocks_now_free = count_free(nanozone, pMeta);
				unsigned blocks_in_use = blocks_touched - blocks_now_free;
				
				size_t size_hiwater = ((j + 1) << SHIFT_NANO_QUANTUM) * blocks_touched;
				size_t size_in_use = ((j + 1) << SHIFT_NANO_QUANTUM) * blocks_in_use;
				size_t size_allocated = ((offset / BAND_SIZE) + 1) * SLOT_IN_BAND_SIZE;
				
				stats->blocks_in_use += blocks_in_use;
				
				stats->max_size_in_use += size_hiwater;
				stats->size_in_use += size_in_use;
				stats->size_allocated += size_allocated;
			}
		}
	}
}
