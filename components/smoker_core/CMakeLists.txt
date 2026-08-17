set(smoker_core_sources
    "src/control.cpp"
    "src/heater_demand.cpp"
    "src/safety.cpp"
    "src/temperature.cpp"
    "src/timer.cpp"
)

if(ESP_PLATFORM)
    idf_component_register(
        SRCS ${smoker_core_sources}
        INCLUDE_DIRS "include"
    )
    set(smoker_core_target ${COMPONENT_LIB})
else()
    add_library(smoker_core STATIC ${smoker_core_sources})
    target_include_directories(smoker_core PUBLIC "include")
    target_compile_features(smoker_core PUBLIC cxx_std_20)
    set_target_properties(smoker_core PROPERTIES CXX_EXTENSIONS OFF)
    set(smoker_core_target smoker_core)
endif()

if(CMAKE_CXX_COMPILER_ID MATCHES "AppleClang|Clang|GNU")
    target_compile_options(${smoker_core_target} PRIVATE
        -Wall
        -Wextra
        -Wpedantic
        -Wconversion
        -Wsign-conversion
    )
endif()
