// SPDX-License-Identifier: Apache-2.0
#include "companion_character_download.h"
#include "companion_cpack.h"
#include "mosaico_nand_store.h"
#include "esp_crt_bundle.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "companion_character_image.h"
#include "psa/crypto.h"
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#define ROOT MOSAICO_NAND_MOUNT_PATH "/muse-character"
#define PACK_PARTIAL ROOT "/candidate.cpack.partial"
#define IMAGE_PARTIAL ROOT "/candidate-ui.bin.partial"
#define PACK_READY ROOT "/candidate.cpack"
#define IMAGE_READY ROOT "/candidate-ui.bin"
enum { CHUNK_BYTES = 4096, HTTP_TIMEOUT_MS = 15000, DOWNLOAD_SECONDS = 180 };
static const char *TAG = "character_download";

static int hash_region(FILE *file, uint32_t at, uint32_t bytes, uint8_t digest[32])
{
    uint8_t *buffer = malloc(CHUNK_BYTES);
    if (!buffer) return -1;
    psa_hash_operation_t op = PSA_HASH_OPERATION_INIT;
    bool valid = psa_crypto_init() == PSA_SUCCESS && fseek(file, at, SEEK_SET) == 0 &&
                 psa_hash_setup(&op, PSA_ALG_SHA_256) == PSA_SUCCESS;
    while (valid && bytes) {
        const size_t n = bytes < CHUNK_BYTES ? bytes : CHUNK_BYTES;
        valid = fread(buffer, 1, n, file) == n && psa_hash_update(&op, buffer, n) == PSA_SUCCESS;
        bytes -= (uint32_t)n;
    }
    size_t count = 0;
    if (valid) valid = psa_hash_finish(&op, digest, 32, &count) == PSA_SUCCESS && count == 32;
    (void)psa_hash_abort(&op);
    free(buffer);
    return valid ? 0 : -1;
}

static bool unexpired(const char *expiry)
{
    struct tm utc = {0};
    char *end = strptime(expiry, "%Y-%m-%dT%H:%M:%SZ", &utc);
    if (!end || *end) return false;
    const time_t now = time(NULL), until = timegm(&utc);
    /* An unsynchronized clock must not make an expired link look valid. */
    return now >= 1704067200 && until > now;
}

static esp_err_t validate_runtime(FILE *file, const companion_cpack_info_t *image)
{
    return companion_character_image_validate(file, image->image_offset, image->image_size);
}

esp_err_t companion_character_download_verify(const character_command_t *command,
    companion_cpack_info_t *info)
{
    uint8_t expected[32], actual[32];
    if (!command || !info || command->kind != CHARACTER_STATUS ||
        command->status != CHARACTER_READY ||
        strcmp(command->compat_version, COMPANION_CPACK_COMPAT) ||
        command->size < 128 || command->size > COMPANION_CPACK_MAX_BYTES ||
        companion_cpack_hex(command->sha256, expected)) return ESP_ERR_INVALID_ARG;
    FILE *pack = fopen(PACK_READY, "rb");
    if (!pack) return ESP_ERR_NOT_FOUND;
    esp_err_t err = ESP_OK;
    if (fseek(pack, 0, SEEK_END) || ftell(pack) != (long)command->size ||
        companion_cpack_validate(pack, command->size, expected, hash_region, info) ||
        strcmp(info->character_version, command->character_version)) err = ESP_ERR_INVALID_RESPONSE;
    if (err == ESP_OK) err = validate_runtime(pack, info);
    if (fclose(pack)) err = ESP_FAIL;
    FILE *image = err == ESP_OK ? fopen(IMAGE_READY, "rb") : NULL;
    if (err == ESP_OK && !image) err = ESP_ERR_NOT_FOUND;
    if (err == ESP_OK && (fseek(image, 0, SEEK_END) || ftell(image) != (long)info->image_size ||
        hash_region(image, 0, info->image_size, actual) || memcmp(actual, info->image_sha256, 32)))
        err = ESP_ERR_INVALID_CRC;
    if (image && fclose(image)) err = ESP_FAIL;
    return err;
}

static esp_err_t download_file(const character_command_t *command, uint8_t *buffer,
                               companion_character_download_result_t *result)
{
    /* Reject redirects entirely: the verified hosting contract needs none.
     * This also prevents a trusted host redirecting us outside its whitelist. */
    const esp_http_client_config_t config = {
        .url = command->pack_url, .method = HTTP_METHOD_GET,
        .crt_bundle_attach = esp_crt_bundle_attach, .timeout_ms = HTTP_TIMEOUT_MS,
        .disable_auto_redirect = true, .buffer_size = CHUNK_BYTES,
    };
    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (!client) return ESP_ERR_NO_MEM;
    esp_err_t err = esp_http_client_open(client, 0);
    if (err != ESP_OK) { esp_http_client_cleanup(client); return err; }
    const int64_t length = esp_http_client_fetch_headers(client);
    result->http_status = esp_http_client_get_status_code(client);
    if (length < 0 || result->http_status != 200 ||
        (length > 0 && length != command->size)) {
        esp_http_client_cleanup(client);
        return ESP_ERR_INVALID_RESPONSE;
    }
    FILE *file = fopen(PACK_PARTIAL, "wb");
    if (!file) { esp_http_client_cleanup(client); return ESP_FAIL; }
    const int64_t started = esp_timer_get_time();
    uint32_t received = 0;
    bool valid = true;
    while (valid && received < command->size) {
        const uint32_t remaining = command->size - received;
        const int wanted = remaining < CHUNK_BYTES ? (int)remaining : CHUNK_BYTES;
        const int n = esp_http_client_read(client, (char *)buffer, wanted);
        valid = n > 0 && (uint32_t)n <= remaining &&
                esp_timer_get_time() - started < (int64_t)DOWNLOAD_SECONDS * 1000000 && unexpired(command->expires_at) &&
                (!command->deadline_ms ||
                 (uint64_t)(esp_timer_get_time() / 1000) < command->deadline_ms);
        if (valid) {
            valid = fwrite(buffer, 1, n, file) == (size_t)n;
            received += (uint32_t)n;
            result->bytes_received = received;
        }
    }
    /* Detect a body larger than its declared size, including chunked bodies. */
    if (valid) valid = esp_http_client_read(client, (char *)buffer, 1) == 0 &&
                       esp_http_client_is_complete_data_received(client);
    result->body_complete = valid;
    if (valid) valid = fflush(file) == 0 && fsync(fileno(file)) == 0;
    if (fclose(file) != 0) valid = false;
    esp_http_client_cleanup(client);
    return valid ? ESP_OK : ESP_FAIL;
}

esp_err_t companion_character_download(const character_command_t *command,
                                       companion_character_download_result_t *result)
{
    if (!result) return ESP_ERR_INVALID_ARG;
    memset(result, 0, sizeof(*result));
    uint8_t expected[32];
    if (!command || command->kind != CHARACTER_STATUS || command->status != CHARACTER_READY ||
        !companion_character_url_allowed(command->pack_url) || !unexpired(command->expires_at) ||
        strcmp(command->compat_version, COMPANION_CPACK_COMPAT) ||
        command->size < 128 || command->size > COMPANION_CPACK_MAX_BYTES ||
        companion_cpack_hex(command->sha256, expected) != 0) return ESP_ERR_INVALID_ARG;
    size_t total = 0, used = 0;
    esp_err_t err = mosaico_nand_store_usage(&total, &used);
    if (err != ESP_OK) return err;
    if (used > total || total - used < command->size + COMPANION_CPACK_MAX_IMAGE + CHUNK_BYTES)
        return ESP_ERR_NO_MEM;
    if (mkdir(ROOT, 0700) != 0 && errno != EEXIST) return ESP_FAIL;
    uint8_t *buffer = malloc(CHUNK_BYTES);
    companion_cpack_info_t *info = calloc(1, sizeof(*info));
    if (!buffer || !info) { free(buffer); free(info); return ESP_ERR_NO_MEM; }
    err = download_file(command, buffer, result);
    FILE *pack = err == ESP_OK ? fopen(PACK_PARTIAL, "rb") : NULL;
    if (err == ESP_OK && !pack) err = ESP_FAIL;
    if (err == ESP_OK && companion_cpack_validate(pack, command->size, expected, hash_region, info))
        err = ESP_ERR_INVALID_RESPONSE;
    if (err == ESP_OK && strcmp(info->character_version, command->character_version))
        err = ESP_ERR_INVALID_RESPONSE;
    if (err == ESP_OK) err = validate_runtime(pack, info);
    result->pack_validated = err == ESP_OK;
    FILE *image = err == ESP_OK ? fopen(IMAGE_PARTIAL, "wb") : NULL;
    if (err == ESP_OK && (!image || fseek(pack, info->image_offset, SEEK_SET))) err = ESP_FAIL;
    uint32_t left = info->image_size;
    while (err == ESP_OK && left) {
        const size_t n = left < CHUNK_BYTES ? left : CHUNK_BYTES;
        if (fread(buffer, 1, n, pack) != n || fwrite(buffer, 1, n, image) != n) err = ESP_FAIL;
        left -= (uint32_t)n;
    }
    if (image) {
        if (err == ESP_OK && (fflush(image) || fsync(fileno(image)))) err = ESP_FAIL;
        if (fclose(image)) err = ESP_FAIL;
    }
    if (pack && fclose(pack)) err = ESP_FAIL;
    if (err == ESP_OK) {
        FILE *check = fopen(IMAGE_PARTIAL, "rb");
        uint8_t actual[32];
        if (!check || hash_region(check, 0, info->image_size, actual) ||
            memcmp(actual, info->image_sha256, 32)) err = ESP_ERR_INVALID_CRC;
        if (check && fclose(check)) err = ESP_FAIL;
    }
    /* Publishing the files does not authorize installation. Recovery consumes
     * only a separately committed, validated transaction record. */
    if (err == ESP_OK) err = mosaico_nand_store_sync();
    if (err == ESP_OK && rename(PACK_PARTIAL, PACK_READY)) err = ESP_FAIL;
    if (err == ESP_OK && rename(IMAGE_PARTIAL, IMAGE_READY)) err = ESP_FAIL;
    if (err == ESP_OK) err = mosaico_nand_store_sync();
    if (err != ESP_OK) { unlink(PACK_PARTIAL); unlink(IMAGE_PARTIAL); }
    result->image_stored = err == ESP_OK;
    ESP_LOGI(TAG, "candidate download/validation result=%s; active UI untouched", esp_err_to_name(err));
    free(info); free(buffer);
    return err;
}
