set(smoker_platform_sources
    "src/ads1115_food_probe_source.cpp"
    "src/blynk_connection_support.cpp"
    "src/blynk_command_support.cpp"
    "src/blynk_provisioning_support.cpp"
    "src/blynk_remote_support.cpp"
    "src/firmware_update_support.cpp"
    "src/history_support.cpp"
    "src/local_network_support.cpp"
    "src/max31865_sensor.cpp"
    "src/runtime_transport_support.cpp"
    "src/simulated_adapters.cpp"
    "src/wall_clock.cpp"
)

if(ESP_PLATFORM)
    list(APPEND smoker_platform_sources
        "src/ads1115_target_backend.cpp"
        "src/blynk_service.cpp"
        "src/firmware_update_service.cpp"
        "src/history_service.cpp"
        "src/local_connectivity.cpp"
        "src/max31865_target_backend.cpp"
        "src/simulation_runtime.cpp"
    )
    if(CONFIG_SMOKER_MAX31865_CONNECTED_DIAGNOSTIC)
        list(APPEND smoker_platform_sources
            "src/max31865_connected_diagnostic.cpp"
        )
    endif()
    idf_component_register(
        SRCS ${smoker_platform_sources}
        INCLUDE_DIRS "include"
        REQUIRES smoker_app
        PRIV_REQUIRES
            esp_event
            esp_driver_uart
            esp_http_client
            esp_http_server
            esp_netif
            app_update
            mbedtls
            esp_psram
            esp_system
            esp_timer
            esp_wifi
            mqtt
            esp-idf-lib__ads111x
            esp-idf-lib__max31865
            espressif__cjson
            espressif__mdns
            freertos
            log
            nvs_flash
            spi_flash
    )
    set(smoker_platform_target ${COMPONENT_LIB})
else()
    add_library(smoker_platform STATIC ${smoker_platform_sources})
    target_include_directories(smoker_platform PUBLIC "include")
    target_link_libraries(smoker_platform PUBLIC smoker_app)
    target_compile_features(smoker_platform PUBLIC cxx_std_20)
    set_target_properties(smoker_platform PROPERTIES CXX_EXTENSIONS OFF)
    set(smoker_platform_target smoker_platform)
endif()

if(CMAKE_CXX_COMPILER_ID MATCHES "AppleClang|Clang|GNU")
    target_compile_options(${smoker_platform_target} PRIVATE
        -Wall
        -Wextra
        -Wconversion
        -Wsign-conversion
    )
    if(NOT ESP_PLATFORM)
        target_compile_options(${smoker_platform_target} PRIVATE -Wpedantic)
    endif()
endif()
