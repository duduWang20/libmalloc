/*
 * Copyright (c) 2015 Apple Inc. All rights reserved.
 *
 * @APPLE_LICENSE_HEADER_START@
 *
 * This file contains Original Code and/or Modifications of Original Code
 * as defined in and that are subject to the Apple Public Source License
 * Version 2.0 (the 'License'). You may not use this file except in
 * compliance with the License. Please obtain a copy of the License at
 * http://www.opensource.apple.com/apsl/ and read it before using this
 * file.
 *
 * The Original Code and all software distributed under the License are
 * distributed on an 'AS IS' basis, WITHOUT WARRANTY OF ANY KIND, EITHER
 * EXPRESS OR IMPLIED, AND APPLE HEREBY DISCLAIMS ALL SUCH WARRANTIES,
 * INCLUDING WITHOUT LIMITATION, ANY WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE, QUIET ENJOYMENT OR NON-INFRINGEMENT.
 * Please see the License for the specific language governing rights and
 * limitations under the License.
 *
 * @APPLE_LICENSE_HEADER_END@
 */

#include "internal.h"

/*
 * For use by CheckFix: create a new zone whose behavior is, apart from
 * the use of death-row and per-CPU magazines, that of Leopard.
 */
static MALLOC_NOINLINE void *
legacy_valloc(szone_t *szone, size_t size)
{
	void *ptr;
	size_t num_kernel_pages;

	num_kernel_pages = round_page_quanta(size) >> vm_page_quanta_shift;
	ptr = large_malloc(szone, num_kernel_pages, 0, TRUE);
#if DEBUG_MALLOC
	if (LOG(szone, ptr)) {
		malloc_printf("legacy_valloc returned %p\n", ptr);
	}
#endif
	return ptr;
}

malloc_zone_t *
create_legacy_scalable_zone(size_t initial_size, unsigned debug_flags)
{
	// legacy always uses 32 small slots
	malloc_zone_t *mzone = create_scalable_zone(initial_size, debug_flags & ~MALLOC_EXTENDED_SMALL_SLOTS);
	szone_t *szone = (szone_t *)mzone;

	if (!szone) {
		return NULL;
	}

	szone->is_largemem = 0;
	szone->large_threshold = LARGE_THRESHOLD;
	szone->vm_copy_threshold = VM_COPY_THRESHOLD;

	mprotect(szone, sizeof(szone->basic_zone), PROT_READ | PROT_WRITE);
	szone->basic_zone.valloc = (void *)legacy_valloc;
	szone->basic_zone.free_definite_size = NULL;
	mprotect(szone, sizeof(szone->basic_zone), PROT_READ);

	return mzone;
}


/* in malloc.c by wjf
 * For use by CheckFix: establish a new default zone whose behavior is, apart from
 * the use of death-row and per-CPU magazines, that of Leopard.
 */
void
malloc_create_legacy_default_zone(void)
{
	malloc_zone_t *zone;
	int i;
	
	_malloc_initialize_once();
	zone = create_legacy_scalable_zone(0, malloc_debug_flags);
	
	MALLOC_LOCK();
	malloc_zone_register_while_locked(zone);
	
	//
	// Establish the legacy scalable zone just created as the default zone.
	//
	malloc_zone_t *hold = malloc_zones[0];
	if (hold->zone_name && strcmp(hold->zone_name, DEFAULT_MALLOC_ZONE_STRING) == 0) {
		malloc_set_zone_name(hold, NULL);
	}
	malloc_set_zone_name(zone, DEFAULT_MALLOC_ZONE_STRING);
	
	unsigned protect_size = malloc_num_zones_allocated * sizeof(malloc_zone_t *);
	mprotect(malloc_zones, protect_size, PROT_READ | PROT_WRITE);
	
	// assert(zone == malloc_zones[malloc_num_zones - 1];
	for (i = malloc_num_zones - 1; i > 0; --i) {
		malloc_zones[i] = malloc_zones[i - 1];
	}
	malloc_zones[0] = zone;
	
	mprotect(malloc_zones, protect_size, PROT_READ);
	MALLOC_UNLOCK();
}
