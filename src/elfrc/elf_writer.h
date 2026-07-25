#ifndef ELFRC_ELF_WRITER_H
#define ELFRC_ELF_WRITER_H

#include "container.h"

/*
 * Writes `container` into a relocatable ELF64 object file at `output_path`,
 * inside a `.resource` section (SHT_PROGBITS, SHF_ALLOC, not writable, not
 * executable). Two global STT_OBJECT symbols bracket the section so
 * libelfr can locate it at link time without depending on section headers
 * being present/mapped at runtime:
 *
 *   symbol_prefix == NULL or "":  _elfr_resource_start / _elfr_resource_end
 *   symbol_prefix == "foo":       _elfr_foo_resource_start / _elfr_foo_resource_end
 *
 * Returns 0 on success, -1 on failure (errbuf is filled with a message).
 */
int elfrc_write_elf_object(const struct elfrc_container *container, const char *symbol_prefix,
                            const char *output_path, char errbuf[ELFRC_ERRBUF_SIZE]);

#endif /* ELFRC_ELF_WRITER_H */
