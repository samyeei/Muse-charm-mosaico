// SPDX-License-Identifier: Apache-2.0
#pragma once
#include "companion_character_protocol.h"
#include "companion_cpack.h"
#include "esp_err.h"
/* Blocking worker-only operation. Caller exclusively owns character storage.
 * Downloads and validates into NAND; never writes NOR or changes the boot slot.
 * Active UI and initial backup are never modified. Candidate files may only be
 * consumed alongside a matching committed Recovery transaction record. */
typedef struct {
    uint32_t bytes_received;
    int http_status;
    bool body_complete, pack_validated, image_stored;
} companion_character_download_result_t;
esp_err_t companion_character_download(const character_command_t *command,
                                       companion_character_download_result_t *result);
/* Worker-only read-back admission: revalidate published pack, native bundle
 * and extracted image. Fixed local paths; caller owns exclusive NAND access. */
esp_err_t companion_character_download_verify(const character_command_t *command,
    companion_cpack_info_t *image);
