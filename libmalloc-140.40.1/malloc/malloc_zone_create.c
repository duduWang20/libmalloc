
#include "malloc_zone_create.h"

/*********	Creation and destruction	************/

static void set_flags_from_environment(void); // innner

// To be used in _malloc_initialize_once() only, call that function instead.
static void
_malloc_initialize(void *context __unused)
{
	MALLOC_LOCK();
	unsigned n;
	malloc_zone_t *zone;
	
	if (!_malloc_entropy_initialized) {
		// Lazy initialization may occur before __malloc_init (rdar://27075409)
		// TODO: make this a fatal error
		malloc_printf("*** malloc was initialized without entropy\n");
	}
	
	set_flags_from_environment(); // will only set flags up to two times
	n = malloc_num_zones;
	
#if CONFIG_NANOZONE
	malloc_zone_t *helper_zone = create_scalable_zone(0, malloc_debug_flags);
	zone = create_nano_zone(0, helper_zone, malloc_debug_flags);
	if (zone) {
		malloc_zone_register_while_locked(zone);
		malloc_zone_register_while_locked(helper_zone);
		
		// Must call malloc_set_zone_name() *after* helper and nano are hooked together.
		malloc_set_zone_name(zone, DEFAULT_MALLOC_ZONE_STRING);
		malloc_set_zone_name(helper_zone, MALLOC_HELPER_ZONE_STRING);
	} else {
		zone = helper_zone;
		malloc_zone_register_while_locked(zone);
		malloc_set_zone_name(zone, DEFAULT_MALLOC_ZONE_STRING);
	}
#else
	zone = create_scalable_zone(0, malloc_debug_flags);
	malloc_zone_register_while_locked(zone);
	malloc_set_zone_name(zone, DEFAULT_MALLOC_ZONE_STRING);
#endif
	
	initial_default_zone = zone;
	
	if (n != 0) { // make the default first, for efficiency
		unsigned protect_size = malloc_num_zones_allocated * sizeof(malloc_zone_t *);
		malloc_zone_t *hold = malloc_zones[0];
		if (hold->zone_name && strcmp(hold->zone_name, DEFAULT_MALLOC_ZONE_STRING) == 0) {
			malloc_set_zone_name(hold, NULL);
		}
		mprotect(malloc_zones, protect_size, PROT_READ | PROT_WRITE);
		malloc_zones[0] = malloc_zones[n];
		malloc_zones[n] = hold;
		mprotect(malloc_zones, protect_size, PROT_READ);
	}
	
	// Only setup stack logging hooks once lazy initialization is complete, the
	// malloc_zone calls above would otherwise initialize malloc stack logging,
	// which calls into malloc re-entrantly from Libc upcalls and so deadlocks
	// in the lazy initialization os_once(). rdar://13046853
	if (stack_logging_enable_logging) {
		switch (stack_logging_mode) {
			case stack_logging_mode_malloc:
				malloc_logger = __disk_stack_logging_log_stack;
				break;
			case stack_logging_mode_vm:
				__syscall_logger = __disk_stack_logging_log_stack;
				break;
			case stack_logging_mode_all:
				malloc_logger = __disk_stack_logging_log_stack;
				__syscall_logger = __disk_stack_logging_log_stack;
				break;
			case stack_logging_mode_lite:
				__syscall_logger = __disk_stack_logging_log_stack;
				create_and_insert_lite_zone_while_locked();
				enable_stack_logging_lite();
				break;
		}
	}
	// _malloc_printf(ASL_LEVEL_INFO, "%d registered zones\n", malloc_num_zones);
	// _malloc_printf(ASL_LEVEL_INFO, "malloc_zones is at %p; malloc_num_zones is at %p\n", (unsigned)&malloc_zones,
	// (unsigned)&malloc_num_zones);
	MALLOC_UNLOCK();
}
MALLOC_ALWAYS_INLINE
static inline void
_malloc_initialize_once(void)
{
	os_once(&_malloc_initialize_pred, NULL, _malloc_initialize);
}
static inline malloc_zone_t *
inline_malloc_default_zone(void)
{
	_malloc_initialize_once();
	// _malloc_printf(ASL_LEVEL_INFO, "In inline_malloc_default_zone with %d %d\n", malloc_num_zones, malloc_has_debug_zone);
	return malloc_zones[0];
}

malloc_zone_t *
malloc_default_zone(void)
{
	return default_zone;
}

static inline malloc_zone_t *inline_malloc_default_scalable_zone(void) __attribute__((always_inline));
static inline malloc_zone_t *
inline_malloc_default_scalable_zone(void)
{
	unsigned index;
	
	_malloc_initialize_once();
	// _malloc_printf(ASL_LEVEL_INFO, "In inline_malloc_default_scalable_zone with %d %d\n", malloc_num_zones,
	// malloc_has_debug_zone);
	
	MALLOC_LOCK();
#if CONFIG_NANOZONE
	for (index = 0; index < malloc_num_zones; ++index) {
		malloc_zone_t *z = malloc_zones[index];
		
		if (z->zone_name && strcmp(z->zone_name, MALLOC_HELPER_ZONE_STRING) == 0) {
			MALLOC_UNLOCK();
			return z;
		}
	}
#endif
	for (index = 0; index < malloc_num_zones; ++index) {
		malloc_zone_t *z = malloc_zones[index];
		
		if (z->zone_name && strcmp(z->zone_name, DEFAULT_MALLOC_ZONE_STRING) == 0) {
			MALLOC_UNLOCK();
			return z;
		}
	}
	MALLOC_UNLOCK();
	
	malloc_printf("*** malloc_default_scalable_zone() failed to find 'DefaultMallocZone'\n");
	return NULL; // FIXME: abort() instead?
}

static void *
legacy_zeroing_large_malloc(malloc_zone_t *zone, size_t size)
{
	if (size > LARGE_THRESHOLD) {			 // Leopard and earlier returned a ZFOD range, so ...
		return default_zone_calloc(zone, 1, size); // Clear to zero always, ham-handedly touching in each page
	} else {
		return default_zone_malloc(zone, size);
	}
}

static void *
legacy_zeroing_large_valloc(malloc_zone_t *zone, size_t size)
{
	void *p = default_zone_valloc(zone, size);
	
	// Leopard and earlier returned a ZFOD range, so ...
	memset(p, 0, size); // Clear to zero always, ham-handedly touching in each page
	return p;
}

void
zeroify_scalable_zone(malloc_zone_t *zone)
{
	// <rdar://problem/27190324> this checkfix should replace the default zone's
	// allocation routines with the zeroing versions. Instead of getting in hot
	// water with the wrong zone, ensure that we're mutating the zone we expect.
	//
	// Additionally, the default_zone is no longer PROT_READ, so the two mprotect
	// calls that were here are no longer needed.
	if (zone == default_zone) {
		zone->malloc = (void *)legacy_zeroing_large_malloc;
		zone->valloc = (void *)legacy_zeroing_large_valloc;
	}
}

/*
 * malloc_engaged_nano() is for the benefit of libdispatch, which calls here just once.
 */
boolean_t
malloc_engaged_nano(void)
{
#if CONFIG_NANOZONE
	return _malloc_engaged_nano;
#else
	return 0;
#endif
}


malloc_zone_t *
malloc_create_zone(vm_size_t start_size, unsigned flags)
{
	malloc_zone_t *zone;
	
	/* start_size doesn't actually appear to be used, but we test anyway. */
	if (start_size > MALLOC_ABSOLUTE_MAX_SIZE) {
		return NULL;
	}
	_malloc_initialize_once();
	zone = create_scalable_zone(start_size, flags | malloc_debug_flags);
	malloc_zone_register(zone);
	return zone;
}

void
malloc_destroy_zone(malloc_zone_t *zone)
{
	malloc_set_zone_name(zone, NULL); // Deallocate zone name wherever it may reside PR_7701095
	malloc_zone_unregister(zone);
	zone->destroy(zone);
}

////////////////////////////

static void
set_flags_from_environment(void)
{
	const char *flag;
	int fd;
	char **env = *_NSGetEnviron();
	char **p;
	char *c;
	bool restricted = 0;
	
	if (malloc_debug_file != STDERR_FILENO) {
		close(malloc_debug_file);
		malloc_debug_file = STDERR_FILENO;
	}
#if __LP64__
	malloc_debug_flags = MALLOC_ABORT_ON_CORRUPTION; // Set always on 64-bit processes
#else
	int libSystemVersion = NSVersionOfLinkTimeLibrary("System");
	if ((-1 != libSystemVersion) && ((libSystemVersion >> 16) < 126) /* Lion or greater */) {
		malloc_debug_flags = 0;
	} else {
		malloc_debug_flags = MALLOC_ABORT_ON_CORRUPTION;
	}
#endif
	stack_logging_enable_logging = 0;
	stack_logging_dontcompact = 0;
	malloc_logger = NULL;
	malloc_check_start = 0;
	malloc_check_each = 1000;
	malloc_check_abort = 0;
	malloc_check_sleep = 100;
	/*
	 * Given that all environment variables start with "Malloc" we optimize by scanning quickly
	 * first the environment, therefore avoiding repeated calls to getenv().
	 * If we are setu/gid these flags are ignored to prevent a malicious invoker from changing
	 * our behaviour.
	 */
	for (p = env; (c = *p) != NULL; ++p) {
		if (!strncmp(c, "Malloc", 6)) {
			if (issetugid()) {
				return;
			}
			break;
		}
	}
	
	/*
	 * Deny certain flags for entitled processes rdar://problem/13521742
	 * MallocLogFile & MallocCorruptionAbort
	 * as these provide the ability to turn *off* aborting in error cases.
	 */
	restricted = dyld_process_is_restricted();
	
	if (c == NULL) {
		return;
	}
	if (!restricted) {
		flag = getenv("MallocLogFile");
		if (flag) {
			fd = open(flag, O_WRONLY | O_APPEND | O_CREAT, 0644);
			if (fd >= 0) {
				malloc_debug_file = fd;
				fcntl(fd, F_SETFD, 0); // clear close-on-exec flag  XXX why?
			} else {
				malloc_printf("Could not open %s, using stderr\n", flag);
			}
		}
	}
	if (getenv("MallocGuardEdges")) {
		malloc_debug_flags |= MALLOC_ADD_GUARD_PAGES;
		_malloc_printf(ASL_LEVEL_INFO, "protecting edges\n");
		if (getenv("MallocDoNotProtectPrelude")) {
			malloc_debug_flags |= MALLOC_DONT_PROTECT_PRELUDE;
			_malloc_printf(ASL_LEVEL_INFO, "... but not protecting prelude guard page\n");
		}
		if (getenv("MallocDoNotProtectPostlude")) {
			malloc_debug_flags |= MALLOC_DONT_PROTECT_POSTLUDE;
			_malloc_printf(ASL_LEVEL_INFO, "... but not protecting postlude guard page\n");
		}
	}
	flag = getenv("MallocStackLogging");
	if (!flag) {
		flag = getenv("MallocStackLoggingNoCompact");
		stack_logging_dontcompact = 1;
	}
	if (flag) {
		// Set up stack logging as early as possible to catch all ensuing VM allocations,
		// including those from _malloc_printf and malloc zone setup.  Make sure to set
		// __syscall_logger after this, because prepare_to_log_stacks() itself makes VM
		// allocations that we aren't prepared to log yet.
		boolean_t lite_mode = strcmp(flag, "lite") == 0;
		
		__prepare_to_log_stacks(lite_mode);
		
		if (strcmp(flag, "lite") == 0) {
			stack_logging_mode = stack_logging_mode_lite;
			_malloc_printf(ASL_LEVEL_INFO, "recording malloc and VM allocation stacks using lite mode\n");
		} else if (strcmp(flag,"malloc") == 0) {
			stack_logging_mode = stack_logging_mode_malloc;
			_malloc_printf(ASL_LEVEL_INFO, "recording malloc (but not VM allocation) stacks to disk using standard recorder\n");
		} else if (strcmp(flag, "vm") == 0) {
			stack_logging_mode = stack_logging_mode_vm;
			_malloc_printf(ASL_LEVEL_INFO, "recording VM allocation (but not malloc) stacks to disk using standard recorder\n");
		} else {
			stack_logging_mode = stack_logging_mode_all;
			_malloc_printf(ASL_LEVEL_INFO, "recording malloc and VM allocation stacks to disk using standard recorder\n");
		}
		stack_logging_enable_logging = 1;
		if (stack_logging_dontcompact) {
			if (stack_logging_mode == stack_logging_mode_all || stack_logging_mode == stack_logging_mode_malloc) {
				_malloc_printf(
							   ASL_LEVEL_INFO, "stack logging compaction turned off; size of log files on disk can increase rapidly\n");
			} else {
				_malloc_printf(ASL_LEVEL_INFO, "stack logging compaction turned off; VM can increase rapidly\n");
			}
		}
	}
	if (getenv("MallocScribble")) {
		malloc_debug_flags |= MALLOC_DO_SCRIBBLE;
		_malloc_printf(ASL_LEVEL_INFO, "enabling scribbling to detect mods to free blocks\n");
	}
	if (getenv("MallocErrorAbort")) {
		malloc_debug_flags |= MALLOC_ABORT_ON_ERROR;
		_malloc_printf(ASL_LEVEL_INFO, "enabling abort() on bad malloc or free\n");
	}
	if (getenv("MallocTracing")) {
		malloc_tracing_enabled = true;
	}
	
#if __LP64__
	/* initialization above forces MALLOC_ABORT_ON_CORRUPTION of 64-bit processes */
#else
	flag = getenv("MallocCorruptionAbort");
	if (!restricted && flag && (flag[0] == '0')) { // Set from an environment variable in 32-bit processes
		malloc_debug_flags &= ~MALLOC_ABORT_ON_CORRUPTION;
	} else if (flag) {
		malloc_debug_flags |= MALLOC_ABORT_ON_CORRUPTION;
	}
#endif
	flag = getenv("MallocCheckHeapStart");
	if (flag) {
		malloc_check_start = (unsigned)strtoul(flag, NULL, 0);
		if (malloc_check_start == 0) {
			malloc_check_start = 1;
		}
		if (malloc_check_start == -1) {
			malloc_check_start = 1;
		}
		flag = getenv("MallocCheckHeapEach");
		if (flag) {
			malloc_check_each = (unsigned)strtoul(flag, NULL, 0);
			if (malloc_check_each == 0) {
				malloc_check_each = 1;
			}
			if (malloc_check_each == -1) {
				malloc_check_each = 1;
			}
		}
		_malloc_printf(
					   ASL_LEVEL_INFO, "checks heap after %dth operation and each %d operations\n", malloc_check_start, malloc_check_each);
		flag = getenv("MallocCheckHeapAbort");
		if (flag) {
			malloc_check_abort = (unsigned)strtol(flag, NULL, 0);
		}
		if (malloc_check_abort) {
			_malloc_printf(ASL_LEVEL_INFO, "will abort on heap corruption\n");
		} else {
			flag = getenv("MallocCheckHeapSleep");
			if (flag) {
				malloc_check_sleep = (unsigned)strtol(flag, NULL, 0);
			}
			if (malloc_check_sleep > 0) {
				_malloc_printf(ASL_LEVEL_INFO, "will sleep for %d seconds on heap corruption\n", malloc_check_sleep);
			} else if (malloc_check_sleep < 0) {
				_malloc_printf(ASL_LEVEL_INFO, "will sleep once for %d seconds on heap corruption\n", -malloc_check_sleep);
			} else {
				_malloc_printf(ASL_LEVEL_INFO, "no sleep on heap corruption\n");
			}
		}
	}
	if (getenv("MallocHelp")) {
		_malloc_printf(ASL_LEVEL_INFO,
					   "environment variables that can be set for debug:\n"
					   "- MallocLogFile <f> to create/append messages to file <f> instead of stderr\n"
					   "- MallocGuardEdges to add 2 guard pages for each large block\n"
					   "- MallocDoNotProtectPrelude to disable protection (when previous flag set)\n"
					   "- MallocDoNotProtectPostlude to disable protection (when previous flag set)\n"
					   "- MallocStackLogging to record all stacks.  Tools like leaks can then be applied\n"
					   "- MallocStackLoggingNoCompact to record all stacks.  Needed for malloc_history\n"
					   "- MallocStackLoggingDirectory to set location of stack logs, which can grow large; default is /tmp\n"
					   "- MallocScribble to detect writing on free blocks and missing initializers:\n"
					   "  0x55 is written upon free and 0xaa is written on allocation\n"
					   "- MallocCheckHeapStart <n> to start checking the heap after <n> operations\n"
					   "- MallocCheckHeapEach <s> to repeat the checking of the heap after <s> operations\n"
					   "- MallocCheckHeapSleep <t> to sleep <t> seconds on heap corruption\n"
					   "- MallocCheckHeapAbort <b> to abort on heap corruption if <b> is non-zero\n"
					   "- MallocCorruptionAbort to abort on malloc errors, but not on out of memory for 32-bit processes\n"
					   "  MallocCorruptionAbort is always set on 64-bit processes\n"
					   "- MallocErrorAbort to abort on any malloc error, including out of memory\n"\
					   "- MallocTracing to emit kdebug trace points on malloc entry points\n"\
					   "- MallocHelp - this help!\n");
	}
}
