//
//  nano_util.c
//  libsystem_malloc
//
//  Created by 王举范 on 2018/12/17.
//

#include "nano_util.h"

static inline unsigned long
divrem(unsigned long a, unsigned int b, unsigned int *remainder)
{
	// Encapsulating the modulo and division in an in-lined function convinces the compiler
	// to issue just a single divide instruction to obtain quotient and remainder. Go figure.
	*remainder = a % b;
	return a / b;
}


static MALLOC_INLINE index_t
offset_to_index(nanozone_t *nanozone, nano_meta_admin_t pMeta, uintptr_t offset)
{
	unsigned int slot_bytes = pMeta->slot_bytes;
	unsigned int slot_objects = pMeta->slot_objects; // SLOT_IN_BAND_SIZE / slot_bytes;
	unsigned int rem;
	unsigned long quo = divrem(offset, BAND_SIZE, &rem);
	
	assert(0 == rem % slot_bytes || pMeta->slot_exhausted);
	return (index_t)((quo * slot_objects) + (rem / slot_bytes));
}

static MALLOC_INLINE uintptr_t
index_to_offset(nanozone_t *nanozone, nano_meta_admin_t pMeta, index_t i)
{
	unsigned int slot_bytes = pMeta->slot_bytes;
	unsigned int slot_objects = pMeta->slot_objects; // SLOT_IN_BAND_SIZE / slot_bytes;
	unsigned int rem;
	unsigned long quo = divrem(i, slot_objects, &rem);
	
	return (quo * BAND_SIZE) + (rem * slot_bytes);
}


static void *
nano_memalign(nanozone_t *nanozone, size_t alignment, size_t size)
{
	malloc_zone_t *zone = (malloc_zone_t *)(nanozone->helper_zone);
	return zone->memalign(zone, alignment, size);
}

