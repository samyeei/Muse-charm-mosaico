// SPDX-License-Identifier: Apache-2.0
#include "companion_character_backup.h"
#include "companion_character_image.h"
#include "mosaico_character_journal.h"
#include "mosaico_nand_store.h"
#include "esp_partition.h"
#include "psa/crypto.h"
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define INITIAL_PATH MOSAICO_NAND_MOUNT_PATH "/muse-character/factory-ui.bin"
#define RECORD_PATH MOSAICO_NAND_MOUNT_PATH "/muse-character/factory-ui.record"
#define RESTORE_PATH MOSAICO_NAND_MOUNT_PATH "/muse-character/factory-ui-restore.bin"
enum { CHUNK_BYTES = 4096, MAX_UI_BYTES = 3 * 1024 * 1024 };
typedef struct { uint8_t magic[8]; uint32_t partition_bytes, partition_address; uint8_t sha256[32]; } backup_record_t;
static const uint8_t record_magic[8] = {'M','C','F','A','C','T',0,1};

static esp_err_t hash_region(const char *path, size_t offset, size_t expected,
    bool exact_eof, uint8_t hash[32])
{
    FILE *file = fopen(path, "rb");
    if (file == NULL) return ESP_ERR_NOT_FOUND;
    uint8_t *chunk = malloc(CHUNK_BYTES);
    if (chunk == NULL) { fclose(file); return ESP_ERR_NO_MEM; }
    psa_hash_operation_t operation = PSA_HASH_OPERATION_INIT;
    bool valid = fseek(file, (long)offset, SEEK_SET) == 0 &&
        psa_crypto_init() == PSA_SUCCESS && psa_hash_setup(&operation, PSA_ALG_SHA_256) == PSA_SUCCESS;
    size_t left = expected;
    while (valid && left != 0) {
        size_t count = left < CHUNK_BYTES ? left : CHUNK_BYTES;
        valid = fread(chunk, 1, count, file) == count && psa_hash_update(&operation, chunk, count) == PSA_SUCCESS;
        left -= count;
    }
    size_t written = 0;
    if (valid) valid = (!exact_eof || (fgetc(file) == EOF && !ferror(file))) &&
        psa_hash_finish(&operation, hash, 32, &written) == PSA_SUCCESS && written == 32;
    if (!valid) (void)psa_hash_abort(&operation);
    if (fclose(file) != 0) valid = false;
    free(chunk);
    return valid ? ESP_OK : ESP_FAIL;
}

static esp_err_t hash_file(const char *path, size_t expected, uint8_t hash[32])
{
    return hash_region(path, 0, expected, true, hash);
}

static esp_err_t verify_existing(backup_record_t *out)
{
    backup_record_t record;
    FILE *file = fopen(RECORD_PATH, "rb");
    if (file == NULL) return ESP_ERR_NOT_FOUND;
    bool valid = fread(&record, 1, sizeof(record), file) == sizeof(record) &&
        fgetc(file) == EOF && !ferror(file);
    if (fclose(file) != 0) valid = false;
    if (!valid || memcmp(record.magic, record_magic, sizeof(record_magic)) != 0 ||
        record.partition_bytes == 0 || record.partition_bytes > MAX_UI_BYTES) return ESP_ERR_INVALID_RESPONSE;
    const esp_partition_t *partition = esp_partition_find_first(ESP_PARTITION_TYPE_DATA,
        ESP_PARTITION_SUBTYPE_ANY, "ui_apps");
    if (!partition || partition->size != record.partition_bytes ||
        partition->address != record.partition_address) return ESP_ERR_INVALID_STATE;
    uint8_t hash[32];
    esp_err_t err = hash_file(INITIAL_PATH, record.partition_bytes, hash);
    if (err == ESP_OK && memcmp(hash, record.sha256, sizeof(hash)) != 0) err = ESP_ERR_INVALID_CRC;
    if (err == ESP_OK && out) *out = record;
    return err;
}

static uint32_t read_u32_le(const uint8_t *bytes)
{
    return (uint32_t)bytes[0] | (uint32_t)bytes[1] << 8 |
        (uint32_t)bytes[2] << 16 | (uint32_t)bytes[3] << 24;
}

esp_err_t companion_character_backup_prepare_restore(companion_character_initial_info_t *info)
{
    if (!info) return ESP_ERR_INVALID_ARG;
    memset(info, 0, sizeof(*info));
    backup_record_t record;
    esp_err_t err = verify_existing(&record);
    if (err != ESP_OK) return err;
    FILE *source = fopen(INITIAL_PATH, "rb");
    if (!source) return ESP_ERR_NOT_FOUND;
    uint8_t header[64];
    if (fread(header, 1, sizeof(header), source) != sizeof(header)) err = ESP_FAIL;
    uint32_t bytes = 0;
    if (err == ESP_OK) {
        const uint32_t payload = read_u32_le(header + 12);
        if (record.partition_bytes < sizeof(header) ||
            payload > record.partition_bytes - sizeof(header)) err = ESP_ERR_INVALID_SIZE;
        else bytes = payload + sizeof(header);
    }
    if (err == ESP_OK && !mosaico_character_image_header_valid(header, sizeof(header), bytes))
        err = ESP_ERR_INVALID_RESPONSE;
    uint8_t digest[32];
    if (err == ESP_OK) err = hash_region(INITIAL_PATH, 64, bytes - 64, false, digest);
    if (err == ESP_OK && memcmp(digest, header + 16, sizeof(digest))) err = ESP_ERR_INVALID_CRC;
    if (err == ESP_OK) err = companion_character_image_validate(source, 0, bytes);
    if (err == ESP_OK) err = hash_region(INITIAL_PATH, 0, bytes, false, digest);
    size_t total = 0, used = 0;
    if (err == ESP_OK) err = mosaico_nand_store_usage(&total, &used);
    if (err == ESP_OK && (total < used || total - used < bytes + 65536U)) err = ESP_ERR_NO_MEM;
    uint8_t *chunk = err == ESP_OK ? malloc(CHUNK_BYTES) : NULL;
    if (err == ESP_OK && !chunk) err = ESP_ERR_NO_MEM;
    FILE *target = err == ESP_OK ? fopen(RESTORE_PATH ".partial", "wb") : NULL;
    if (err == ESP_OK && (!target || fseek(source, 0, SEEK_SET))) err = ESP_FAIL;
    uint32_t left = bytes;
    while (err == ESP_OK && left) {
        const size_t count = left < CHUNK_BYTES ? left : CHUNK_BYTES;
        if (fread(chunk, 1, count, source) != count || fwrite(chunk, 1, count, target) != count)
            err = ESP_FAIL;
        left -= count;
    }
    if (target) {
        if (err == ESP_OK && (fflush(target) || fsync(fileno(target)))) err = ESP_FAIL;
        if (fclose(target)) err = ESP_FAIL;
    }
    if (fclose(source)) err = ESP_FAIL;
    free(chunk);
    uint8_t actual[32];
    if (err == ESP_OK) err = hash_file(RESTORE_PATH ".partial", bytes, actual);
    if (err == ESP_OK && memcmp(actual, digest, sizeof(actual))) err = ESP_ERR_INVALID_CRC;
    if (err == ESP_OK) err = mosaico_nand_store_sync();
    if (err == ESP_OK && rename(RESTORE_PATH ".partial", RESTORE_PATH)) err = ESP_FAIL;
    if (err == ESP_OK) err = mosaico_nand_store_sync();
    if (err == ESP_OK) err = hash_file(RESTORE_PATH, bytes, actual);
    if (err == ESP_OK && memcmp(actual, digest, sizeof(actual))) err = ESP_ERR_INVALID_CRC;
    if (err == ESP_OK) {
        info->partition_address = record.partition_address;
        info->partition_bytes = record.partition_bytes;
        info->image_bytes = bytes;
        memcpy(info->partition_sha256, record.sha256, 32);
        memcpy(info->image_sha256, digest, 32);
    }
    /* A rename may already be durable when the final sync fails. The output
     * file alone is never install authority; leave it for inspection/retry. */
    return err;
}

esp_err_t companion_character_backup_initial(void)
{
    struct stat status;
    if (stat(RECORD_PATH, &status) == 0) return verify_existing(NULL);
    if (errno != ENOENT) return ESP_FAIL;
    /* Incomplete/unknown initial backups require inspection; do not silently
     * replace them with whichever character happens to be active now. */
    if (stat(INITIAL_PATH, &status) == 0 || errno != ENOENT) return ESP_ERR_INVALID_STATE;
    const esp_partition_t *partition = esp_partition_find_first(ESP_PARTITION_TYPE_DATA,
                                                               ESP_PARTITION_SUBTYPE_ANY, "ui_apps");
    if (partition == NULL || partition->size == 0 || partition->size > MAX_UI_BYTES) return ESP_ERR_NOT_FOUND;
    uint8_t header[64];
    esp_err_t err = esp_partition_read(partition, 0, header, sizeof(header));
    if (err != ESP_OK) return err;
    const uint8_t magic[8] = {'M','O','S','G','S','P',0,1};
    if (memcmp(header, magic, 8) != 0) return ESP_ERR_INVALID_RESPONSE;
    size_t total = 0, used = 0;
    err = mosaico_nand_store_usage(&total, &used);
    if (err != ESP_OK) return err;
    if (total < used || total - used < partition->size + 65536U) return ESP_ERR_NO_MEM;
    if (mkdir(MOSAICO_NAND_MOUNT_PATH "/muse-character", 0700) != 0 && errno != EEXIST) return ESP_FAIL;
    uint8_t *chunk = malloc(CHUNK_BYTES);
    if (chunk == NULL) return ESP_ERR_NO_MEM;
    FILE *file = fopen(INITIAL_PATH ".partial", "wb");
    if (file == NULL) { free(chunk); return ESP_FAIL; }
    psa_hash_operation_t operation = PSA_HASH_OPERATION_INIT;
    bool valid = psa_crypto_init() == PSA_SUCCESS && psa_hash_setup(&operation, PSA_ALG_SHA_256) == PSA_SUCCESS;
    for (size_t offset = 0; valid && offset < partition->size;) {
        size_t count = partition->size - offset;
        if (count > CHUNK_BYTES) count = CHUNK_BYTES;
        valid = esp_partition_read(partition, offset, chunk, count) == ESP_OK &&
                fwrite(chunk, 1, count, file) == count && psa_hash_update(&operation, chunk, count) == PSA_SUCCESS;
        offset += count;
    }
    backup_record_t record = {.partition_bytes = partition->size, .partition_address = partition->address};
    memcpy(record.magic, record_magic, sizeof(record_magic));
    size_t hash_bytes = 0;
    if (valid) valid = psa_hash_finish(&operation, record.sha256, sizeof(record.sha256), &hash_bytes) == PSA_SUCCESS && hash_bytes == 32;
    if (!valid) (void)psa_hash_abort(&operation);
    if (valid) valid = fflush(file) == 0 && fsync(fileno(file)) == 0;
    if (fclose(file) != 0) valid = false;
    free(chunk);
    if (!valid) return ESP_FAIL;
    uint8_t readback[32];
    err = hash_file(INITIAL_PATH ".partial", partition->size, readback);
    if (err != ESP_OK) return err;
    if (memcmp(readback, record.sha256, 32) != 0) return ESP_ERR_INVALID_CRC;
    if (rename(INITIAL_PATH ".partial", INITIAL_PATH) != 0) return ESP_FAIL;
    err = mosaico_nand_store_sync();
    if (err != ESP_OK) return err;
    file = fopen(RECORD_PATH ".partial", "wb");
    if (file == NULL) return ESP_FAIL;
    valid = fwrite(&record, 1, sizeof(record), file) == sizeof(record);
    if (valid) valid = fflush(file) == 0 && fsync(fileno(file)) == 0;
    if (fclose(file) != 0) valid = false;
    if (!valid || rename(RECORD_PATH ".partial", RECORD_PATH) != 0) return ESP_FAIL;
    err = mosaico_nand_store_sync();
    if (err == ESP_OK) err = verify_existing(NULL);
    return err;
}
