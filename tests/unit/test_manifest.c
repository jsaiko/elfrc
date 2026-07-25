#include "../../src/elfrc/manifest.h"
#include "test_util.h"

#include <stdio.h>
#include <unistd.h>

static char g_tmpdir[256];

static void write_file(const char *relname, const char *content) {
    char path[512];
    snprintf(path, sizeof(path), "%s/%s", g_tmpdir, relname);
    FILE *f = fopen(path, "wb");
    CHECK(f != NULL);
    fwrite(content, 1, strlen(content), f);
    fclose(f);
}

static char *manifest_path(const char *relname) {
    static char path[512];
    snprintf(path, sizeof(path), "%s/%s", g_tmpdir, relname);
    return path;
}

int main(void) {
    char tmpl[] = "/tmp/elfrc_test_manifest_XXXXXX";
    CHECK(mkdtemp(tmpl) != NULL);
    snprintf(g_tmpdir, sizeof(g_tmpdir), "%s", tmpl);

    write_file("logo.png", "PNGDATA");
    write_file("config.json", "{}");

    /* 1. Basic parse: type inference from extension, explicit id kept,
     * auto id assigned to the other entry without colliding. */
    write_file("basic.elfrc",
                "version: 1\n"
                "resources:\n"
                "  - name: logo\n"
                "    file: logo.png\n"
                "    id: 100\n"
                "  - name: config\n"
                "    file: config.json\n");
    {
        struct elfrc_manifest m;
        char err[ELFRC_ERRBUF_SIZE];
        CHECK(elfrc_manifest_parse(manifest_path("basic.elfrc"), &m, err) == 0);
        CHECK(m.resource_count == 2);
        CHECK_STREQ(m.resources[0].name, "logo");
        CHECK(m.resources[0].type == 5 /* ERF_TYPE_PNG */);
        CHECK(m.resources[0].final_id == 100);
        CHECK_STREQ(m.resources[1].name, "config");
        CHECK(m.resources[1].type == 2 /* ERF_TYPE_JSON */);
        CHECK(m.resources[1].final_id != 100);
        elfrc_manifest_free(&m);
    }

    /* 2. defaults inheritance + per-resource override. */
    write_file("defaults.elfrc",
                "version: 1\n"
                "defaults:\n"
                "  language: fr-FR\n"
                "resources:\n"
                "  - name: a\n"
                "    file: config.json\n"
                "  - name: b\n"
                "    file: config.json\n"
                "    language: en-US\n");
    {
        struct elfrc_manifest m;
        char err[ELFRC_ERRBUF_SIZE];
        CHECK(elfrc_manifest_parse(manifest_path("defaults.elfrc"), &m, err) == 0);
        CHECK(m.resources[0].language == 1036 /* fr-FR default */);
        CHECK(m.resources[1].language == 1033 /* en-US override */);
        elfrc_manifest_free(&m);
    }

    /* 3. Localization: same name, different language is allowed. */
    write_file("localized.elfrc",
                "version: 1\n"
                "resources:\n"
                "  - name: strings\n"
                "    language: en-US\n"
                "    file: config.json\n"
                "  - name: strings\n"
                "    language: fr-FR\n"
                "    file: config.json\n");
    {
        struct elfrc_manifest m;
        char err[ELFRC_ERRBUF_SIZE];
        CHECK(elfrc_manifest_parse(manifest_path("localized.elfrc"), &m, err) == 0);
        CHECK(m.resource_count == 2);
        elfrc_manifest_free(&m);
    }

    /* 4. Duplicate (name, language) pair must fail. */
    write_file("dup_name_lang.elfrc",
                "version: 1\n"
                "resources:\n"
                "  - name: a\n"
                "    file: config.json\n"
                "  - name: a\n"
                "    file: config.json\n");
    {
        struct elfrc_manifest m;
        char err[ELFRC_ERRBUF_SIZE];
        CHECK(elfrc_manifest_parse(manifest_path("dup_name_lang.elfrc"), &m, err) != 0);
        CHECK(strstr(err, "duplicate") != NULL);
    }

    /* 5. Duplicate explicit id must fail. */
    write_file("dup_id.elfrc",
                "version: 1\n"
                "resources:\n"
                "  - name: a\n"
                "    file: config.json\n"
                "    id: 5\n"
                "  - name: b\n"
                "    file: config.json\n"
                "    id: 5\n");
    {
        struct elfrc_manifest m;
        char err[ELFRC_ERRBUF_SIZE];
        CHECK(elfrc_manifest_parse(manifest_path("dup_id.elfrc"), &m, err) != 0);
        CHECK(strstr(err, "duplicate") != NULL);
    }

    /* 6. Missing file must fail. */
    write_file("missing_file.elfrc",
                "version: 1\n"
                "resources:\n"
                "  - name: a\n"
                "    file: does_not_exist.bin\n");
    {
        struct elfrc_manifest m;
        char err[ELFRC_ERRBUF_SIZE];
        CHECK(elfrc_manifest_parse(manifest_path("missing_file.elfrc"), &m, err) != 0);
    }

    /* 7. Unsupported compression algorithm must fail (v1 only implements
     * 'none'). */
    write_file("bad_compression.elfrc",
                "version: 1\n"
                "resources:\n"
                "  - name: a\n"
                "    file: config.json\n"
                "    compression: deflate\n");
    {
        struct elfrc_manifest m;
        char err[ELFRC_ERRBUF_SIZE];
        CHECK(elfrc_manifest_parse(manifest_path("bad_compression.elfrc"), &m, err) != 0);
        CHECK(strstr(err, "compression") != NULL);
    }

    /* 8. Missing required 'file' field must fail. */
    write_file("missing_field.elfrc",
                "version: 1\n"
                "resources:\n"
                "  - name: a\n");
    {
        struct elfrc_manifest m;
        char err[ELFRC_ERRBUF_SIZE];
        CHECK(elfrc_manifest_parse(manifest_path("missing_field.elfrc"), &m, err) != 0);
    }

    printf("test_manifest: OK\n");
    return 0;
}
