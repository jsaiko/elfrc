/*
 * libelfr: runtime access library for ERF resource containers embedded by
 * elfrc into a `.resource` ELF section (see ELFR Resource Format spec).
 */
#ifndef ELFR_H
#define ELFR_H

#include <stddef.h>
#include <stdint.h>

#include "erf_format.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct ERFContainer ERFContainer; /* opaque */

typedef struct ERFResource {
    uint32_t id;
    uint32_t type;
    uint32_t language;
    uint32_t flags;
    const char *name;
    const void *data;
    size_t size;
    uint64_t timestamp;
    uint64_t metadata_offset; /* 0 if this resource has no metadata */
} ERFResource;

typedef enum ERFError {
    ERF_OK = 0,
    ERF_ERR_BAD_MAGIC,
    ERF_ERR_UNSUPPORTED_VERSION,
    ERF_ERR_TRUNCATED,
    ERF_ERR_BAD_OFFSET,
    ERF_ERR_NOT_FOUND,
    ERF_ERR_INVALID_ARGUMENT,
    ERF_ERR_NO_DEFAULT_CONTAINER,
} ERFError;

/* Returns the error code set by the most recent failing libelfr call on
 * this thread. */
ERFError erf_last_error(void);
const char *erf_strerror(ERFError err);

/* ---- Explicit-handle API ----
 * For a container whose bytes you already have (mmap'd file, a symbol
 * pair from an elfrc-built object, etc). Validates the header per the ERF
 * spec (Appendix B) before returning a handle. Zero-copy: resource data
 * pointers point directly into `base`, which must remain valid for the
 * lifetime of the returned container. */
ERFContainer *erf_open_memory(const void *base, size_t size);
void erf_close(ERFContainer *container);

const ERFResource *erf_find_in(const ERFContainer *container, const char *name);
const ERFResource *erf_find_id_in(const ERFContainer *container, uint32_t id);
const void *erf_resource_data_in(const ERFContainer *container, const char *name, size_t *size);

uint32_t erf_resource_count(const ERFContainer *container);
const ERFResource *erf_resource_at(const ERFContainer *container, uint32_t index);

/* Recomputes the CRC-64 container checksum and compares it against the
 * stored value. Not called automatically by erf_open_memory -- integrity
 * verification is opt-in. */
int erf_verify_checksum(const ERFContainer *container);

/* Metadata key/value iteration for a resource returned by this same
 * container (section 8). Returns 0 if the resource has no metadata. */
int erf_metadata_count(const ERFContainer *container, const ERFResource *res);
int erf_metadata_get(const ERFContainer *container, const ERFResource *res, int index, const char **key,
                      const char **value);

/* ---- Default-container convenience API (spec section 19) ----
 * Operates on the .resource section built with the default (unprefixed)
 * elfrc symbol names. Lazily opens on first use. Not safe to call from
 * multiple threads concurrently before the first successful call returns;
 * for concurrent/multi-container use, prefer the explicit-handle API
 * above. If no elfrc-built object with default symbol names was linked
 * into this binary, these return NULL / ERF_ERR_NO_DEFAULT_CONTAINER. */
const ERFResource *erf_find(const char *name);
const ERFResource *erf_find_id(uint32_t id);
const void *erf_resource_data(const char *name, size_t *size);

/*
 * ---- Multi-container symbol helpers ----
 * elfrc --symbol-prefix NAME emits _elfr_NAME_resource_start/_end. Use
 * ELFR_DECLARE(NAME) once at file scope to declare them, then
 * ELFR_START/ELFR_SIZE to pass them to erf_open_memory. This resolves
 * entirely at link time (safe under static linking / stripped binaries;
 * no dlsym involved), so multiple elfrc-built objects with distinct
 * prefixes can be linked into one binary without symbol collisions.
 */
#define ELFR_DECLARE(prefix)                                  \
    extern const char _elfr_##prefix##_resource_start[];      \
    extern const char _elfr_##prefix##_resource_end[]

#define ELFR_START(prefix) ((const void *)_elfr_##prefix##_resource_start)
#define ELFR_SIZE(prefix) ((size_t)(_elfr_##prefix##_resource_end - _elfr_##prefix##_resource_start))

#ifdef __cplusplus
}
#endif

#endif /* ELFR_H */
