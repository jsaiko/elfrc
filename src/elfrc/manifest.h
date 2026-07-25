#ifndef ELFRC_MANIFEST_H
#define ELFRC_MANIFEST_H

#include <stddef.h>
#include <stdint.h>

#define ELFRC_ERRBUF_SIZE 512

struct elfrc_metadata_kv {
    char *key;
    char *value;
};

struct elfrc_resource {
    char *name;     /* logical resource name, e.g. "shader/main" */
    char *file;     /* resolved filesystem path (relative to manifest dir) */

    uint32_t type;
    uint32_t language;
    uint32_t compression; /* enum erf_compression; must be ERF_COMPRESSION_NONE in v1 */
    uint32_t flags;       /* ERF_RFLAG_* bitmask */

    int id_explicit;
    uint32_t id;
    uint32_t final_id; /* resolved after id-assignment pass */

    struct elfrc_metadata_kv *metadata;
    size_t metadata_count;

    int line; /* 1-based line in the manifest, for diagnostics; 0 if unknown */
};

struct elfrc_manifest {
    int version;
    char *module; /* optional top-level "module:" field -> default symbol prefix */

    struct elfrc_resource *resources;
    size_t resource_count;
};

/*
 * Parse `path` (a YAML manifest) into *out_manifest. Resource `file` paths
 * are resolved relative to the manifest's own directory. On failure, writes
 * a human-readable "path:line: message" string into errbuf and returns -1.
 * On success returns 0; caller must elfrc_manifest_free() the result.
 */
int elfrc_manifest_parse(const char *path, struct elfrc_manifest *out_manifest,
                          char errbuf[ELFRC_ERRBUF_SIZE]);

void elfrc_manifest_free(struct elfrc_manifest *manifest);

#endif /* ELFRC_MANIFEST_H */
