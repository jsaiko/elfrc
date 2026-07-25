/*
 * ERF (ELFR Resource Format) on-disk structures.
 *
 * Transcribed from "ELFR Resource Format Specification" v1.0 Draft.
 * This header is the single source of truth shared by the elfrc compiler
 * and libelfr runtime library -- both must agree on this exact layout.
 *
 * ERFHeader is 160 bytes (section 5, Appendix A); ERFDirectoryEntry is 128
 * bytes (section 6, Appendix A). elfrc still sets header_size to the real
 * sizeof(ERFHeader) rather than a hardcoded constant, per section 5.1 and
 * section 16's forward-compatibility guidance.
 */

#ifndef ELFR_ERF_FORMAT_H
#define ELFR_ERF_FORMAT_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define ERF_MAGIC "ELFR"
#define ERF_MAGIC_LEN 4

#define ERF_VERSION_MAJOR 1
#define ERF_VERSION_MINOR 0

/* ERFHeader.flags bits (section 5.1) */
#define ERF_HFLAG_LOCALIZED (1u << 0)
#define ERF_HFLAG_METADATA_PRESENT (1u << 1)
#define ERF_HFLAG_HASH_TABLE_PRESENT (1u << 2)
#define ERF_HFLAG_SIGNATURE_PRESENT (1u << 3)
#define ERF_HFLAG_COMPRESSED_RESOURCES_EXIST (1u << 4)
#define ERF_HFLAG_ENCRYPTED_RESOURCES_EXIST (1u << 5)

/* ERFDirectoryEntry.flags bits (section 6.1) */
#define ERF_RFLAG_COMPRESSED (1u << 0)
#define ERF_RFLAG_ENCRYPTED (1u << 1)
#define ERF_RFLAG_READ_ONLY (1u << 2)
#define ERF_RFLAG_EXECUTABLE (1u << 3)
#define ERF_RFLAG_OPTIONAL (1u << 4)

/* ERFDirectoryEntry.type values (section 6.1) -- values >= 1000 are
 * application-defined per spec. */
enum erf_resource_type {
    ERF_TYPE_BINARY = 0,
    ERF_TYPE_STRING = 1,
    ERF_TYPE_JSON = 2,
    ERF_TYPE_XML = 3,
    ERF_TYPE_YAML = 4,
    ERF_TYPE_PNG = 5,
    ERF_TYPE_JPEG = 6,
    ERF_TYPE_SVG = 7,
    ERF_TYPE_GIF = 8,
    ERF_TYPE_ICON = 9,
    ERF_TYPE_FONT = 10,
    ERF_TYPE_AUDIO = 11,
    ERF_TYPE_VIDEO = 12,
    ERF_TYPE_SHADER = 13,
    ERF_TYPE_CERTIFICATE = 14,
};

/* Compression identifiers (section 12). v1 only implements NONE; any other
 * value requested in a manifest is a hard compiler error. */
enum erf_compression {
    ERF_COMPRESSION_NONE = 0,
    ERF_COMPRESSION_DEFLATE = 1,
    ERF_COMPRESSION_ZSTD = 2,
    ERF_COMPRESSION_LZ4 = 3,
    ERF_COMPRESSION_BROTLI = 4,
    ERF_COMPRESSION_XZ = 5,
};

/* Well-known language identifiers (section 14). 0 = neutral. Others are
 * implementation-defined; see erf_language_from_tag() in the compiler. */
#define ERF_LANGUAGE_NEUTRAL 0u

typedef struct ERFHeader {
    /* Identification */
    char magic[4];
    uint16_t version_major;
    uint16_t version_minor;
    uint32_t header_size;

    /* Container */
    uint32_t flags;
    uint32_t resource_count;
    uint32_t directory_entry_size;
    uint32_t reserved0;

    /* Offsets (relative to the start of .resource) */
    uint64_t directory_offset;
    uint64_t string_offset;
    uint64_t metadata_offset;
    uint64_t hash_table_offset;
    uint64_t signature_offset;
    uint64_t data_offset;

    /* Size */
    uint64_t total_size;
    uint64_t checksum;

    /* Reserved */
    uint64_t reserved[8];
} ERFHeader;

typedef struct ERFDirectoryEntry {
    uint32_t id;
    uint32_t type;
    uint32_t language;
    uint32_t flags;
    uint32_t name_offset;
    uint32_t reserved0;

    uint64_t data_offset;
    uint64_t data_size;
    uint64_t original_size;
    uint64_t metadata_offset;
    uint64_t hash;
    uint64_t checksum;
    uint64_t timestamp;

    uint64_t reserved[6];
} ERFDirectoryEntry;

_Static_assert(sizeof(ERFHeader) == 160, "ERFHeader must match spec Appendix A (160 bytes)");
_Static_assert(sizeof(ERFDirectoryEntry) == 128, "ERFDirectoryEntry must match spec Appendix A (128 bytes)");

/*
 * Hash table (section 9). The spec describes the lookup semantics
 * ("Hash -> Bucket -> Directory Entry") but not an exact byte layout, so
 * this is our own concrete, self-consistent format: a small header giving
 * the bucket count, followed by that many open-addressed buckets. Both
 * elfrc (writer) and libelfr (reader) share this definition.
 */
typedef struct ERFHashTableHeader {
    uint32_t bucket_count;
    uint32_t reserved0;
} ERFHashTableHeader;

#define ERF_HASH_EMPTY 0xFFFFFFFFu

typedef struct ERFHashBucket {
    uint64_t hash;
    uint32_t directory_index; /* ERF_HASH_EMPTY if this bucket is unused */
    uint32_t reserved0;
} ERFHashBucket;

_Static_assert(sizeof(ERFHashTableHeader) == 8, "ERFHashTableHeader layout drifted");
_Static_assert(sizeof(ERFHashBucket) == 16, "ERFHashBucket layout drifted");

#ifdef __cplusplus
}
#endif

#endif /* ELFR_ERF_FORMAT_H */
