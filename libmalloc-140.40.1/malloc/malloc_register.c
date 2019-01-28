
#include "malloc_register.h"

/*********	Functions for zone implementors	************/

static void
malloc_zone_register_while_locked(malloc_zone_t *zone)
{
	size_t protect_size;
	unsigned i;
	
	/* scan the list of zones, to see if this zone is already registered.  If
	 * so, print an error message and return. */
	for (i = 0; i != malloc_num_zones; ++i) {
		if (zone == malloc_zones[i]) {
			_malloc_printf(ASL_LEVEL_ERR, "Attempted to register zone more than once: %p\n", zone);
			return;
		}
	}
	
	if (malloc_num_zones == malloc_num_zones_allocated) {
		size_t malloc_zones_size = malloc_num_zones * sizeof(malloc_zone_t *);
		mach_vm_size_t alloc_size = round_page(malloc_zones_size + vm_page_size);
		mach_vm_address_t vm_addr;
		int alloc_flags = VM_FLAGS_ANYWHERE | VM_MAKE_TAG(VM_MEMORY_MALLOC);
		
		//		kern_return_t mach_vm_allocate(
		//		vm_map_t target,   			//mach_port_t
		//		mach_vm_address_t *address,  	//64
		//		mach_vm_size_t size, 			//64
		//		int flags);
		
		//		kern_return_t   vm_allocate
		//		(vm_task_t                          target_task,
		//		 vm_address_t                           address,
		//		 vm_size_t                                 size,
		//		 boolean_t                             anywhere);
		//		PARAMETERS
		//		target_task
		//		[in task send right] The port for the task in whose address space the region is to be allocated.
		//		address
		//			[pointer to in/out scalar] The starting address for the region. If the region as specified by the given starting address and size would not lie within the task's un-allocated memory, the kernel does not allocate the region. If allocated, the kernel returns the starting address actually used for the allocated region.
		//		size
		//				[in scalar] The number of bytes to allocate.
		//		anywhere
		//				[in scalar] Placement indicator. The valid values are:
		//			TRUE
		//				The kernel allocates the region in the next unused space that is sufficient within the address space. The kernel returns the starting address actually used in address.
		//			FALSE
		//				The kernel allocates the region starting at address unless that space is already allocated.
		
		vm_addr = vm_page_size;
		kern_return_t kr = mach_vm_allocate(mach_task_self(),
											&vm_addr,
											alloc_size,
											alloc_flags);
		if (kr) {
			_malloc_printf(ASL_LEVEL_ERR,
						   "malloc_zone_register allocation failed: %d\n", kr);
			return;
		}
		
		malloc_zone_t **new_zones = (malloc_zone_t **)vm_addr;
		/* If there were previously allocated malloc zones, we need to copy them
		 * out of the previous array and into the new zones array */
		if (malloc_zones) {
			memcpy(new_zones, malloc_zones, malloc_zones_size);
			vm_addr = (mach_vm_address_t)malloc_zones;
			mach_vm_size_t dealloc_size = round_page(malloc_zones_size);
			mach_vm_deallocate(mach_task_self(), vm_addr, dealloc_size);
		}
		
		/* Update the malloc_zones pointer, which we leak if it was previously
		 * allocated, and the number of zones allocated */
		protect_size = (size_t)alloc_size;
		malloc_zones = new_zones;
		malloc_num_zones_allocated = (int32_t)(alloc_size / sizeof(malloc_zone_t *));
	} else {
		/* If we don't need to reallocate zones, we need to briefly change the
		 * page protection the malloc zones to allow writes */
		protect_size = malloc_num_zones_allocated * sizeof(malloc_zone_t *);
		mprotect(malloc_zones, protect_size, PROT_READ | PROT_WRITE);
	}
	
	/* <rdar://problem/12871662> This store-increment needs to be visible in the correct
	 * order to any threads in find_registered_zone, such that if the incremented value
	 * in malloc_num_zones is visible then the pointer write before it must also be visible.
	 *
	 * While we could be slightly more efficent here with atomic ops the cleanest way to
	 * ensure the proper store-release operation is performed is to use OSAtomic*Barrier
	 * to update malloc_num_zones.
	 */
	malloc_zones[malloc_num_zones] = zone;
	OSAtomicIncrement32Barrier(&malloc_num_zones);
	
	/* Finally, now that the zone is registered, disallow write access to the
	 * malloc_zones array */
	mprotect(malloc_zones, protect_size, PROT_READ);
	//_malloc_printf(ASL_LEVEL_INFO, "Registered malloc_zone %p in malloc_zones %p [%u zones, %u bytes]\n", zone, malloc_zones,
	// malloc_num_zones, protect_size);
}
void malloc_zone_register(malloc_zone_t *zone)
{
	MALLOC_LOCK();
	malloc_zone_register_while_locked(zone);
	MALLOC_UNLOCK();
}

void malloc_zone_unregister(malloc_zone_t *z)
{
	unsigned index;
	
	if (malloc_num_zones == 0) {
		return;
	}
	
	MALLOC_LOCK();
	for (index = 0; index < malloc_num_zones; ++index) {
		if (z != malloc_zones[index]) {
			continue;
		}
		
		// Modify the page to be allow write access, so that we can update the
		// malloc_zones array.
		size_t protect_size = malloc_num_zones_allocated * sizeof(malloc_zone_t *);
		mprotect(malloc_zones, protect_size, PROT_READ | PROT_WRITE);
		
		// If we found a match, replace it with the entry at the end of the list, shrink the list,
		// and leave the end of the list intact to avoid racing with find_registered_zone().
		
		malloc_zones[index] = malloc_zones[malloc_num_zones - 1];
		--malloc_num_zones;
		
		mprotect(malloc_zones, protect_size, PROT_READ);
		
		// Exchange the roles of the FRZ counters. The counter that has captured the number of threads presently
		// executing *inside* find_regiatered_zone is swapped with the counter drained to zero last time through.
		// The former is then allowed to drain to zero while this thread yields.
		int32_t volatile *p = pFRZCounterLive;
		pFRZCounterLive = pFRZCounterDrain;
		pFRZCounterDrain = p;
		OSMemoryBarrier(); // Full memory barrier
		
		while (0 != *pFRZCounterDrain) {
			yield();
		}
		
		MALLOC_UNLOCK();
		
		return;
	}
	MALLOC_UNLOCK();
	malloc_printf("*** malloc_zone_unregister() failed for %p\n", z);
}

void
malloc_set_zone_name(malloc_zone_t *z, const char *name)
{
	char *newName;
	
	mprotect(z, sizeof(malloc_zone_t), PROT_READ | PROT_WRITE);
	if (z->zone_name) {
		free((char *)z->zone_name);
		z->zone_name = NULL;
	}
	if (name) {
		size_t buflen = strlen(name) + 1; //how to decide the end of chars
		newName = malloc_zone_malloc(z, buflen);
		if (newName) {
			strlcpy(newName, name, buflen);// copy the end flag too
			z->zone_name = (const char *)newName;
		} else {
			z->zone_name = NULL;
		}
	}
	mprotect(z, sizeof(malloc_zone_t), PROT_READ);
}
const char *
malloc_get_zone_name(malloc_zone_t *zone)
{
	return zone->zone_name;
}
