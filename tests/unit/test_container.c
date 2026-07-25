#include "../../src/elfrc/container.h"
#include "../../src/elfrc/manifest.h"
#include "../../src/common/crc64.h"
#include "test_util.h"

#include "elfr/erf_format.h"

#include <stdio.h>
#include <unistd.h>

static char *write_temp_file(const char *dir, const char *name, const char *content) {
    static char path[512];
    snprintf(path, sizeof(path), "%s/%s", dir, name);
    FILE *f = fopen(path, "wb");
    CHECK(f != NULL);
    fwrite(content, 1, strlen(content), f);
    fclose(f);
    return strdup(path);
}

static struct elfrc_resource make_resource(const char *name, const char *file, uint32_t language,
                                            uint32_t final_id) {
    struct elfrc_resource r;
    memset(&r, 0, sizeof(r));
    r.name = strdup(name);
    r.file = strdup(file);
    r.type = ERF_TYPE_STRING;
    r.language = language;
    r.compression = ERF_COMPRESSION_NONE;
    r.final_id = final_id;
    return r;
}

int main(void) {
    char tmpl[] = "/tmp/elfrc_test_container_XXXXXX";
    char *dir = mkdtemp(tmpl);
    CHECK(dir != NULL);

    char *file_en = write_temp_file(dir, "en.txt", "hello");
    char *file_fr = write_temp_file(dir, "fr.txt", "bonjour");

    struct elfrc_manifest m;
    memset(&m, 0, sizeof(m));
    m.version = 1;
    m.resource_count = 2;
    m.resources = calloc(2, sizeof(struct elfrc_resource));
    m.resources[0] = make_resource("greeting", file_en, 1033, 1);
    m.resources[1] = make_resource("greeting", file_fr, 1036, 2);

    char err[ELFRC_ERRBUF_SIZE];
    struct elfrc_container c1;
    CHECK(elfrc_container_build(&m, &c1, err) == 0);

    ERFHeader hdr;
    memcpy(&hdr, c1.data, sizeof(hdr));
    CHECK(memcmp(hdr.magic, ERF_MAGIC, 4) == 0);
    CHECK(hdr.resource_count == 2);
    CHECK(hdr.total_size == c1.size);

    ERFDirectoryEntry entries[2];
    memcpy(entries, c1.data + hdr.directory_offset, sizeof(entries));
    /* Same logical name -> must dedup to the same string table offset. */
    CHECK(entries[0].name_offset == entries[1].name_offset);
    /* Different language variants -> different data. */
    CHECK(entries[0].data_offset != entries[1].data_offset);
    CHECK(entries[0].data_size == 5);  /* "hello" */
    CHECK(entries[1].data_size == 7);  /* "bonjour" */
    CHECK(entries[0].hash == entries[1].hash); /* hash is name-only */

    /* Determinism: rebuilding from equivalent input must be byte-identical. */
    struct elfrc_container c2;
    CHECK(elfrc_container_build(&m, &c2, err) == 0);
    CHECK(c1.size == c2.size);
    CHECK(memcmp(c1.data, c2.data, c1.size) == 0);

    /* Header checksum must validate against a from-scratch recomputation
     * with the checksum field zeroed. */
    unsigned char *scratch = malloc(c1.size);
    memcpy(scratch, c1.data, c1.size);
    ((ERFHeader *)scratch)->checksum = 0;
    CHECK(elfr_crc64(scratch, c1.size) == hdr.checksum);
    free(scratch);

    elfrc_container_free(&c1);
    elfrc_container_free(&c2);
    free(m.resources[0].name);
    free(m.resources[0].file);
    free(m.resources[1].name);
    free(m.resources[1].file);
    free(m.resources);
    free(file_en);
    free(file_fr);

    printf("test_container: OK\n");
    return 0;
}
