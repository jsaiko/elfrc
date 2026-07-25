#include "crc64.h"

#define CRC64_POLY 0xC96C5795D7870F42ULL /* reflected form of 0x42F0E1EBA9EA3693 */

static uint64_t crc64_table[256];

__attribute__((constructor)) static void crc64_init_table(void) {
    for (uint32_t i = 0; i < 256; i++) {
        uint64_t c = i;
        for (int k = 0; k < 8; k++) {
            c = (c & 1) ? (CRC64_POLY ^ (c >> 1)) : (c >> 1);
        }
        crc64_table[i] = c;
    }
}

uint64_t elfr_crc64(const void *data, size_t len) {
    const unsigned char *p = (const unsigned char *)data;
    uint64_t crc = 0xFFFFFFFFFFFFFFFFULL;
    for (size_t i = 0; i < len; i++) {
        crc = crc64_table[(crc ^ p[i]) & 0xFF] ^ (crc >> 8);
    }
    return crc ^ 0xFFFFFFFFFFFFFFFFULL;
}
