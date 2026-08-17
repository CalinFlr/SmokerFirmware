cmake_minimum_required(VERSION 3.22)

set(IDF_TARGET "esp32s3" CACHE STRING "ESP-IDF build target")

include($ENV{IDF_PATH}/tools/cmake/project.cmake)

set(SMOKER_REQUIRED_ESP_IDF_VERSION "6.0.2")
set(smoker_detected_esp_idf_version
    "${IDF_VERSION_MAJOR}.${IDF_VERSION_MINOR}.${IDF_VERSION_PATCH}"
)
if(NOT smoker_detected_esp_idf_version STREQUAL SMOKER_REQUIRED_ESP_IDF_VERSION)
    message(FATAL_ERROR
        "Smoker Controller requires ESP-IDF v${SMOKER_REQUIRED_ESP_IDF_VERSION}; "
        "detected v${smoker_detected_esp_idf_version}. Activate the pinned toolchain."
    )
endif()

# ESP-IDF v6.0.2 defaults to GNU++26. Project-owned code is deliberately kept
# on the same strict C++20 baseline as the native host tests.
idf_build_replace_option_from_property(
    CXX_COMPILE_OPTIONS
    "-std=gnu++26"
    "-std=c++20"
)

# Build only main and its explicitly declared dependency graph.
idf_build_set_property(MINIMAL_BUILD ON)

project(smoker_controller)

# M13 ordinary builds are deliberately unsigned: the private release key never
# enters normal developer or CI builds. ESP-IDF still creates serial flash
# targets for that unsigned image, so make every target which could install a
# new boot layout/application fail closed. The reviewed USB workflow verifies
# and substitutes the signed application through tools/flash_signed_firmware.py.
add_custom_target(smoker_signed_flash_required
    COMMAND "${CMAKE_COMMAND}"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/tools/reject_unsigned_flash.cmake"
    VERBATIM
)
foreach(smoker_unsafe_flash_target IN ITEMS
    flash
    app-flash
    bootloader-flash
    partition-table-flash
    otadata-flash
)
    if(TARGET "${smoker_unsafe_flash_target}")
        add_dependencies(
            "${smoker_unsafe_flash_target}"
            smoker_signed_flash_required
        )
    endif()
endforeach()
