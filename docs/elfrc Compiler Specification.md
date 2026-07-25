# ELFR Resource Compiler (`elfrc`) Specification

**Version:** 1.0 Draft
**Status:** Proposed
**Author:** ELFR Working Group (Draft)

---

# 1. Introduction

`elfrc` is the reference resource compiler for the **ELFR Resource Format (ERF)**.

The compiler reads a project manifest describing a collection of resources, validates the input, constructs an ERF resource container, and emits a relocatable ELF object containing a `.resource` section.

Unlike traditional resource compilers that require numerous command-line arguments, `elfrc` is **manifest driven**, allowing projects of arbitrary size to be described in a single declarative file.

---

# 2. Design Goals

The compiler SHALL:

* Produce ERF-compliant resource containers.
* Support deterministic builds.
* Eliminate duplicate resource names.
* Automatically construct the ERF String Table.
* Support arbitrary resource types.
* Support localization.
* Support metadata.
* Support optional compression.
* Support optional encryption.
* Produce standard relocatable ELF object files.
* Require no linker modifications.

---

# 3. Invocation

The preferred invocation is

```bash
elfrc resources.elfrc
```

or

```bash
elfrc resources.elfrc -o resources.o
```

where:

* `resources.elfrc` is the project manifest.
* `resources.o` is the generated ELF object.

If no output filename is specified, implementations may derive one from the manifest filename.

---

# 4. Manifest Format

The manifest is a structured document describing every resource that shall be compiled into the ERF container.

The manifest format is intentionally independent of the ERF binary format.

Implementations may support YAML, JSON, TOML, XML, or another structured representation provided identical input produces identical ERF output.

Examples in this specification use YAML.

---

# 5. Manifest Structure

Every manifest contains three top-level sections.

```yaml
version: 1

defaults:
    ...

resources:
    ...
```

| Field     | Required | Description                 |
| --------- | -------- | --------------------------- |
| version   | Yes      | Manifest version            |
| defaults  | No       | Default resource properties |
| resources | Yes      | List of resources           |

---

# 6. Defaults

The optional `defaults` section specifies values inherited by every resource unless explicitly overridden.

Example:

```yaml
defaults:

    language: neutral

    compression: zstd

    encryption: none
```

Defaults reduce duplication in large projects.

---

# 7. Resource Definitions

Each entry within `resources` defines one logical ERF resource.

Example:

```yaml
resources:

  - name: logo
    file: assets/logo.png
    type: png
    id: 100

  - name: config
    file: config/app.json
    type: json

  - name: shader/main
    file: shaders/main.spv
    type: shader

  - name: font/default
    file: fonts/Roboto.ttf
    type: font
```

Each resource becomes one `ERFDirectoryEntry`.

---

# 8. Resource Fields

## name

Logical resource name.

Required.

The name uniquely identifies the resource at runtime.

Example

```yaml
name: shader/main
```

---

## file

Filesystem path to the source resource.

Required.

Example

```yaml
file: assets/logo.png
```

---

## type

Resource type.

Optional.

If omitted, implementations may infer the type from the filename extension.

Example

```yaml
type: png
```

---

## id

Numeric resource identifier.

Optional.

If omitted, the compiler SHALL assign a unique identifier.

---

## language

Language identifier.

Optional.

Overrides the manifest default.

Example

```yaml
language: en-US
```

---

## compression

Compression algorithm.

Optional.

Overrides the manifest default.

Example

```yaml
compression: zstd
```

---

## encryption

Encryption policy.

Optional.

---

## metadata

Optional key/value metadata associated with the resource.

Example

```yaml
metadata:

    Author: Jane Smith

    License: MIT

    Description: Application logo
```

---

## flags

Optional implementation-defined resource flags.

---

# 9. Localization

Multiple resources may share the same logical name while differing only by language.

Example

```yaml
resources:

  - name: strings
    language: en-US
    file: locale/en.json

  - name: strings
    language: fr-FR
    file: locale/fr.json

  - name: strings
    language: de-DE
    file: locale/de.json
```

The compiler generates three directory entries referencing a single resource name within the ERF String Table.

---

# 10. Automatic String Table Generation

The ERF String Table is **not** authored by the user.

Instead, the compiler automatically constructs the table from every unique resource name appearing in the manifest.

Given

```yaml
resources:

  - name: logo
    file: logo.png

  - name: config
    file: config.json

  - name: shader/main
    file: shader.spv

  - name: logo
    language: fr-FR
    file: logo_fr.png
```

the compiler generates

```text
logo\0
config\0
shader/main\0
```

Only unique names are stored.

Each directory entry contains a `name_offset` referencing one of these strings.

---

# 11. Compiler Responsibilities

The compiler SHALL:

* Parse the manifest.
* Validate manifest syntax.
* Validate every resource path.
* Load every resource.
* Apply inherited defaults.
* Infer resource types where appropriate.
* Assign resource identifiers.
* Construct the ERF String Table.
* Construct the Metadata Table.
* Construct the Resource Directory.
* Compress resources when requested.
* Encrypt resources when requested.
* Compute hashes.
* Compute checksums.
* Emit a valid ERF resource container.
* Produce a relocatable ELF object.

---

# 12. Build Example

Project layout

```
assets/
    logo.png

config/
    app.json

fonts/
    Roboto.ttf

shaders/
    main.spv

resources.elfrc
```

Manifest

```yaml
version: 1

defaults:

    compression: zstd

resources:

  - name: logo
    file: assets/logo.png

  - name: config
    file: config/app.json

  - name: font/default
    file: fonts/Roboto.ttf

  - name: shader/main
    file: shaders/main.spv
```

Compile

```bash
elfrc resources.elfrc -o resources.o
```

Link

```bash
gcc main.c resources.o -o application
```

---

# 13. Deterministic Builds

For identical manifest input and identical resource files, implementations SHOULD produce byte-for-byte identical ERF containers.

Automatic identifier assignment, string table generation, directory ordering, hashes, and metadata ordering SHOULD be deterministic.

---

# 14. Error Reporting

Compilation SHALL fail when:

* The manifest is malformed.
* A required field is missing.
* A referenced file cannot be opened.
* Duplicate explicit resource identifiers exist.
* Resource names are invalid.
* Unsupported compression or encryption algorithms are requested.
* Resource metadata is malformed.

Implementations SHOULD report the offending resource and manifest location.

---

# 15. Future Extensions

Future versions of `elfrc` may support:

* Manifest inclusion.
* Resource groups.
* Build profiles.
* Conditional resources.
* Platform-specific resources.
* Variable substitution.
* Environment expansion.
* Digital signing.
* Incremental compilation.
* Resource dependency graphs.
* Generated resources.
* Custom preprocessing stages.

Such extensions SHALL NOT alter the binary layout defined by the ERF specification.
