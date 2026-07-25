#ifndef ELFR_LIBELFR_INTERNAL_H
#define ELFR_LIBELFR_INTERNAL_H

#include "elfr/elfr.h"

/* Sets the calling thread's erf_last_error() value. Shared between the
 * reader (erf_reader.c) and the default-container convenience API
 * (erf_lookup.c) so both report errors through the same mechanism. */
void elfr_set_last_error(ERFError e);

#endif /* ELFR_LIBELFR_INTERNAL_H */
