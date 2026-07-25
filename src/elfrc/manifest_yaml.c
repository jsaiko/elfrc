#include "manifest.h"

#include <ctype.h>
#include <errno.h>
#include <libgen.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <yaml.h>

#include "../../include/elfr/erf_format.h"
#include "../common/fnv1a.h"

struct defaults {
    char *language;
    char *compression;
    char *encryption;
};

struct parse_ctx {
    yaml_document_t *doc;
    const char *manifest_path;
    char *manifest_dir; /* owned */
    char errbuf_local[ELFRC_ERRBUF_SIZE];
    int failed;
};

static void set_error(struct parse_ctx *ctx, int line, const char *fmt, ...) {
    if (ctx->failed) {
        return; /* keep first error */
    }
    ctx->failed = 1;
    char msg[400];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(msg, sizeof(msg), fmt, ap);
    va_end(ap);
    if (line > 0) {
        snprintf(ctx->errbuf_local, ELFRC_ERRBUF_SIZE, "%s:%d: error: %s", ctx->manifest_path, line, msg);
    } else {
        snprintf(ctx->errbuf_local, ELFRC_ERRBUF_SIZE, "%s: error: %s", ctx->manifest_path, msg);
    }
}

static yaml_node_t *node_at(struct parse_ctx *ctx, int id) {
    if (id == 0) {
        return NULL;
    }
    return yaml_document_get_node(ctx->doc, id);
}

static int node_line(yaml_node_t *node) {
    return node ? (int)(node->start_mark.line + 1) : 0;
}

static char *scalar_dup(yaml_node_t *node) {
    if (!node || node->type != YAML_SCALAR_NODE) {
        return NULL;
    }
    return strndup((const char *)node->data.scalar.value, node->data.scalar.length);
}

static yaml_node_t *map_get(struct parse_ctx *ctx, yaml_node_t *map, const char *key) {
    if (!map || map->type != YAML_MAPPING_NODE) {
        return NULL;
    }
    for (yaml_node_pair_t *pair = map->data.mapping.pairs.start; pair < map->data.mapping.pairs.top; pair++) {
        yaml_node_t *k = node_at(ctx, pair->key);
        if (k && k->type == YAML_SCALAR_NODE &&
            strncmp((const char *)k->data.scalar.value, key, k->data.scalar.length) == 0 &&
            strlen(key) == k->data.scalar.length) {
            return node_at(ctx, pair->value);
        }
    }
    return NULL;
}

static int parse_uint(const char *s, uint32_t *out) {
    if (!s || !*s) {
        return -1;
    }
    char *end;
    long v = strtol(s, &end, 10);
    if (*end != '\0' || v < 0) {
        return -1;
    }
    *out = (uint32_t)v;
    return 0;
}

/* ---- lookup tables ---- */

static int type_from_name(const char *name, uint32_t *out) {
    static const struct { const char *name; uint32_t type; } table[] = {
        {"binary", ERF_TYPE_BINARY},       {"string", ERF_TYPE_STRING}, {"json", ERF_TYPE_JSON},
        {"xml", ERF_TYPE_XML},             {"yaml", ERF_TYPE_YAML},     {"png", ERF_TYPE_PNG},
        {"jpeg", ERF_TYPE_JPEG},           {"jpg", ERF_TYPE_JPEG},      {"svg", ERF_TYPE_SVG},
        {"gif", ERF_TYPE_GIF},             {"icon", ERF_TYPE_ICON},     {"ico", ERF_TYPE_ICON},
        {"font", ERF_TYPE_FONT},           {"ttf", ERF_TYPE_FONT},      {"audio", ERF_TYPE_AUDIO},
        {"video", ERF_TYPE_VIDEO},         {"shader", ERF_TYPE_SHADER}, {"certificate", ERF_TYPE_CERTIFICATE},
        {"cert", ERF_TYPE_CERTIFICATE},
    };
    for (size_t i = 0; i < sizeof(table) / sizeof(table[0]); i++) {
        if (strcasecmp(name, table[i].name) == 0) {
            *out = table[i].type;
            return 0;
        }
    }
    return -1;
}

static uint32_t type_from_extension(const char *filename) {
    const char *dot = strrchr(filename, '.');
    if (!dot) {
        return ERF_TYPE_BINARY;
    }
    static const struct { const char *ext; uint32_t type; } table[] = {
        {".png", ERF_TYPE_PNG},   {".jpg", ERF_TYPE_JPEG},   {".jpeg", ERF_TYPE_JPEG}, {".svg", ERF_TYPE_SVG},
        {".gif", ERF_TYPE_GIF},   {".ico", ERF_TYPE_ICON},   {".ttf", ERF_TYPE_FONT},  {".otf", ERF_TYPE_FONT},
        {".json", ERF_TYPE_JSON}, {".xml", ERF_TYPE_XML},    {".yaml", ERF_TYPE_YAML}, {".yml", ERF_TYPE_YAML},
        {".wav", ERF_TYPE_AUDIO}, {".mp3", ERF_TYPE_AUDIO},  {".ogg", ERF_TYPE_AUDIO}, {".mp4", ERF_TYPE_VIDEO},
        {".webm", ERF_TYPE_VIDEO},{".spv", ERF_TYPE_SHADER}, {".glsl", ERF_TYPE_SHADER}, {".hlsl", ERF_TYPE_SHADER},
        {".pem", ERF_TYPE_CERTIFICATE}, {".crt", ERF_TYPE_CERTIFICATE}, {".cer", ERF_TYPE_CERTIFICATE},
        {".txt", ERF_TYPE_STRING},
    };
    for (size_t i = 0; i < sizeof(table) / sizeof(table[0]); i++) {
        if (strcasecmp(dot, table[i].ext) == 0) {
            return table[i].type;
        }
    }
    return ERF_TYPE_BINARY;
}

static int compression_from_name(const char *name, uint32_t *out) {
    static const struct { const char *name; uint32_t id; } table[] = {
        {"none", ERF_COMPRESSION_NONE}, {"deflate", ERF_COMPRESSION_DEFLATE},
        {"zstd", ERF_COMPRESSION_ZSTD}, {"zstandard", ERF_COMPRESSION_ZSTD},
        {"lz4", ERF_COMPRESSION_LZ4},   {"brotli", ERF_COMPRESSION_BROTLI},
        {"xz", ERF_COMPRESSION_XZ},
    };
    for (size_t i = 0; i < sizeof(table) / sizeof(table[0]); i++) {
        if (strcasecmp(name, table[i].name) == 0) {
            *out = table[i].id;
            return 0;
        }
    }
    return -1;
}

static int flag_from_name(const char *name, uint32_t *out) {
    static const struct { const char *name; uint32_t bit; } table[] = {
        {"compressed", ERF_RFLAG_COMPRESSED}, {"encrypted", ERF_RFLAG_ENCRYPTED},
        {"read_only", ERF_RFLAG_READ_ONLY},   {"readonly", ERF_RFLAG_READ_ONLY},
        {"executable", ERF_RFLAG_EXECUTABLE}, {"optional", ERF_RFLAG_OPTIONAL},
    };
    for (size_t i = 0; i < sizeof(table) / sizeof(table[0]); i++) {
        if (strcasecmp(name, table[i].name) == 0) {
            *out = table[i].bit;
            return 0;
        }
    }
    return -1;
}

static uint32_t language_from_tag(const char *tag) {
    if (!tag || !*tag || strcasecmp(tag, "neutral") == 0) {
        return ERF_LANGUAGE_NEUTRAL;
    }
    static const struct { const char *tag; uint32_t id; } table[] = {
        {"en-us", 1033}, {"en_us", 1033}, {"en", 1033},
        {"de-de", 1031}, {"de_de", 1031}, {"de", 1031},
        {"fr-fr", 1036}, {"fr_fr", 1036}, {"fr", 1036},
    };
    for (size_t i = 0; i < sizeof(table) / sizeof(table[0]); i++) {
        if (strcasecmp(tag, table[i].tag) == 0) {
            return table[i].id;
        }
    }
    char lower[128];
    size_t n = strlen(tag);
    if (n >= sizeof(lower)) {
        n = sizeof(lower) - 1;
    }
    for (size_t i = 0; i < n; i++) {
        lower[i] = (char)tolower((unsigned char)tag[i]);
    }
    lower[n] = '\0';
    uint32_t h = elfr_fnv1a32(lower, n);
    return h == 0 ? 1 : h;
}

/* ---- resource array growth ---- */

static struct elfrc_resource *push_resource(struct elfrc_manifest *m) {
    m->resources = realloc(m->resources, (m->resource_count + 1) * sizeof(*m->resources));
    struct elfrc_resource *r = &m->resources[m->resource_count++];
    memset(r, 0, sizeof(*r));
    return r;
}

/* ---- per-resource parsing ---- */

static void parse_metadata(struct parse_ctx *ctx, yaml_node_t *meta_map, struct elfrc_resource *r) {
    if (!meta_map) {
        return;
    }
    if (meta_map->type != YAML_MAPPING_NODE) {
        set_error(ctx, node_line(meta_map), "resource '%s': 'metadata' must be a mapping", r->name);
        return;
    }
    for (yaml_node_pair_t *pair = meta_map->data.mapping.pairs.start; pair < meta_map->data.mapping.pairs.top; pair++) {
        yaml_node_t *kn = node_at(ctx, pair->key);
        yaml_node_t *vn = node_at(ctx, pair->value);
        char *key = scalar_dup(kn);
        char *val = scalar_dup(vn);
        if (!key || !val) {
            set_error(ctx, node_line(meta_map), "resource '%s': metadata keys/values must be scalars", r->name);
            free(key);
            free(val);
            return;
        }
        for (size_t i = 0; i < r->metadata_count; i++) {
            if (strcmp(r->metadata[i].key, key) == 0) {
                set_error(ctx, node_line(meta_map), "resource '%s': duplicate metadata key '%s'", r->name, key);
                free(key);
                free(val);
                return;
            }
        }
        r->metadata = realloc(r->metadata, (r->metadata_count + 1) * sizeof(*r->metadata));
        r->metadata[r->metadata_count].key = key;
        r->metadata[r->metadata_count].value = val;
        r->metadata_count++;
    }
}

static void parse_flags(struct parse_ctx *ctx, yaml_node_t *flags_node, struct elfrc_resource *r) {
    if (!flags_node) {
        return;
    }
    if (flags_node->type == YAML_SCALAR_NODE) {
        char *s = scalar_dup(flags_node);
        uint32_t v;
        if (parse_uint(s, &v) == 0) {
            r->flags = v;
        } else {
            set_error(ctx, node_line(flags_node), "resource '%s': invalid 'flags' value '%s'", r->name, s);
        }
        free(s);
        return;
    }
    if (flags_node->type != YAML_SEQUENCE_NODE) {
        set_error(ctx, node_line(flags_node), "resource '%s': 'flags' must be an integer or a list of flag names",
                   r->name);
        return;
    }
    for (yaml_node_item_t *item = flags_node->data.sequence.items.start; item < flags_node->data.sequence.items.top;
         item++) {
        yaml_node_t *in = node_at(ctx, *item);
        char *name = scalar_dup(in);
        uint32_t bit;
        if (!name || flag_from_name(name, &bit) != 0) {
            set_error(ctx, node_line(flags_node), "resource '%s': unknown flag name '%s'", r->name,
                       name ? name : "?");
            free(name);
            return;
        }
        r->flags |= bit;
        free(name);
    }
}

static void parse_resource(struct parse_ctx *ctx, yaml_node_t *rnode, const struct defaults *defs,
                            struct elfrc_manifest *m) {
    if (rnode->type != YAML_MAPPING_NODE) {
        set_error(ctx, node_line(rnode), "each entry in 'resources' must be a mapping");
        return;
    }

    struct elfrc_resource *r = push_resource(m);
    r->line = node_line(rnode);

    yaml_node_t *name_n = map_get(ctx, rnode, "name");
    if (!name_n) {
        set_error(ctx, r->line, "resource is missing required field 'name'");
        return;
    }
    r->name = scalar_dup(name_n);

    yaml_node_t *file_n = map_get(ctx, rnode, "file");
    if (!file_n) {
        set_error(ctx, r->line, "resource '%s' is missing required field 'file'", r->name);
        return;
    }
    char *file_rel = scalar_dup(file_n);
    if (file_rel[0] == '/') {
        r->file = file_rel;
    } else {
        size_t len = strlen(ctx->manifest_dir) + 1 + strlen(file_rel) + 1;
        r->file = malloc(len);
        snprintf(r->file, len, "%s/%s", ctx->manifest_dir, file_rel);
        free(file_rel);
    }
    struct stat st;
    if (stat(r->file, &st) != 0 || !S_ISREG(st.st_mode)) {
        set_error(ctx, r->line, "resource '%s': cannot open file '%s'", r->name, r->file);
        return;
    }

    yaml_node_t *type_n = map_get(ctx, rnode, "type");
    if (type_n) {
        char *type_s = scalar_dup(type_n);
        uint32_t t;
        if (type_from_name(type_s, &t) == 0) {
            r->type = t;
        } else if (parse_uint(type_s, &t) == 0) {
            r->type = t;
        } else {
            set_error(ctx, node_line(type_n), "resource '%s': unknown type '%s'", r->name, type_s);
        }
        free(type_s);
    } else {
        r->type = type_from_extension(r->file);
    }

    yaml_node_t *id_n = map_get(ctx, rnode, "id");
    if (id_n) {
        char *id_s = scalar_dup(id_n);
        if (parse_uint(id_s, &r->id) != 0) {
            set_error(ctx, node_line(id_n), "resource '%s': invalid 'id' value '%s'", r->name, id_s);
        }
        r->id_explicit = 1;
        free(id_s);
    }

    yaml_node_t *lang_n = map_get(ctx, rnode, "language");
    char *lang_s = lang_n ? scalar_dup(lang_n) : strdup(defs->language);
    r->language = language_from_tag(lang_s);
    free(lang_s);

    yaml_node_t *comp_n = map_get(ctx, rnode, "compression");
    char *comp_s = comp_n ? scalar_dup(comp_n) : strdup(defs->compression);
    uint32_t comp;
    if (compression_from_name(comp_s, &comp) != 0) {
        set_error(ctx, node_line(comp_n ? comp_n : rnode), "resource '%s': unknown compression algorithm '%s'",
                   r->name, comp_s);
    } else if (comp != ERF_COMPRESSION_NONE) {
        set_error(ctx, node_line(comp_n ? comp_n : rnode),
                   "resource '%s': unsupported compression algorithm '%s' (this version of elfrc only "
                   "implements 'none')",
                   r->name, comp_s);
    } else {
        r->compression = comp;
    }
    free(comp_s);

    yaml_node_t *enc_n = map_get(ctx, rnode, "encryption");
    char *enc_s = enc_n ? scalar_dup(enc_n) : strdup(defs->encryption);
    if (strcasecmp(enc_s, "none") != 0 && enc_s[0] != '\0') {
        set_error(ctx, node_line(enc_n ? enc_n : rnode),
                   "resource '%s': unsupported encryption algorithm '%s' (this version of elfrc only "
                   "implements 'none')",
                   r->name, enc_s);
    }
    free(enc_s);

    parse_metadata(ctx, map_get(ctx, rnode, "metadata"), r);
    parse_flags(ctx, map_get(ctx, rnode, "flags"), r);
}

/* ---- id assignment & duplicate validation ---- */

static int uint32_cmp(const void *a, const void *b) {
    uint32_t x = *(const uint32_t *)a, y = *(const uint32_t *)b;
    return (x > y) - (x < y);
}

static void assign_and_validate_ids(struct parse_ctx *ctx, struct elfrc_manifest *m) {
    if (m->resource_count == 0) {
        return;
    }
    uint32_t *explicit_ids = malloc(m->resource_count * sizeof(uint32_t));
    size_t explicit_count = 0;
    for (size_t i = 0; i < m->resource_count; i++) {
        if (m->resources[i].id_explicit) {
            explicit_ids[explicit_count++] = m->resources[i].id;
        }
    }
    qsort(explicit_ids, explicit_count, sizeof(uint32_t), uint32_cmp);
    for (size_t i = 1; i < explicit_count; i++) {
        if (explicit_ids[i] == explicit_ids[i - 1]) {
            set_error(ctx, 0, "duplicate explicit resource id %u", explicit_ids[i]);
            free(explicit_ids);
            return;
        }
    }

    uint32_t next = 1;
    for (size_t i = 0; i < m->resource_count; i++) {
        struct elfrc_resource *r = &m->resources[i];
        if (r->id_explicit) {
            r->final_id = r->id;
            continue;
        }
        while (bsearch(&next, explicit_ids, explicit_count, sizeof(uint32_t), uint32_cmp)) {
            next++;
        }
        r->final_id = next++;
    }
    free(explicit_ids);

    for (size_t i = 0; i < m->resource_count; i++) {
        for (size_t j = i + 1; j < m->resource_count; j++) {
            if (strcmp(m->resources[i].name, m->resources[j].name) == 0 &&
                m->resources[i].language == m->resources[j].language) {
                set_error(ctx, m->resources[j].line,
                           "duplicate resource: name '%s' language %u already defined at line %d",
                           m->resources[j].name, m->resources[j].language, m->resources[i].line);
                return;
            }
        }
    }
}

/* ---- top level ---- */

int elfrc_manifest_parse(const char *path, struct elfrc_manifest *out_manifest, char errbuf[ELFRC_ERRBUF_SIZE]) {
    memset(out_manifest, 0, sizeof(*out_manifest));
    errbuf[0] = '\0';

    FILE *f = fopen(path, "rb");
    if (!f) {
        snprintf(errbuf, ELFRC_ERRBUF_SIZE, "%s: error: cannot open manifest: %s", path, strerror(errno));
        return -1;
    }

    yaml_parser_t parser;
    yaml_document_t document;
    if (!yaml_parser_initialize(&parser)) {
        snprintf(errbuf, ELFRC_ERRBUF_SIZE, "%s: error: failed to initialize YAML parser", path);
        fclose(f);
        return -1;
    }
    yaml_parser_set_input_file(&parser, f);
    if (!yaml_parser_load(&parser, &document)) {
        snprintf(errbuf, ELFRC_ERRBUF_SIZE, "%s:%zu: error: YAML parse error: %s", path,
                  parser.problem_mark.line + 1, parser.problem ? parser.problem : "malformed document");
        yaml_parser_delete(&parser);
        fclose(f);
        return -1;
    }
    yaml_parser_delete(&parser);
    fclose(f);

    struct parse_ctx ctx;
    memset(&ctx, 0, sizeof(ctx));
    ctx.doc = &document;
    ctx.manifest_path = path;
    char pathbuf[4096];
    snprintf(pathbuf, sizeof(pathbuf), "%s", path);
    ctx.manifest_dir = strdup(dirname(pathbuf));

    yaml_node_t *root = yaml_document_get_root_node(&document);
    if (!root || root->type != YAML_MAPPING_NODE) {
        set_error(&ctx, 0, "manifest root must be a mapping with 'version' and 'resources' keys");
        goto done;
    }

    yaml_node_t *version_n = map_get(&ctx, root, "version");
    if (!version_n) {
        set_error(&ctx, node_line(root), "manifest is missing required field 'version'");
        goto done;
    }
    char *version_s = scalar_dup(version_n);
    uint32_t version = 0;
    if (parse_uint(version_s, &version) != 0 || version != 1) {
        set_error(&ctx, node_line(version_n), "unsupported manifest version '%s' (only version 1 is supported)",
                   version_s);
        free(version_s);
        goto done;
    }
    free(version_s);
    out_manifest->version = 1;

    yaml_node_t *module_n = map_get(&ctx, root, "module");
    if (module_n) {
        out_manifest->module = scalar_dup(module_n);
    }

    struct defaults defs = {strdup("neutral"), strdup("none"), strdup("none")};
    yaml_node_t *defaults_n = map_get(&ctx, root, "defaults");
    if (defaults_n) {
        yaml_node_t *l = map_get(&ctx, defaults_n, "language");
        yaml_node_t *c = map_get(&ctx, defaults_n, "compression");
        yaml_node_t *e = map_get(&ctx, defaults_n, "encryption");
        if (l) {
            free(defs.language);
            defs.language = scalar_dup(l);
        }
        if (c) {
            free(defs.compression);
            defs.compression = scalar_dup(c);
        }
        if (e) {
            free(defs.encryption);
            defs.encryption = scalar_dup(e);
        }
    }

    yaml_node_t *resources_n = map_get(&ctx, root, "resources");
    if (!resources_n || resources_n->type != YAML_SEQUENCE_NODE) {
        set_error(&ctx, node_line(root), "manifest is missing required sequence field 'resources'");
        free(defs.language);
        free(defs.compression);
        free(defs.encryption);
        goto done;
    }
    for (yaml_node_item_t *item = resources_n->data.sequence.items.start;
         item < resources_n->data.sequence.items.top; item++) {
        yaml_node_t *rnode = node_at(&ctx, *item);
        parse_resource(&ctx, rnode, &defs, out_manifest);
        if (ctx.failed) {
            break;
        }
    }
    free(defs.language);
    free(defs.compression);
    free(defs.encryption);

    if (!ctx.failed) {
        assign_and_validate_ids(&ctx, out_manifest);
    }

done:
    yaml_document_delete(&document);
    free(ctx.manifest_dir);
    if (ctx.failed) {
        snprintf(errbuf, ELFRC_ERRBUF_SIZE, "%s", ctx.errbuf_local);
        elfrc_manifest_free(out_manifest);
        memset(out_manifest, 0, sizeof(*out_manifest));
        return -1;
    }
    return 0;
}

void elfrc_manifest_free(struct elfrc_manifest *manifest) {
    if (!manifest) {
        return;
    }
    free(manifest->module);
    for (size_t i = 0; i < manifest->resource_count; i++) {
        struct elfrc_resource *r = &manifest->resources[i];
        free(r->name);
        free(r->file);
        for (size_t j = 0; j < r->metadata_count; j++) {
            free(r->metadata[j].key);
            free(r->metadata[j].value);
        }
        free(r->metadata);
    }
    free(manifest->resources);
    memset(manifest, 0, sizeof(*manifest));
}
