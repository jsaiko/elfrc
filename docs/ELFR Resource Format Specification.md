# ELFR Resource Format (ERF) Specification

**Version:** 1.0 Draft  
**Status:** Proposed  
**Author:** ELFR Working Group (Draft)  
**Target Format:** ELF (Executable and Linkable Format)

---

# 1. Introduction

The **ELFR Resource Format (ERF)** defines a standardized method for embedding arbitrary resources into ELF binaries.

The format introduces a convention for a dedicated ELF section named:

```
.resource
```

that contains a structured resource database.

Unlike Windows PE, no modifications to the ELF specification, operating system loader, linker, or kernel are required.

ERF is entirely implemented as an ELF convention.

---

# 2. Design Goals

The ERF specification has the following goals:

- Embed arbitrary files into ELF binaries.
- Efficient runtime lookup.
- Zero-copy resource access where possible.
- Compatible with existing ELF loaders.
- Compatible with GNU ld, LLVM lld, and mold.
- Position independent.
- Extensible.
- Versioned.
- Forward compatible.
- Backward compatible.

---

# 3. ELF Integration

Resources are stored inside a standard ELF section.

```
.resource
```

Section properties:

| Property | Value |
|----------|------|
| Type | SHT_PROGBITS |
| Flags | SHF_ALLOC |
| Writable | No |
| Executable | No |
| Alignment | 16 bytes recommended |

Since ELF permits arbitrary sections, existing loaders ignore the section unless explicitly accessed by the application.

---

# 4. Resource Section Layout

```
.resource

+--------------------------------------+
| ERF Header                           |
+--------------------------------------+
| Resource Directory                   |
+--------------------------------------+
| String Table                         |
+--------------------------------------+
| Metadata Table                       |
+--------------------------------------+
| Hash Table (optional)                |
+--------------------------------------+
| Signature Table (optional)           |
+--------------------------------------+
| Resource Data                        |
+--------------------------------------+
```

Every offset stored inside the format is relative to the beginning of the `.resource` section.

---

# 5. ERF Header

The ERF header identifies the resource container and provides offsets to every major component.

## Structure

```c
typedef struct ERFHeader
{
    /* Identification */

    char     magic[4];

    uint16_t version_major;
    uint16_t version_minor;

    uint32_t header_size;

    /* Container */

    uint32_t flags;

    uint32_t resource_count;

    uint32_t directory_entry_size;

    uint32_t reserved0;

    /* Offsets */

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
```

Header size:

```
160 bytes
```

---

## 5.1 Header Fields

### magic

ASCII identifier.

```
ELFR
```

Hex:

```
45 4C 46 52
```

Parsers SHALL reject any resource container whose magic value does not equal `"ELFR"`.

---

### version_major

Major format version.

Incremented whenever incompatible changes are introduced.

---

### version_minor

Minor format version.

Incremented whenever backward-compatible features are added.

---

### header_size

Size of the header structure.

Readers SHALL use this value instead of assuming a fixed header size.

---

### flags

Global properties affecting the entire resource container.

| Bit | Meaning |
|------|----------|
|0|Localized resources present|
|1|Metadata table present|
|2|Hash table present|
|3|Signature table present|
|4|Compressed resources exist|
|5|Encrypted resources exist|
|6-31|Reserved|

Unknown bits SHALL be ignored.

---

### resource_count

Total number of resource directory entries.

---

### directory_entry_size

Size of every directory entry.

Allows future revisions to extend directory entries without changing parsing logic.

---

### reserved0

Reserved.

Must be zero.

---

### directory_offset

Offset to the resource directory.

---

### string_offset

Offset to the string table.

---

### metadata_offset

Offset to metadata table.

Zero if absent.

---

### hash_table_offset

Offset to hash table.

Zero if absent.

---

### signature_offset

Offset to digital signature table.

Zero if absent.

---

### data_offset

Offset to the beginning of resource data.

---

### total_size

Total size of the `.resource` section.

---

### checksum

Container integrity checksum.

Version 1 recommends:

```
CRC-64
```

computed over the entire resource container with the checksum field zeroed.

---

### reserved[8]

Reserved for future expansion.

Must be initialized to zero.

Readers SHALL ignore these fields.

Future versions may assign meanings without changing the header size.

---

# 6. Resource Directory

The directory contains one entry per resource.

Resources are never searched by scanning the data section.

---

## Structure

```c
typedef struct ERFDirectoryEntry
{
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
```

Directory entry size:

```
128 bytes
```

---

# 6.1 Directory Fields

## id

Numeric resource identifier.

Unique within the container.

---

## type

Resource type.

Examples:

| ID | Type |
|----|------|
|0|Binary|
|1|String|
|2|JSON|
|3|XML|
|4|YAML|
|5|PNG|
|6|JPEG|
|7|SVG|
|8|GIF|
|9|Icon|
|10|Font|
|11|Audio|
|12|Video|
|13|Shader|
|14|Certificate|

Values ≥1000 are application-defined.

---

## language

Language identifier.

```
0 = Neutral
1033 = English
1031 = German
1036 = French
```

---

## flags

Resource-specific flags.

Suggested assignments:

| Bit | Meaning |
|------|----------|
|0|Compressed|
|1|Encrypted|
|2|Read-only|
|3|Executable|
|4|Optional|
|5-31|Reserved|

---

## name_offset

Offset into the string table.

---

## data_offset

Offset to the resource bytes.

---

## data_size

Stored size.

---

## original_size

Size before compression.

Equal to data_size if not compressed.

---

## metadata_offset

Offset into metadata table.

Zero if absent.

---

## hash

64-bit hash of the resource name.

Recommended:

```
FNV-1a 64
```

---

## checksum

Per-resource integrity checksum.

---

## timestamp

Creation or modification timestamp.

Recommended format:

```
Unix Epoch (UTC)
```

---

## reserved[6]

Reserved for future expansion.

Must be initialized to zero.

Readers SHALL ignore these fields.

---

# 7. String Table

Resource names are stored only once.

Example:

```
logo\0
config\0
font/default\0
shader/main\0
```

Directory entries reference names by offset.

---

# 8. Metadata Table

Metadata is optional.

Recommended keys:

- Author
- Description
- License
- MIME Type
- Original Filename
- Build Date
- Generator
- Comments

Layout:

```
Key Length

Key

Value Length

Value
```

---

# 9. Hash Table

Optional.

Provides constant-time resource lookup.

```
Hash

↓

Bucket

↓

Directory Entry
```

Recommended algorithm:

```
FNV-1a 64
```

---

# 10. Signature Table

Optional.

Stores digital signatures.

Recommended algorithms:

- Ed25519
- ECDSA P-256
- RSA-4096

Applications define verification policy.

---

# 11. Resource Data

Contains raw bytes.

No interpretation is imposed by ERF.

Examples:

- PNG
- JPEG
- JSON
- ZIP
- Fonts
- Audio
- Video
- SQL
- Scripts

---

# 12. Compression

Compression is applied per resource.

Supported identifiers:

| ID | Algorithm |
|----|-----------|
|0|None|
|1|DEFLATE|
|2|Zstandard|
|3|LZ4|
|4|Brotli|
|5|XZ|

---

# 13. Encryption

Encryption is optional.

Recommended algorithms:

- AES-256-GCM
- ChaCha20-Poly1305

Key management is outside the scope of this specification.

---

# 14. Localization

Multiple resources may share the same identifier with different language IDs.

Example:

```
logo/en
logo/fr
logo/de
```

Runtime selection is implementation-defined.

---

# 15. Runtime Parsing

Applications should perform the following sequence:

```
Locate .resource

↓

Read ERFHeader

↓

Verify magic

↓

Verify version

↓

Validate header

↓

Read directory

↓

Resolve names

↓

Locate resource

↓

Return pointer
```

---

# 16. Forward Compatibility

All reserved fields shall be initialized to zero.

Readers shall ignore unknown reserved fields.

Readers shall use:

- header_size
- directory_entry_size

instead of compile-time constants.

---

# 17. Toolchain

Recommended utilities:

```
elfrc
```

Compiles resource files into an ELF object.

```
elfresdump
```

Lists or extracts resources.

```
elfresedit
```

Updates existing resource sections.

```
libelfr
```

Runtime access library.

---

# 18. Example Build

```
elfrc \
    logo.png \
    config.json \
    font.ttf \
    -o resources.o

gcc main.c resources.o -o app
```

---

# 19. Runtime API

```c
const ERFResource *
erf_find(const char *name);

const ERFResource *
erf_find_id(uint32_t id);

const void *
erf_resource_data(
    const char *name,
    size_t *size);
```

---

# 20. Design Principles

ERF follows these principles:

- No ELF modifications.
- No kernel modifications.
- Position independent.
- Offset-based structures.
- Fixed-size headers.
- Forward-compatible layouts.
- Efficient lookup.
- Memory-mappable.
- Extensible through reserved fields.

---

# 21. Future Extensions

Reserved areas permit future additions such as:

- Resource dependency graphs
- Shared resource pools
- Embedded package manifests
- Resource deduplication
- Delta updates
- Integrity trees
- Streaming resources
- Signed update catalogs
- Compression dictionaries
- Embedded debug resources

without requiring structural changes to the ERF header or directory entry layouts.

---

# Appendix A – Structure Sizes

| Structure | Size |
|------------|------|
|ERFHeader|160 bytes|
|ERFDirectoryEntry|128 bytes|

---

# Appendix B – Required Reader Behavior

Readers SHALL:

- Verify the magic value.
- Verify the major version.
- Validate all offsets.
- Validate all sizes.
- Ignore unknown flags.
- Ignore reserved fields.
- Reject malformed containers.

---

# Appendix C – Required Writer Behavior

Writers SHALL:

- Initialize all reserved fields to zero.
- Write valid offsets.
- Align resources appropriately.
- Store offsets relative to `.resource`.
- Maintain unique resource IDs.
- Update checksums after modifications.

