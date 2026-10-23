// SPDX-License-Identifier: Apache-2.0
#pragma once
#include "companion_model.h"
#include "companion_character_progress.h"
typedef enum { COMP_MSG_REPLY,COMP_MSG_DONE,COMP_MSG_ERROR, COMP_MSG_CHARACTER } companion_message_kind_t;
typedef struct {
    companion_message_kind_t kind;
    /* CHARACTER carries companion_character_progress_t here; other kinds
     * carry the conversation generation. Progress never replaces chat/TTS. */
    uint32_t generation;
    bool speaking;
    char text[COMP_REPLY_CAP];
} companion_message_t;
/* Render task only calls these bounded platform operations. No network IO
 * or audio playback occurs on that task. Each platform owns its queues. */
void companion_platform_tick(uint32_t ms);
void companion_platform_status(bool *online,bool *paired,int *battery,bool *key,bool *motion);
bool companion_platform_message(companion_message_t *out);
bool companion_platform_effect(unsigned effects,const companion_snapshot_t *snapshot);
void companion_platform_brightness(unsigned value);
void companion_platform_load_prefs(companion_snapshot_t *prefs);
bool companion_platform_confirm_pairing(void);
bool companion_platform_setup(const char **ssid,const char **password);
bool companion_platform_setup_request(void);
bool companion_platform_setup_request_failed(void);
