set(smoker_platform_sources
    "src/firmware_update_support.cpp"
    "src/local_network_support.cpp"
    "src/simulated_adapters.cpp"
)

if(ESP_PLATFORM)
    list(APPEND smoker_platform_sources
        "src/firmware_update_service.cpp"
        "src/local_connectivity.cpp"
        "src/simulation_runtime.cpp"
    )
    idf_component_register(
        SRCS ${smoker_platform_sources}
        INCLUDE_DIRS "include"
        REQUIRES smoker_app
        PRIV_REQUIRES
            esp_event
            esp_http_client
            esp_http_server
            esp_netif
            app_update
            mbedtls
            esp_psram
            esp_system
            esp_timer
            esp_wifi
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
