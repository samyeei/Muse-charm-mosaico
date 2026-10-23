// SPDX-License-Identifier: Apache-2.0
#include "companion_character_trial.h"
#include "mosaico_character_journal_store.h"
#include "mosaico_character_install_policy.h"
#include "mosaico_nand_store.h"
#include "esp_partition.h"
#include "esp_ota_ops.h"
#include "psa/crypto.h"
#include <stdlib.h>
#include <string.h>

/* Match Recovery's admission of the protected local initial image. */
static const bool initial_restore_enabled = true;
enum { HASH_CHUNK_BYTES = 4096, FIRST_FRAME_TIMEOUT_MS = 5000 };
struct companion_character_trial_t {
    mosaico_character_journal_store_handle_t store;
    mosaico_character_record_t record;
    const esp_partition_t *partition;
    bool trial;
    bool restored;
};

static esp_err_t verify_image(companion_character_trial_handle_t h)
{
    uint8_t *chunk = malloc(HASH_CHUNK_BYTES);
    if (!chunk) return ESP_ERR_NO_MEM;
    psa_hash_operation_t hash = PSA_HASH_OPERATION_INIT;
    uint8_t digest[32]; size_t digest_bytes = 0;
    esp_err_t err = psa_crypto_init() == PSA_SUCCESS &&
        psa_hash_setup(&hash, PSA_ALG_SHA_256) == PSA_SUCCESS ? ESP_OK : ESP_FAIL;
    const size_t image_bytes = h->restored ? h->record.partition_bytes : h->record.image_bytes;
    const uint8_t *expected = h->restored ? h->record.backup_sha256 : h->record.image_sha256;
    for (size_t offset = 0; err == ESP_OK && offset < image_bytes;) {
        size_t bytes = image_bytes - offset;
        if (bytes > HASH_CHUNK_BYTES) bytes = HASH_CHUNK_BYTES;
        err = esp_partition_read(h->partition, offset, chunk, bytes);
        if (err == ESP_OK && psa_hash_update(&hash, chunk, bytes) != PSA_SUCCESS) err = ESP_FAIL;
        offset += bytes;
    }
    if (err == ESP_OK && (psa_hash_finish(&hash, digest, sizeof(digest), &digest_bytes) != PSA_SUCCESS ||
        digest_bytes != sizeof(digest) || memcmp(digest, expected, sizeof(digest))))
        err = ESP_ERR_INVALID_CRC;
    psa_hash_abort(&hash); free(chunk);
    return err;
}

void companion_character_trial_delete(companion_character_trial_handle_t h)
{
    if (!h) return;
    mosaico_character_journal_store_delete(h->store); free(h);
}

esp_err_t companion_character_trial_create(companion_character_trial_handle_t *out)
{
    if (!out) return ESP_ERR_INVALID_ARG;
    *out = NULL;
    companion_character_trial_handle_t h = calloc(1, sizeof(*h));
    if (!h) return ESP_ERR_NO_MEM;
    esp_err_t err = mosaico_nand_store_mount();
    if (err != ESP_OK) { companion_character_trial_delete(h); return err; }
    err = mosaico_character_journal_store_create(
        MOSAICO_NAND_MOUNT_PATH "/muse-character", &h->store);
    if (err != ESP_OK) { companion_character_trial_delete(h); return err; }
    err = mosaico_character_journal_store_load(h->store, &h->record);
    if (err == ESP_ERR_NOT_FOUND) { *out = h; return ESP_OK; }
    if (err == ESP_OK) {
        h->partition = esp_partition_find_first(ESP_PARTITION_TYPE_DATA,
            ESP_PARTITION_SUBTYPE_ANY, "ui_apps");
        if (!h->partition || h->record.partition_address != h->partition->address ||
            h->record.partition_bytes != h->partition->size) err = ESP_ERR_INVALID_STATE;
        else if (h->record.phase == MOSAICO_CHARACTER_TRIAL ||
            h->record.phase == MOSAICO_CHARACTER_ROLLED_BACK ||
            h->record.phase == MOSAICO_CHARACTER_ROLLBACK_TRIAL) {
            h->trial = true;
            h->restored = h->record.phase != MOSAICO_CHARACTER_TRIAL;
            const bool initial = mosaico_character_record_is_initial(&h->record);
            const bool admitted = initial ? initial_restore_enabled :
                mosaico_character_install_admitted(h->record.job, h->record.compat_version);
            const char *expected_compat = initial ?
                MOSAICO_CHARACTER_INITIAL_COMPAT : mosaico_character_install_compat();
            if (!h->restored && (!admitted || !expected_compat ||
                strcmp(h->record.compat_version, expected_compat))) err = ESP_ERR_NOT_SUPPORTED;
            else {
                err = verify_image(h);
                /* If power failed after Recovery selected ota_0 but before
                 * recording trial handoff, complete that handoff here. */
                if (err == ESP_OK && h->record.phase == MOSAICO_CHARACTER_ROLLED_BACK) {
                    h->record.phase = MOSAICO_CHARACTER_ROLLBACK_TRIAL;
                    h->record.sequence++;
                    err = mosaico_character_journal_store_save(h->store, &h->record);
                }
                /* Arm the retained Recovery before entering the renderer.
                 * A panic/watchdog/power loss must not loop in the trial app. */
                const esp_partition_t *factory = esp_partition_find_first(
                    ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_APP_FACTORY, NULL);
                if (err == ESP_OK) err = factory ? esp_ota_set_boot_partition(factory) : ESP_ERR_NOT_FOUND;
            }
        } else if (mosaico_character_boot_action(&h->record, false, NULL,
            h->partition->address, h->partition->size) != MOSAICO_CHARACTER_BOOT_NORMAL)
            err = ESP_ERR_INVALID_STATE;
    }
    if (err != ESP_OK) { companion_character_trial_delete(h); return err; }
    *out = h; return ESP_OK;
}

esp_err_t companion_character_trial_confirm(companion_character_trial_handle_t h, esp_gsp_handle_t ui)
{
    if (!h || !ui) return ESP_ERR_INVALID_ARG;
    if (!h->trial) return ESP_OK;
    /* Called only after native bundle parsing, strict first frame and all
     * companion bindings succeed. Flush waits for the submitted UI changes. */
    if (esp_gsp_flush(ui, FIRST_FRAME_TIMEOUT_MS) != ESP_GSP_OK) return ESP_ERR_TIMEOUT;
    uint32_t failures = 0; gsp_err_t last = GSP_OK;
    esp_gsp_render_error_stats(ui, &failures, &last);
    if (failures || last != GSP_OK) return ESP_FAIL;
    esp_err_t err = verify_image(h);
    if (err != ESP_OK) return err;
    const esp_partition_t *running = esp_ota_get_running_partition();
    if (!running || running->type != ESP_PARTITION_TYPE_APP ||
        running->subtype != ESP_PARTITION_SUBTYPE_APP_OTA_0) return ESP_ERR_INVALID_STATE;
    /* Re-arm the validated app before persisting CONFIRMED. If saving fails,
     * the caller explicitly enters Recovery; interrupted save remains TRIAL
     * or CONFIRMED, never an unrecorded successful update. */
    err = esp_ota_set_boot_partition(running);
    if (err != ESP_OK) return err;
    h->record.phase = h->restored ? MOSAICO_CHARACTER_ROLLBACK_CONFIRMED : MOSAICO_CHARACTER_CONFIRMED;
    h->record.sequence++;
    err = mosaico_character_journal_store_save(h->store, &h->record);
    if (err == ESP_OK) h->trial = false;
    return err;
}
