#include "fnv1a.h"

uint64_t elfr_fnv1a64(const void *data, size_t len) {
    const unsigned char *p = (const unsigned char *)data;
    uint64_t hash = 0xcbf29ce484222325ULL;
    for (size_t i = 0; i < len; i++) {
        hash ^= (uint64_t)p[i];
        hash *= 0x100000001b3ULL;
    }
    return hash;
}

uint32_t elfr_fnv1a32(const void *data, size_t len) {
    const unsigned char *p = (const unsigned char *)data;
    uint32_t hash = 0x811c9dc5u;
    for (size_t i = 0; i < len; i++) {
        hash ^= (uint32_t)p[i];
        hash *= 0x01000193u;
    }
    return hash;
}
