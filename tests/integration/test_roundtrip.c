/*
 * End-to-end integration test: elfrc compiles examples/basic/resources.elfrc
 * into an ELF object, which this program is linked against together with
 * libelfr. Verifies that data read back through libelfr matches the
 * original source files byte-for-byte, and exercises both the default
 * convenience API and the explicit-handle API (including metadata).
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "elfr/elfr.h"

/* Provided by the elfrc-built object linked into this test binary. */
extern const char _elfr_resource_start[];
extern const char _elfr_resource_end[];

#define CHECK(cond)                                                              \
    do {                                                                         \
        if (!(cond)) {                                                           \
            fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);      \
            return 1;                                                            \
        }                                                                        \
    } while (0)

static int read_file(const char *path, unsigned char **out, size_t *out_size) {
    FILE *f = fopen(path, "rb");
    if (!f) {
        return -1;
    }
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    *out = malloc(sz > 0 ? (size_t)sz : 1);
    if (fread(*out, 1, (size_t)sz, f) != (size_t)sz) {
        fclose(f);
        return -1;
    }
    fclose(f);
    *out_size = (size_t)sz;
    return 0;
}

static int check_matches_file(const char *examples_dir, const char *relpath, const void *data, size_t size) {
    char path[1024];
    snprintf(path, sizeof(path), "%s/%s", examples_dir, relpath);
    unsigned char *filedata;
    size_t filesize;
    CHECK(read_file(path, &filedata, &filesize) == 0);
    CHECK(filesize == size);
    CHECK(memcmp(filedata, data, size) == 0);
    free(filedata);
    return 0;
}

int main(int argc, char **argv) {
    CHECK(argc == 2);
    const char *examples_dir = argv[1];

    /* ---- default convenience API ---- */
    size_t logo_size;
    const void *logo = erf_resource_data("logo", &logo_size);
    CHECK(logo != NULL);
    CHECK(check_matches_file(examples_dir, "assets/logo.png", logo, logo_size) == 0);

    const ERFResource *config = erf_find("config");
    CHECK(config != NULL);
    CHECK(check_matches_file(examples_dir, "assets/config.json", config->data, config->size) == 0);

    const ERFResource *by_id = erf_find_id(100);
    CHECK(by_id != NULL);
    CHECK(strcmp(by_id->name, "logo") == 0);

    CHECK(erf_find("this-resource-does-not-exist") == NULL);
    CHECK(erf_last_error() == ERF_ERR_NOT_FOUND);

    const ERFResource *shader = erf_find("shader/main");
    CHECK(shader != NULL);
    CHECK(check_matches_file(examples_dir, "assets/shaders/main.spv", shader->data, shader->size) == 0);

    const ERFResource *font = erf_find("font/default");
    CHECK(font != NULL);
    CHECK(check_matches_file(examples_dir, "assets/font/Roboto.ttf", font->data, font->size) == 0);

    /* ---- explicit-handle API + metadata ---- */
    size_t total_size = (size_t)(_elfr_resource_end - _elfr_resource_start);
    ERFContainer *c = erf_open_memory(_elfr_resource_start, total_size);
    CHECK(c != NULL);
    CHECK(erf_verify_checksum(c));
    CHECK(erf_resource_count(c) == 6);

    const ERFResource *logo2 = erf_find_in(c, "logo");
    CHECK(logo2 != NULL);
    int mcount = erf_metadata_count(c, logo2);
    CHECK(mcount == 2);
    int saw_author = 0, saw_license = 0;
    for (int i = 0; i < mcount; i++) {
        const char *key, *value;
        CHECK(erf_metadata_get(c, logo2, i, &key, &value) == 0);
        if (strcmp(key, "Author") == 0) {
            CHECK(strcmp(value, "ELFR Example") == 0);
            saw_author = 1;
        } else if (strcmp(key, "License") == 0) {
            CHECK(strcmp(value, "MIT") == 0);
            saw_license = 1;
        }
    }
    CHECK(saw_author && saw_license);

    /* Localized resources: both language variants of "strings" must be
     * independently reachable by walking the directory, even though
     * erf_find() alone only returns one of them for an ambiguous name. */
    int saw_en = 0, saw_fr = 0;
    for (uint32_t i = 0; i < erf_resource_count(c); i++) {
        const ERFResource *r = erf_resource_at(c, i);
        if (strcmp(r->name, "strings") == 0) {
            if (r->language == 1033) {
                saw_en = 1;
                CHECK(check_matches_file(examples_dir, "assets/locale/en.json", r->data, r->size) == 0);
            } else if (r->language == 1036) {
                saw_fr = 1;
                CHECK(check_matches_file(examples_dir, "assets/locale/fr.json", r->data, r->size) == 0);
            }
        }
    }
    CHECK(saw_en && saw_fr);

    erf_close(c);

    printf("test_roundtrip: OK\n");
    return 0;
}
