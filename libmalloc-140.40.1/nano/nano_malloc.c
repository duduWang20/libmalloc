#include "internal.h"

/* nano_malloc for 64bit ABI */
#if CONFIG_NANOZONE

/******************           nanozone methods           **********************/
/*
 * These methods are called with "ptr" known to possess the nano signature (from
 * which we can additionally infer "ptr" is not NULL), and with "size" bounded to
 * the extent of the nano allocation regime -- (0, 256].
 */

static MALLOC_INLINE void *
_nano_realloc(nanozone_t *nanozone, void *ptr, size_t new_size)
{
	size_t old_size, new_good_size, valid_size;
	void *new_ptr;

	if (FALSE && NULL == ptr) { // ptr has our_signature so can't be NULL, but if it were Posix sez ...
		// If ptr is a null pointer, realloc() shall be equivalent to malloc() for the specified size.
		return _nano_malloc_check_scribble(nanozone, new_size);
	} else if (0 == new_size) {
		// If size is 0 and ptr is not a null pointer, the object pointed to is freed.
		_nano_free_check_scribble(nanozone, ptr,
								  (nanozone->debug_flags & MALLOC_DO_SCRIBBLE));
		// If size is 0, either a null pointer or a unique pointer that can be successfully passed
		// to free() shall be returned.
		return _nano_malloc_check_scribble(nanozone, 1);
	}

	old_size = _nano_vet_and_size_of_live(nanozone, ptr);
	if (!old_size) {
		nanozone_error(nanozone, 1, "pointer being reallocated was not allocated", ptr, NULL);
		return NULL;
	}

	new_good_size = _nano_good_size(nanozone, new_size);
	if (new_good_size > old_size) {
		/* Must grow. FALL THROUGH to alloc/copy/free. */
	} else if (new_good_size <= (old_size >> 1)) {
		/* Serious shrinkage (more than half). FALL THROUGH to alloc/copy/free. */
	} else {
		/* Let's hang on to what we got. */
		if (nanozone->debug_flags & MALLOC_DO_SCRIBBLE) {
			memset(ptr + new_size, SCRIBBLE_BYTE, old_size - new_size);
		}
		return ptr;
	}

	/*
	 * Allocate a new buffer and copy.
	 */
	new_ptr = _nano_malloc_check_scribble(nanozone, new_good_size);
	if (new_ptr == NULL) {
		return NULL;
	}

	valid_size = MIN(old_size, new_good_size);
	memcpy(new_ptr, ptr, valid_size);
	_nano_free_check_scribble(nanozone, ptr, (nanozone->debug_flags & MALLOC_DO_SCRIBBLE));

	return new_ptr;
}

static MALLOC_INLINE void
_nano_destroy(nanozone_t *nanozone)
{
	/* Now destroy the separate nanozone region */
	nano_deallocate_pages(nanozone, (void *)nanozone, NANOZONE_PAGED_SIZE, 0);
}

/******************           nanozone dispatch          **********************/

static void *
nano_malloc(nanozone_t *nanozone, size_t size)
{
	if (size <= NANO_MAX_SIZE) {//256
		void *p = _nano_malloc_check_clear(nanozone, size, 0);
		if (p) {
			return p;
		} else {
			/* FALLTHROUGH to helper zone */
		}
	}

	malloc_zone_t *zone = (malloc_zone_t *)(nanozone->helper_zone);
	return zone->malloc(zone, size);
}

static void *
nano_forked_malloc(nanozone_t *nanozone, size_t size)
{
	malloc_zone_t *zone = (malloc_zone_t *)(nanozone->helper_zone);
	return zone->malloc(zone, size);
}


static void *
nano_calloc(nanozone_t *nanozone, size_t num_items, size_t size)
{
	size_t total_bytes = num_items * size;

	// Check for overflow of integer multiplication
	if (num_items > 1) {
		/* size_t is uint64_t */
		if ((num_items | size) & 0xffffffff00000000ul) {
			// num_items or size equals or exceeds sqrt(2^64) == 2^32, appeal to wider arithmetic
			__uint128_t product = ((__uint128_t)num_items) * ((__uint128_t)size);
			if ((uint64_t)(product >> 64)) { // compiles to test on upper register of register pair
				return NULL;
			}
		}
	}

	if (total_bytes <= NANO_MAX_SIZE) {
		void *p = _nano_malloc_check_clear(nanozone, total_bytes, 1);
		if (p) {
			return p;
		} else {
			/* FALLTHROUGH to helper zone */
		}
	}
	malloc_zone_t *zone = (malloc_zone_t *)(nanozone->helper_zone);
	return zone->calloc(zone, 1, total_bytes);
}

static void *
nano_forked_calloc(nanozone_t *nanozone, size_t num_items, size_t size)
{
	malloc_zone_t *zone = (malloc_zone_t *)(nanozone->helper_zone);
	return zone->calloc(zone, num_items, size);
}

static void *
nano_valloc(nanozone_t *nanozone, size_t size)
{
	malloc_zone_t *zone = (malloc_zone_t *)(nanozone->helper_zone);
	return zone->valloc(zone, size);
}

static MALLOC_INLINE void
__nano_free_definite_size(nanozone_t *nanozone,
						  void *ptr,
						  size_t size,
						  boolean_t do_scribble) MALLOC_ALWAYS_INLINE;

static MALLOC_INLINE void
__nano_free_definite_size(nanozone_t *nanozone, void *ptr, size_t size, boolean_t do_scribble)
{
	nano_blk_addr_t p; // happily, the compiler holds this in a register

	p.addr = (uint64_t)ptr; // place ptr on the dissecting table
	if (nanozone->our_signature == p.fields.nano_signature) {
		if (size == ((p.fields.nano_slot + 1) << SHIFT_NANO_QUANTUM)) { // "Trust but verify."
			_nano_free_trusted_size_check_scribble(nanozone, ptr, size, do_scribble);
			return;
		} else {
			nanozone_error(nanozone, 1, "Freeing pointer whose size was misdeclared", ptr, NULL);
		}
	} else {
		malloc_zone_t *zone = (malloc_zone_t *)(nanozone->helper_zone);
		zone->free_definite_size(zone, ptr, size);
		return;
	}
	/* NOTREACHED */
}

static void
nano_free_definite_size(nanozone_t *nanozone, void *ptr, size_t size)
{
	__nano_free_definite_size(nanozone, ptr, size, 0);
}

static MALLOC_INLINE void __nano_free(nanozone_t *nanozone, void *ptr, boolean_t do_scribble) MALLOC_ALWAYS_INLINE;

static MALLOC_INLINE void
__nano_free(nanozone_t *nanozone, void *ptr, boolean_t do_scribble)
{
	MALLOC_TRACE(TRACE_nano_free, (uintptr_t)nanozone, (uintptr_t)ptr, do_scribble, 0);

	if (!ptr) {
		return; // Protect against malloc_zone_free() passing NULL.
	}

	// <rdar://problem/26481467> exhausting a slot may result in a pointer with
	// the nanozone prefix being given to nano_free via malloc_zone_free. Calling
	// vet_and_size here, instead of in _nano_free_check_scribble means we can
	// early-out into the helper_zone if it turns out nano does not own this ptr.
	size_t sz = _nano_vet_and_size_of_live(nanozone, ptr);

	if (sz) {
		_nano_free_trusted_size_check_scribble(nanozone, ptr, sz, do_scribble);
		return;
	} else {
		malloc_zone_t *zone = (malloc_zone_t *)(nanozone->helper_zone);
		zone->free(zone, ptr);
		return;
	}
	/* NOTREACHED */
}

static void
nano_free(nanozone_t *nanozone, void *ptr)
{
	__nano_free(nanozone, ptr, 0);
}

static void
nano_forked_free(nanozone_t *nanozone, void *ptr) {
	if (!ptr) {
		return; // Protect against malloc_zone_free() passing NULL.
	}

	// <rdar://problem/26481467> exhausting a slot may result in a pointer with
	// the nanozone prefix being given to nano_free via malloc_zone_free. Calling
	// vet_and_size here, instead of in _nano_free_check_scribble means we can
	// early-out into the helper_zone if it turns out nano does not own this ptr.
	size_t sz = _nano_vet_and_size_of_live(nanozone, ptr);

	if (sz) {
		/* NOTHING. Drop it on the floor as nanozone metadata could be fouled by fork. */
		return;
	} else {
		malloc_zone_t *zone = (malloc_zone_t *)(nanozone->helper_zone);
		zone->free(zone, ptr);
		return;
	}
	/* NOTREACHED */
}

static void
nano_forked_free_definite_size(nanozone_t *nanozone, void *ptr, size_t size)
{
	nano_forked_free(nanozone, ptr);
}



static void *
nano_realloc(nanozone_t *nanozone, void *ptr, size_t new_size)
{
	// could occur through malloc_zone_realloc() path
	if (!ptr) {
		// If ptr is a null pointer, realloc() shall be equivalent to malloc() for the specified size.
		return nano_malloc(nanozone, new_size);
	}

	size_t old_size = _nano_vet_and_size_of_live(nanozone, ptr);
	if (!old_size) {
		// not-nano pointer, hand down to helper zone
		malloc_zone_t *zone = (malloc_zone_t *)(nanozone->helper_zone);
		return zone->realloc(zone, ptr, new_size);
	} else {
		if (new_size <= NANO_MAX_SIZE) {
			// nano to nano?
			void *q = _nano_realloc(nanozone, ptr, new_size);
			if (q) {
				return q;
			} else { 
				// nano exhausted
				/* FALLTHROUGH to helper zone copying case */
			}
		}

		malloc_zone_t *zone = (malloc_zone_t *)(nanozone->helper_zone);
		void *new_ptr = zone->malloc(zone, new_size);

		if (new_ptr) {
			size_t valid_size = MIN(old_size, new_size);
			memcpy(new_ptr, ptr, valid_size);
			_nano_free_check_scribble(nanozone, ptr, (nanozone->debug_flags & MALLOC_DO_SCRIBBLE));
			return new_ptr;
		} else {
			/* Original ptr is left intact */
			return NULL;
		}
		/* NOTREACHED */
	}
	/* NOTREACHED */
}

static void *
nano_forked_realloc(nanozone_t *nanozone, void *ptr, size_t new_size)
{
	// could occur through malloc_zone_realloc() path
	if (!ptr) {
		// If ptr is a null pointer, realloc() shall be equivalent to malloc() for the specified size.
		return nano_forked_malloc(nanozone, new_size);
	}

	size_t old_size = _nano_vet_and_size_of_live(nanozone, ptr);
	if (!old_size) {
		// not-nano pointer, hand down to helper zone
		malloc_zone_t *zone = (malloc_zone_t *)(nanozone->helper_zone);
		return zone->realloc(zone, ptr, new_size);
	} else {
		if (0 == new_size) {
			// If size is 0 and ptr is not a null pointer, the object pointed to is freed.
			// However as nanozone metadata could be fouled by fork, we'll intentionally leak it.

			// If size is 0, either a null pointer or a unique pointer that can be successfully passed
			// to free() shall be returned.
			return nano_forked_malloc(nanozone, 1);
		}

		malloc_zone_t *zone = (malloc_zone_t *)(nanozone->helper_zone);
		void *new_ptr = zone->malloc(zone, new_size);

		if (new_ptr) {
			size_t valid_size = MIN(old_size, new_size);
			memcpy(new_ptr, ptr, valid_size);
			/* Original pointer is intentionally leaked as nanozone metadata could be fouled by fork. */
			return new_ptr;
		} else {
			/* Original ptr is left intact */
			return NULL;
		}
		/* NOTREACHED */
	}
	/* NOTREACHED */
}

static void
nano_destroy(nanozone_t *nanozone)
{
	malloc_zone_t *zone = (malloc_zone_t *)(nanozone->helper_zone);
	zone->destroy(zone);

	_nano_destroy(nanozone);
}


