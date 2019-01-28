
#include "nano_allocate.h"

static void *
allocate_based_pages(nanozone_t *nanozone,
					 size_t size,
					 unsigned char align,
					 unsigned debug_flags,
					 int vm_page_label,
					 void *base_addr)
{
	boolean_t add_guard_pages = debug_flags & MALLOC_ADD_GUARD_PAGES;
	mach_vm_address_t vm_addr;
	uintptr_t addr;
	
	mach_vm_size_t allocation_size = round_page(size);
	mach_vm_offset_t allocation_mask = ((mach_vm_offset_t)1 << align) - 1;
	int alloc_flags = VM_FLAGS_ANYWHERE | VM_MAKE_TAG(vm_page_label);
	kern_return_t kr;
	
	if (!allocation_size) {
		allocation_size = vm_page_size;
	}
	if (add_guard_pages) {
		allocation_size += 2 * vm_page_size;
	}
	if (allocation_size < size) { // size_t arithmetic wrapped!
		return NULL;
	}
	
	vm_addr = round_page((mach_vm_address_t)base_addr);
	if (!vm_addr) {
		vm_addr = vm_page_size;
	}
	kr = mach_vm_map(mach_task_self(),
					 &vm_addr,
					 allocation_size,
					 allocation_mask,
					 alloc_flags,
					 MEMORY_OBJECT_NULL,
					 0, FALSE,
					 VM_PROT_DEFAULT,
					 VM_PROT_ALL,
					 VM_INHERIT_DEFAULT);
	if (kr) {
		nanozone_error(nanozone, 0, "can't allocate pages", NULL, "*** mach_vm_map(size=%lu) failed (error code=%d)\n", size, kr);
		return NULL;
	}
	addr = (uintptr_t)vm_addr;
	
	if (add_guard_pages) {
		addr += vm_page_size;
		mvm_protect((void *)addr, size, PROT_NONE, debug_flags);
	}
	return (void *)addr;
}

static void *
nano_allocate_pages(nanozone_t *nanozone, size_t size, unsigned char align, unsigned debug_flags, int vm_page_label)
{
	return allocate_based_pages(nanozone, size,
								align, debug_flags,
								vm_page_label, 0);
}

static void
nano_deallocate_pages(nanozone_t *nanozone, void *addr, size_t size, unsigned debug_flags)
{
	boolean_t add_guard_pages = debug_flags & MALLOC_ADD_GUARD_PAGES;
	mach_vm_address_t vm_addr = (mach_vm_address_t)addr;
	mach_vm_size_t allocation_size = size;
	kern_return_t kr;
	
	if (add_guard_pages) {
		vm_addr -= vm_page_size;
		allocation_size += 2 * vm_page_size;
	}
	kr = mach_vm_deallocate(mach_task_self(), vm_addr, allocation_size);
	if (kr && nanozone) {
		nanozone_error(nanozone, 0, "Can't deallocate_pages at", addr, NULL);
	}
}

//////////////////////////////////////////////////////////

#if NANO_PREALLOCATE_BAND_VM
static boolean_t
nano_preallocate_band_vm(void)
{
	nano_blk_addr_t u;
	uintptr_t s, e;
	
	u.fields.nano_signature = NANOZONE_SIGNATURE;
	u.fields.nano_mag_index = 0;
	u.fields.nano_band = 0;
	u.fields.nano_slot = 0;
	u.fields.nano_offset = 0;
	
	s = u.addr; // start of first possible band
	
	u.fields.nano_mag_index = (1 << NANO_MAG_BITS) - 1;//6
	u.fields.nano_band = (1 << NANO_BAND_BITS) - 1;
	
	e = u.addr + BAND_SIZE; // end of last possible band
	
	mach_vm_address_t vm_addr = s;
	mach_vm_size_t vm_size = (e - s);
	
	kern_return_t kr = mach_vm_map(mach_task_self(),
								   &vm_addr, vm_size, 0,
								   VM_MAKE_TAG(VM_MEMORY_MALLOC_NANO),
								   MEMORY_OBJECT_NULL, 0, FALSE,
								   VM_PROT_DEFAULT,
								   VM_PROT_ALL,
								   VM_INHERIT_DEFAULT);
	
	void *q = (void *)vm_addr;
	if (kr || q != (void*)s) { // Must get exactly what we asked for
		if (!kr) {
			mach_vm_deallocate(mach_task_self(), vm_addr, vm_size);
		}
		return FALSE;
	}
	return TRUE;
}
#endif
