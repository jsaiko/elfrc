#include "../../src/common/crc64.h"
#include "../../src/common/fnv1a.h"
#include "test_util.h"

int main(void) {
    /* FNV-1a 64 offset basis check: hashing the empty string must yield
     * the raw offset basis. */
    CHECK(elfr_fnv1a64("", 0) == 0xcbf29ce484222325ULL);
    CHECK(elfr_fnv1a32("", 0) == 0x811c9dc5u);

    /* Same name must always hash the same; different names should (in
     * practice) differ. */
    CHECK(elfr_fnv1a64("logo", 4) == elfr_fnv1a64("logo", 4));
    CHECK(elfr_fnv1a64("logo", 4) != elfr_fnv1a64("config", 6));

    /* CRC-64/XZ standard check value for the ASCII string "123456789". */
    CHECK(elfr_crc64("123456789", 9) == 0x995DC9BBDF1939FAULL);

    /* checksum-with-zeroed-field round trip: recomputing over unchanged
     * bytes must reproduce the same value. */
    unsigned char buf[16] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16};
    uint64_t c1 = elfr_crc64(buf, sizeof(buf));
    uint64_t c2 = elfr_crc64(buf, sizeof(buf));
    CHECK(c1 == c2);

    printf("test_hash: OK\n");
    return 0;
}
