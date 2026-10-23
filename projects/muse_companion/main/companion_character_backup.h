// SPDX-License-Identifier: Apache-2.0
#pragma once
#include "esp_err.h"
#include <stdint.h>
/* Caller holds the character storage mutex. Backs up actual complete ui_apps,
 * including padding, to NAND once. Existing committed backup is verified and
 * never overwritten. No active Flash write. */
esp_err_t companion_character_backup_initial(void);

typedef struct {
    uint32_t partition_address, partition_bytes, image_bytes;
    uint8_t partition_sha256[32], image_sha256[32];
} companion_character_initial_info_t;
/* Worker exclusively owns NAND. Requires an EXISTING protected backup; never
 * substitutes the current character if missing. Verifies live layout, complete
 * backup, MOSGSP payload and native GSP structure; publishes only the unpadded
 * image at /nand/muse-character/factory-ui-restore.bin with durable read-back.
 * No journal/boot/NOR mutation. This is preparation, not install admission. */
esp_err_t companion_character_backup_prepare_restore(companion_character_initial_info_t *info);
