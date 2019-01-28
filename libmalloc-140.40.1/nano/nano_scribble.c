//
//  nano_scribble.c
//  libsystem_malloc
//
//  Created by 王举范 on 2018/12/17.
//

#include "nano_scribble.h"


static void *
nano_malloc_scribble(nanozone_t *nanozone, size_t size)
{
	if (size <= NANO_MAX_SIZE) {
		void *ptr = _nano_malloc_check_clear(nanozone, size, 0);
		if (ptr) {
			/*
			 * Scribble on allocated memory.
			 */
			if (size) {
				memset(ptr, SCRIBBLE_BYTE, _nano_vet_and_size_of_live(nanozone, ptr));
			}
			
			return ptr;
		} else {
			/* FALLTHROUGH to helper zone */
		}
	}
	malloc_zone_t *zone = (malloc_zone_t *)(nanozone->helper_zone);
	return zone->malloc(zone, size);
}


static void
nano_free_scribble(nanozone_t *nanozone, void *ptr)
{
	__nano_free(nanozone, ptr, 1);
}


static void
nano_free_definite_size_scribble(nanozone_t *nanozone, void *ptr, size_t size)
{
	__nano_free_definite_size(nanozone, ptr, size, 1);
}


static MALLOC_INLINE void _nano_free_trusted_size_check_scribble(nanozone_t *nanozone,
																 void *ptr,
																 size_t trusted_size,
																 boolean_t do_scribble) MALLOC_ALWAYS_INLINE;

static MALLOC_INLINE void
_nano_free_trusted_size_check_scribble(nanozone_t *nanozone, void *ptr, size_t trusted_size, boolean_t do_scribble)
{
	if (trusted_size) {
		nano_blk_addr_t p; // happily, the compiler holds this in a register
		nano_meta_admin_t pMeta;
		
		if (do_scribble) {
			(void)memset(ptr, SCRABBLE_BYTE, trusted_size);
		}
		((chained_block_t)ptr)->double_free_guard = (0xBADDC0DEDEADBEADULL ^ nanozone->cookie);
		
		p.addr = (uint64_t)ptr; // place ptr on the dissecting table
		pMeta = &(nanozone->meta_data[p.fields.nano_mag_index][p.fields.nano_slot]);
		OSAtomicEnqueue(&(pMeta->slot_LIFO), ptr, offsetof(struct chained_block_s, next));
	} else {
		nanozone_error(nanozone, 1, "Freeing unallocated pointer", ptr, NULL);
	}
}

static MALLOC_INLINE void _nano_free_check_scribble(nanozone_t *nanozone, void *ptr, boolean_t do_scribble) MALLOC_ALWAYS_INLINE;

static MALLOC_INLINE void
_nano_free_check_scribble(nanozone_t *nanozone, void *ptr, boolean_t do_scribble)
{
	_nano_free_trusted_size_check_scribble(nanozone, ptr, _nano_vet_and_size_of_live(nanozone, ptr), do_scribble);
}

static void *
_nano_malloc_check_scribble(nanozone_t *nanozone, size_t size)
{
	void *ptr = _nano_malloc_check_clear(nanozone, size, 0);
	
	/*
	 * Scribble on allocated memory when requested.
	 */
	if ((nanozone->debug_flags & MALLOC_DO_SCRIBBLE) && ptr && size) {
		memset(ptr, SCRIBBLE_BYTE, _nano_vet_and_size_of_live(nanozone, ptr));
	}
	
	return ptr;
}
