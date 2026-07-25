#ifndef ELFRC_CONTAINER_H
#define ELFRC_CONTAINER_H

#include <stddef.h>

#include "manifest.h"

struct elfrc_container {
    unsigned char *data;
    size_t size;
};

/* Builds a complete ERF resource container (header, directory, string
 * table, metadata table, hash table, resource data) in memory from a
 * parsed+validated manifest. Reads every resource's file bytes. */
int elfrc_container_build(const struct elfrc_manifest *manifest, struct elfrc_container *out,
                           char errbuf[ELFRC_ERRBUF_SIZE]);

void elfrc_container_free(struct elfrc_container *c);

#endif /* ELFRC_CONTAINER_H */
