
#include "internal.h"

boolean_t malloc_engaged_nano(void);

/*********	Utilities	************/
static bool _malloc_entropy_initialized;

void __malloc_init(const char *apple[]);

static int
__entropy_from_kernel(const char *str)
{
	unsigned long long val;
	char tmp[20], *p;
	int idx = 0;

	/* Skip over key to the first value */
	str = strchr(str, '=');
	if (str == NULL) {
		return 0;
	}
	str++;

	while (str && idx < sizeof(malloc_entropy) / sizeof(malloc_entropy[0])) {
		strlcpy(tmp, str, 20);
		p = strchr(tmp, ',');
		if (p) {
			*p = '\0';
		}
		val = strtoull_l(tmp, NULL, 0, NULL);
		malloc_entropy[idx] = (uint64_t)val;
		idx++;
		if ((str = strchr(str, ',')) != NULL) {
			str++;
		}
	}
	return idx;
}

/* TODO: Investigate adding _malloc_initialize() into this libSystem initializer */
void
__malloc_init(const char *apple[])
{
#if CONFIG_NANOZONE
	// TODO: envp should be passed down from Libsystem
	const char **envp = (const char **)*_NSGetEnviron();
	nano_init(envp, apple);
#endif

	const char **p;
	for (p = apple; p && *p; p++) {
		if (strstr(*p, "malloc_entropy") == *p) {
			int count = __entropy_from_kernel(*p);
			bzero((void *)*p, strlen(*p));

			if (sizeof(malloc_entropy) / sizeof(malloc_entropy[0]) == count) {
				_malloc_entropy_initialized = true;
			}
			break;
		}
	}
	if (!_malloc_entropy_initialized) {
		getentropy((void*)malloc_entropy, sizeof(malloc_entropy));
		_malloc_entropy_initialized = true;
	}

	mvm_aslr_init();
}

static malloc_zone_t* lite_zone = NULL;

MALLOC_ALWAYS_INLINE
static inline malloc_zone_t *
runtime_default_zone() {
	return (lite_zone) ? lite_zone : inline_malloc_default_zone();
}

static size_t
default_zone_size(malloc_zone_t *zone, const void *ptr)
{
	zone = runtime_default_zone();
	
	return zone->size(zone, ptr);
}

static void *
default_zone_malloc(malloc_zone_t *zone, size_t size)
{
	zone = runtime_default_zone();
	
	return zone->malloc(zone, size);
}

static void *
default_zone_calloc(malloc_zone_t *zone, size_t num_items, size_t size)
{
	zone = runtime_default_zone();
	
	return zone->calloc(zone, num_items, size);
}

static void *
default_zone_valloc(malloc_zone_t *zone, size_t size)
{
	zone = runtime_default_zone();
	
	return zone->valloc(zone, size);
}

static void
default_zone_free(malloc_zone_t *zone, void *ptr)
{
	zone = runtime_default_zone();
	
	return zone->free(zone, ptr);
}

static void *
default_zone_realloc(malloc_zone_t *zone, void *ptr, size_t new_size)
{
	zone = runtime_default_zone();
	
	return zone->realloc(zone, ptr, new_size);
}

static void
default_zone_destroy(malloc_zone_t *zone)
{
	zone = runtime_default_zone();
	
	return zone->destroy(zone);
}

static unsigned
default_zone_batch_malloc(malloc_zone_t *zone, size_t size, void **results, unsigned count)
{
	zone = runtime_default_zone();
	
	return zone->batch_malloc(zone, size, results, count);
}

static void
default_zone_batch_free(malloc_zone_t *zone, void **to_be_freed, unsigned count)
{
	zone = runtime_default_zone();
	
	return zone->batch_free(zone, to_be_freed, count);
}

static void *
default_zone_memalign(malloc_zone_t *zone, size_t alignment, size_t size)
{
	zone = runtime_default_zone();
	
	return zone->memalign(zone, alignment, size);
}

static void
default_zone_free_definite_size(malloc_zone_t *zone, void *ptr, size_t size)
{
	zone = runtime_default_zone();
	
	return zone->free_definite_size(zone, ptr, size);
}

static size_t
default_zone_pressure_relief(malloc_zone_t *zone, size_t goal)
{
	zone = runtime_default_zone();
	
	return zone->pressure_relief(zone, goal);
}

static kern_return_t
default_zone_ptr_in_use_enumerator(task_t task,
								   void *context,
								   unsigned type_mask,
								   vm_address_t zone_address,
								   memory_reader_t reader,
								   vm_range_recorder_t recorder)
{
	malloc_zone_t *zone = runtime_default_zone();
	
	return zone->introspect->enumerator(task, context, type_mask, (vm_address_t) zone, reader, recorder);
}

static size_t
default_zone_good_size(malloc_zone_t *zone, size_t size)
{
	zone = runtime_default_zone();
	
	return zone->introspect->good_size(zone, size);
}

static boolean_t
default_zone_check(malloc_zone_t *zone)
{
	zone = runtime_default_zone();
	
	return zone->introspect->check(zone);
}

static void
default_zone_print(malloc_zone_t *zone, boolean_t verbose)
{
	zone = runtime_default_zone();
	
	return (void)zone->introspect->check(zone);
}

static void
default_zone_log(malloc_zone_t *zone, void *log_address)
{
	zone = runtime_default_zone();
	
	return zone->introspect->log(zone, log_address);
}

static void
default_zone_force_lock(malloc_zone_t *zone)
{
	zone = runtime_default_zone();
	
	return zone->introspect->force_lock(zone);
}

static void
default_zone_force_unlock(malloc_zone_t *zone)
{
	zone = runtime_default_zone();
	
	return zone->introspect->force_unlock(zone);
}

static void
default_zone_statistics(malloc_zone_t *zone, malloc_statistics_t *stats)
{
	zone = runtime_default_zone();
	
	return zone->introspect->statistics(zone, stats);
}

static boolean_t
default_zone_locked(malloc_zone_t *zone)
{
	zone = runtime_default_zone();
	
	return zone->introspect->zone_locked(zone);
}

static void
default_zone_reinit_lock(malloc_zone_t *zone)
{
	zone = runtime_default_zone();
	
	return zone->introspect->reinit_lock(zone);
}

static struct malloc_introspection_t default_zone_introspect = {
	default_zone_ptr_in_use_enumerator,
	default_zone_good_size,
	default_zone_check,
	default_zone_print,
	default_zone_log,
	default_zone_force_lock,
	default_zone_force_unlock,
	default_zone_statistics,
	default_zone_locked,
	NULL,
	NULL,
	NULL,
	NULL,
	default_zone_reinit_lock
};

typedef struct {
	malloc_zone_t malloc_zone;
	uint8_t pad[PAGE_MAX_SIZE - sizeof(malloc_zone_t)];
} virtual_default_zone_t;

static virtual_default_zone_t virtual_default_zone
__attribute__((section("__DATA,__v_zone")))
__attribute__((aligned(PAGE_MAX_SIZE))) = {
	NULL,
	NULL,
	default_zone_size,
	default_zone_malloc,
	default_zone_calloc,
	default_zone_valloc,
	default_zone_free,
	default_zone_realloc,
	default_zone_destroy,
	DEFAULT_MALLOC_ZONE_STRING,
	default_zone_batch_malloc,
	default_zone_batch_free,
	&default_zone_introspect,
	9,
	default_zone_memalign,
	default_zone_free_definite_size,
	default_zone_pressure_relief
};

static malloc_zone_t *default_zone = &virtual_default_zone.malloc_zone;

static boolean_t
has_default_zone0(void)
{
	if (!malloc_zones) {
		return false;
	}
	
	return initial_default_zone == malloc_zones[0];
}

static inline malloc_zone_t *find_registered_zone(const void *, size_t *) __attribute__((always_inline));
static inline malloc_zone_t *
find_registered_zone(const void *ptr, size_t *returned_size)
{
	// Returns a zone which contains ptr, else NULL

	if (0 == malloc_num_zones) {
		if (returned_size) {
			*returned_size = 0;
		}
		return NULL;
	}

	// first look in the lite zone
	if (lite_zone) {
		malloc_zone_t *zone = lite_zone;
		size_t size = zone->size(zone, ptr);
		if (size) { // Claimed by this zone?
			if (returned_size) {
				*returned_size = size;
			}
			// Return the virtual default zone instead of the lite zone - see <rdar://problem/24994311>
			return default_zone;
		}
	}
	
	// The default zone is registered in malloc_zones[0]. There's no danger that it will ever be unregistered.
	// So don't advance the FRZ counter yet.
	malloc_zone_t *zone = malloc_zones[0];
	size_t size = zone->size(zone, ptr);
	if (size) { // Claimed by this zone?
		if (returned_size) {
			*returned_size = size;
		}

		// Asan and others replace the zone at position 0 with their own zone.
		// In that case just return that zone as they need this information.
		// Otherwise return the virtual default zone, not the actual zone in position 0.
		if (!has_default_zone0()) {
			return zone;
		} else {
			return default_zone;
		}
	}

	int32_t volatile *pFRZCounter = pFRZCounterLive;   // Capture pointer to the counter of the moment
	OSAtomicIncrement32Barrier(pFRZCounter); // Advance this counter -- our thread is in FRZ

	unsigned index;
	int32_t limit = *(int32_t volatile *)&malloc_num_zones;
	malloc_zone_t **zones = &malloc_zones[1];

	// From this point on, FRZ is accessing the malloc_zones[] array without locking
	// in order to avoid contention on common operations (such as non-default-zone free()).
	// In order to ensure that this is actually safe to do, register/unregister take care
	// to:
	//
	//   1. Register ensures that newly inserted pointers in malloc_zones[] are visible
	//      when malloc_num_zones is incremented. At the moment, we're relying on that store
	//      ordering to work without taking additional steps here to ensure load memory
	//      ordering.
	//
	//   2. Unregister waits for all readers in FRZ to complete their iteration before it
	//      returns from the unregister call (during which, even unregistered zone pointers
	//      are still valid). It also ensures that all the pointers in the zones array are
	//      valid until it returns, so that a stale value in limit is not dangerous.

	for (index = 1; index < limit; ++index, ++zones) {
		zone = *zones;
		size = zone->size(zone, ptr);
		if (size) { // Claimed by this zone?
			goto out;
		}
	}
	// Unclaimed by any zone.
	zone = NULL;
	size = 0;
out:
	if (returned_size) {
		*returned_size = size;
	}
	OSAtomicDecrement32Barrier(pFRZCounter); // our thread is leaving FRZ
	return zone;
}

void
malloc_error_break(void)
{
	// Provides a non-inlined place for various malloc error procedures to call
	// that will be called after an error message appears.  It does not make
	// sense for developers to call this function, so it is marked
	// hidden to prevent it from becoming API.
	MAGMALLOC_MALLOCERRORBREAK(); // DTrace USDT probe
}

int
malloc_gdb_po_unsafe(void)
{
	// In order to implement "po" other data formatters in gdb, the debugger
	// calls functions that call malloc.  The debugger will  only run one thread
	// of the program in this case, so if another thread is holding a zone lock,
	// gdb may deadlock in this case.
	//
	// Iterate over the zones in malloc_zones, and call "trylock" on the zone
	// lock.  If trylock succeeds, unlock it, otherwise return "locked".  Returns
	// 0 == safe, 1 == locked/unsafe.

	if (__stack_logging_locked()) {
		return 1;
	}

	malloc_zone_t **zones = malloc_zones;
	unsigned i, e = malloc_num_zones;

	for (i = 0; i != e; ++i) {
		malloc_zone_t *zone = zones[i];

		// Version must be >= 5 to look at the new introspection field.
		if (zone->version < 5) {
			continue;
		}

		if (zone->introspect->zone_locked && zone->introspect->zone_locked(zone)) {
			return 1;
		}
	}
	return 0;
}


