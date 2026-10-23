// SPDX-License-Identifier: Apache-2.0
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef enum { CHARACTER_NONE, CHARACTER_PACK, CHARACTER_STATUS, CHARACTER_INVALID, CHARACTER_RESTORE } character_command_kind_t;
typedef enum { CHARACTER_BUILDING, CHARACTER_READY, CHARACTER_FAILED } character_status_t;
typedef struct {
    character_command_kind_t kind;
    character_status_t status;
    char job[64], pack_url[1024], sha256[65], expires_at[21];
    char character_version[64], compat_version[96], error[192];
    uint32_t size;
    char target[16];
    /* Local monotonic deadline; never supplied by the wire parser. Zero is
     * reserved for explicit USB maintenance downloads. */
    uint64_t deadline_ms;
} character_command_t;
/* Single-owner, bounded parser. Removes all recognized control spans in place
 * even when invalid, preserving ordinary prose. Multiple controls fail closed.
 * Does no network IO, persistence, UI access or resource writes. */
character_command_kind_t companion_character_extract(char *text, character_command_t *out);
bool companion_character_url_allowed(const char *url);
