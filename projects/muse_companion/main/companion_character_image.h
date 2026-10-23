// SPDX-License-Identifier: Apache-2.0
#pragma once
#include "esp_err.h"
#include <stdint.h>
#include <stdio.h>

/* Read-only native GSP parsing, after the caller has validated MOSGSP and
 * hashes. Worker owns the file; allocates the bundle in PSRAM temporarily.
 * Does not prove UI binding/first-frame acceptance or authorize installation. */
esp_err_t companion_character_image_validate(FILE *file, uint32_t offset, uint32_t bytes);
