idf_component_register(
    SRCS "main.c" "companion_ui.c" "companion_model.c" "companion_runtime.c" "companion_tts.c" "companion_qwen.c" "companion_pcm.c" "companion_setup.c" "companion_provision.c" "companion_generation.c" "board_display.c"
    INCLUDE_DIRS "."
    REQUIRES esp_iris esp-mosaico-bsp esp_mosaico_app_recovery esp_mosaico_nand_store esp_mosaico_character_journal
        esp_mosaico_gsp_bundle esp_mosaico_gsp_iris freertos nvs_flash muse_link_sdk companion_credentials
    PRIV_REQUIRES cjson esp_timer esp-gsp esp_display_present esp_lcd esp_http_client esp-tls esp_http_server esp_wifi esp_netif esp_event esp_websocket_client mbedtls
    EMBED_TXTFILES "setup.html"
    EMBED_FILES "../ui/fonts/NotoSansSC-Regular.ttf")
target_sources(${COMPONENT_LIB} PRIVATE "companion_character_storage.c" "companion_character_backup.c")
target_sources(${COMPONENT_LIB} PRIVATE "companion_bridge.c")
target_sources(${COMPONENT_LIB} PRIVATE "companion_character_image.c")
target_sources(${COMPONENT_LIB} PRIVATE "companion_character_trial.c")
target_sources(${COMPONENT_LIB} PRIVATE "companion_character_stage.c")
target_sources(${COMPONENT_LIB} PRIVATE "companion_character_job.c" "companion_character_job_store.c")
target_sources(${COMPONENT_LIB} PRIVATE "companion_cpack.c" "companion_character_protocol.c" "companion_character_download.c")
file(READ "${PROJECT_DIR}/tools/character_template/contract.json" companion_character_contract)
string(JSON companion_character_compat GET "${companion_character_contract}" compat_version)
set(MOSAICO_CHARACTER_INSTALL_COMPAT "${companion_character_compat}" CACHE STRING "Compatible character template" FORCE)
include("${MOSAICO_UTILS_ROOT}/esp-mosaico-vibe-mode/cmake/character_install_policy.cmake")
include("${MOSAICO_UTILS_ROOT}/mosaico-tools/cmake/gsp_bundle.cmake")
mosaico_gsp_add_ui_bundle(${COMPONENT_LIB} "../ui/main.json;../ui/settings.json")
include("${CMAKE_CURRENT_LIST_DIR}/../managed_components/espressif__esp-gsp/cmake/gsp_bundle.cmake" OPTIONAL)
if(NOT COMMAND gsp_enable_freetype)
    include("${PROJECT_DIR}/managed_components/espressif__esp-gsp/cmake/gsp_bundle.cmake")
endif()
gsp_enable_freetype()
