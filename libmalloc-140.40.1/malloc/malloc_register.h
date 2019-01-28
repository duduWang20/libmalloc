//
//  malloc_register.h
//  libsystem_malloc
//
//  Created by 王举范 on 2018/12/17.
//

#ifndef malloc_register_h
#define malloc_register_h

#include <stdio.h>


#if TARGET_OS_EMBEDDED || TARGET_IPHONE_SIMULATOR
// _malloc_printf(ASL_LEVEL_INFO...) on iOS doesn't show up in the Xcode Console log of the device,
// but ASL_LEVEL_NOTICE does.  So raising the log level is helpful.
#undef ASL_LEVEL_INFO
#define ASL_LEVEL_INFO ASL_LEVEL_NOTICE
#endif

/*
 * MALLOC_ABSOLUTE_MAX_SIZE - There are many instances of addition to a
 * user-specified size_t, which can cause overflow (and subsequent crashes)
 * for values near SIZE_T_MAX.  Rather than add extra "if" checks everywhere
 * this occurs, it is easier to just set an absolute maximum request size,
 * and immediately return an error if the requested size exceeds this maximum.
 * Of course, values less than this absolute max can fail later if the value
 * is still too large for the available memory.  The largest value added
 * seems to be PAGE_SIZE (in the macro round_page()), so to be safe, we set
 * the maximum to be 2 * PAGE_SIZE less than SIZE_T_MAX.
 */
#define MALLOC_ABSOLUTE_MAX_SIZE (SIZE_T_MAX - (2 * PAGE_SIZE))

#define USE_SLEEP_RATHER_THAN_ABORT 0

/*
 MAX_LITE_MALLOCS
 
 If msl lite is turned on due to a memory resource exception use this value as the maximum
 number of allocations allowed before msl lite is turned off. This prevents msl lite from being
 enabled indefinitely if the process never reaches 100% of its jetsam limit.
 See rdar://problem/25950426 for a discussion of how this number was determined.
 */

#define MAX_LITE_MALLOCS 100000000

typedef void(malloc_logger_t)(uint32_t type,
							  uintptr_t arg1,
							  uintptr_t arg2,
							  uintptr_t arg3,
							  uintptr_t result,
							  uint32_t num_hot_frames_to_skip);

extern malloc_logger_t *__syscall_logger; // use this to set up syscall logging (e.g., vm_allocate, vm_deallocate, mmap, munmap)

static _malloc_lock_s _malloc_lock = _MALLOC_LOCK_INIT;
#define MALLOC_LOCK() _malloc_lock_lock(&_malloc_lock)
#define MALLOC_TRY_LOCK() _malloc_lock_trylock(&_malloc_lock)
#define MALLOC_UNLOCK() _malloc_lock_unlock(&_malloc_lock)
#define MALLOC_REINIT_LOCK() _malloc_lock_init(&_malloc_lock)

/* The following variables are exported for the benefit of performance tools
 *
 * It should always be safe to first read malloc_num_zones, then read
 * malloc_zones without taking the lock, if only iteration is required and
 * provided that when malloc_destroy_zone is called all prior operations on that
 * zone are complete and no further calls referencing that zone can be made.
 */
int32_t malloc_num_zones = 0;
int32_t malloc_num_zones_allocated = 0;
malloc_zone_t **malloc_zones = 0;
malloc_logger_t *malloc_logger = NULL;
static malloc_zone_t *initial_default_zone = NULL;

unsigned malloc_debug_flags = 0;
boolean_t malloc_tracing_enabled = false;

unsigned malloc_check_start = 0; // 0 means don't check
unsigned malloc_check_counter = 0;
unsigned malloc_check_each = 1000;

/* global flag to suppress ASL logging e.g. for syslogd */
int _malloc_no_asl_log = 0;

static int malloc_check_sleep = 100; // default 100 second sleep
static int malloc_check_abort = 0;   // default is to sleep, not abort

static int malloc_debug_file = STDERR_FILENO;
static os_once_t _malloc_initialize_pred;

static const char Malloc_Facility[] = "com.apple.Libsystem.malloc";

// Used by memory resource exceptions and enabling/disabling malloc stack logging via malloc_memory_event_handler
static boolean_t warn_mode_entered = false;
static boolean_t warn_mode_disable_retries = false;
static stack_logging_mode_type msl_type_enabled_at_runtime = stack_logging_mode_none;

/*
 * Counters that coordinate zone destruction (in malloc_zone_unregister) with
 * find_registered_zone (here abbreviated as FRZ).
 */
static int32_t volatile counterAlice = 0, counterBob = 0;
static int32_t volatile * volatile pFRZCounterLive = &counterAlice;
static int32_t volatile * volatile pFRZCounterDrain = &counterBob;

static inline malloc_zone_t *inline_malloc_default_zone(void) __attribute__((always_inline));

#define MALLOC_LOG_TYPE_ALLOCATE stack_logging_type_alloc
#define MALLOC_LOG_TYPE_DEALLOCATE stack_logging_type_dealloc
#define MALLOC_LOG_TYPE_HAS_ZONE stack_logging_flag_zone
#define MALLOC_LOG_TYPE_CLEARED stack_logging_flag_cleared

#define DEFAULT_MALLOC_ZONE_STRING "DefaultMallocZone"
#define DEFAULT_PUREGEABLE_ZONE_STRING "DefaultPurgeableMallocZone"


//#include <sys/mman.h>
//int mprotect(void *addr, size_t len, int prot);
//mprotect() changes protection for the calling process's memory page(s) containing any part of the address range in the interval [addr, addr+len-1]. addr must be aligned to a page boundary.

#endif /* malloc_register_h */

