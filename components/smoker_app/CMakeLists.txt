set(smoker_app_sources
    "src/command_mailbox.cpp"
    "src/snapshot_exchange.cpp"
    "src/smoker_application.cpp"
)

if(ESP_PLATFORM)
    idf_component_register(
        SRCS ${smoker_app_sources}
        INCLUDE_DIRS "include"
        REQUIRES smoker_core
    )
    set(smoker_app_target ${COMPONENT_LIB})
else()
    add_library(smoker_app STATIC ${smoker_app_sources})
    target_include_directories(smoker_app PUBLIC "include")
    target_link_libraries(smoker_app PUBLIC smoker_core)
    target_compile_features(smoker_app PUBLIC cxx_std_20)
    set_target_properties(smoker_app PROPERTIES CXX_EXTENSIONS OFF)
    set(smoker_app_target smoker_app)
endif()

if(CMAKE_CXX_COMPILER_ID MATCHES "AppleClang|Clang|GNU")
    target_compile_options(${smoker_app_target} PRIVATE
        -Wall
        -Wextra
        -Wpedantic
        -Wconversion
        -Wsign-conversion
    )
endif()
