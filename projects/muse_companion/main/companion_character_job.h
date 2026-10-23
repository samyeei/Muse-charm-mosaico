// SPDX-License-Identifier: Apache-2.0
#pragma once
#include "esp_err.h"
#include "companion_character_progress.h"
#include "companion_character_protocol.h"

#define COMPANION_CHARACTER_JOB_RECORD_BYTES 160U
typedef struct companion_character_job_t *companion_character_job_handle_t;
typedef enum { CHARACTER_JOB_NONE, CHARACTER_JOB_STARTED, CHARACTER_JOB_READY_HELD,
    CHARACTER_JOB_FAILED, CHARACTER_JOB_TIMEOUT, CHARACTER_JOB_REJECTED,
    CHARACTER_JOB_DOWNLOADED, CHARACTER_JOB_RESTORE_DISABLED,
    CHARACTER_JOB_RESTORE_HANDOFF, CHARACTER_JOB_RESTORE_FAILED } companion_character_job_event_t;
typedef struct {
    void *context;
    esp_err_t (*load)(void *, uint8_t *, size_t);
    esp_err_t (*save)(void *, const uint8_t *, size_t);
    /* Nonblocking adapters: INVALID_STATE means storage temporarily busy.
     * start copies command; poll completion is bound to the returned token. */
    esp_err_t (*download_start)(void *, const character_command_t *, uint64_t *);
    esp_err_t (*download_poll)(void *, uint64_t, bool *, esp_err_t *);
    esp_err_t (*restore_start)(void *, const char *, uint64_t *);
    esp_err_t (*restore_poll)(void *, uint64_t, bool *, esp_err_t *);
} companion_character_job_config_t;
/* Voice-worker single owner. Storage callbacks synchronously commit a bounded
 * record. Optional asynchronous adapters download or submit a local restore;
 * installation admission stays closed. No SDK, TTS or NOR calls here. */
esp_err_t companion_character_job_create(const companion_character_job_config_t *config,
    uint64_t now_ms, uint64_t utc_seconds, companion_character_job_handle_t *out);
void companion_character_job_delete(companion_character_job_handle_t job);
character_command_kind_t companion_character_job_reply(companion_character_job_handle_t job,
    char *text, uint64_t now_ms, uint64_t utc_seconds, companion_character_job_event_t *event);
bool companion_character_job_take_query(companion_character_job_handle_t job,
    uint64_t now_ms, bool busy, bool connected, char *query, size_t capacity);
void companion_character_job_query_finished(companion_character_job_handle_t job);
companion_character_job_event_t companion_character_job_tick(companion_character_job_handle_t job,
    uint64_t now_ms);
companion_character_job_event_t companion_character_job_download_tick(companion_character_job_handle_t job,
    uint64_t now_ms, bool busy);
/* The retained download_tick API schedules both download and restore work.
 * busy defers starting either operation until the conversation/TTS is idle;
 * pending completions can be polled. Restore handoff is not display success.
 * Interrupted local jobs are retired on reboot; Recovery owns any committed
 * transaction. No restore job produces CHARACTER_QUERY requests. */
/* Application-owned NVS adapter; call only in the voice worker. */
esp_err_t companion_character_job_load(void *, uint8_t *, size_t);
esp_err_t companion_character_job_save(void *, const uint8_t *, size_t);

/* Voice-worker only: UI progress is published through the platform queue. */
companion_character_progress_t companion_character_job_progress(companion_character_job_handle_t job);
