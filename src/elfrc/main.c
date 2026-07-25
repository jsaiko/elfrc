#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "container.h"
#include "elf_writer.h"
#include "manifest.h"

static void usage(const char *prog) {
    fprintf(stderr,
            "usage: %s <manifest.elfrc> [-o output.o] [--symbol-prefix NAME]\n"
            "\n"
            "Compiles an ERF resource manifest into a relocatable ELF object\n"
            "containing a .resource section, per the ELFR Resource Format spec.\n",
            prog);
}

static char *derive_output_name(const char *manifest_path) {
    const char *base = strrchr(manifest_path, '/');
    base = base ? base + 1 : manifest_path;
    const char *dot = strrchr(base, '.');
    size_t stem_len = dot ? (size_t)(dot - base) : strlen(base);
    char *out = malloc(stem_len + 3);
    memcpy(out, base, stem_len);
    out[stem_len] = '.';
    out[stem_len + 1] = 'o';
    out[stem_len + 2] = '\0';
    return out;
}

int main(int argc, char **argv) {
    const char *manifest_path = NULL;
    const char *output_path = NULL;
    const char *symbol_prefix = NULL;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-o") == 0 && i + 1 < argc) {
            output_path = argv[++i];
        } else if (strcmp(argv[i], "--symbol-prefix") == 0 && i + 1 < argc) {
            symbol_prefix = argv[++i];
        } else if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            usage(argv[0]);
            return 0;
        } else if (argv[i][0] == '-') {
            fprintf(stderr, "%s: unknown option '%s'\n", argv[0], argv[i]);
            usage(argv[0]);
            return 1;
        } else if (!manifest_path) {
            manifest_path = argv[i];
        } else {
            fprintf(stderr, "%s: unexpected argument '%s'\n", argv[0], argv[i]);
            usage(argv[0]);
            return 1;
        }
    }

    if (!manifest_path) {
        usage(argv[0]);
        return 1;
    }

    char errbuf[ELFRC_ERRBUF_SIZE];
    struct elfrc_manifest manifest;
    if (elfrc_manifest_parse(manifest_path, &manifest, errbuf) != 0) {
        fprintf(stderr, "%s\n", errbuf);
        return 1;
    }

    if (!symbol_prefix) {
        symbol_prefix = manifest.module;
    }

    struct elfrc_container container;
    if (elfrc_container_build(&manifest, &container, errbuf) != 0) {
        fprintf(stderr, "%s\n", errbuf);
        elfrc_manifest_free(&manifest);
        return 1;
    }

    char *derived_output = NULL;
    if (!output_path) {
        derived_output = derive_output_name(manifest_path);
        output_path = derived_output;
    }

    int rc = elfrc_write_elf_object(&container, symbol_prefix, output_path, errbuf);
    if (rc != 0) {
        fprintf(stderr, "%s\n", errbuf);
    } else {
        fprintf(stderr, "elfrc: wrote %s (%zu bytes, %zu resource%s)\n", output_path, container.size,
                manifest.resource_count, manifest.resource_count == 1 ? "" : "s");
    }

    free(derived_output);
    elfrc_container_free(&container);
    elfrc_manifest_free(&manifest);
    return rc == 0 ? 0 : 1;
}
