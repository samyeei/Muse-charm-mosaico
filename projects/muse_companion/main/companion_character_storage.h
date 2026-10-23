// SPDX-License-Identifier: Apache-2.0
#pragma once
#include "esp_err.h"
#include "companion_character_download.h"
/* Internal asynchronous NAND-only operation. Command is copied before return.
 * Tokens bind cached completion to its caller; no install or NOR writes. */
esp_err_t companion_character_storage_download_start(void *, const character_command_t *, uint64_t *token);
esp_err_t companion_character_storage_download_poll(void *, uint64_t token, bool *finished, esp_err_t *result);
/* Local initial restore: returns promptly, copies job, serializes with every
 * mutable NAND operation. Poll tokens identify this request only. ESP_OK from
 * start means queued, not restored. Result reflects staging; NOR belongs to
 * Recovery and successful display requires the later first-frame confirmation.
 * Closed production policy completes with NOT_SUPPORTED and changes no role. */
esp_err_t companion_character_storage_restore_start(const char *job, uint64_t *token);
esp_err_t companion_character_storage_restore_poll(uint64_t token, bool *finished, esp_err_t *result);
/* Optional NAND mount at startup, with automatic formatting disabled.
 * USB RPC 0x7e12/1: 1=space/job status, 2=write/read marker, 3=reread,
 * 4=start initial backup/verification worker (poll 1, backup_state=2 means ready),
 * 5/6=readonly diagnostic exports (diagnostic builds),
 * 7 + exact UTF-8 INIT_NAND_V1=explicit filesystem initialization,
 * 8=sync mounted NAND and schedule reboot, 9=download READY control text,
 * 10=cached download status, 11=start initial restore (exact
 * RESTORE_INITIAL_V1 plus a space plus an ASCII job ID), 12=cached restore
 * status. Commands 11/12 are USB maintenance; Muse text cannot invoke RPC.
 * Empty payload means space.
 * Command 7 is destructive and requires explicit user authorization.
 * No writes to NOR firmware, credentials or ui_apps. */
esp_err_t companion_character_storage_start(void);
