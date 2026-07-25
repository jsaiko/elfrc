#include <stddef.h>

#include "elfr/elfr.h"
#include "internal.h"

/* Provided by an elfrc-built object linked with the default (unprefixed)
 * symbol name, i.e. `elfrc manifest.elfrc` with no --symbol-prefix. Marked
 * weak so linking libelfr does not force every consumer to embed a
 * default-named resource object -- if nothing provides these, both decay
 * to NULL and get_default_container() reports ERF_ERR_NO_DEFAULT_CONTAINER
 * instead of failing to link. */
extern const char _elfr_resource_start[] __attribute__((weak));
extern const char _elfr_resource_end[] __attribute__((weak));

static ERFContainer *g_default_container = NULL;
static int g_default_attempted = 0;

/* Lazily opens on first use. Not safe to call concurrently from multiple
 * threads before the first successful call returns (see elfr.h); callers
 * needing that should use the explicit-handle API instead. */
static ERFContainer *get_default_container(void) {
    if (g_default_attempted) {
        return g_default_container;
    }
    g_default_attempted = 1;
    if (!_elfr_resource_start || !_elfr_resource_end) {
        elfr_set_last_error(ERF_ERR_NO_DEFAULT_CONTAINER);
        return NULL;
    }
    size_t size = (size_t)(_elfr_resource_end - _elfr_resource_start);
    g_default_container = erf_open_memory(_elfr_resource_start, size);
    return g_default_container;
}

const ERFResource *erf_find(const char *name) {
    ERFContainer *c = get_default_container();
    if (!c) {
        return NULL;
    }
    return erf_find_in(c, name);
}

const ERFResource *erf_find_id(uint32_t id) {
    ERFContainer *c = get_default_container();
    if (!c) {
        return NULL;
    }
    return erf_find_id_in(c, id);
}

const void *erf_resource_data(const char *name, size_t *size) {
    ERFContainer *c = get_default_container();
    if (!c) {
        if (size) {
            *size = 0;
        }
        return NULL;
    }
    return erf_resource_data_in(c, name, size);
}
