#include "container.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../../include/elfr/erf_format.h"
#include "../common/byte_buf.h"
#include "../common/crc64.h"
#include "../common/fnv1a.h"

#define buf_append_zeros byte_buf_append_zeros
#define buf_append byte_buf_append
#define buf_align byte_buf_align

/* ---- string table ---- */

struct string_entry {
    char *name;
    uint32_t offset;
};

struct string_table {
    struct string_entry *entries;
    size_t count;
};

static uint32_t string_table_intern(struct string_table *st, struct byte_buf *buf, uint32_t table_base,
                                     const char *name) {
    for (size_t i = 0; i < st->count; i++) {
        if (strcmp(st->entries[i].name, name) == 0) {
            return st->entries[i].offset;
        }
    }
    uint32_t offset = (uint32_t)(table_base + (buf->len - table_base));
    buf_append(buf, name, strlen(name) + 1);
    st->entries = realloc(st->entries, (st->count + 1) * sizeof(*st->entries));
    st->entries[st->count].name = strdup(name);
    st->entries[st->count].offset = offset;
    st->count++;
    return offset;
}

static void string_table_free(struct string_table *st) {
    for (size_t i = 0; i < st->count; i++) {
        free(st->entries[i].name);
    }
    free(st->entries);
}

/* ---- hash table (open addressing, linear probing) ---- */

static uint32_t next_pow2(uint32_t v) {
    uint32_t p = 16;
    while (p < v) {
        p *= 2;
    }
    return p;
}

int elfrc_container_build(const struct elfrc_manifest *manifest, struct elfrc_container *out,
                           char errbuf[ELFRC_ERRBUF_SIZE]) {
    memset(out, 0, sizeof(*out));
    errbuf[0] = '\0';

    struct byte_buf buf = {0};
    size_t n = manifest->resource_count;

    /* Header: reserved as zero placeholder, patched at the very end. */
    buf_append_zeros(&buf, sizeof(ERFHeader));

    /* Directory: reserved as zero placeholder; filled once every other
     * section (which later entries depend on) has been built. */
    uint64_t directory_offset = buf.len;
    buf_append_zeros(&buf, n * sizeof(ERFDirectoryEntry));

    /* String table. */
    buf_align(&buf, 8);
    uint64_t string_offset = buf.len;
    struct string_table strtab = {0};
    uint32_t *name_offsets = calloc(n ? n : 1, sizeof(uint32_t));
    for (size_t i = 0; i < n; i++) {
        name_offsets[i] = string_table_intern(&strtab, &buf, (uint32_t)string_offset, manifest->resources[i].name);
    }

    /* Metadata table: one optional block per resource, each block is
     * [uint32_t entry_count]{[uint32_t key_len][key][uint32_t value_len][value]}*entry_count */
    buf_align(&buf, 8);
    uint64_t metadata_table_start = buf.len;
    uint64_t *metadata_offsets = calloc(n ? n : 1, sizeof(uint64_t));
    int any_metadata = 0;
    for (size_t i = 0; i < n; i++) {
        const struct elfrc_resource *r = &manifest->resources[i];
        if (r->metadata_count == 0) {
            metadata_offsets[i] = 0;
            continue;
        }
        any_metadata = 1;
        buf_align(&buf, 8);
        metadata_offsets[i] = buf.len;
        uint32_t count = (uint32_t)r->metadata_count;
        buf_append(&buf, &count, sizeof(count));
        for (size_t j = 0; j < r->metadata_count; j++) {
            /* key/value lengths exclude the trailing NUL; the NUL is
             * written anyway so readers get directly usable C strings. */
            uint32_t klen = (uint32_t)strlen(r->metadata[j].key);
            uint32_t vlen = (uint32_t)strlen(r->metadata[j].value);
            buf_append(&buf, &klen, sizeof(klen));
            buf_append(&buf, r->metadata[j].key, klen + 1);
            buf_append(&buf, &vlen, sizeof(vlen));
            buf_append(&buf, r->metadata[j].value, vlen + 1);
        }
    }
    uint64_t metadata_offset_hdr = any_metadata ? metadata_table_start : 0;

    /* Hash table: bucket content only depends on (name, directory index),
     * both already known, so it can be built for real now. */
    buf_align(&buf, 8);
    uint64_t hash_table_offset = buf.len;
    uint32_t bucket_count = next_pow2((uint32_t)(n * 2));
    if (n == 0) {
        bucket_count = 16;
    }
    ERFHashTableHeader hth = {.bucket_count = bucket_count, .reserved0 = 0};
    buf_append(&buf, &hth, sizeof(hth));
    ERFHashBucket *buckets = calloc(bucket_count, sizeof(ERFHashBucket));
    for (uint32_t i = 0; i < bucket_count; i++) {
        buckets[i].directory_index = ERF_HASH_EMPTY;
    }
    uint64_t *name_hashes = calloc(n ? n : 1, sizeof(uint64_t));
    for (size_t i = 0; i < n; i++) {
        const char *name = manifest->resources[i].name;
        uint64_t h = elfr_fnv1a64(name, strlen(name));
        name_hashes[i] = h;
        uint32_t slot = (uint32_t)(h % bucket_count);
        while (buckets[slot].directory_index != ERF_HASH_EMPTY) {
            slot = (slot + 1) % bucket_count;
        }
        buckets[slot].hash = h;
        buckets[slot].directory_index = (uint32_t)i;
    }
    buf_append(&buf, buckets, bucket_count * sizeof(ERFHashBucket));
    free(buckets);

    /* Resource data. */
    buf_align(&buf, 16);
    uint64_t data_offset_hdr = buf.len;
    uint64_t *data_offsets = calloc(n ? n : 1, sizeof(uint64_t));
    uint64_t *data_sizes = calloc(n ? n : 1, sizeof(uint64_t));
    uint64_t *data_checksums = calloc(n ? n : 1, sizeof(uint64_t));
    for (size_t i = 0; i < n; i++) {
        const struct elfrc_resource *r = &manifest->resources[i];
        FILE *f = fopen(r->file, "rb");
        if (!f) {
            snprintf(errbuf, ELFRC_ERRBUF_SIZE, "%s: error: cannot open '%s' for reading", r->name, r->file);
            goto fail;
        }
        fseek(f, 0, SEEK_END);
        long fsize = ftell(f);
        fseek(f, 0, SEEK_SET);
        unsigned char *filedata = malloc(fsize > 0 ? (size_t)fsize : 1);
        size_t got = fsize > 0 ? fread(filedata, 1, (size_t)fsize, f) : 0;
        fclose(f);
        if (got != (size_t)fsize) {
            free(filedata);
            snprintf(errbuf, ELFRC_ERRBUF_SIZE, "%s: error: short read on '%s'", r->name, r->file);
            goto fail;
        }
        buf_align(&buf, 16);
        data_offsets[i] = buf.len;
        data_sizes[i] = (uint64_t)fsize;
        data_checksums[i] = elfr_crc64(filedata, (size_t)fsize);
        buf_append(&buf, filedata, (size_t)fsize);
        free(filedata);
    }

    /* Fill in the directory now that every dependent offset is known. */
    ERFDirectoryEntry *entries = calloc(n ? n : 1, sizeof(ERFDirectoryEntry));
    int localized = 0;
    for (size_t i = 0; i < n; i++) {
        const struct elfrc_resource *r = &manifest->resources[i];
        entries[i].id = r->final_id;
        entries[i].type = r->type;
        entries[i].language = r->language;
        entries[i].flags = r->flags;
        entries[i].name_offset = name_offsets[i];
        entries[i].reserved0 = 0;
        entries[i].data_offset = data_offsets[i];
        entries[i].data_size = data_sizes[i];
        entries[i].original_size = data_sizes[i];
        entries[i].metadata_offset = metadata_offsets[i];
        entries[i].hash = name_hashes[i];
        entries[i].checksum = data_checksums[i];
        entries[i].timestamp = 0;
        memset(entries[i].reserved, 0, sizeof(entries[i].reserved));
        for (size_t j = 0; j < i; j++) {
            if (strcmp(manifest->resources[j].name, r->name) == 0) {
                localized = 1;
            }
        }
    }
    memcpy(buf.data + directory_offset, entries, n * sizeof(ERFDirectoryEntry));
    free(entries);

    /* Header. */
    ERFHeader header = {0};
    memcpy(header.magic, ERF_MAGIC, 4);
    header.version_major = ERF_VERSION_MAJOR;
    header.version_minor = ERF_VERSION_MINOR;
    header.header_size = sizeof(ERFHeader);
    header.flags = ERF_HFLAG_HASH_TABLE_PRESENT;
    if (any_metadata) {
        header.flags |= ERF_HFLAG_METADATA_PRESENT;
    }
    if (localized) {
        header.flags |= ERF_HFLAG_LOCALIZED;
    }
    header.resource_count = (uint32_t)n;
    header.directory_entry_size = sizeof(ERFDirectoryEntry);
    header.reserved0 = 0;
    header.directory_offset = directory_offset;
    header.string_offset = string_offset;
    header.metadata_offset = metadata_offset_hdr;
    header.hash_table_offset = hash_table_offset;
    header.signature_offset = 0;
    header.data_offset = data_offset_hdr;
    header.total_size = buf.len;
    header.checksum = 0;
    memset(header.reserved, 0, sizeof(header.reserved));

    memcpy(buf.data, &header, sizeof(header)); /* checksum field is still zero here */
    header.checksum = elfr_crc64(buf.data, buf.len);
    memcpy(buf.data, &header, sizeof(header));

    free(name_offsets);
    free(metadata_offsets);
    free(name_hashes);
    free(data_offsets);
    free(data_sizes);
    free(data_checksums);
    string_table_free(&strtab);

    out->data = buf.data;
    out->size = buf.len;
    return 0;

fail:
    free(name_offsets);
    free(metadata_offsets);
    free(name_hashes);
    free(data_offsets);
    free(data_sizes);
    free(data_checksums);
    string_table_free(&strtab);
    free(buf.data);
    return -1;
}

void elfrc_container_free(struct elfrc_container *c) {
    free(c->data);
    c->data = NULL;
    c->size = 0;
}
