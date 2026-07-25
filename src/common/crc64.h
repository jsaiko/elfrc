#ifndef ELFR_COMMON_CRC64_H
#define ELFR_COMMON_CRC64_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* CRC-64/XZ (poly 0x42F0E1EBA9EA3693, reflected, init/xorout all-ones), as
 * used by xz-utils. The ERF spec recommends "CRC-64" for section 5's
 * checksum field without pinning an exact variant; this is the one we use
 * for both the container-level and per-resource checksums. */
uint64_t elfr_crc64(const void *data, size_t len);

#ifdef __cplusplus
}
#endif

#endif /* ELFR_COMMON_CRC64_H */
