// SPDX-License-Identifier: Apache-2.0
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
typedef struct companion_model_t *companion_model_handle_t;
typedef enum { COMP_IDLE, COMP_LISTENING, COMP_THINKING, COMP_SPEAKING,
    COMP_SLEEPY, COMP_ERROR } companion_mode_t;
typedef enum { COMP_PRESS, COMP_RELEASE, COMP_CANCEL, COMP_MUTE,
    COMP_SETTINGS, COMP_BACK, COMP_WAKE, COMP_VOLUME, COMP_BRIGHTNESS,
    COMP_PAGE_NEXT, COMP_PAGE_PREV, COMP_RETRY, COMP_AUTO_PAGE_NEXT, COMP_LANGUAGE, COMP_SLEEP_TIMEOUT, COMP_SPEECH_RATE, COMP_PAGE_INTERVAL, COMP_PAGE_SET } companion_action_t;
enum { COMP_EFFECT_NONE=0, COMP_EFFECT_BEGIN=1, COMP_EFFECT_END=2,
    COMP_EFFECT_CANCEL=4, COMP_EFFECT_PREFS=8, COMP_EFFECT_RETRY=16,
    COMP_EFFECT_PREVIEW=32 };
#define COMP_REPLY_CAP 4096U
typedef struct {
    companion_mode_t mode;
    uint32_t generation;
    bool settings, muted, key_down, online, paired, asleep;
    unsigned volume, brightness, display_brightness, page;
    unsigned language, sleep_seconds, speech_rate, page_seconds;
    int battery;
    char reply[COMP_REPLY_CAP];
    char error[128];
} companion_snapshot_t;
/* UI/render task owns this model; other tasks send bounded messages only. */
companion_model_handle_t companion_model_create(void);
void companion_model_delete(companion_model_handle_t model);
unsigned companion_model_action(companion_model_handle_t model,
    companion_action_t action, unsigned value);
void companion_model_tick(companion_model_handle_t model, uint32_t ms);
void companion_model_connection(companion_model_handle_t model, bool online,
    bool paired, int battery);
void companion_model_reply(companion_model_handle_t model, uint32_t generation,
    const char *text, bool speaking);
void companion_model_done(companion_model_handle_t model, uint32_t generation);
void companion_model_error(companion_model_handle_t model, uint32_t generation,
    const char *message);
void companion_model_prefs(companion_model_handle_t model,unsigned volume,
    unsigned brightness,bool muted);
void companion_model_options(companion_model_handle_t model, unsigned language,
    unsigned sleep_seconds, unsigned speech_rate, unsigned page_seconds);
void companion_model_snapshot(companion_model_handle_t model,
    companion_snapshot_t *out);
