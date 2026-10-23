// SPDX-License-Identifier: Apache-2.0
#include "companion_cpack.h"
#include "cJSON.h"
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

enum { MANIFEST_LIMIT = 8192, READ_BYTES = 1024, LOCAL_BYTES = 30, CENTRAL_BYTES = 46 };
typedef struct {
    uint32_t local, data, bytes, crc;
    uint16_t name_bytes;
    char name[14];
} entry_t;
static uint16_t u16(const uint8_t *p) { return (uint16_t)p[0] | (uint16_t)p[1] << 8; }
static uint32_t u32(const uint8_t *p) { return (uint32_t)u16(p) | (uint32_t)u16(p + 2) << 16; }
static bool read_at(FILE *f, uint32_t at, void *buffer, size_t bytes)
{ return fseek(f, (long)at, SEEK_SET) == 0 && fread(buffer, 1, bytes, f) == bytes; }
static bool range(uint32_t at, uint32_t n, uint32_t limit)
{ return at <= limit && n <= limit - at; }
static uint32_t crc_update(uint32_t crc, const uint8_t *p, size_t bytes)
{
    while (bytes--) {
        crc ^= *p++;
        for (unsigned bit = 0; bit < 8; ++bit) crc = (crc >> 1) ^ (0xedb88320U & (0U - (crc & 1U)));
    }
    return crc;
}
static bool crc_region(FILE *f, const entry_t *e, uint8_t *buffer)
{
    uint32_t crc = UINT32_MAX;
    for (uint32_t done = 0; done < e->bytes;) {
        size_t n = e->bytes - done;
        if (n > READ_BYTES) n = READ_BYTES;
        if (!read_at(f, e->data + done, buffer, n)) return false;
        crc = crc_update(crc, buffer, n);
        done += (uint32_t)n;
    }
    return ~crc == e->crc;
}
static bool bundle_crc(FILE *f, uint32_t at, uint32_t bytes, uint32_t expected, uint8_t *buffer)
{
    uint32_t crc = UINT32_MAX;
    for (uint32_t done = 0; done < bytes;) {
        size_t n = bytes - done;
        if (n > READ_BYTES) n = READ_BYTES;
        if (!read_at(f, at + done, buffer, n)) return false;
        /* GSPB computes its CRC with the checksum field cleared. */
        if (done == 0) memset(buffer + 12, 0, 4);
        crc = crc_update(crc, buffer, n);
        done += (uint32_t)n;
    }
    return ~crc == expected;
}
int companion_cpack_hex(const char *hex, uint8_t digest[32])
{
    if (hex == NULL || digest == NULL || strlen(hex) != 64) return -1;
    for (unsigned i = 0; i < 32; ++i) {
        unsigned value = 0;
        for (unsigned n = 0; n < 2; ++n) {
            unsigned char c = (unsigned char)hex[2 * i + n];
            unsigned v = c >= '0' && c <= '9' ? c - '0' :
                         c >= 'a' && c <= 'f' ? c - 'a' + 10 :
                         c >= 'A' && c <= 'F' ? c - 'A' + 10 : 16;
            if (v > 15) return -1;
            value = (value << 4) | v;
        }
        digest[i] = (uint8_t)value;
    }
    return 0;
}
static bool version_valid(const char *p)
{
    if (p == NULL || !*p || strlen(p) >= 64) return false;
    for (; *p; ++p) if (!((*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z') ||
                         (*p >= '0' && *p <= '9') || *p == '-' || *p == '_' || *p == '.')) return false;
    return true;
}
static const char *text_field(cJSON *j, const char *key)
{ return cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(j, key)); }
static bool manifest(FILE *f, const entry_t *e, const entry_t *image, companion_cpack_info_t *out)
{
    char *buffer = malloc(e->bytes + 1U);
    if (buffer == NULL) return false;
    bool valid = read_at(f, e->data, buffer, e->bytes) && memchr(buffer, '\0', e->bytes) == NULL;
    buffer[e->bytes] = '\0';
    /* cJSON stores decoded strings as C strings. Reject escaped NULs as well,
     * so a matching prefix cannot conceal an incompatible identifier suffix. */
    if (strstr(buffer, "\\u0000") != NULL) valid = false;
    const char *end = NULL;
    cJSON *j = valid ? cJSON_ParseWithLengthOpts(buffer, e->bytes + 1U, &end, true) : NULL;
    valid = cJSON_IsObject(j) && end == buffer + e->bytes;
    unsigned fields = 0;
    for (cJSON *a = j ? j->child : NULL; a && valid; a = a->next) {
        ++fields;
        for (cJSON *b = a->next; b; b = b->next)
            if (strcmp(a->string, b->string) == 0) valid = false;
    }
    const char *schema = text_field(j, "schema");
    const char *compat = text_field(j, "compat_version");
    const char *templ = text_field(j, "template_id");
    const char *version = text_field(j, "character_version");
    cJSON *size = cJSON_GetObjectItemCaseSensitive(j, "image_size");
    valid = valid && fields == 6 && schema && strcmp(schema, "muse-cpack/v1") == 0 &&
            compat && strcmp(compat, COMPANION_CPACK_COMPAT) == 0 &&
            templ && strcmp(templ, COMPANION_CPACK_TEMPLATE) == 0 && version_valid(version) &&
            cJSON_IsNumber(size) && size->valuedouble == image->bytes &&
            companion_cpack_hex(text_field(j, "image_sha256"), out->image_sha256) == 0;
    if (valid) {
        out->image_offset = image->data;
        out->image_size = image->bytes;
        memcpy(out->character_version, version, strlen(version) + 1U);
    }
    cJSON_Delete(j);
    free(buffer);
    return valid;
}
int companion_cpack_validate(FILE *file, uint32_t bytes, const uint8_t pack_sha256[32],
                             companion_cpack_hash_fn hash, companion_cpack_info_t *out)
{
    if (!file || !pack_sha256 || !hash || !out || bytes < 128 || bytes > COMPANION_CPACK_MAX_BYTES) return -1;
    uint8_t *buffer = malloc(READ_BYTES);
    entry_t *entries = calloc(2, sizeof(*entries));
    if (!buffer || !entries) { free(buffer); free(entries); return -1; }
    uint8_t digest[32];
    bool valid = fseek(file, 0, SEEK_END) == 0 && ftell(file) == (long)bytes &&
                 hash(file, 0, bytes, digest) == 0 && memcmp(digest, pack_sha256, 32) == 0;
    uint32_t at = 0;
    for (unsigned i = 0; i < 2 && valid; ++i) {
        entry_t *e = &entries[i];
        e->local = at;
        valid = range(at, LOCAL_BYTES, bytes) && read_at(file, at, buffer, LOCAL_BYTES) &&
                u32(buffer) == 0x04034b50 && u16(buffer + 4) <= 20 &&
                u16(buffer + 6) == 0 && u16(buffer + 8) == 0 && u16(buffer + 28) == 0;
        if (!valid) break;
        e->crc = u32(buffer + 14); e->bytes = u32(buffer + 22); e->name_bytes = u16(buffer + 26);
        valid = u32(buffer + 18) == e->bytes && (e->name_bytes == 13 || e->name_bytes == 11) &&
                range(at + LOCAL_BYTES, e->name_bytes, bytes);
        if (!valid) break;
        valid = read_at(file, at + LOCAL_BYTES, e->name, e->name_bytes);
        e->name[e->name_bytes] = '\0';
        bool is_manifest = strcmp(e->name, "manifest.json") == 0;
        valid = valid && (is_manifest || strcmp(e->name, "ui_apps.bin") == 0) &&
                e->bytes > 0 && e->bytes <= (is_manifest ? MANIFEST_LIMIT : COMPANION_CPACK_MAX_IMAGE);
        e->data = at + LOCAL_BYTES + e->name_bytes;
        valid = valid && range(e->data, e->bytes, bytes) && crc_region(file, e, buffer);
        at = e->data + e->bytes;
    }
    valid = valid && strcmp(entries[0].name, entries[1].name) != 0;
    const uint32_t central = at;
    for (unsigned i = 0; i < 2 && valid; ++i) {
        const entry_t *e = &entries[i];
        valid = range(at, CENTRAL_BYTES + e->name_bytes, bytes) && read_at(file, at, buffer, CENTRAL_BYTES) &&
                u32(buffer) == 0x02014b50 && u16(buffer + 6) <= 20 &&
                u16(buffer + 8) == 0 && u16(buffer + 10) == 0 &&
                u32(buffer + 16) == e->crc && u32(buffer + 20) == e->bytes && u32(buffer + 24) == e->bytes &&
                u16(buffer + 28) == e->name_bytes && u16(buffer + 30) == 0 &&
                u16(buffer + 32) == 0 && u16(buffer + 34) == 0 && u32(buffer + 42) == e->local;
        if (!valid) break;
        valid = read_at(file, at + CENTRAL_BYTES, buffer, e->name_bytes) &&
                memcmp(buffer, e->name, e->name_bytes) == 0;
        at += CENTRAL_BYTES + e->name_bytes;
    }
    valid = valid && range(at, 22, bytes) && at + 22 == bytes && read_at(file, at, buffer, 22) &&
            u32(buffer) == 0x06054b50 && u16(buffer + 4) == 0 && u16(buffer + 6) == 0 &&
            u16(buffer + 8) == 2 && u16(buffer + 10) == 2 && u16(buffer + 20) == 0 &&
            u32(buffer + 12) == at - central && u32(buffer + 16) == central;
    const entry_t *m = strcmp(entries[0].name, "manifest.json") == 0 ? &entries[0] : &entries[1];
    const entry_t *image = m == &entries[0] ? &entries[1] : &entries[0];
    valid = valid && image->bytes >= 96 && manifest(file, m, image, out) &&
            hash(file, image->data, image->bytes, digest) == 0 && memcmp(digest, out->image_sha256, 32) == 0 &&
            read_at(file, image->data, buffer, 96);
    if (valid) {
        static const uint8_t magic[8] = {'M','O','S','G','S','P',0,1};
        valid = memcmp(buffer, magic, 8) == 0 && u16(buffer + 8) == 1 && u16(buffer + 10) == 64 &&
                u32(buffer + 12) == image->bytes - 64 && memcmp(buffer + 64, "GSPB", 4) == 0 &&
                u32(buffer + 72) == image->bytes - 64 &&
                u16(buffer + 68) == 1 && u16(buffer + 70) <= 4 &&
                u16(buffer + 80) > 0 && u16(buffer + 82) == 0 && u32(buffer + 92) == 0 &&
                32U + (uint32_t)u16(buffer + 80) * 24U <= image->bytes - 64;
        const uint32_t expected_crc = u32(buffer + 76);
        for (unsigned i = 48; i < 64; ++i) if (buffer[i] != 0) valid = false;
        uint8_t bundle_hash[32]; memcpy(bundle_hash, buffer + 16, 32);
        valid = valid && hash(file, image->data + 64, image->bytes - 64, digest) == 0 &&
                memcmp(digest, bundle_hash, 32) == 0 &&
                bundle_crc(file, image->data + 64, image->bytes - 64, expected_crc, buffer);
    }
    free(buffer); free(entries);
    if (!valid) memset(out, 0, sizeof(*out));
    return valid ? 0 : -1;
}
