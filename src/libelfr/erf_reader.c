#include <stdlib.h>
#include <string.h>

#include "elfr/elfr.h"
#include "elfr/erf_format.h"
#include "../common/crc64.h"
#include "internal.h"

static _Thread_local ERFError g_last_error = ERF_OK;

void elfr_set_last_error(ERFError e) { g_last_error = e; }
static void set_error(ERFError e) { elfr_set_last_error(e); }

ERFError erf_last_error(void) { return g_last_error; }

const char *erf_strerror(ERFError err) {
    switch (err) {
        case ERF_OK:
            return "ok";
        case ERF_ERR_BAD_MAGIC:
            return "bad ERF magic";
        case ERF_ERR_UNSUPPORTED_VERSION:
            return "unsupported ERF major version";
        case ERF_ERR_TRUNCATED:
            return "container truncated / smaller than header claims";
        case ERF_ERR_BAD_OFFSET:
            return "container has an out-of-bounds offset or size";
        case ERF_ERR_NOT_FOUND:
            return "resource not found";
        case ERF_ERR_INVALID_ARGUMENT:
            return "invalid argument";
        case ERF_ERR_NO_DEFAULT_CONTAINER:
            return "no default .resource object linked into this binary";
    }
    return "unknown error";
}

struct ERFContainer {
    const unsigned char *base;
    size_t size;
    ERFHeader header;
    uint32_t entry_stride; /* header.directory_entry_size, validated */

    ERFResource *resources; /* materialized at open time, resource_count entries */
    uint32_t resource_count;

    int has_hash_table;
    uint32_t hash_bucket_count;
    const ERFHashBucket *hash_buckets;
};

static int in_bounds(size_t size, uint64_t offset, uint64_t len) {
    if (offset > size) {
        return 0;
    }
    if (len > size - offset) {
        return 0;
    }
    return 1;
}

ERFContainer *erf_open_memory(const void *base, size_t size) {
    if (!base || size < sizeof(ERFHeader)) {
        set_error(size == 0 || !base ? ERF_ERR_INVALID_ARGUMENT : ERF_ERR_TRUNCATED);
        return NULL;
    }

    ERFHeader header;
    memcpy(&header, base, sizeof(header));

    if (memcmp(header.magic, ERF_MAGIC, ERF_MAGIC_LEN) != 0) {
        set_error(ERF_ERR_BAD_MAGIC);
        return NULL;
    }
    if (header.version_major != ERF_VERSION_MAJOR) {
        set_error(ERF_ERR_UNSUPPORTED_VERSION);
        return NULL;
    }
    if (header.header_size < sizeof(ERFHeader)) {
        set_error(ERF_ERR_BAD_OFFSET);
        return NULL;
    }
    if (header.total_size < header.header_size || header.total_size > size) {
        set_error(ERF_ERR_BAD_OFFSET);
        return NULL;
    }
    if (header.directory_entry_size < sizeof(ERFDirectoryEntry)) {
        set_error(ERF_ERR_BAD_OFFSET);
        return NULL;
    }
    uint64_t directory_span;
    if (__builtin_mul_overflow((uint64_t)header.resource_count, (uint64_t)header.directory_entry_size,
                                &directory_span) ||
        !in_bounds(header.total_size, header.directory_offset, directory_span)) {
        set_error(ERF_ERR_BAD_OFFSET);
        return NULL;
    }
    if (header.resource_count > 0 && !in_bounds(header.total_size, header.string_offset, 0)) {
        set_error(ERF_ERR_BAD_OFFSET);
        return NULL;
    }

    const unsigned char *b = (const unsigned char *)base;

    int has_hash = (header.flags & ERF_HFLAG_HASH_TABLE_PRESENT) && header.hash_table_offset != 0;
    uint32_t hash_bucket_count = 0;
    const ERFHashBucket *hash_buckets = NULL;
    if (has_hash) {
        if (!in_bounds(header.total_size, header.hash_table_offset, sizeof(ERFHashTableHeader))) {
            set_error(ERF_ERR_BAD_OFFSET);
            return NULL;
        }
        ERFHashTableHeader hth;
        memcpy(&hth, b + header.hash_table_offset, sizeof(hth));
        uint64_t buckets_span;
        if (__builtin_mul_overflow((uint64_t)hth.bucket_count, (uint64_t)sizeof(ERFHashBucket), &buckets_span) ||
            !in_bounds(header.total_size, header.hash_table_offset + sizeof(ERFHashTableHeader), buckets_span)) {
            set_error(ERF_ERR_BAD_OFFSET);
            return NULL;
        }
        hash_bucket_count = hth.bucket_count;
        hash_buckets = (const ERFHashBucket *)(b + header.hash_table_offset + sizeof(ERFHashTableHeader));
    }

    ERFResource *resources = calloc(header.resource_count ? header.resource_count : 1, sizeof(ERFResource));
    for (uint32_t i = 0; i < header.resource_count; i++) {
        ERFDirectoryEntry entry;
        memset(&entry, 0, sizeof(entry));
        memcpy(&entry, b + header.directory_offset + (uint64_t)i * header.directory_entry_size,
               sizeof(ERFDirectoryEntry) < header.directory_entry_size ? sizeof(ERFDirectoryEntry)
                                                                        : header.directory_entry_size);

        if (!in_bounds(header.total_size, entry.name_offset, 1) ||
            !in_bounds(header.total_size, entry.data_offset, entry.data_size)) {
            free(resources);
            set_error(ERF_ERR_BAD_OFFSET);
            return NULL;
        }

        resources[i].id = entry.id;
        resources[i].type = entry.type;
        resources[i].language = entry.language;
        resources[i].flags = entry.flags;
        resources[i].name = (const char *)(b + entry.name_offset);
        resources[i].data = b + entry.data_offset;
        resources[i].size = entry.data_size;
        resources[i].timestamp = entry.timestamp;
        resources[i].metadata_offset = entry.metadata_offset;
    }

    ERFContainer *c = malloc(sizeof(ERFContainer));
    c->base = b;
    c->size = size;
    c->header = header;
    c->entry_stride = header.directory_entry_size;
    c->resources = resources;
    c->resource_count = header.resource_count;
    c->has_hash_table = has_hash;
    c->hash_bucket_count = hash_bucket_count;
    c->hash_buckets = hash_buckets;

    set_error(ERF_OK);
    return c;
}

void erf_close(ERFContainer *container) {
    if (!container) {
        return;
    }
    free(container->resources);
    free(container);
}

uint32_t erf_resource_count(const ERFContainer *container) { return container ? container->resource_count : 0; }

const ERFResource *erf_resource_at(const ERFContainer *container, uint32_t index) {
    if (!container || index >= container->resource_count) {
        set_error(ERF_ERR_INVALID_ARGUMENT);
        return NULL;
    }
    return &container->resources[index];
}

const ERFResource *erf_find_in(const ERFContainer *container, const char *name) {
    if (!container || !name) {
        set_error(ERF_ERR_INVALID_ARGUMENT);
        return NULL;
    }

    if (container->has_hash_table && container->hash_bucket_count > 0) {
        uint64_t h = 0xcbf29ce484222325ULL;
        for (const unsigned char *p = (const unsigned char *)name; *p; p++) {
            h ^= (uint64_t)*p;
            h *= 0x100000001b3ULL;
        }
        uint32_t slot = (uint32_t)(h % container->hash_bucket_count);
        uint32_t probed = 0;
        while (probed < container->hash_bucket_count) {
            const ERFHashBucket *bucket = &container->hash_buckets[slot];
            if (bucket->directory_index == ERF_HASH_EMPTY) {
                break;
            }
            if (bucket->hash == h && bucket->directory_index < container->resource_count &&
                strcmp(container->resources[bucket->directory_index].name, name) == 0) {
                set_error(ERF_OK);
                return &container->resources[bucket->directory_index];
            }
            slot = (slot + 1) % container->hash_bucket_count;
            probed++;
        }
        set_error(ERF_ERR_NOT_FOUND);
        return NULL;
    }

    for (uint32_t i = 0; i < container->resource_count; i++) {
        if (strcmp(container->resources[i].name, name) == 0) {
            set_error(ERF_OK);
            return &container->resources[i];
        }
    }
    set_error(ERF_ERR_NOT_FOUND);
    return NULL;
}

const ERFResource *erf_find_id_in(const ERFContainer *container, uint32_t id) {
    if (!container) {
        set_error(ERF_ERR_INVALID_ARGUMENT);
        return NULL;
    }
    for (uint32_t i = 0; i < container->resource_count; i++) {
        if (container->resources[i].id == id) {
            set_error(ERF_OK);
            return &container->resources[i];
        }
    }
    set_error(ERF_ERR_NOT_FOUND);
    return NULL;
}

const void *erf_resource_data_in(const ERFContainer *container, const char *name, size_t *size) {
    const ERFResource *r = erf_find_in(container, name);
    if (!r) {
        if (size) {
            *size = 0;
        }
        return NULL;
    }
    if (size) {
        *size = r->size;
    }
    return r->data;
}

int erf_metadata_count(const ERFContainer *container, const ERFResource *res) {
    if (!container || !res || res->metadata_offset == 0) {
        return 0;
    }
    if (!in_bounds(container->size, res->metadata_offset, sizeof(uint32_t))) {
        return 0;
    }
    uint32_t count;
    memcpy(&count, container->base + res->metadata_offset, sizeof(count));
    return (int)count;
}

int erf_metadata_get(const ERFContainer *container, const ERFResource *res, int index, const char **key,
                      const char **value) {
    if (!container || !res || res->metadata_offset == 0 || index < 0) {
        return -1;
    }
    uint64_t off = res->metadata_offset;
    if (!in_bounds(container->size, off, sizeof(uint32_t))) {
        return -1;
    }
    uint32_t count;
    memcpy(&count, container->base + off, sizeof(count));
    if ((uint32_t)index >= count) {
        return -1;
    }
    off += sizeof(uint32_t);
    for (int i = 0; i <= index; i++) {
        if (!in_bounds(container->size, off, sizeof(uint32_t))) {
            return -1;
        }
        uint32_t klen;
        memcpy(&klen, container->base + off, sizeof(klen));
        off += sizeof(uint32_t);
        if (!in_bounds(container->size, off, (uint64_t)klen + 1)) {
            return -1;
        }
        const char *k = (const char *)(container->base + off);
        off += (uint64_t)klen + 1; /* +1 for the NUL terminator elfrc writes */
        if (!in_bounds(container->size, off, sizeof(uint32_t))) {
            return -1;
        }
        uint32_t vlen;
        memcpy(&vlen, container->base + off, sizeof(vlen));
        off += sizeof(uint32_t);
        if (!in_bounds(container->size, off, (uint64_t)vlen + 1)) {
            return -1;
        }
        const char *v = (const char *)(container->base + off);
        off += (uint64_t)vlen + 1;
        if (i == index) {
            if (key) {
                *key = k;
            }
            if (value) {
                *value = v;
            }
        }
    }
    return 0;
}

int erf_verify_checksum(const ERFContainer *container) {
    if (!container) {
        return 0;
    }
    unsigned char *tmp = malloc(container->size);
    memcpy(tmp, container->base, container->size);
    ERFHeader *h = (ERFHeader *)tmp;
    h->checksum = 0;
    extern uint64_t elfr_crc64(const void *data, size_t len);
    uint64_t computed = elfr_crc64(tmp, container->size);
    free(tmp);
    return computed == container->header.checksum;
}
