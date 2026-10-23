// SPDX-License-Identifier: Apache-2.0
#include "companion_character_image.h"
#include "companion_cpack.h"
#include "esp_heap_caps.h"
#include "esp_gsp_deployable.h"
#include <stdbool.h>
#include <stdlib.h>

esp_err_t companion_character_image_validate(FILE *file, uint32_t offset, uint32_t image_bytes)
{
    if (!file || image_bytes < 96U || image_bytes > COMPANION_CPACK_MAX_IMAGE ||
        offset > UINT32_MAX - image_bytes) return ESP_ERR_INVALID_ARG;
    const size_t bytes = image_bytes - 64U;
    void *data = heap_caps_aligned_alloc(64, bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    esp_gsp_config_t *config = calloc(1, sizeof(*config));
    if (!data || !config) { heap_caps_free(data); free(config); return ESP_ERR_NO_MEM; }
    esp_gsp_deployable_bundle_t *bundle = NULL;
    esp_gsp_deployable_info_t info = {.struct_size = sizeof(info)};
    bool valid = fseek(file, offset + 64U, SEEK_SET) == 0 &&
        fread(data, 1, bytes, file) == bytes &&
        esp_gsp_deployable_bundle_open(data, bytes, true, &bundle) == ESP_GSP_OK &&
        esp_gsp_deployable_bundle_get_info(bundle, &info) == ESP_GSP_OK &&
        info.scene_count == 2 && info.dynamic_image_slots == 0 &&
        esp_gsp_deployable_bundle_make_config(bundle, config) == ESP_GSP_OK &&
        config->directory_count == 2;
    if (bundle) esp_gsp_deployable_bundle_close(bundle);
    heap_caps_free(data);
    free(config);
    return valid ? ESP_OK : ESP_ERR_INVALID_RESPONSE;
}
