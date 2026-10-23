// SPDX-License-Identifier: Apache-2.0
#pragma once
#include "esp_err.h"
#include "companion_character_protocol.h"

/* Storage worker owns NAND for this entire synchronous operation. Firmware
 * policy alone authorizes staging. NOT_SUPPORTED means candidate gate closed:
 * no journal, backup or boot mutation. prepared means a committed STAGED may
 * exist (including uncertain publication after a failed save); retain storage
 * ownership until reboot.
 * No NOR writer here. Recovery entry uses the retained application adapter. */
esp_err_t companion_character_stage(const character_command_t *command, bool *prepared);
/* Local restore request, storage worker only. Same trusted closed gate and
 * prepared ownership contract; requires the existing protected initial backup.
 * Never calls backup_initial() to create a substitute from the active role. */
esp_err_t companion_character_stage_initial(const char *job, bool *prepared);
