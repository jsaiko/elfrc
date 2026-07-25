/*
 * elfresdump: lists or extracts resources from an ERF `.resource` section
 * inside an ELF object/executable/shared library on disk. Unlike libelfr
 * (which locates .resource in a *running* process via link-time symbols),
 * this tool reads the file directly, so it can freely walk the ELF section
 * header table to find .resource by name.
 */
#include <elf.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#include "elfr/elfr.h"

static const char *type_name(uint32_t type) {
    static const char *names[] = {"binary", "string",  "json",  "xml",  "yaml",        "png",  "jpeg",
                                   "svg",    "gif",     "icon",  "font", "audio",       "video", "shader",
                                   "certificate"};
    if (type < sizeof(names) / sizeof(names[0])) {
        return names[type];
    }
    return "application-defined";
}

static void usage(const char *prog) {
    fprintf(stderr,
            "usage: %s <elf-file> [--extract NAME -o OUTFILE]\n"
            "\n"
            "Lists (or extracts) resources from the .resource ERF section of an\n"
            "ELF object, executable, or shared library built by elfrc.\n",
            prog);
}

int main(int argc, char **argv) {
    if (argc < 2) {
        usage(argv[0]);
        return 1;
    }
    const char *file_path = argv[1];
    const char *extract_name = NULL;
    const char *output_path = NULL;
    for (int i = 2; i < argc; i++) {
        if (strcmp(argv[i], "--extract") == 0 && i + 1 < argc) {
            extract_name = argv[++i];
        } else if (strcmp(argv[i], "-o") == 0 && i + 1 < argc) {
            output_path = argv[++i];
        } else if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            usage(argv[0]);
            return 0;
        } else {
            fprintf(stderr, "%s: unknown argument '%s'\n", argv[0], argv[i]);
            usage(argv[0]);
            return 1;
        }
    }
    if (extract_name && !output_path) {
        fprintf(stderr, "%s: --extract requires -o OUTFILE\n", argv[0]);
        return 1;
    }

    int fd = open(file_path, O_RDONLY);
    if (fd < 0) {
        fprintf(stderr, "%s: cannot open '%s': %s\n", argv[0], file_path, strerror(errno));
        return 1;
    }
    struct stat st;
    if (fstat(fd, &st) != 0) {
        fprintf(stderr, "%s: fstat failed on '%s'\n", argv[0], file_path);
        close(fd);
        return 1;
    }
    void *map = mmap(NULL, (size_t)st.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
    close(fd);
    if (map == MAP_FAILED) {
        fprintf(stderr, "%s: mmap failed on '%s'\n", argv[0], file_path);
        return 1;
    }
    size_t file_size = (size_t)st.st_size;
    const unsigned char *base = (const unsigned char *)map;

    if (file_size < sizeof(Elf64_Ehdr) || memcmp(base, ELFMAG, SELFMAG) != 0) {
        fprintf(stderr, "%s: '%s' is not an ELF file\n", argv[0], file_path);
        munmap(map, file_size);
        return 1;
    }
    const Elf64_Ehdr *ehdr = (const Elf64_Ehdr *)base;
    if (ehdr->e_ident[EI_CLASS] != ELFCLASS64 || ehdr->e_ident[EI_DATA] != ELFDATA2LSB) {
        fprintf(stderr, "%s: only 64-bit little-endian ELF files are supported\n", argv[0]);
        munmap(map, file_size);
        return 1;
    }
    if (ehdr->e_shoff == 0 || ehdr->e_shnum == 0 || ehdr->e_shoff + (uint64_t)ehdr->e_shnum * ehdr->e_shentsize >
                                                        file_size) {
        fprintf(stderr, "%s: '%s' has no usable section header table\n", argv[0], file_path);
        munmap(map, file_size);
        return 1;
    }
    const Elf64_Shdr *shdrs = (const Elf64_Shdr *)(base + ehdr->e_shoff);
    const Elf64_Shdr *shstrtab_hdr = &shdrs[ehdr->e_shstrndx];
    const char *shstrtab = (const char *)(base + shstrtab_hdr->sh_offset);

    const Elf64_Shdr *resource_hdr = NULL;
    for (int i = 0; i < ehdr->e_shnum; i++) {
        const char *name = shstrtab + shdrs[i].sh_name;
        if (strcmp(name, ".resource") == 0) {
            resource_hdr = &shdrs[i];
            break;
        }
    }
    if (!resource_hdr) {
        fprintf(stderr, "%s: '%s' has no .resource section\n", argv[0], file_path);
        munmap(map, file_size);
        return 1;
    }

    ERFContainer *container = erf_open_memory(base + resource_hdr->sh_offset, resource_hdr->sh_size);
    if (!container) {
        fprintf(stderr, "%s: '%s': invalid ERF container: %s\n", argv[0], file_path,
                erf_strerror(erf_last_error()));
        munmap(map, file_size);
        return 1;
    }

    int rc = 0;
    if (extract_name) {
        const ERFResource *r = erf_find_in(container, extract_name);
        if (!r) {
            fprintf(stderr, "%s: resource '%s' not found\n", argv[0], extract_name);
            rc = 1;
        } else {
            FILE *out = fopen(output_path, "wb");
            if (!out || fwrite(r->data, 1, r->size, out) != r->size) {
                fprintf(stderr, "%s: failed to write '%s'\n", argv[0], output_path);
                rc = 1;
            }
            if (out) {
                fclose(out);
            }
        }
    } else {
        printf("%-6s %-24s %-12s %-8s %10s\n", "ID", "NAME", "TYPE", "LANG", "SIZE");
        for (uint32_t i = 0; i < erf_resource_count(container); i++) {
            const ERFResource *r = erf_resource_at(container, i);
            printf("%-6u %-24s %-12s %-8u %10zu\n", r->id, r->name, type_name(r->type), r->language, r->size);
        }
    }

    erf_close(container);
    munmap(map, file_size);
    return rc;
}
