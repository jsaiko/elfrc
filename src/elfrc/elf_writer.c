#include "elf_writer.h"

#include <elf.h>
#include <stdio.h>
#include <string.h>

#include "../common/byte_buf.h"

#define SEC_NULL 0
#define SEC_RESOURCE 1
#define SEC_SYMTAB 2
#define SEC_STRTAB 3
#define SEC_SHSTRTAB 4
#define SEC_COUNT 5

int elfrc_write_elf_object(const struct elfrc_container *container, const char *symbol_prefix,
                            const char *output_path, char errbuf[ELFRC_ERRBUF_SIZE]) {
    errbuf[0] = '\0';

    char start_name[256], end_name[256];
    if (symbol_prefix && symbol_prefix[0] != '\0') {
        snprintf(start_name, sizeof(start_name), "_elfr_%s_resource_start", symbol_prefix);
        snprintf(end_name, sizeof(end_name), "_elfr_%s_resource_end", symbol_prefix);
    } else {
        snprintf(start_name, sizeof(start_name), "_elfr_resource_start");
        snprintf(end_name, sizeof(end_name), "_elfr_resource_end");
    }

    /* .shstrtab */
    struct byte_buf shstrtab = {0};
    byte_buf_append_zeros(&shstrtab, 1); /* index 0: empty string */
    size_t off_resource_name = byte_buf_append(&shstrtab, ".resource", strlen(".resource") + 1);
    size_t off_symtab_name = byte_buf_append(&shstrtab, ".symtab", strlen(".symtab") + 1);
    size_t off_strtab_name = byte_buf_append(&shstrtab, ".strtab", strlen(".strtab") + 1);
    size_t off_shstrtab_name = byte_buf_append(&shstrtab, ".shstrtab", strlen(".shstrtab") + 1);

    /* .strtab (symbol names) */
    struct byte_buf strtab = {0};
    byte_buf_append_zeros(&strtab, 1); /* index 0: empty string */
    size_t off_start_sym = byte_buf_append(&strtab, start_name, strlen(start_name) + 1);
    size_t off_end_sym = byte_buf_append(&strtab, end_name, strlen(end_name) + 1);

    /* .symtab: null symbol, then the two global boundary symbols. */
    Elf64_Sym syms[3];
    memset(syms, 0, sizeof(syms));
    syms[1].st_name = (Elf32_Word)off_start_sym;
    syms[1].st_info = (unsigned char)ELF64_ST_INFO(STB_GLOBAL, STT_OBJECT);
    syms[1].st_other = STV_DEFAULT;
    syms[1].st_shndx = SEC_RESOURCE;
    syms[1].st_value = 0;
    syms[1].st_size = 0;
    syms[2].st_name = (Elf32_Word)off_end_sym;
    syms[2].st_info = (unsigned char)ELF64_ST_INFO(STB_GLOBAL, STT_OBJECT);
    syms[2].st_other = STV_DEFAULT;
    syms[2].st_shndx = SEC_RESOURCE;
    syms[2].st_value = container->size;
    syms[2].st_size = 0;

    /* Lay out the file: Ehdr, .resource, .symtab, .strtab, .shstrtab, then
     * the section header table. */
    struct byte_buf file = {0};
    byte_buf_append_zeros(&file, sizeof(Elf64_Ehdr));

    byte_buf_align(&file, 16);
    size_t resource_off = byte_buf_append(&file, container->data, container->size);

    byte_buf_align(&file, 8);
    size_t symtab_off = byte_buf_append(&file, syms, sizeof(syms));

    byte_buf_align(&file, 1);
    size_t strtab_off = byte_buf_append(&file, strtab.data, strtab.len);

    size_t shstrtab_off = byte_buf_append(&file, shstrtab.data, shstrtab.len);

    byte_buf_align(&file, 8);
    size_t shoff = file.len;

    Elf64_Shdr shdrs[SEC_COUNT];
    memset(shdrs, 0, sizeof(shdrs));

    shdrs[SEC_RESOURCE].sh_name = (Elf32_Word)off_resource_name;
    shdrs[SEC_RESOURCE].sh_type = SHT_PROGBITS;
    shdrs[SEC_RESOURCE].sh_flags = SHF_ALLOC;
    shdrs[SEC_RESOURCE].sh_addr = 0;
    shdrs[SEC_RESOURCE].sh_offset = resource_off;
    shdrs[SEC_RESOURCE].sh_size = container->size;
    shdrs[SEC_RESOURCE].sh_link = 0;
    shdrs[SEC_RESOURCE].sh_info = 0;
    shdrs[SEC_RESOURCE].sh_addralign = 16;
    shdrs[SEC_RESOURCE].sh_entsize = 0;

    shdrs[SEC_SYMTAB].sh_name = (Elf32_Word)off_symtab_name;
    shdrs[SEC_SYMTAB].sh_type = SHT_SYMTAB;
    shdrs[SEC_SYMTAB].sh_flags = 0;
    shdrs[SEC_SYMTAB].sh_offset = symtab_off;
    shdrs[SEC_SYMTAB].sh_size = sizeof(syms);
    shdrs[SEC_SYMTAB].sh_link = SEC_STRTAB;
    shdrs[SEC_SYMTAB].sh_info = 1; /* index of first non-local (GLOBAL) symbol */
    shdrs[SEC_SYMTAB].sh_addralign = 8;
    shdrs[SEC_SYMTAB].sh_entsize = sizeof(Elf64_Sym);

    shdrs[SEC_STRTAB].sh_name = (Elf32_Word)off_strtab_name;
    shdrs[SEC_STRTAB].sh_type = SHT_STRTAB;
    shdrs[SEC_STRTAB].sh_offset = strtab_off;
    shdrs[SEC_STRTAB].sh_size = strtab.len;
    shdrs[SEC_STRTAB].sh_addralign = 1;

    shdrs[SEC_SHSTRTAB].sh_name = (Elf32_Word)off_shstrtab_name;
    shdrs[SEC_SHSTRTAB].sh_type = SHT_STRTAB;
    shdrs[SEC_SHSTRTAB].sh_offset = shstrtab_off;
    shdrs[SEC_SHSTRTAB].sh_size = shstrtab.len;
    shdrs[SEC_SHSTRTAB].sh_addralign = 1;

    byte_buf_append(&file, shdrs, sizeof(shdrs));

    Elf64_Ehdr ehdr;
    memset(&ehdr, 0, sizeof(ehdr));
    ehdr.e_ident[EI_MAG0] = ELFMAG0;
    ehdr.e_ident[EI_MAG1] = ELFMAG1;
    ehdr.e_ident[EI_MAG2] = ELFMAG2;
    ehdr.e_ident[EI_MAG3] = ELFMAG3;
    ehdr.e_ident[EI_CLASS] = ELFCLASS64;
    ehdr.e_ident[EI_DATA] = ELFDATA2LSB;
    ehdr.e_ident[EI_VERSION] = EV_CURRENT;
    ehdr.e_ident[EI_OSABI] = ELFOSABI_NONE;
    ehdr.e_ident[EI_ABIVERSION] = 0;
    ehdr.e_type = ET_REL;
    ehdr.e_machine = EM_X86_64;
    ehdr.e_version = EV_CURRENT;
    ehdr.e_entry = 0;
    ehdr.e_phoff = 0;
    ehdr.e_shoff = shoff;
    ehdr.e_flags = 0;
    ehdr.e_ehsize = sizeof(Elf64_Ehdr);
    ehdr.e_phentsize = 0;
    ehdr.e_phnum = 0;
    ehdr.e_shentsize = sizeof(Elf64_Shdr);
    ehdr.e_shnum = SEC_COUNT;
    ehdr.e_shstrndx = SEC_SHSTRTAB;

    memcpy(file.data, &ehdr, sizeof(ehdr));

    FILE *out = fopen(output_path, "wb");
    if (!out) {
        snprintf(errbuf, ELFRC_ERRBUF_SIZE, "cannot open '%s' for writing", output_path);
        byte_buf_free(&file);
        byte_buf_free(&strtab);
        byte_buf_free(&shstrtab);
        return -1;
    }
    size_t file_len = file.len;
    size_t written = fwrite(file.data, 1, file_len, out);
    fclose(out);
    byte_buf_free(&file);
    byte_buf_free(&strtab);
    byte_buf_free(&shstrtab);
    if (written != file_len) {
        snprintf(errbuf, ELFRC_ERRBUF_SIZE, "short write to '%s'", output_path);
        return -1;
    }
    return 0;
}
