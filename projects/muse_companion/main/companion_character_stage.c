// SPDX-License-Identifier: Apache-2.0
#include "companion_character_stage.h"
#include "companion_character_download.h"
#include "companion_character_backup.h"
#include "mosaico_character_journal_store.h"
#include "mosaico_character_install_policy.h"
#include "mosaico_nand_store.h"
#include "iris_ota_support.h"
#include "esp_partition.h"
#include "esp_timer.h"
#include <stdlib.h>
#include <string.h>

/* Local protected backup only; this does not admit downloaded packages. */
static const bool initial_restore_enabled = true;
enum { STAGE_RESERVE_BYTES = 65536 };

typedef struct {
    mosaico_character_journal_store_handle_t store;
    mosaico_character_record_t prior, next, readback;
    companion_cpack_info_t image;
} stage_context_t;

static bool same_staged(const mosaico_character_record_t *a,
    const mosaico_character_record_t *b)
{
    return a->phase == MOSAICO_CHARACTER_STAGED && a->sequence == b->sequence &&
        a->partition_address == b->partition_address && a->partition_bytes == b->partition_bytes &&
        a->image_bytes == b->image_bytes && !strcmp(a->job, b->job) &&
        !strcmp(a->compat_version, b->compat_version) &&
        !strcmp(a->character_version, b->character_version) &&
        !memcmp(a->image_sha256, b->image_sha256, 32) &&
        !memcmp(a->backup_sha256, b->backup_sha256, 32);
}

static esp_err_t stage_prepare_handoff_source(const character_command_t *command,
    bool *prepared, bool initial)
{
    *prepared = false;
    stage_context_t *s = calloc(1, sizeof(*s));
    if (!s) return ESP_ERR_NO_MEM;
    const esp_partition_t *part = esp_partition_find_first(ESP_PARTITION_TYPE_DATA,
        ESP_PARTITION_SUBTYPE_ANY, "ui_apps");
    esp_err_t err = part && part->size <= COMPANION_CPACK_MAX_IMAGE && part->size ?
        ESP_OK : ESP_ERR_INVALID_STATE;
    if (err == ESP_OK) err = mosaico_character_journal_store_create(
        MOSAICO_NAND_MOUNT_PATH "/muse-character", &s->store);
    bool exists = false;
    if (err == ESP_OK) {
        err = mosaico_character_journal_store_load(s->store, &s->prior);
        exists = err == ESP_OK;
        if (err == ESP_ERR_NOT_FOUND) err = ESP_OK;
    }
    /* A pending transaction owns its source. Check before any initial-source
     * extraction can replace a file referenced by that transaction. */
    if (err == ESP_OK && exists && (!mosaico_character_record_valid(&s->prior) ||
        s->prior.partition_address != part->address || s->prior.partition_bytes != part->size))
        err = ESP_ERR_INVALID_STATE;
    if (err == ESP_OK && exists &&
        (s->prior.phase == MOSAICO_CHARACTER_STAGED ?
            strcmp(s->prior.job, command->job) ||
            strcmp(s->prior.compat_version, command->compat_version) ||
            strcmp(s->prior.character_version, command->character_version) :
            (s->prior.phase != MOSAICO_CHARACTER_CONFIRMED &&
             s->prior.phase != MOSAICO_CHARACTER_ROLLBACK_CONFIRMED &&
             s->prior.phase != MOSAICO_CHARACTER_CANCELED))) err = ESP_ERR_INVALID_STATE;
    if (err == ESP_OK && initial && exists && s->prior.phase == MOSAICO_CHARACTER_STAGED) {
        /* Resume the committed source without extracting over it. Recovery
         * rechecks its bytes before writing. Keep storage ownership on error. */
        *prepared = true;
        s->image.image_size = s->prior.image_bytes;
        memcpy(s->image.image_sha256, s->prior.image_sha256, 32);
    } else if (err == ESP_OK && initial) {
        companion_character_initial_info_t info;
        err = companion_character_backup_prepare_restore(&info);
        if (err == ESP_OK && (info.partition_address != part->address ||
            info.partition_bytes != part->size)) err = ESP_ERR_INVALID_STATE;
        if (err == ESP_OK) {
            s->image.image_size = info.image_bytes;
            memcpy(s->image.image_sha256, info.image_sha256, 32);
        }
    } else if (err == ESP_OK) err = companion_character_download_verify(command, &s->image);
    if (err == ESP_OK && s->image.image_size > part->size) err = ESP_ERR_INVALID_SIZE;
    if (err == ESP_OK) {
        s->next.sequence = exists ? s->prior.sequence + 1 : 1;
        s->next.phase = MOSAICO_CHARACTER_STAGED;
        s->next.partition_address = part->address;
        s->next.partition_bytes = part->size;
        s->next.image_bytes = s->image.image_size;
        memcpy(s->next.image_sha256, s->image.image_sha256, 32);
        /* Parser bounds all identifiers; copying is additionally bounded here. */
        if (strnlen(command->job, sizeof(command->job)) >= sizeof(s->next.job) ||
            strnlen(command->compat_version, sizeof(command->compat_version)) >= sizeof(s->next.compat_version) ||
            strnlen(command->character_version, sizeof(command->character_version)) >= sizeof(s->next.character_version))
            err = ESP_ERR_INVALID_ARG;
        else {
            strcpy(s->next.job, command->job);
            strcpy(s->next.compat_version, command->compat_version);
            strcpy(s->next.character_version, command->character_version);
        }
        /* Only an identical previously staged task is resumable. Never replace
         * unresolved WRITING/TRIAL/RESTORING or a corrupt history. */
        if (exists && s->prior.phase == MOSAICO_CHARACTER_STAGED) {
            s->next.sequence = s->prior.sequence;
            if (!same_staged(&s->prior, &s->next)) err = ESP_ERR_INVALID_STATE;
            else *prepared = true;
        } else if (exists && (!mosaico_character_record_valid(&s->prior) ||
            (s->prior.phase != MOSAICO_CHARACTER_CONFIRMED &&
             s->prior.phase != MOSAICO_CHARACTER_ROLLBACK_CONFIRMED &&
             s->prior.phase != MOSAICO_CHARACTER_CANCELED) ||
            s->prior.sequence == UINT64_MAX || !strcmp(s->prior.job, s->next.job) ||
            s->prior.partition_address != s->next.partition_address ||
            s->prior.partition_bytes != s->next.partition_bytes)) err = ESP_ERR_INVALID_STATE;
        if (err == ESP_OK && !mosaico_character_record_valid(&s->next)) err = ESP_ERR_INVALID_STATE;
    }
    if (err == ESP_OK && !initial) err = companion_character_backup_initial();
    if (err == ESP_OK) {
        size_t total = 0, used = 0;
        err = mosaico_nand_store_usage(&total, &used);
        /* Recovery needs a complete previous-ui backup plus its journal files.
         * Do not count the protected initial backup as reusable scratch. */
        if (err == ESP_OK && (used > total || total - used < part->size + STAGE_RESERVE_BYTES))
            err = ESP_ERR_NO_MEM;
    }
    /* Once the journal is published Recovery owns the transaction. Until
     * then a timed-out voice task cannot trigger a late installation, even
     * if NAND hashing or backup verification took a long time. */
    if (err == ESP_OK && !*prepared && command->deadline_ms &&
        (uint64_t)(esp_timer_get_time() / 1000) >= command->deadline_ms)
        err = ESP_ERR_TIMEOUT;
    if (err == ESP_OK && !*prepared) {
        /* Once a save is attempted its publication is uncertain until proven.
         * Retain NAND ownership even when read-back itself fails. */
        *prepared = true;
        err = mosaico_character_journal_store_save(s->store, &s->next);
        esp_err_t loaded = mosaico_character_journal_store_load(s->store, &s->readback);
        /* A failed save may have renamed its slot. Keep ownership, but never
         * enter Recovery based on that unsuccessful persistence operation. */
        if (err == ESP_OK && (loaded != ESP_OK || !same_staged(&s->readback, &s->next)))
            err = loaded == ESP_OK ? ESP_ERR_INVALID_STATE : loaded;
    }
    if (err == ESP_OK) err = iris_ota_support_enter_recovery();
    mosaico_character_journal_store_delete(s->store);
    free(s);
    return err;
}

static esp_err_t stage_prepare_handoff(const character_command_t *command, bool *prepared)
{
    return stage_prepare_handoff_source(command, prepared, false);
}

/* Internal local request; external pack fields cannot select this source. */
static esp_err_t stage_initial_handoff(const char *job, bool *prepared)
{
    character_command_t *command = calloc(1, sizeof(*command));
    if (!command) return ESP_ERR_NO_MEM;
    esp_err_t err = ESP_ERR_INVALID_ARG;
    if (job && strnlen(job, sizeof(command->job)) < sizeof(command->job)) {
        strcpy(command->job, job);
        strcpy(command->compat_version, MOSAICO_CHARACTER_INITIAL_COMPAT);
        strcpy(command->character_version, MOSAICO_CHARACTER_INITIAL_VERSION);
        err = stage_prepare_handoff_source(command, prepared, true);
    }
    free(command);
    return err;
}

esp_err_t companion_character_stage_initial(const char *job, bool *prepared)
{
    if (!prepared) return ESP_ERR_INVALID_ARG;
    *prepared = false;
    if (!job || !*job || strnlen(job, MOSAICO_CHARACTER_JOB_BYTES) >= MOSAICO_CHARACTER_JOB_BYTES)
        return ESP_ERR_INVALID_ARG;
    if (!initial_restore_enabled) return ESP_ERR_NOT_SUPPORTED;
    return stage_initial_handoff(job, prepared);
}

esp_err_t companion_character_stage(const character_command_t *command, bool *prepared)
{
    if (!prepared) return ESP_ERR_INVALID_ARG;
    *prepared = false;
    if (!command || command->kind != CHARACTER_STATUS || command->status != CHARACTER_READY)
        return ESP_ERR_INVALID_ARG;
    if (!mosaico_character_install_admitted(command->job, command->compat_version))
        return ESP_ERR_NOT_SUPPORTED;
    return stage_prepare_handoff(command, prepared);
}
