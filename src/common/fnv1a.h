#ifndef ELFR_COMMON_FNV1A_H
#define ELFR_COMMON_FNV1A_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* FNV-1a 64-bit, as recommended by the ERF spec for name hashing (section 9). */
uint64_t elfr_fnv1a64(const void *data, size_t len);

/* 32-bit variant, used as a deterministic fallback for mapping unrecognized
 * language tags to a numeric language id. */
uint32_t elfr_fnv1a32(const void *data, size_t len);

#ifdef __cplusplus
}
#endif

#endif /* ELFR_COMMON_FNV1A_H */
