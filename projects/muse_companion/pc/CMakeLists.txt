cmake_minimum_required(VERSION 3.20)
project(companion_backend LANGUAGES C)

if(NOT ESP_GSP_COMPONENT_DIR AND DEFINED ENV{ESP_GSP_COMPONENT_DIR})
    set(ESP_GSP_COMPONENT_DIR "$ENV{ESP_GSP_COMPONENT_DIR}")
endif()
if(NOT ESP_GSP_COMPONENT_DIR)
    foreach(candidate IN ITEMS
            "${CMAKE_CURRENT_LIST_DIR}/../managed_components/espressif__esp-gsp")
        get_filename_component(candidate "${candidate}" ABSOLUTE)
        if(EXISTS "${candidate}/tools/sim_bridge/CMakeLists.txt")
            set(ESP_GSP_COMPONENT_DIR "${candidate}")
            break()
        endif()
    endforeach()
endif()
if(NOT EXISTS "${ESP_GSP_COMPONENT_DIR}/tools/sim_bridge/CMakeLists.txt")
    message(FATAL_ERROR
        "Install espressif/esp-gsp first, or set ESP_GSP_COMPONENT_DIR")
endif()

add_subdirectory("${ESP_GSP_COMPONENT_DIR}/tools/sim_bridge" sim_bridge)
gsp_add_backend(companion_backend
    PROFILE "${CMAKE_CURRENT_LIST_DIR}/../ui/companion_rgb565.yaml"
    SOURCES ../main/companion_ui.c ../main/companion_model.c platform_pc.c
    INCLUDE_DIRECTORIES ../main
    SCENES ../ui/main.json ../ui/settings.json)
add_executable(companion_model_test companion_model_test.c ../main/companion_model.c)
add_executable(character_protocol_test ../tests/character_protocol_test.c ../main/companion_character_protocol.c)
target_include_directories(character_protocol_test PRIVATE ../main)
target_include_directories(companion_model_test PRIVATE ../main)
if(MSVC)
    target_compile_options(companion_model_test PRIVATE /utf-8 /UNDEBUG)
    target_compile_options(character_protocol_test PRIVATE /utf-8 /UNDEBUG)
else()
    target_compile_options(companion_model_test PRIVATE -UNDEBUG)
    target_compile_options(character_protocol_test PRIVATE -UNDEBUG)
endif()
