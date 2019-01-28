//
//  nano_introspection.c
//  libsystem_malloc
//
//  Created by 王举范 on 2018/12/17.
//

#include "nano_introspection.h"

/****************           introspection methods         *********************/

static kern_return_t
nanozone_default_reader(task_t task, vm_address_t address, vm_size_t size, void **ptr)
{
	*ptr = (void *)address;
	return 0;
}

static kern_return_t
nano_ptr_in_use_enumerator(task_t task,
						   void *context,
						   unsigned type_mask,
						   vm_address_t zone_address,
						   memory_reader_t reader,
						   vm_range_recorder_t recorder)
{
	nanozone_t *nanozone;
	kern_return_t err;
	
	if (!reader) {
		reader = nanozone_default_reader;
	}
	
	err = reader(task, zone_address, sizeof(nanozone_t), (void **)&nanozone);
	if (err) {
		return err;
	}
	
	err = segregated_in_use_enumerator(task, context, type_mask, nanozone, reader, recorder);
	return err;
}

static size_t
nano_good_size(nanozone_t *nanozone, size_t size)
{
	if (size <= NANO_MAX_SIZE) {
		return _nano_good_size(nanozone, size);
	} else {
		malloc_zone_t *zone = (malloc_zone_t *)(nanozone->helper_zone);
		return zone->introspect->good_size(zone, size);
	}
}

// TODO sanity checks
unsigned nanozone_check_counter = 0;
unsigned nanozone_check_start = 0;
unsigned nanozone_check_modulo = 1;

static boolean_t
nano_check_all(nanozone_t *nanozone, const char *function)
{
	return 1;
}

static boolean_t
nanozone_check(nanozone_t *nanozone)
{
	if ((++nanozone_check_counter % 10000) == 0) {
		_malloc_printf(ASL_LEVEL_NOTICE, "at nanozone_check counter=%d\n", nanozone_check_counter);
	}
	
	if (nanozone_check_counter < nanozone_check_start) {
		return 1;
	}
	
	if (nanozone_check_counter % nanozone_check_modulo) {
		return 1;
	}
	
	return nano_check_all(nanozone, "");
}

static unsigned
count_free(nanozone_t *nanozone, nano_meta_admin_t pMeta)
{
	chained_block_t head = NULL, tail = NULL, t;
	unsigned count = 0;
	
	unsigned stoploss = (unsigned)pMeta->slot_objects_mapped;
	while ((t = OSAtomicDequeue(&(pMeta->slot_LIFO), offsetof(struct chained_block_s, next)))) {
		if (0 == stoploss) {
			nanozone_error(nanozone, 1, "Free list walk in count_free exceeded object count.", (void *)&(pMeta->slot_LIFO), NULL);
		}
		stoploss--;
		
		if (NULL == head) {
			head = t;
		} else {
			tail->next = t;
		}
		tail = t;
		
		count++;
	}
	if (tail) {
		tail->next = NULL;
	}
	
	// push the free list extracted above back onto the LIFO, all at once
	if (head) {
		OSAtomicEnqueue(&(pMeta->slot_LIFO),
						head,
						(uintptr_t)tail - (uintptr_t)head + offsetof(struct chained_block_s, next));
	}
	
	return count;
}

static void
nano_print(nanozone_t *nanozone, boolean_t verbose)
{
	unsigned int mag_index, slot_key;
	malloc_statistics_t stats;
	
	nano_statistics(nanozone, &stats);
	_malloc_printf(MALLOC_PRINTF_NOLOG | MALLOC_PRINTF_NOPREFIX, "Nanozone %p: inUse=%d(%dKB) touched=%dKB allocated=%dMB\n",
				   nanozone, stats.blocks_in_use, stats.size_in_use >> 10, stats.max_size_in_use >> 10, stats.size_allocated >> 20);
	
	for (mag_index = 0; mag_index < nanozone->phys_ncpus; mag_index++) {
		nano_blk_addr_t p;
		
		// Establish p as base address for band 0, slot 0, offset 0
		p.fields.nano_signature = NANOZONE_SIGNATURE;
		p.fields.nano_mag_index = mag_index;
		p.fields.nano_band = 0;
		p.fields.nano_slot = 0;
		p.fields.nano_offset = 0;
		
		for (slot_key = 0; slot_key < SLOT_KEY_LIMIT; p.addr += SLOT_IN_BAND_SIZE, // Advance to next slot base
			 slot_key++) {
			nano_meta_admin_t pMeta = &(nanozone->meta_data[mag_index][slot_key]);
			uintptr_t slot_bump_addr = pMeta->slot_bump_addr;		 // capture this volatile pointer
			size_t slot_objects_mapped = pMeta->slot_objects_mapped; // capture this volatile count
			
			if (0 == slot_objects_mapped) { // Nothing allocated in this magazine for this slot?
				_malloc_printf(MALLOC_PRINTF_NOLOG | MALLOC_PRINTF_NOPREFIX, "Magazine %2d(%3d) Unrealized\n", mag_index,
							   (slot_key + 1) << SHIFT_NANO_QUANTUM);
				continue;
			}
			
			uintptr_t offset = (0 == slot_bump_addr ? 0 : slot_bump_addr - p.addr);
			unsigned blocks_touched = offset_to_index(nanozone, pMeta, offset) - (unsigned)pMeta->slot_objects_skipped;
			unsigned blocks_now_free = count_free(nanozone, pMeta);
			unsigned blocks_in_use = blocks_touched - blocks_now_free;
			
			size_t size_hiwater = ((slot_key + 1) << SHIFT_NANO_QUANTUM) * blocks_touched;
			size_t size_in_use = ((slot_key + 1) << SHIFT_NANO_QUANTUM) * blocks_in_use;
			size_t size_allocated = ((offset / BAND_SIZE) + 1) * SLOT_IN_BAND_SIZE;
			
			_malloc_printf(MALLOC_PRINTF_NOLOG | MALLOC_PRINTF_NOPREFIX,
						   "Magazine %2d(%3d) [%p, %3dKB] \t Allocations in use=%4d \t Bytes in use=%db \t Untouched=%dKB\n", mag_index,
						   (slot_key + 1) << SHIFT_NANO_QUANTUM, p, (size_allocated >> 10), blocks_in_use, size_in_use,
						   (size_allocated - size_hiwater) >> 10);
			
			if (!verbose) {
				continue;
			} else {
				// Walk the slot free list and populate a bitarray_t
				int log_size = 64 - __builtin_clzl(slot_objects_mapped);
				bitarray_t slot_bitarray = bitarray_create(log_size);
				
				if (!slot_bitarray) {
					malloc_printf("bitarray_create(%d) in nano_print returned errno=%d.", log_size, errno);
					return;
				}
				
				chained_block_t head = NULL, tail = NULL, t;
				unsigned stoploss = (unsigned)slot_objects_mapped;
				while ((t = OSAtomicDequeue(&(pMeta->slot_LIFO), offsetof(struct chained_block_s, next)))) {
					if (0 == stoploss) {
						malloc_printf("Free list walk in nano_print exceeded object count.");
						break;
					}
					stoploss--;
					
					uintptr_t offset = ((uintptr_t)t - p.addr); // offset from beginning of slot
					index_t block_index = offset_to_index(nanozone, pMeta, offset);
					
					if (NULL == head) {
						head = t;
					} else {
						tail->next = t;
					}
					tail = t;
					
					if (block_index < slot_objects_mapped) {
						bitarray_set(slot_bitarray, log_size, block_index);
					}
				}
				if (tail) {
					tail->next = NULL;
				}
				
				index_t i;
				for (i = 0; i < slot_objects_mapped; ++i) {
					nano_blk_addr_t q;
					size_t pgnum;
					uintptr_t block_offset = index_to_offset(nanozone, pMeta, i);
					if (p.addr + block_offset >= slot_bump_addr) {
						break;
					}
					
					q.addr = p.addr + block_offset;
					pgnum = ((((unsigned)q.fields.nano_band) << NANO_OFFSET_BITS) | ((unsigned)q.fields.nano_offset)) >>
					vm_kernel_page_shift;
					
					if (i < pMeta->slot_objects_skipped) {
						_malloc_printf(MALLOC_PRINTF_NOLOG | MALLOC_PRINTF_NOPREFIX, "_");
					} else if (bitarray_get(slot_bitarray, log_size, i)) {
						_malloc_printf(MALLOC_PRINTF_NOLOG | MALLOC_PRINTF_NOPREFIX, "F");
					} else if (pMeta->slot_madvised_pages && (pgnum < (1 << pMeta->slot_madvised_log_page_count)) &&
							   bitarray_get(pMeta->slot_madvised_pages, pMeta->slot_madvised_log_page_count, (index_t)pgnum)) {
						_malloc_printf(MALLOC_PRINTF_NOLOG | MALLOC_PRINTF_NOPREFIX, "M");
					} else {
						_malloc_printf(MALLOC_PRINTF_NOLOG | MALLOC_PRINTF_NOPREFIX, ".");
					}
				}
				_malloc_printf(MALLOC_PRINTF_NOLOG | MALLOC_PRINTF_NOPREFIX, "\n");
				
				free(slot_bitarray);
				
				// push the free list extracted above back onto the LIFO, all at once
				if (head) {
					OSAtomicEnqueue(
									&(pMeta->slot_LIFO), head, (uintptr_t)tail - (uintptr_t)head + offsetof(struct chained_block_s, next));
				}
			}
		}
	}
	return;
}

static void
nano_log(malloc_zone_t *zone, void *log_address)
{
}

static void
nano_force_lock(nanozone_t *nanozone)
{
	int i;
	
	for (i = 0; i < nanozone->phys_ncpus; ++i) {
		_malloc_lock_lock(&nanozone->band_resupply_lock[i]);
	}
}

static void
nano_force_unlock(nanozone_t *nanozone)
{
	int i;
	
	for (i = 0; i < nanozone->phys_ncpus; ++i) {
		_malloc_lock_unlock(&nanozone->band_resupply_lock[i]);
	}
}

static void
nano_reinit_lock(nanozone_t *nanozone)
{
	int i;
	
	for (i = 0; i < nanozone->phys_ncpus; ++i) {
		_malloc_lock_init(&nanozone->band_resupply_lock[i]);
	}
}

static boolean_t
_nano_locked(nanozone_t *nanozone)
{
	int i;
	
	for (i = 0; i < nanozone->phys_ncpus; ++i) {
		if (_malloc_lock_trylock(&nanozone->band_resupply_lock[i])) {
			_malloc_lock_unlock(&nanozone->band_resupply_lock[i]);
			return TRUE;
		}
	}
	return FALSE;
}

static boolean_t
nano_locked(nanozone_t *nanozone)
{
	malloc_zone_t *zone = (malloc_zone_t *)(nanozone->helper_zone);
	
	return _nano_locked(nanozone) || zone->introspect->zone_locked(zone);
}

#endif // CONFIG_NANOZONE

/* vim: set noet:ts=4:sw=4:cindent: */
