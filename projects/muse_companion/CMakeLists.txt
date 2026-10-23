cmake_minimum_required(VERSION 3.16)
if(CMAKE_HOST_WIN32)
    # Pinned configdep 0.2.3 drops sdkconfig.h dependencies on this Windows
    # workspace without adding cdep records. Use ordinary compiler dependencies
    # so changing a radio/memory option actually recompiles its consumers.
    set(CONFIGDEP_ENABLE OFF CACHE BOOL "Use reliable SDK configuration dependencies" FORCE)
endif()
set(MOSAICO_UTILS_ROOT "${CMAKE_CURRENT_LIST_DIR}/../../submodule/esp-mosaico-utils")
set(MOSAICO_BSP_ROOT "${CMAKE_CURRENT_LIST_DIR}/../../submodule/esp-mosaico-bsp")
if(NOT SDKCONFIG)
    set(SDKCONFIG "${CMAKE_BINARY_DIR}/sdkconfig")
endif()
if(NOT SDKCONFIG_DEFAULTS)
    set(SDKCONFIG_DEFAULTS "sdkconfig.defaults;sdkconfig.application.defaults")
endif()
if(CMAKE_HOST_WIN32 AND NOT DEFINED ENV{GSPC_EXECUTABLE})
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
        "${CMAKE_CURRENT_LIST_DIR}/tools/windows_gspc_path.py"
        "${CMAKE_CURRENT_LIST_DIR}/tools/gspc_windows_wrapper.py"
        "${CMAKE_CURRENT_LIST_DIR}/ui/companion_rgb565.yaml")
    find_program(companion_host_python NAMES python REQUIRED)
    execute_process(COMMAND "${companion_host_python}" "${CMAKE_CURRENT_LIST_DIR}/tools/windows_gspc_path.py"
        OUTPUT_VARIABLE companion_gspc_wrapper OUTPUT_STRIP_TRAILING_WHITESPACE
        COMMAND_ERROR_IS_FATAL ANY)
    set(GSPC_EXECUTABLE "${companion_gspc_wrapper}" CACHE FILEPATH "Pinned compiler Windows depfile adapter" FORCE)
endif()
set(MOSAICO_GSPC_VERSION "0.6.1")
include("${MOSAICO_UTILS_ROOT}/mosaico-tools/cmake/gsp_compiler.cmake")
list(APPEND EXTRA_COMPONENT_DIRS
    "${MOSAICO_UTILS_ROOT}/esp-mosaico-vibe-mode/components/esp_mosaico_nand_store"
    "${MOSAICO_UTILS_ROOT}/esp-mosaico-vibe-mode/components/esp_mosaico_character_journal"
    "${MOSAICO_UTILS_ROOT}/esp-mosaico-vibe-mode/components/esp_mosaico_app_recovery"
    "${MOSAICO_UTILS_ROOT}/mosaico-tools/components/esp_mosaico_gsp_bundle"
    "${MOSAICO_UTILS_ROOT}/mosaico-tools/components/esp_mosaico_gsp_iris")
include("${MOSAICO_UTILS_ROOT}/esp-mosaico-vibe-mode/cmake/mosaico_idf_project.cmake")
project(muse_companion VERSION 2.0.0)
include("${MOSAICO_UTILS_ROOT}/mosaico-tools/cmake/system_update.cmake")
