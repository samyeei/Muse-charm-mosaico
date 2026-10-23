// SPDX-License-Identifier: Apache-2.0

#include "board_display.h"
#include "bsp/esp_mosaico.h"
#include "esp_gsp_esp_lcd.h"
#include "esp_iris.h"
#include "esp_log.h"
#include "companion_ui.h"
#include "companion_runtime.h"
#include "companion_character_storage.h"
#include "companion_character_trial.h"
#include "companion_credentials.h"
#include "companion_setup.h"
#include "companion_provision.h"
#include "iris_screen_mirror.h"
#include "iris_ota_support.h"
#include "nvs_flash.h"
#include "ui_bundle.h"

static const char *TAG = "muse_companion";
enum { COMPANION_FONT_RENDER_STACK_BYTES = 64 * 1024 };
static companion_ui_handle_t s_companion;
extern const unsigned char font_start[] asm("_binary_NotoSansSC_Regular_ttf_start");
extern const unsigned char font_end[] asm("_binary_NotoSansSC_Regular_ttf_end");

void app_main(void)
{
    ESP_ERROR_CHECK_WITHOUT_ABORT(esp_iris_boot_probe());
    ESP_ERROR_CHECK(nvs_flash_init());

    /* Keep Recovery reachable even when the external UI image is absent or
     * invalid. */
    iris_ota_support_start();
    companion_character_trial_handle_t trial = NULL;
    esp_err_t startup_err = companion_character_trial_create(&trial);
    if (startup_err != ESP_OK) goto failed;
    /* NAND diagnostics stay closed until the boot owner has confirmed the
     * journal, preventing background download/backup races with confirmation. */
#define STARTUP_CHECK(call) do { startup_err = (call); if (startup_err != ESP_OK) goto failed; } while (0)
    STARTUP_CHECK(companion_credentials_init());
    STARTUP_CHECK(companion_setup_start());
    STARTUP_CHECK(companion_provision_start());

    esp_gsp_config_t app_config;
    const esp_err_t bundle_err = ui_bundle_open(&app_config);
    if (bundle_err != ESP_OK) {
        ESP_LOGE(TAG,
                 "GSP UI unavailable (0x%x); ESP-Iris Recovery RPC remains active",
                 bundle_err);
        startup_err = bundle_err;
        goto failed;
    }

    app_config.ttf=font_start;
    app_config.ttf_size=(size_t)(font_end-font_start);
    esp_display_present_target_config_t display;
    STARTUP_CHECK(board_display_init(&display));

    esp_lcd_touch_handle_t touch = NULL;
    STARTUP_CHECK(board_touch_init(&touch));

    esp_gsp_esp_lcd_config_t lcd = ESP_GSP_ESP_LCD_CONFIG_INIT();
    lcd.display = display;
    lcd.touch = touch;
    /* Render callbacks only enqueue preference writes; the internal-stack
     * voice worker owns NVS. The renderer can therefore use PSRAM safely. */
    lcd.task_stack_psram = true;
    /* FreeType's gray rasterizer alone uses a 16 KiB local pool. Chinese
     * replies exceed the default 24 KiB render stack once the GSP callback
     * and glyph resolution frames are included (archived device core). */
    lcd.task_stack_size_freetype = COMPANION_FONT_RENDER_STACK_BYTES;

    STARTUP_CHECK(iris_screen_mirror_init());

    esp_gsp_handle_t ui;
    STARTUP_CHECK(esp_gsp_esp_lcd_start(&app_config, &lcd, &ui));
    STARTUP_CHECK(iris_screen_mirror_attach(ui));
    STARTUP_CHECK(companion_ui_create(ui, &s_companion));
    if(!companion_setup_active(NULL,NULL))STARTUP_CHECK(companion_runtime_start());
    STARTUP_CHECK(companion_character_trial_confirm(trial, ui));
    companion_character_trial_delete(trial);
    trial = NULL;
    ESP_ERROR_CHECK_WITHOUT_ABORT(companion_character_storage_start());

    ESP_LOGI(TAG, "Muse companion ready at %dx%d RGB565; AI hold-to-talk",
             BSP_LCD_H_RES, BSP_LCD_V_RES);
    return;
failed:
    /* Preserve TRIAL on disk. Recovery decides whether restoration is needed;
     * do not announce rollback until its writer has verified the old image. */
    companion_character_trial_delete(trial);
    ESP_LOGE(TAG, "Startup validation failed: %s; requesting retained Recovery", esp_err_to_name(startup_err));
    ESP_ERROR_CHECK_WITHOUT_ABORT(iris_ota_support_enter_recovery());
#undef STARTUP_CHECK
}
