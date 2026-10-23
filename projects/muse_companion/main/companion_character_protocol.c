// SPDX-License-Identifier: Apache-2.0
#include "companion_character_protocol.h"
#include <ctype.h>
#include <string.h>

enum { MAX_PACK_BYTES = 3 * 1024 * 1024 + 12288, MAX_CONTROL_BYTES = 2048 };
typedef struct { const char *name; char *value; size_t capacity; } field_t;

static bool identifier(const char *value)
{
    if (!*value) return false;
    for (; *value; ++value) {
        unsigned char c = (unsigned char)*value;
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
              (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.')) return false;
    }
    return true;
}

bool companion_character_url_allowed(const char *url)
{
    static const char prefix[] = "https://muse.ai/";
    if (url == NULL || strncmp(url, prefix, sizeof(prefix) - 1) != 0 || !url[sizeof(prefix) - 1]) return false;
    for (const unsigned char *p = (const unsigned char *)url; *p; ++p) {
        if (*p <= 32 || *p >= 127 || *p == '\\' || *p == '"' || *p == '#' || *p == '[' || *p == ']') return false;
    }
    return true;
}

static bool expires_valid(const char *s)
{
    if (strlen(s) != 20) return false;
    for (unsigned i = 0; i < 20; ++i) {
        char separator = i == 4 || i == 7 ? '-' : i == 10 ? 'T' : i == 13 || i == 16 ? ':' : i == 19 ? 'Z' : 0;
        if (separator ? s[i] != separator : !isdigit((unsigned char)s[i])) return false;
    }
    unsigned year = (s[0]-'0')*1000U+(s[1]-'0')*100U+(s[2]-'0')*10U+s[3]-'0';
    unsigned month = (s[5]-'0')*10U+s[6]-'0', day = (s[8]-'0')*10U+s[9]-'0';
    unsigned hour = (s[11]-'0')*10U+s[12]-'0', minute = (s[14]-'0')*10U+s[15]-'0';
    unsigned second = (s[17]-'0')*10U+s[18]-'0';
    static const unsigned days[] = {31,28,31,30,31,30,31,31,30,31,30,31};
    if (year < 2026 || month == 0 || month > 12 || day == 0 || hour > 23 || minute > 59 || second > 59) return false;
    bool leap = year % 4 == 0 && (year % 100 != 0 || year % 400 == 0);
    return day <= days[month - 1] + (unsigned)(month == 2 && leap);
}

static bool parse(const char *begin, const char *end, character_command_t *out)
{
    char status[16] = {0}, size[16] = {0};
    /* Bounded 240-byte descriptor table on the 8 KiB voice worker stack;
     * field payloads live in the caller-owned command, not this frame. */
    field_t fields[] = {
        {"job", out->job, sizeof(out->job)}, {"status", status, sizeof(status)},
        {"pack_url", out->pack_url, sizeof(out->pack_url)}, {"sha256", out->sha256, sizeof(out->sha256)},
        {"size", size, sizeof(size)}, {"expires_at", out->expires_at, sizeof(out->expires_at)},
        {"character_version", out->character_version, sizeof(out->character_version)},
        {"compat_version", out->compat_version, sizeof(out->compat_version)}, {"error", out->error, sizeof(out->error)},
        {"target", out->target, sizeof(out->target)},
    };
    const char *p = begin;
    if ((size_t)(end - begin) >= MAX_CONTROL_BYTES) return false;
    if (end - p >= 15 && memcmp(p, "[CHARACTER_PACK", 15) == 0) { out->kind = CHARACTER_PACK; p += 15; }
    else if (end - p >= 17 && memcmp(p, "[CHARACTER_STATUS", 17) == 0) { out->kind = CHARACTER_STATUS; p += 17; }
    else if (end - p >= 18 && memcmp(p, "[CHARACTER_RESTORE", 18) == 0) { out->kind = CHARACTER_RESTORE; p += 18; }
    else return false;
    if (p == end || !isspace((unsigned char)*p)) return false;
    unsigned mask = 0;
    while (p < end) {
        while (p < end && isspace((unsigned char)*p)) ++p;
        if (p == end) break;
        const char *key = p;
        while (p < end && ((*p >= 'a' && *p <= 'z') || (*p >= '0' && *p <= '9') || *p == '_')) ++p;
        size_t key_len = (size_t)(p - key);
        if (key_len == 0 || p == end || *p++ != '=' || p == end || *p++ != '"') return false;
        unsigned index = 0;
        while (index < sizeof(fields)/sizeof(fields[0]) &&
               (strlen(fields[index].name) != key_len || memcmp(fields[index].name, key, key_len) != 0)) ++index;
        if (index == sizeof(fields)/sizeof(fields[0]) || mask & (1U << index)) return false;
        const char *value = p;
        while (p < end && *p != '"') {
            if ((unsigned char)*p < 32 || *p == '\\') return false;
            ++p;
        }
        size_t value_len = (size_t)(p - value);
        if (p == end || value_len >= fields[index].capacity) return false;
        memcpy(fields[index].value, value, value_len);
        fields[index].value[value_len] = 0;
        mask |= 1U << index;
        ++p;
        if (p < end && !isspace((unsigned char)*p)) return false;
    }
    if (!identifier(out->job)) return false;
    if (out->kind == CHARACTER_PACK) return mask == 1;
    if (out->kind == CHARACTER_RESTORE) return mask == (1U | (1U << 9)) && !strcmp(out->target, "initial");
    if (strcmp(status, "building") == 0) {
        out->status = CHARACTER_BUILDING;
        /* Some producers include their common empty error field while building.
         * Accept that harmless field, but reject errors or ready-only metadata. */
        return mask == 3 || (mask == (3U | (1U << 8)) && !out->error[0]);
    }
    if (strcmp(status, "failed") == 0) { out->status = CHARACTER_FAILED; return mask == (3U | (1U << 8)) && out->error[0]; }
    if (strcmp(status, "ready") != 0 || mask != 511U || !companion_character_url_allowed(out->pack_url) ||
        !identifier(out->character_version) || !identifier(out->compat_version) ||
        strstr(out->compat_version, "TBD") != NULL || !expires_valid(out->expires_at) || strlen(out->sha256) != 64 || !size[0]) return false;
    for (unsigned i = 0; i < 64; ++i) {
        if (!isxdigit((unsigned char)out->sha256[i])) return false;
        out->sha256[i] = (char)tolower((unsigned char)out->sha256[i]);
    }
    for (const char *q = size; *q; ++q) {
        if (!isdigit((unsigned char)*q) || out->size > MAX_PACK_BYTES / 10U) return false;
        out->size = out->size * 10U + (unsigned)(*q - '0');
        if (out->size > MAX_PACK_BYTES) return false;
    }
    out->status = CHARACTER_READY;
    return out->size != 0;
}

static char *control_end(char *p)
{
    bool quoted=false;
    for(;*p;++p){
        /* Escapes are rejected by parse(), but honor them while finding the
         * outer delimiter so invalid controls cannot leak a tail into TTS. */
        if(*p=='\\'&&p[1]){++p;continue;}
        if(*p=='"')quoted=!quoted;
        else if(*p==']'&&!quoted)return p;
    }
    return NULL;
}
character_command_kind_t companion_character_extract(char *text, character_command_t *out)
{
    if (text == NULL || out == NULL) return CHARACTER_INVALID;
    memset(out, 0, sizeof(*out));
    unsigned count = 0;
    bool valid = true;
    char *search = text, *control;
    while ((control = strstr(search, "[CHARACTER_")) != NULL) {
        char *end = control_end(control);
        ++count;
        if (end == NULL) { *control = 0; valid = false; break; }
        if (count == 1 && !parse(control, end, out)) valid = false;
        memmove(control, end + 1, strlen(end + 1) + 1);
        search = control;
    }
    if (count == 0) return CHARACTER_NONE;
    if (!valid || count != 1) { memset(out, 0, sizeof(*out)); out->kind = CHARACTER_INVALID; }
    return out->kind;
}
