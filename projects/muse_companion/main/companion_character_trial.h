// SPDX-License-Identifier: Apache-2.0
#pragma once
#include "esp_err.h"
#include "esp_gsp.h"

typedef struct companion_character_trial_t *companion_character_trial_handle_t;
/* Boot-task-only owner. Run before exposing NAND diagnostics/workers. No NOR
 * writes. Candidate compatibility remains closed until device acceptance. */
esp_err_t companion_character_trial_create(companion_character_trial_handle_t *out);
esp_err_t companion_character_trial_confirm(companion_character_trial_handle_t handle,
    esp_gsp_handle_t ui);
void companion_character_trial_delete(companion_character_trial_handle_t handle);
