//
//  nano_segregated.c
//  libsystem_malloc
//
//  Created by 王举范 on 2018/12/17.
//

#include "nano_segregated.h"
/*
 * We maintain separate free lists for each (quantized) size. The literature
 * calls this the "segregated policy".
 */

static boolean_t
segregated_band_grow(nanozone_t *nanozone,
					 nano_meta_admin_t pMeta,
					 size_t slot_bytes,
					 unsigned int mag_index)
{
	nano_blk_addr_t u; // the compiler holds this in a register
	uintptr_t p, s;
	size_t watermark, hiwater;
	
	if (0 == pMeta->slot_current_base_addr) { // First encounter?
		
		u.fields.nano_signature = NANOZONE_SIGNATURE;
		u.fields.nano_mag_index = mag_index;
		u.fields.nano_band = 0;
		u.fields.nano_slot = (slot_bytes >> SHIFT_NANO_QUANTUM) - 1;
		u.fields.nano_offset = 0;
		
		p = u.addr;
		pMeta->slot_bytes = (unsigned int)slot_bytes;
		pMeta->slot_objects = SLOT_IN_BAND_SIZE / slot_bytes;
	} else {
		p = pMeta->slot_current_base_addr + BAND_SIZE; // Growing, so stride ahead by BAND_SIZE
		
		u.addr = (uint64_t)p;
		if (0 == u.fields.nano_band) { // Did the band index wrap?
			return FALSE;
		}
		
		assert(slot_bytes == pMeta->slot_bytes);
	}
	pMeta->slot_current_base_addr = p;
	
	mach_vm_address_t vm_addr = p & ~((uintptr_t)(BAND_SIZE - 1)); // Address of the (2MB) band covering this (128KB) slot
	if (nanozone->band_max_mapped_baseaddr[mag_index] < vm_addr) {
#if !NANO_PREALLOCATE_BAND_VM
		// Obtain the next band to cover this slot
		kern_return_t kr = mach_vm_map(mach_task_self(),
									   &vm_addr,
									   BAND_SIZE, 0,
									   VM_MAKE_TAG(VM_MEMORY_MALLOC_NANO),
									   MEMORY_OBJECT_NULL, 0, FALSE,
									   VM_PROT_DEFAULT,
									   VM_PROT_ALL,
									   VM_INHERIT_DEFAULT);
		
		void *q = (void *)vm_addr;
		if (kr || q != (void *)(p & ~((uintptr_t)(BAND_SIZE - 1)))) { // Must get exactly what we asked for
			if (!kr) {
				mach_vm_deallocate(mach_task_self(), vm_addr, BAND_SIZE);
			}
			return FALSE;
		}
#endif
		nanozone->band_max_mapped_baseaddr[mag_index] = vm_addr;
	}
	
	// Randomize the starting allocation from this slot (introduces 11 to 14 bits of entropy)
	if (0 == pMeta->slot_objects_mapped) { // First encounter?
		pMeta->slot_objects_skipped = (malloc_entropy[1] % (SLOT_IN_BAND_SIZE / slot_bytes));
		pMeta->slot_bump_addr = p + (pMeta->slot_objects_skipped * slot_bytes);
	} else {
		pMeta->slot_bump_addr = p;
	}
	
	pMeta->slot_limit_addr = p + (SLOT_IN_BAND_SIZE / slot_bytes) * slot_bytes;
	pMeta->slot_objects_mapped += (SLOT_IN_BAND_SIZE / slot_bytes);
	
	u.fields.nano_signature = NANOZONE_SIGNATURE;
	u.fields.nano_mag_index = mag_index;
	u.fields.nano_band = 0;
	u.fields.nano_slot = 0;
	u.fields.nano_offset = 0;
	s = u.addr; // Base for this core.
	
	// Set the high water mark for this CPU's entire magazine, if this resupply raised it.
	watermark = nanozone->core_mapped_size[mag_index];
	hiwater = MAX(watermark, p - s + SLOT_IN_BAND_SIZE);
	nanozone->core_mapped_size[mag_index] = hiwater;
	
	return TRUE;
}



static MALLOC_INLINE void *
segregated_next_block(nanozone_t *nanozone,
					  nano_meta_admin_t pMeta,
					  size_t slot_bytes, unsigned int mag_index)
{
	while (1) {
		uintptr_t theLimit = pMeta->slot_limit_addr; // Capture the slot limit that bounds slot_bump_addr right now
		uintptr_t b = OSAtomicAdd64Barrier(slot_bytes, (volatile int64_t *)&(pMeta->slot_bump_addr));
		b -= slot_bytes; // Atomic op returned addr of *next* free block. Subtract to get addr for *this* allocation.
		
		if (b < theLimit) {   // Did we stay within the bound of the present slot allocation?
			return (void *)b; // Yep, so the slot_bump_addr this thread incremented is good to go
		} else {
			if (pMeta->slot_exhausted) { // exhausted all the bands availble for this slot?
				return 0;				 // We're toast
			} else {
				// One thread will grow the heap, others will see its been grown and retry allocation
				_malloc_lock_lock(&nanozone->band_resupply_lock[mag_index]);
				// re-check state now that we've taken the lock
				if (pMeta->slot_exhausted) {
					_malloc_lock_unlock(&nanozone->band_resupply_lock[mag_index]);
					return 0; // Toast
				} else if (b < pMeta->slot_limit_addr) {
					_malloc_lock_unlock(&nanozone->band_resupply_lock[mag_index]);
					continue; // ... the slot was successfully grown by first-taker (not us). Now try again.
				} else if (segregated_band_grow(nanozone, pMeta, slot_bytes, mag_index)) {
					_malloc_lock_unlock(&nanozone->band_resupply_lock[mag_index]);
					continue; // ... the slot has been successfully grown by us. Now try again.
				} else {
					pMeta->slot_exhausted = TRUE;
					_malloc_lock_unlock(&nanozone->band_resupply_lock[mag_index]);
					return 0;
				}
			}
		}
	}
}

static MALLOC_INLINE size_t
segregated_size_to_fit(nanozone_t *nanozone, size_t size, size_t *pKey)
{
	size_t k, slot_bytes;
	
	if (0 == size) {
		size = NANO_REGIME_QUANTA_SIZE; // Historical behavior
	}
	k = (size + NANO_REGIME_QUANTA_SIZE - 1) >> SHIFT_NANO_QUANTUM; // round up and shift for number of quanta
	slot_bytes = k << SHIFT_NANO_QUANTUM;							// multiply by power of two quanta size
	*pKey = k - 1;													// Zero-based!
	
	return slot_bytes;
}


static kern_return_t
segregated_in_use_enumerator(task_t task,
							 void *context,
							 unsigned type_mask,
							 nanozone_t *nanozone,
							 memory_reader_t reader,
							 vm_range_recorder_t recorder)
{
	unsigned int mag_index, slot_key;
	vm_range_t ptr_range;
	vm_range_t buffer[MAX_RECORDER_BUFFER];
	kern_return_t err;
	unsigned count = 0;
	
	for (mag_index = 0; mag_index < nanozone->phys_ncpus; mag_index++) {
		uintptr_t clone_magazine;  // magazine base for ourselves
		nano_blk_addr_t p;		   // slot base for remote
		uintptr_t clone_slot_base; // slot base for ourselves (tracks with "p")
		
		// Establish p as base address for slot 0 in remote
		p.fields.nano_signature = NANOZONE_SIGNATURE;//6
		p.fields.nano_mag_index = mag_index;
		p.fields.nano_band = 0;
		p.fields.nano_slot = 0;
		p.fields.nano_offset = 0;
		
		if (type_mask & MALLOC_PTR_IN_USE_RANGE_TYPE) {
			mach_vm_address_t vm_addr;
			mach_vm_size_t alloc_size = nanozone->core_mapped_size[mag_index];
			int alloc_flags = VM_FLAGS_ANYWHERE | VM_MAKE_TAG(VM_MEMORY_MALLOC);
			
			vm_addr = vm_page_size;
			kern_return_t kr = mach_vm_allocate(mach_task_self(), &vm_addr, alloc_size, alloc_flags);
			if (kr) {
				return kr;
			}
			clone_magazine = (uintptr_t)vm_addr;
			clone_slot_base = clone_magazine; // base for slot 0 in this local magazine
		} else {
			clone_slot_base = clone_magazine = 0; // and won't be used in this loop
		}
		
		for (slot_key = 0;
			 slot_key < SLOT_KEY_LIMIT;
			 p.addr += SLOT_IN_BAND_SIZE, // Advance to next slot base for remote
			 	clone_slot_base += SLOT_IN_BAND_SIZE,							   // Advance to next slot base for ourselves
			 	slot_key++) {
			nano_meta_admin_t pMeta = &(nanozone->meta_data[mag_index][slot_key]);
			size_t slot_objects_mapped = pMeta->slot_objects_mapped; // capture this volatile count
			
			if (0 == slot_objects_mapped) { // Nothing allocated in this magazine for this slot?
				continue;
			}
			
			if (type_mask & MALLOC_ADMIN_REGION_RANGE_TYPE) {
				/* do NOTHING as there is no distinct admin region */
			}
			
			if (type_mask & (MALLOC_PTR_REGION_RANGE_TYPE | MALLOC_ADMIN_REGION_RANGE_TYPE)) {
				nano_blk_addr_t q = p;
				uintptr_t skip_adj = index_to_offset(nanozone, pMeta, (index_t)pMeta->slot_objects_skipped);
				
				while (q.addr < pMeta->slot_limit_addr) {
					ptr_range.address = q.addr + skip_adj;
					ptr_range.size = SLOT_IN_BAND_SIZE - skip_adj;
					skip_adj = 0;
					recorder(task, context, MALLOC_PTR_REGION_RANGE_TYPE, &ptr_range, 1);
					q.addr += BAND_SIZE;
				}
			}
			
			if (type_mask & MALLOC_PTR_IN_USE_RANGE_TYPE) {
				nano_blk_addr_t q = p;
				uintptr_t slot_band, clone_slot_band_base = clone_slot_base;
				uintptr_t skip_adj = index_to_offset(nanozone, pMeta, (index_t)pMeta->slot_objects_skipped);
				
				while (q.addr < pMeta->slot_limit_addr) {
					// read slot in each remote band. Lands in some random location.
					size_t len = MIN(pMeta->slot_bump_addr - q.addr, SLOT_IN_BAND_SIZE);
					err = reader(task, (vm_address_t)(q.addr + skip_adj), len - skip_adj, (void **)&slot_band);
					if (err) {
						return err;
					}
					
					// Place the data just read in the correct position relative to the local magazine.
					memcpy((void *)(clone_slot_band_base + skip_adj), (void *)slot_band, len - skip_adj);
					
					// Simultaneously advance pointers in remote and ourselves to the next band.
					q.addr += BAND_SIZE;
					clone_slot_band_base += BAND_SIZE;
					skip_adj = 0;
				}
				
				// Walk the slot free list and populate a bitarray_t
				int log_size = 64 - __builtin_clzl(slot_objects_mapped);
				bitarray_t slot_bitarray = bitarray_create(log_size);
				
				if (!slot_bitarray) {
					return errno;
				}
				
				chained_block_t t;
				unsigned stoploss = (unsigned)slot_objects_mapped;
				while ((t = OSAtomicDequeue(
							&(pMeta->slot_LIFO),
							offsetof(struct chained_block_s, next) + (clone_slot_base - p.addr)))) {
					if (0 == stoploss) {
						malloc_printf("Free list walk in segregated_in_use_enumerator exceeded object count.");
						break;
					}
					stoploss--;
					
					uintptr_t offset = ((uintptr_t)t - p.addr); // offset from beginning of slot, task-independent
					index_t block_index = offset_to_index(nanozone, pMeta, offset);
					
					if (block_index < slot_objects_mapped) {
						bitarray_set(slot_bitarray, log_size, block_index);
					}
				}
				// N.B. pMeta->slot_LIFO in *this* task is now drained (remote free list has *not* been disturbed)
				
				// Copy the bitarray_t denoting madvise()'d pages (if any) into *this* task's address space
				bitarray_t madv_page_bitarray;
				int log_page_count;
				
				if (pMeta->slot_madvised_pages) {
					log_page_count = pMeta->slot_madvised_log_page_count;
					err = reader(task, (vm_address_t)(pMeta->slot_madvised_pages), bitarray_size(log_page_count),
								 (void **)&madv_page_bitarray);
					if (err) {
						return err;
					}
				} else {
					madv_page_bitarray = NULL;
					log_page_count = 0;
				}
				
				// Enumerate all the block indices issued to date, and report those not on the free list
				index_t i;
				for (i = (index_t)pMeta->slot_objects_skipped; i < slot_objects_mapped; ++i) {
					uintptr_t block_offset = index_to_offset(nanozone, pMeta, i);
					if (p.addr + block_offset >= pMeta->slot_bump_addr) {
						break;
					}
					
					// blocks falling on madvise()'d pages are free! So not enumerated.
					if (madv_page_bitarray) {
						nano_blk_addr_t q;
						index_t pgnum, pgnum_end;
						
						q.addr = p.addr + block_offset;
						pgnum = ((((unsigned)q.fields.nano_band) << NANO_OFFSET_BITS) | ((unsigned)q.fields.nano_offset)) >>
						vm_kernel_page_shift;
						q.addr += pMeta->slot_bytes - 1;
						pgnum_end = ((((unsigned)q.fields.nano_band) << NANO_OFFSET_BITS) | ((unsigned)q.fields.nano_offset)) >>
						vm_kernel_page_shift;
						
						if (pgnum < (1 << log_page_count)) { // bounds check for bitarray_get()'s that follow
							if (bitarray_get(madv_page_bitarray, log_page_count, pgnum) ||
								bitarray_get(madv_page_bitarray, log_page_count, pgnum_end)) {
								continue;
							}
						}
					}
					
					if (!bitarray_get(slot_bitarray, log_size, i)) {
						buffer[count].address = p.addr + block_offset;
						buffer[count].size = (slot_key + 1) << SHIFT_NANO_QUANTUM;
						count++;
						if (count >= MAX_RECORDER_BUFFER) {
							recorder(task, context, MALLOC_PTR_IN_USE_RANGE_TYPE, buffer, count);
							count = 0;
						}
					}
				}
				if (count) {
					recorder(task, context, MALLOC_PTR_IN_USE_RANGE_TYPE, buffer, count);
					count = 0;
				}
				
				free(slot_bitarray);
			}
		}
		if (clone_magazine) {
			mach_vm_address_t vm_addr = clone_magazine;
			mach_vm_size_t alloc_size = nanozone->core_mapped_size[mag_index];
			mach_vm_deallocate(mach_task_self(), vm_addr, alloc_size);
		}
	}
	return 0;
}
