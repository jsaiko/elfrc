# ELFR toolchain: elfrc + libelfr

An implementation of the **ELF Resource Format (ERF)** — see `docs/` for
the two governing specifications:

- `docs/ELF Resource Format Specification.md` — the on-disk container format
- `docs/elfrc Compiler Specification.md` — the manifest-driven compiler

This repo provides:

- **`elfrc`** — compiles a YAML resource manifest into a relocatable ELF64
  object containing a `.resource` section (ERF container).
- **`libelfr`** — a C runtime library (static `libelfr.a` and shared
  `libelfr.so`) for reading resources back out of a running binary.
- **`elfresdump`** — lists or extracts resources from an already-built ELF
  object, executable, or shared library.

## v1 scope

ELF64 little-endian / x86-64 only. Manifests are YAML (via libyaml).
Compression and encryption manifest fields only accept `none` — the
container format defines the other algorithm IDs, but this version doesn't
implement them, and will fail the build if you request one.

## Building

Requires CMake, a C11 compiler, and `libyaml` (`apt install libyaml-dev` /
`dnf install libyaml-devel` / `brew install libyaml`).

```bash
cmake -B build
cmake --build build
ctest --test-dir build --output-on-failure
```

## Using elfrc

```bash
elfrc resources.elfrc -o resources.o
gcc main.c resources.o -o app -I/path/to/include -L/path/to/lib -lelfr
```

Manifest example:

```yaml
version: 1

defaults:
    language: neutral
    compression: none

resources:
  - name: logo
    file: assets/logo.png
    type: png
    id: 100
    metadata:
        Author: Jane Smith
        License: MIT

  - name: strings
    language: en-US
    file: locale/en.json

  - name: strings
    language: fr-FR
    file: locale/fr.json
```

See `examples/basic/` for a complete manifest + assets + a `main.c` that
reads them back with libelfr.

## Using libelfr

For the common case (one `elfrc`-built object linked into the binary, no
`--symbol-prefix`), the convenience API just works:

```c
#include "elfr/elfr.h"

const void *data = erf_resource_data("logo", &size);
const ERFResource *r = erf_find("config");
const ERFResource *by_id = erf_find_id(100);
```

For multiple `elfrc`-built objects in one binary, build each with a
distinct `elfrc --symbol-prefix NAME`, then use the explicit-handle API:

```c
ELFR_DECLARE(myresources); // at file scope

ERFContainer *c = erf_open_memory(ELFR_START(myresources), ELFR_SIZE(myresources));
const ERFResource *r = erf_find_in(c, "logo");
```

Resource lookups are zero-copy: `ERFResource.data` points directly into
the linked `.resource` section.

## How the runtime finds `.resource`

ELF section headers aren't guaranteed to be mapped at runtime, so
`elfrc` doesn't rely on them being readable from a running process.
Instead it writes two global `STT_OBJECT` symbols bracketing the section
(`_elfr_resource_start` / `_elfr_resource_end`, or `_elfr_<prefix>_..." with
`--symbol-prefix`). These resolve entirely at link time, so they work
under static linking and survive `strip`. `elfresdump`, by contrast, reads
files on disk and so parses the section header table directly.

## Not implemented (see docs for the full spec)

- Compression / encryption (container fields exist; only `none` is wired up)
- Signature table verification (raw bytes are preserved if present, unverified)
- `elfresedit` (in-place resource updates)
- 32-bit / cross-endian ELF, non-YAML manifests (JSON/TOML/XML)
