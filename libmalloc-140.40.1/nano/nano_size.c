
#include "nano_size.h"

static MALLOC_INLINE size_t
_nano_size(nanozone_t *nanozone, const void *ptr)
{
	return _nano_vet_and_size_of_live(nanozone, ptr);
}


static MALLOC_INLINE size_t
_nano_vet_and_size_of_live(nanozone_t *nanozone, const void *ptr)
{
	size_t size = __nano_vet_and_size(nanozone, ptr);
	
	if (0 == size) { // ptr fails sanity check?
		return 0;
	}
	
	// We have the invariant: If ptr is on a free list, then ptr->double_free_guard is the canary.
	// So if ptr->double_free_guard is NOT the canary, then ptr is not on a free list, hence is live.
	if ((((chained_block_t)ptr)->double_free_guard ^ nanozone->cookie)
		!= 0xBADDC0DEDEADBEADULL) {
		return size; // Common case: not on a free list, hence live. Return its size.
	} else {
		// confirm that ptr is live despite ptr->double_free_guard having the canary value
		if (_nano_block_inuse_p(nanozone, ptr)) {
			return size; // live block that exhibits canary
		} else {
			return 0; // ptr wasn't live after all (likely a double free)
		}
	}
}

static MALLOC_INLINE MALLOC_UNUSED boolean_t
_nano_block_inuse_p(nanozone_t *nanozone, const void *ptr)
{
	nano_blk_addr_t p; // happily, the compiler holds this in a register
	nano_meta_admin_t pMeta;
	chained_block_t head = NULL, tail = NULL, t;
	boolean_t inuse = TRUE;
	
	p.addr = (uint64_t)ptr; // place ptr on the dissecting table
	
	pMeta = &(nanozone->meta_data[p.fields.nano_mag_index][p.fields.nano_slot]);
	
	// pop elements off the free list all the while looking for ptr.
	unsigned stoploss = (unsigned)pMeta->slot_objects_mapped;
	while ((t = OSAtomicDequeue(&(pMeta->slot_LIFO), offsetof(struct chained_block_s, next)))) {
		if (0 == stoploss) {
			nanozone_error(
						   nanozone, 1, "Free list walk in _nano_block_inuse_p exceeded object count.", (void *)&(pMeta->slot_LIFO), NULL);
		}
		stoploss--;
		
		if (NULL == head) {
			head = t;
		} else {
			tail->next = t;
		}
		tail = t;
		
		if (ptr == t) {
			inuse = FALSE;
			break;
		}
	}
	if (tail) {
		tail->next = NULL;
	}
	
	// push the free list extracted above back onto the LIFO, all at once
	if (head) {
		OSAtomicEnqueue(&(pMeta->slot_LIFO), head, (uintptr_t)tail - (uintptr_t)head + offsetof(struct chained_block_s, next));
	}
	
	return inuse;
}


static MALLOC_INLINE size_t
__nano_vet_and_size(nanozone_t *nanozone, const void *ptr)
{
	// Extracts the size of the block in bytes. Checks for a plausible ptr.
	nano_blk_addr_t p; // the compiler holds this in a register
	nano_meta_admin_t pMeta;
	
	p.addr = (uint64_t)ptr; // Begin the dissection of ptr
	
	if (nanozone->our_signature != p.fields.nano_signature) {
		return 0;
	}
	
	if (nanozone->phys_ncpus <= p.fields.nano_mag_index) {
		return 0;
	}
	
	if (p.fields.nano_offset & NANO_QUANTA_MASK) { // stray low-order bits?
		return 0;
	}
	
	pMeta = &(nanozone->meta_data[p.fields.nano_mag_index][p.fields.nano_slot]);
	if ((void *)(pMeta->slot_bump_addr) <= ptr) {
		return 0; // Beyond what's ever been allocated!
	}
	if ((p.fields.nano_offset % pMeta->slot_bytes) != 0) {
		return 0; // Not an exact multiple of the block size for this slot
	}
	return pMeta->slot_bytes;
}


static MALLOC_INLINE size_t
_nano_vet_and_size_of_free(nanozone_t *nanozone, const void *ptr)
{
	size_t size = __nano_vet_and_size(nanozone, ptr);
	
	if (0 == size) { // ptr fails sanity check?
		return 0;
	}
	
	// ptr was just dequed from a free list, so ptr->double_free_guard must have the canary value.
	if ((((chained_block_t)ptr)->double_free_guard ^ nanozone->cookie) == 0xBADDC0DEDEADBEADULL) {
		return size; // return the size of this well formed free block.
	} else {
		return 0; // Broken invariant: If ptr is on a free list, then ptr->double_free_guard is the canary. (likely use after free)
	}
}



static MALLOC_INLINE size_t
_nano_good_size(nanozone_t *nanozone, size_t size)
{
	return (size <= NANO_REGIME_QUANTA_SIZE) ? NANO_REGIME_QUANTA_SIZE
	: (((size + NANO_REGIME_QUANTA_SIZE - 1) >> SHIFT_NANO_QUANTUM) << SHIFT_NANO_QUANTUM);
}


static size_t
nano_size(nanozone_t *nanozone, const void *ptr)
{
	nano_blk_addr_t p; // happily, the compiler holds this in a register
	
	p.addr = (uint64_t)ptr; // place ptr on the dissecting table
	if (nanozone->our_signature == p.fields.nano_signature) { // Our signature?
		return _nano_size(nanozone, ptr);
	} else {
		malloc_zone_t *zone = (malloc_zone_t *)(nanozone->helper_zone);
		return zone->size(zone, ptr); // Not nano. Try other sizes.
	}
	/* NOTREACHED */
}

