// SPDX-License-Identifier: Apache-2.0
#pragma once
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#define COMPANION_CPACK_COMPAT "muse-ui-v5-gsp1.5.1-gspc0.6.1-bd2b06c6a30dacae"
#define COMPANION_CPACK_TEMPLATE "muse-companion-bd2b06c6a30dacae"
#define COMPANION_CPACK_MAX_IMAGE (3U * 1024U * 1024U)
#define COMPANION_CPACK_MAX_BYTES (COMPANION_CPACK_MAX_IMAGE + 8192U + 4096U)
typedef struct {
    uint32_t image_offset, image_size;
    uint8_t image_sha256[32];
    char character_version[64];
} companion_cpack_info_t;
/* Stateless bounded reader. Callback hashes exactly the requested region.
 * No extraction, writes, networking or UI; returns zero only after both
 * SHA-256 layers, ZIP CRC/layout and MOSGSP/GSPB headers are validated. */
typedef int (*companion_cpack_hash_fn)(FILE *, uint32_t, uint32_t, uint8_t[32]);
int companion_cpack_validate(FILE *file, uint32_t bytes, const uint8_t pack_sha256[32],
                             companion_cpack_hash_fn hash, companion_cpack_info_t *out);
int companion_cpack_hex(const char *hex, uint8_t digest[32]);
