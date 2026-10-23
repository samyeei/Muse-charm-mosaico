// SPDX-License-Identifier: Apache-2.0
#include "companion_character_storage.h"
#include "companion_character_backup.h"
#include "companion_character_download.h"
#include "companion_character_stage.h"
#include "mosaico_nand_store.h"
#include "esp_iris.h"
#include "esp_random.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

enum { SERVICE = 0x7e12, STATUS = 1, WRITE_PROBE = 2, READ_PROBE = 3, BACKUP_INITIAL = 4, EXPORT_READ = 5, EXPORT_BAD_BLOCKS = 6, INITIALIZE_EMPTY = 7, SYNC_REBOOT = 8, DOWNLOAD = 9, DOWNLOAD_STATUS = 10, RESTORE_INITIAL = 11, RESTORE_STATUS = 12, PROBE_BYTES = 32 };
enum { EXPORT_CHUNK_BYTES = 1024 };
/* Deliberate maintenance command, never part of a Muse text or network flow. */
static const char initialize_confirmation[] = "INIT_NAND_V1";
static const char restore_confirmation[] = "RESTORE_INITIAL_V1 ";
#define STORAGE_ROOT MOSAICO_NAND_MOUNT_PATH "/muse-character"
#define PROBE_PATH STORAGE_ROOT "/persistence-probe.bin"
#define PROBE_TEMP STORAGE_ROOT "/persistence-probe.partial"
static esp_err_t mount_result = ESP_ERR_INVALID_STATE;
static StaticSemaphore_t lock_storage;
static SemaphoreHandle_t lock;
static bool restarting;
enum { BACKUP_NONE, BACKUP_RUNNING, BACKUP_READY, BACKUP_FAILED };
/* RPC owner serializes filesystem calls. During a background backup, status
 * uses cached usage and all other commands are refused until the worker ends. */
static unsigned backup_state;
static esp_err_t backup_result = ESP_ERR_INVALID_STATE;
static size_t cached_total, cached_used;
static unsigned download_state;
static esp_err_t download_result = ESP_ERR_INVALID_STATE;
static companion_character_download_result_t download_details;
static uint64_t download_token;
static bool download_handoff_pending; /* Protected by lock; no claim about NOR after handoff. */
static unsigned restore_state;
static esp_err_t restore_result = ESP_ERR_INVALID_STATE;
static uint64_t restore_token;
static char restore_job[64];

static bool storage_busy(void)
{
    return restarting || backup_state == BACKUP_RUNNING ||
        download_state == BACKUP_RUNNING || restore_state == BACKUP_RUNNING;
}

static bool restore_job_valid(const char *job)
{
    if (!job || !*job || strnlen(job, sizeof(restore_job)) >= sizeof(restore_job)) return false;
    for (const unsigned char *p = (const unsigned char *)job; *p; ++p)
        if (!((*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z') ||
            (*p >= '0' && *p <= '9') || *p == '-' || *p == '_' || *p == '.')) return false;
    return true;
}

static void restore_worker(void *context)
{
    char *job = context;
    bool prepared = false;
    esp_err_t result = companion_character_stage_initial(job, &prepared);
    free(job);
    size_t total = 0, used = 0;
    /* Usage scan stays outside the RPC mutex; queries see cached counters. */
    esp_err_t usage = prepared || result == ESP_ERR_NOT_SUPPORTED ?
        ESP_ERR_INVALID_STATE : mosaico_nand_store_usage(&total, &used);
    if (xSemaphoreTake(lock, portMAX_DELAY) == pdTRUE) {
        if (usage == ESP_OK) { cached_total = total; cached_used = used; }
        restore_result = result;
        restore_state = result == ESP_OK ? BACKUP_READY : BACKUP_FAILED;
        if (prepared) restarting = true; /* uncertain journal publication owns NAND */
        xSemaphoreGive(lock);
    }
    vTaskDelete(NULL);
}

/* Lock protects job and cached state; the worker owns its immutable heap copy. */
static esp_err_t restore_start_locked(const char *job, uint64_t *token)
{
    if (!token || !restore_job_valid(job)) return ESP_ERR_INVALID_ARG;
    if (mount_result != ESP_OK || restarting) return ESP_ERR_INVALID_STATE;
    if (restore_token && !strcmp(job, restore_job)) {
        *token = restore_token; /* same local task never requeues or resets result */
        return ESP_OK;
    }
    if (storage_busy()) return ESP_ERR_INVALID_STATE;
    if (restore_token == UINT64_MAX) return ESP_ERR_INVALID_STATE;
    char *copy = malloc(strlen(job) + 1);
    if (!copy) return ESP_ERR_NO_MEM;
    strcpy(copy, job);
    if (xTaskCreate(restore_worker, "character_restore", 8192, copy, 3, NULL) != pdPASS) {
        free(copy); return ESP_ERR_NO_MEM;
    }
    strcpy(restore_job, job);
    *token = ++restore_token;
    restore_state = BACKUP_RUNNING;
    restore_result = ESP_ERR_INVALID_STATE;
    return ESP_OK;
}

esp_err_t companion_character_storage_restore_start(const char *job, uint64_t *token)
{
    if (!lock || xSemaphoreTake(lock, 0) != pdTRUE) return ESP_ERR_INVALID_STATE;
    esp_err_t err = restore_start_locked(job, token);
    xSemaphoreGive(lock);
    return err;
}

esp_err_t companion_character_storage_restore_poll(uint64_t token, bool *finished, esp_err_t *result)
{
    if (!token || !finished || !result) return ESP_ERR_INVALID_ARG;
    *finished = false;
    if (!lock || xSemaphoreTake(lock, 0) != pdTRUE) return ESP_ERR_INVALID_STATE;
    esp_err_t err = token == restore_token ? ESP_OK : ESP_ERR_NOT_FOUND;
    if (err == ESP_OK) {
        *finished = restore_state == BACKUP_READY || restore_state == BACKUP_FAILED;
        *result = restore_result;
    }
    xSemaphoreGive(lock);
    return err;
}

static void download_worker(void *context);
/* Caller holds lock. The worker owns its heap copy until completion. */
static esp_err_t download_start_locked(const character_command_t *source, uint64_t *token)
{
    if (mount_result != ESP_OK || storage_busy()) return ESP_ERR_INVALID_STATE;
    if (!source || source->kind != CHARACTER_STATUS || source->status != CHARACTER_READY ||
        !token || download_token == UINT64_MAX) return ESP_ERR_INVALID_ARG;
    character_command_t *copy = malloc(sizeof(*copy));
    if (!copy) return ESP_ERR_NO_MEM;
    *copy = *source;
    if (xTaskCreate(download_worker, "character_dl", 8192, copy, 3, NULL) != pdPASS) {
        free(copy); return ESP_ERR_NO_MEM;
    }
    ++download_token;
    *token = download_token;
    download_state = BACKUP_RUNNING;
    download_result = ESP_ERR_INVALID_STATE;
    download_handoff_pending = false;
    memset(&download_details, 0, sizeof(download_details));
    return ESP_OK;
}

esp_err_t companion_character_storage_download_start(void *context,
    const character_command_t *command, uint64_t *token)
{
    (void)context;
    if (!lock) return ESP_ERR_INVALID_STATE;
    if (xSemaphoreTake(lock, 0) != pdTRUE) return ESP_ERR_INVALID_STATE;
    esp_err_t err = download_start_locked(command, token);
    xSemaphoreGive(lock);
    return err;
}

esp_err_t companion_character_storage_download_poll(void *context, uint64_t token,
    bool *finished, esp_err_t *result)
{
    (void)context;
    if (!finished || !result || !token) return ESP_ERR_INVALID_ARG;
    *finished = false;
    if (!lock || xSemaphoreTake(lock, 0) != pdTRUE) return ESP_ERR_INVALID_STATE;
    esp_err_t err = token == download_token ? ESP_OK : ESP_ERR_NOT_FOUND;
    if (err == ESP_OK) {
        *finished = download_state == BACKUP_READY || download_state == BACKUP_FAILED;
        *result = download_result;
    }
    xSemaphoreGive(lock);
    return err;
}

static void download_worker(void *context)
{
    character_command_t *command = context;
    companion_character_download_result_t details = {0};
    esp_err_t result = companion_character_download(command, &details);
    bool prepared = false;
    if (result == ESP_OK && command->deadline_ms &&
        (uint64_t)(esp_timer_get_time() / 1000) >= command->deadline_ms)
        result = ESP_ERR_TIMEOUT;
    if (result == ESP_OK) {
        esp_err_t staged = companion_character_stage(command, &prepared);
        /* Candidate gate closed is a successful download, not an installation.
         * Other staging failures are reported without claiming a role change. */
        if (command->deadline_ms || staged != ESP_ERR_NOT_SUPPORTED || prepared) result = staged;
    }
    free(command);
    size_t total = 0, used = 0;
    esp_err_t usage_result = prepared ? ESP_ERR_INVALID_STATE :
        mosaico_nand_store_usage(&total, &used);
    if (xSemaphoreTake(lock, portMAX_DELAY) == pdTRUE) {
        if (usage_result == ESP_OK) { cached_total = total; cached_used = used; }
        download_result = result;
        download_handoff_pending = prepared;
        if (prepared) restarting = true; /* no further mutable NAND operations */
        download_details = details;
        download_state = result == ESP_OK ? BACKUP_READY : BACKUP_FAILED;
        xSemaphoreGive(lock);
    }
    vTaskDelete(NULL);
}

static void backup_initial_worker(void *context)
{
    (void)context;
    esp_err_t result = companion_character_backup_initial();
    size_t total = 0, used = 0;
    esp_err_t usage_result = mosaico_nand_store_usage(&total, &used);
    if (xSemaphoreTake(lock, portMAX_DELAY) == pdTRUE) {
        if (usage_result == ESP_OK) { cached_total = total; cached_used = used; }
        backup_result = result;
        backup_state = result == ESP_OK ? BACKUP_READY : BACKUP_FAILED;
        xSemaphoreGive(lock);
    }
    vTaskDelete(NULL);
}

static void restart_after_sync(void *context)
{
    (void)context;
    /* Let the successful USB response be delivered before the planned boot. */
    vTaskDelay(pdMS_TO_TICKS(1000));
    esp_restart();
}

static esp_err_t read_probe(uint8_t value[PROBE_BYTES])
{
    FILE *file = fopen(PROBE_PATH, "rb");
    if (file == NULL) return ESP_ERR_NOT_FOUND;
    const bool valid = fread(value, 1, PROBE_BYTES, file) == PROBE_BYTES && fgetc(file) == EOF;
    const int closed = fclose(file);
    return valid && closed == 0 ? ESP_OK : ESP_FAIL;
}

static esp_err_t write_probe(uint8_t value[PROBE_BYTES])
{
    if (mkdir(STORAGE_ROOT, 0700) != 0 && errno != EEXIST) return ESP_FAIL;
    esp_fill_random(value, PROBE_BYTES);
    FILE *file = fopen(PROBE_TEMP, "wb");
    if (file == NULL) return ESP_FAIL;
    bool valid = fwrite(value, 1, PROBE_BYTES, file) == PROBE_BYTES;
    if (valid) valid = fflush(file) == 0 && fsync(fileno(file)) == 0;
    if (fclose(file) != 0) valid = false;
    if (!valid || rename(PROBE_TEMP, PROBE_PATH) != 0) return ESP_FAIL;
    esp_err_t synced = mosaico_nand_store_sync();
    if (synced != ESP_OK) return synced;
    uint8_t readback[PROBE_BYTES];
    esp_err_t err = read_probe(readback);
    if (err == ESP_OK && memcmp(readback, value, PROBE_BYTES) != 0) err = ESP_FAIL;
    return err;
}

static esp_err_t storage_rpc(const esp_iris_rpc_request_t *request, uint8_t *response,
                             size_t capacity, size_t *size, void *context)
{
    (void)context;
    if (request == NULL || response == NULL || size == NULL) return ESP_ERR_INVALID_ARG;
    *size = 0;
    const unsigned method = request->payload_size > 0 ? request->payload[0] : STATUS;
    if (method < STATUS || method > RESTORE_STATUS) return ESP_ERR_INVALID_ARG;
    if ((method == EXPORT_READ && request->payload_size != 10) ||
        (method == EXPORT_BAD_BLOCKS && request->payload_size != 9) ||
        ((method < EXPORT_READ || method == SYNC_REBOOT || method == DOWNLOAD_STATUS || method == RESTORE_STATUS) && request->payload_size > 1) ||
        (method == DOWNLOAD && (request->payload_size < 2 || request->payload_size > 2048 ||
          memchr(request->payload + 1, 0, request->payload_size - 1))) ||
        (method == INITIALIZE_EMPTY &&
         (request->payload_size != sizeof(initialize_confirmation) ||
          memcmp(request->payload + 1, initialize_confirmation, sizeof(initialize_confirmation) - 1) != 0)))
        return ESP_ERR_INVALID_ARG;
    esp_iris_status_t iris = {0};
    esp_err_t err = esp_iris_get_status(&iris);
    if (err != ESP_OK) return err;
    if (!iris.session_ready || iris.transport != ESP_IRIS_TRANSPORT_KIND_USB) return ESP_ERR_NOT_ALLOWED;
    char requested_job[64] = {0};
    if (method == RESTORE_INITIAL) {
        const size_t prefix = sizeof(restore_confirmation) - 1;
        if (request->payload_size <= 1 + prefix || request->payload_size > prefix + sizeof(requested_job) ||
            memcmp(request->payload + 1, restore_confirmation, prefix) ||
            memchr(request->payload + 1, 0, request->payload_size - 1)) return ESP_ERR_INVALID_ARG;
        memcpy(requested_job, request->payload + 1 + prefix, request->payload_size - 1 - prefix);
        if (!restore_job_valid(requested_job)) return ESP_ERR_INVALID_ARG;
    }
    if (xSemaphoreTake(lock, pdMS_TO_TICKS(5000)) != pdTRUE) return ESP_ERR_TIMEOUT;
    if (restarting && method != RESTORE_STATUS && method != DOWNLOAD_STATUS && method != STATUS) {
        xSemaphoreGive(lock); return ESP_ERR_INVALID_STATE;
    }
    if (storage_busy() && method != STATUS && method != DOWNLOAD_STATUS && method != RESTORE_STATUS &&
        !(method == RESTORE_INITIAL && !strcmp(requested_job, restore_job))) {
        xSemaphoreGive(lock);
        return ESP_ERR_INVALID_STATE;
    }
    if (method == RESTORE_INITIAL || method == RESTORE_STATUS) {
        uint64_t token = restore_token;
        if (method == RESTORE_INITIAL) err = restore_start_locked(requested_job, &token);
        if (err != ESP_OK) { xSemaphoreGive(lock); return err; }
        const int written = snprintf((char *)response, capacity,
            "{\"restore_state\":%u,\"restore_result\":%ld,\"token\":%llu,\"job\":\"%s\",\"handoff_pending\":%s}",
            restore_state, (long)restore_result, (unsigned long long)token, restore_job,
            restarting ? "true" : "false");
        xSemaphoreGive(lock);
        if (written < 0 || (size_t)written >= capacity) return ESP_ERR_INVALID_SIZE;
        *size = (size_t)written;
        return ESP_OK;
    }
    if (method == DOWNLOAD || method == DOWNLOAD_STATUS) {
        if (method == DOWNLOAD) {
            character_command_t *command = calloc(1, sizeof(*command));
            char *text = calloc(1, request->payload_size);
            if (!command || !text) { free(command); free(text); xSemaphoreGive(lock); return ESP_ERR_NO_MEM; }
            memcpy(text, request->payload + 1, request->payload_size - 1);
            const character_command_kind_t kind = companion_character_extract(text, command);
            free(text);
            if (mount_result != ESP_OK || kind != CHARACTER_STATUS || command->status != CHARACTER_READY) {
                free(command); xSemaphoreGive(lock); return ESP_ERR_INVALID_ARG;
            }
            uint64_t token = 0;
            err = download_start_locked(command, &token);
            free(command);
            if (err != ESP_OK) { xSemaphoreGive(lock); return err; }
        }
        const int written = snprintf((char *)response, capacity,
            "{\"download_state\":%u,\"download_result\":%ld,\"bytes_received\":%u,\"http_status\":%d,"
            "\"body_complete\":%s,\"pack_validated\":%s,\"image_stored\":%s,\"handoff_pending\":%s,\"active_ui_untouched\":%s}",
            download_state, (long)download_result, (unsigned)download_details.bytes_received,
            download_details.http_status, download_details.body_complete ? "true" : "false",
            download_details.pack_validated ? "true" : "false", download_details.image_stored ? "true" : "false",
            download_handoff_pending ? "true" : "false", download_handoff_pending ? "null" : "true");
        xSemaphoreGive(lock);
        if (written < 0 || (size_t)written >= capacity) return ESP_ERR_INVALID_SIZE;
        *size = (size_t)written;
        return ESP_OK;
    }
    if (method == SYNC_REBOOT) {
        err = mosaico_nand_store_sync();
        if (err == ESP_OK) err = esp_iris_mark_planned_restart();
        if (err == ESP_OK) {
            restarting = true;
            if (xTaskCreate(restart_after_sync, "nand_restart", 2048, NULL, 3, NULL) != pdPASS) {
                restarting = false;
                err = ESP_ERR_NO_MEM;
            }
        }
        xSemaphoreGive(lock);
        return err;
    }
    if (method == INITIALIZE_EMPTY) {
        /* Single explicit USB request. Refuse repeat initialization once the
         * filesystem is mounted; startup never formats on a mount failure. */
        err = mosaico_nand_store_initialize_empty();
        if (err != ESP_OK) { xSemaphoreGive(lock); return err; }
        mount_result = ESP_OK;
    }
    if (method == EXPORT_BAD_BLOCKS) {
        uint32_t first = 0, count = 0;
        for (unsigned i = 0; i < 4; ++i) {
            first |= (uint32_t)request->payload[1 + i] << (8 * i);
            count |= (uint32_t)request->payload[5 + i] << (8 * i);
        }
        if (count == 0 || count > 128 || count > capacity) err = ESP_ERR_INVALID_SIZE;
        else err = mosaico_nand_store_readonly_bad_blocks(first, response, count);
        xSemaphoreGive(lock);
        if (err == ESP_OK) *size = count;
        return err;
    }
    if (method == EXPORT_READ) {
        const uint8_t *data = request->payload;
        uint32_t address = 0, bytes = 0;
        for (unsigned i = 0; i < 4; ++i) {
            address |= (uint32_t)data[2 + i] << (8 * i);
            bytes |= (uint32_t)data[6 + i] << (8 * i);
        }
        if (data[1] > 1 || bytes != EXPORT_CHUNK_BYTES || address % EXPORT_CHUNK_BYTES || bytes > capacity) err = ESP_ERR_INVALID_ARG;
        else err = mosaico_nand_store_readonly_read(data[1] == 0, address, response, bytes);
        xSemaphoreGive(lock);
        if (err == ESP_OK) *size = bytes;
        return err;
    }
    size_t total = cached_total, used = cached_used;
    uint8_t probe[PROBE_BYTES] = {0};
    char hex[PROBE_BYTES * 2 + 1] = {0};
    const mosaico_nand_diagnostics_t *diagnostic = mosaico_nand_store_diagnostics();
    /* Rare USB diagnostic response; avoid a large task-stack array. Encode
     * retained SDK text so quotes/ANSI bytes cannot corrupt the JSON envelope. */
    char *error_hex = calloc(1, sizeof(diagnostic->filesystem_error) * 2 + 1);
    if (error_hex == NULL) { xSemaphoreGive(lock); return ESP_ERR_NO_MEM; }
    size_t error_bytes = strnlen(diagnostic->filesystem_error, sizeof(diagnostic->filesystem_error));
    for (size_t i = 0; i < error_bytes; ++i)
        snprintf(error_hex + i * 2, 3, "%02x", (unsigned char)diagnostic->filesystem_error[i]);
    err = mount_result;
    /* lfs_fs_size walks the allocated filesystem. After a multi-megabyte
     * candidate is stored this can exceed the device RPC deadline. Ordinary
     * queries only read the snapshots published by storage workers. */
    if (err == ESP_OK && method == INITIALIZE_EMPTY) {
        err = mosaico_nand_store_usage(&total, &used);
        if (err == ESP_OK) { cached_total = total; cached_used = used; }
    }
    if (err == ESP_OK && method == WRITE_PROBE) err = write_probe(probe);
    if (err == ESP_OK && method == READ_PROBE) err = read_probe(probe);
    if (err == ESP_OK && method == BACKUP_INITIAL && backup_state != BACKUP_RUNNING) {
        backup_state = BACKUP_RUNNING;
        backup_result = ESP_ERR_INVALID_STATE;
        if (xTaskCreate(backup_initial_worker, "nand_backup", 6144, NULL, 3, NULL) != pdPASS) {
            backup_state = BACKUP_FAILED;
            backup_result = ESP_ERR_NO_MEM;
            err = ESP_ERR_NO_MEM;
        }
    }
    if (err == ESP_OK && (method == WRITE_PROBE || method == READ_PROBE)) {
        for (unsigned i = 0; i < PROBE_BYTES; ++i) snprintf(hex + i * 2, 3, "%02x", probe[i]);
    }
    const int written = snprintf((char *)response, capacity,
        "{\"mounted\":%s,\"total\":%u,\"used\":%u,\"free\":%u,\"usage_cached\":true,\"usage_known\":%s,\"result\":%ld,\"backup_state\":%u,\"backup_result\":%ld,\"probe\":\"%s\",\"mount_stage\":\"%s\","
        "\"logical_bytes\":%llu,\"page_bytes\":%u,\"sample0_result\":%ld,\"sample1_result\":%ld,"
        "\"sample0_non_ff\":%u,\"sample1_non_ff\":%u,\"sample0_zero\":%u,\"sample1_zero\":%u,"
        "\"raw\":%s,\"physical_bytes\":%llu,\"raw_blocks_checked\":%u,\"raw_blocks_non_ff\":%u,"
        "\"raw_blocks_read_errors\":%u,\"sample0_format\":%u,\"sample1_format\":%u,"
        "\"denied_writes\":%u,\"sample0_prefix_hex\":\"%s\",\"sample1_prefix_hex\":\"%s\",\"filesystem_error_hex\":\"%s\"}",
        mount_result == ESP_OK ? "true" : "false", (unsigned)total, (unsigned)used,
        (unsigned)(total >= used ? total - used : 0), total != 0 ? "true" : "false", (long)err, backup_state, (long)backup_result, hex, mosaico_nand_store_mount_stage(),
        (unsigned long long)(diagnostic->raw ? 0 : diagnostic->logical_bytes), (unsigned)diagnostic->page_bytes,
        (long)diagnostic->sample_result[0], (long)diagnostic->sample_result[1],
        (unsigned)diagnostic->sample_non_ff[0], (unsigned)diagnostic->sample_non_ff[1],
        (unsigned)diagnostic->sample_zero[0], (unsigned)diagnostic->sample_zero[1],
        diagnostic->raw ? "true" : "false", (unsigned long long)(diagnostic->raw ? diagnostic->logical_bytes : 0),
        (unsigned)diagnostic->raw_blocks_checked, (unsigned)diagnostic->raw_blocks_non_ff,
        (unsigned)diagnostic->raw_blocks_read_errors, (unsigned)diagnostic->sample_format[0],
        (unsigned)diagnostic->sample_format[1], (unsigned)diagnostic->denied_writes,
        diagnostic->sample_prefix_hex[0], diagnostic->sample_prefix_hex[1], error_hex);
    free(error_hex);
    xSemaphoreGive(lock);
    if (written < 0 || (size_t)written >= capacity) return ESP_ERR_INVALID_SIZE;
    *size = (size_t)written;
    return ESP_OK;
}

esp_err_t companion_character_storage_start(void)
{
    lock = xSemaphoreCreateMutexStatic(&lock_storage);
    if (lock == NULL) return ESP_ERR_NO_MEM;
    /* Ordinary boots never format. Explicit USB initialization is separate;
     * an unrecognized filesystem leaves normal conversation usable. */
    mount_result = mosaico_nand_store_mount();
    /* Existing app uses eleven of twelve RPC entries. Keep diagnostics in
     * one dispatcher instead of exhausting the registry before voice starts. */
    esp_err_t err = esp_iris_rpc_register(SERVICE, STATUS, storage_rpc, NULL);
    if (err != ESP_OK) return err;
    return mount_result;
}
