
#include "malloc_zone_introspection.h"

/*
 * XXX malloc_printf now uses _simple_*printf.  It only deals with a
 * subset of printf format specifiers, but it doesn't call malloc.
 */

__attribute__((visibility("hidden"))) void
_malloc_vprintf(int flags, const char *format, va_list ap)
{
	_SIMPLE_STRING b;
	
	if (_malloc_no_asl_log || (flags & MALLOC_PRINTF_NOLOG) || (b = _simple_salloc()) == NULL) {
		if (!(flags & MALLOC_PRINTF_NOPREFIX)) {
			void *self = _os_tsd_get_direct(__TSD_THREAD_SELF);
			_simple_dprintf(malloc_debug_file, "%s(%d,%p) malloc: ", getprogname(), getpid(), self);
		}
		_simple_vdprintf(malloc_debug_file, format, ap);
		return;
	}
	if (!(flags & MALLOC_PRINTF_NOPREFIX)) {
		void *self = _os_tsd_get_direct(__TSD_THREAD_SELF);
		_simple_sprintf(b, "%s(%d,%p) malloc: ", getprogname(), getpid(), self);
	}
	_simple_vsprintf(b, format, ap);
	_simple_put(b, malloc_debug_file);
	_simple_asl_log(flags & MALLOC_PRINTF_LEVEL_MASK, Malloc_Facility, _simple_string(b));
	_simple_sfree(b);
}

__attribute__((visibility("hidden"))) void
_malloc_printf(int flags, const char *format, ...)
{
	va_list ap;
	
	va_start(ap, format);
	_malloc_vprintf(flags, format, ap);
	va_end(ap);
}

void
malloc_printf(const char *format, ...)
{
	va_list ap;
	
	va_start(ap, format);
	_malloc_vprintf(ASL_LEVEL_ERR, format, ap);
	va_end(ap);
}

