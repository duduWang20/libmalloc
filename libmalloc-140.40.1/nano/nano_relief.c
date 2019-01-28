//
//  nano_relief.c
//  libsystem_malloc
//
//  Created by 王举范 on 2018/12/18.
//

#include "nano_relief.h"

static size_t
nano_try_madvise(nanozone_t *nanozone, size_t goal)
{
	unsigned int mag_index, slot_key;
	size_t bytes_toward_goal = 0;
	
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
			// _malloc_printf(ASL_LEVEL_WARNING,"nano_try_madvise examining slot base %p\n", p.addr);
			nano_meta_admin_t pMeta = &(nanozone->meta_data[mag_index][slot_key]);
			uintptr_t slot_bump_addr = pMeta->slot_bump_addr;		 // capture this volatile pointer
			size_t slot_objects_mapped = pMeta->slot_objects_mapped; // capture this volatile count
			
			if (0 == slot_objects_mapped) { // Nothing allocated in this magazine for this slot?
				continue;
			} else {
				// Walk the slot free list and populate a bitarray_t
				int log_size = 64 - __builtin_clzl(slot_objects_mapped);
				bitarray_t slot_bitarray = bitarray_create(log_size);
				
				unsigned int slot_bytes = pMeta->slot_bytes;
				int log_page_count = 64 - __builtin_clzl((slot_objects_mapped * slot_bytes) / vm_kernel_page_size);
				log_page_count = 1 + MAX(0, log_page_count);
				bitarray_t page_bitarray = bitarray_create(log_page_count);
				
				// _malloc_printf(ASL_LEVEL_WARNING,"slot_bitarray: %db page_bitarray: %db\n", bitarray_size(log_size),
				// bitarray_size(log_page_count));
				if (!slot_bitarray) {
					malloc_printf("bitarray_create(%d) in nano_try_madvise returned errno=%d.", log_size, errno);
					free(page_bitarray);
					return bytes_toward_goal;
				}
				
				if (!page_bitarray) {
					malloc_printf("bitarray_create(%d) in nano_try_madvise returned errno=%d.", log_page_count, errno);
					free(slot_bitarray);
					return bytes_toward_goal;
				}
				
				chained_block_t head = NULL, tail = NULL, t;
				unsigned stoploss = (unsigned)slot_objects_mapped;
				while ((t = OSAtomicDequeue(&(pMeta->slot_LIFO), offsetof(struct chained_block_s, next)))) {
					if (0 == stoploss) {
						malloc_printf("Free list walk in nano_try_madvise exceeded object count.");
						break;
					}
					stoploss--;
					
					uintptr_t offset = ((uintptr_t)t - p.addr); // offset from beginning of slot
					index_t block_index = offset_to_index(nanozone, pMeta, offset);
					
					// build a simple linked list of the free blocks we're able to obtain
					if (NULL == head) {
						head = t;
					} else {
						tail->next = t;
					}
					tail = t;
					
					// take note in a bitarray_t of each free block we're able to obtain (allows fast lookup below)
					if (block_index < slot_objects_mapped) {
						bitarray_set(slot_bitarray, log_size, block_index);
					}
				}
				if (tail) {
					tail->next = NULL;
				}
				
				if (NULL == head) {
					free(slot_bitarray);
					free(page_bitarray);
					continue;
				}
				
				index_t i;
				nano_blk_addr_t q;
				size_t pgnum;
				for (i = (index_t)pMeta->slot_objects_skipped; i < slot_objects_mapped; ++i) {
					uintptr_t block_offset = index_to_offset(nanozone, pMeta, i);
					if (p.addr + block_offset >= slot_bump_addr) {
						break;
					}
					
					if (!bitarray_get(slot_bitarray, log_size, i)) { // is block i allocated or already on an madvise'd page?
						
						// Mark the page(s) it resides on as live
						q.addr = p.addr + block_offset;
						pgnum = ((((unsigned)q.fields.nano_band) << NANO_OFFSET_BITS) | ((unsigned)q.fields.nano_offset)) >>
						vm_kernel_page_shift;
						bitarray_set(page_bitarray, log_page_count, (index_t)pgnum);
						
						q.addr += slot_bytes - 1;
						pgnum = ((((unsigned)q.fields.nano_band) << NANO_OFFSET_BITS) | ((unsigned)q.fields.nano_offset)) >>
						vm_kernel_page_shift;
						bitarray_set(page_bitarray, log_page_count, (index_t)pgnum);
					}
				}
				
				free(slot_bitarray);
				
				q.addr = p.addr + index_to_offset(nanozone, pMeta, (index_t)pMeta->slot_objects_skipped);
				index_t pgstart =
				((((unsigned)q.fields.nano_band) << NANO_OFFSET_BITS) | ((unsigned)q.fields.nano_offset)) >> vm_kernel_page_shift;
				
				q.addr = slot_bump_addr - slot_bytes;
				pgnum = ((((unsigned)q.fields.nano_band) << NANO_OFFSET_BITS) | ((unsigned)q.fields.nano_offset)) >> vm_kernel_page_shift;
				
				// _malloc_printf(ASL_LEVEL_WARNING,"Examining %d pages. Slot base %p.\n", pgnum - pgstart + 1, p.addr);
				
				if (pMeta->slot_madvised_pages) {
					if (pMeta->slot_madvised_log_page_count < log_page_count) {
						bitarray_t new_madvised_pages = bitarray_create(log_page_count);
						index_t index;
						while (bitarray_zap_first_set(pMeta->slot_madvised_pages, pMeta->slot_madvised_log_page_count, &index)) {
							bitarray_set(new_madvised_pages, log_page_count, index);
						}
						free(pMeta->slot_madvised_pages);
						pMeta->slot_madvised_pages = new_madvised_pages;
						pMeta->slot_madvised_log_page_count = log_page_count;
					}
				} else {
					pMeta->slot_madvised_pages = bitarray_create(log_page_count);
					pMeta->slot_madvised_log_page_count = log_page_count;
				}
				
				bitarray_t will_madvise_pages = bitarray_create(log_page_count);
				int num_advised = 0;
				
				for (i = pgstart; i < pgnum; ++i) {
					if ((i < (1 << log_page_count)) && // bounds check for the bitarray_get()'s that follow.
						!bitarray_get(pMeta->slot_madvised_pages, log_page_count, i) && // already madvise'd?
						!bitarray_get(page_bitarray, log_page_count, i))				// no live allocations?
					{
						num_advised++;
						bitarray_set(will_madvise_pages, log_page_count, i);
					}
				}
				free(page_bitarray);
				
				if (num_advised) {
					chained_block_t new_head = NULL, new_tail = NULL;
					// _malloc_printf(ASL_LEVEL_WARNING,"Constructing residual free list starting at %p num_advised %d\n", head,
					// num_advised);
					t = head;
					while (t) {
						q.addr = (uintptr_t)t;
						index_t pgnum_start =
						((((unsigned)q.fields.nano_band) << NANO_OFFSET_BITS) | ((unsigned)q.fields.nano_offset)) >>
						vm_kernel_page_shift;
						q.addr += slot_bytes - 1;
						index_t pgnum_end =
						((((unsigned)q.fields.nano_band) << NANO_OFFSET_BITS) | ((unsigned)q.fields.nano_offset)) >>
						vm_kernel_page_shift;
						
						// bounds check for the bitarray_get()'s that follow. If the pgnum is beyond the
						// capacity of the will_madvise_pages just restore the block to the free list.
						if (pgnum_start >= (1 << log_page_count)) {
							if (NULL == new_head) {
								new_head = t;
							} else {
								new_tail->next = t;
							}
							new_tail = t;
						}
						// If the block nowhere lies on an madvise()'d page restore it to the slot free list.
						else if (!bitarray_get(will_madvise_pages, log_page_count, pgnum_start) &&
								 !bitarray_get(will_madvise_pages, log_page_count, pgnum_end)) {
							if (NULL == new_head) {
								new_head = t;
							} else {
								new_tail->next = t;
							}
							new_tail = t;
						}
						
						t = t->next;
					}
					if (new_tail) {
						new_tail->next = NULL;
					}
					
					// push the free list extracted above back onto the LIFO, all at once
					if (new_head) {
						OSAtomicEnqueue(&(pMeta->slot_LIFO), new_head,
										(uintptr_t)new_tail - (uintptr_t)new_head + offsetof(struct chained_block_s, next));
					}
				} else {
					// _malloc_printf(ASL_LEVEL_WARNING,"Reinstating free list since no pages were madvised (%d).\n", num_advised);
					if (head) {
						OSAtomicEnqueue(&(pMeta->slot_LIFO), head,
										(uintptr_t)tail - (uintptr_t)head + offsetof(struct chained_block_s, next));
					}
				}
				
				for (i = pgstart; i < pgnum; ++i) {
					if ((i < (1 << log_page_count)) && bitarray_get(will_madvise_pages, log_page_count, i)) {
						q = p;
						q.fields.nano_band = (i << vm_kernel_page_shift) >> NANO_OFFSET_BITS;
						q.fields.nano_offset = (i << vm_kernel_page_shift) & ((1 << NANO_OFFSET_BITS) - 1);
						// _malloc_printf(ASL_LEVEL_WARNING,"Entire page non-live: %d. Slot base %p, madvising %p\n", i, p.addr,
						// q.addr);
						
						if (nanozone->debug_flags & MALLOC_DO_SCRIBBLE) {
							memset((void *)q.addr, SCRUBBLE_BYTE, vm_kernel_page_size);
						}
						
						if (-1 == madvise((void *)q.addr, vm_kernel_page_size, MADV_FREE_REUSABLE))
						{
							/* -1 return: VM map entry change makes this unfit for reuse. Something evil lurks. */
#if DEBUG_MADVISE
							nanozone_error(nanozone, 0, "madvise(..., MADV_FREE_REUSABLE) failed", (void *)cwq.addrpgLo,
										   "length=%d\n", vm_page_size);
#endif
						} else {
							bytes_toward_goal += vm_kernel_page_size;
							bitarray_set(pMeta->slot_madvised_pages, log_page_count, i);
						}
					}
				}
				free(will_madvise_pages);
				
				if (!bitarray_first_set(pMeta->slot_madvised_pages, log_page_count)) {
					free(pMeta->slot_madvised_pages);
					pMeta->slot_madvised_pages = NULL;
					pMeta->slot_madvised_log_page_count = 0;
				}
				
				if (goal && bytes_toward_goal >= goal) {
					return bytes_toward_goal;
				}
			}
		}
	}
	return bytes_toward_goal;
}

static size_t
nano_pressure_relief(nanozone_t *nanozone, size_t goal)
{
	return nano_try_madvise(nanozone, goal);
}

