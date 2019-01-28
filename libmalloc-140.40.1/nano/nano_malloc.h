
#ifndef __NANO_MALLOC_H
#define __NANO_MALLOC_H

#define MALLOC_HELPER_ZONE_STRING "MallocHelperZone"

// Forward decl for the nanozone.
typedef struct nanozone_s nanozone_t;

boolean_t _malloc_engaged_nano; //in .c


// Nano malloc enabled flag
MALLOC_NOEXPORT
extern boolean_t _malloc_engaged_nano;

#endif // __NANO_MALLOC_H
