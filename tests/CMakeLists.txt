cmake_minimum_required(VERSION 3.22)

project(smoker_core_host_tests LANGUAGES CXX)

include(CTest)
find_package(Threads REQUIRED)

add_subdirectory(
    "${CMAKE_CURRENT_LIST_DIR}/../components/smoker_core"
    "${CMAKE_CURRENT_BINARY_DIR}/smoker_core"
)
add_subdirectory(
    "${CMAKE_CURRENT_LIST_DIR}/../components/smoker_app"
    "${CMAKE_CURRENT_BINARY_DIR}/smoker_app"
)
add_subdirectory(
    "${CMAKE_CURRENT_LIST_DIR}/../components/smoker_platform"
    "${CMAKE_CURRENT_BINARY_DIR}/smoker_platform"
)

if(BUILD_TESTING)
    add_executable(smoker_core_tests
        "host/smoker_core_tests.cpp"
    )

    add_executable(smoker_v0_tests
        "host/smoker_v0_tests.cpp"
    )

    add_executable(smoker_m12_tests
        "host/smoker_m12_tests.cpp"
    )

    add_executable(smoker_m13_tests
        "host/smoker_m13_tests.cpp"
    )

    add_executable(smoker_m14_tests
        "host/smoker_m14_tests.cpp"
    )

    add_executable(smoker_m15_tests
        "host/smoker_m15_tests.cpp"
    )

    target_link_libraries(smoker_core_tests PRIVATE smoker_core)
    target_link_libraries(smoker_v0_tests PRIVATE smoker_platform)
    target_link_libraries(smoker_m12_tests PRIVATE smoker_platform Threads::Threads)
    target_link_libraries(smoker_m13_tests PRIVATE smoker_platform)
    target_link_libraries(smoker_m14_tests PRIVATE smoker_platform Threads::Threads)
    target_link_libraries(smoker_m15_tests PRIVATE smoker_platform Threads::Threads)
    target_compile_features(smoker_core_tests PRIVATE cxx_std_20)
    target_compile_features(smoker_v0_tests PRIVATE cxx_std_20)
    target_compile_features(smoker_m12_tests PRIVATE cxx_std_20)
    target_compile_features(smoker_m13_tests PRIVATE cxx_std_20)
    target_compile_features(smoker_m14_tests PRIVATE cxx_std_20)
    target_compile_features(smoker_m15_tests PRIVATE cxx_std_20)
    set_target_properties(smoker_core_tests PROPERTIES CXX_EXTENSIONS OFF)
    set_target_properties(smoker_v0_tests PROPERTIES CXX_EXTENSIONS OFF)
    set_target_properties(smoker_m12_tests PROPERTIES CXX_EXTENSIONS OFF)
    set_target_properties(smoker_m13_tests PROPERTIES CXX_EXTENSIONS OFF)
    set_target_properties(smoker_m14_tests PROPERTIES CXX_EXTENSIONS OFF)
    set_target_properties(smoker_m15_tests PROPERTIES CXX_EXTENSIONS OFF)

    if(CMAKE_CXX_COMPILER_ID MATCHES "AppleClang|Clang|GNU")
        target_compile_options(smoker_core PRIVATE -Werror -fno-exceptions -fno-rtti)
        target_compile_options(smoker_app PRIVATE -Werror -fno-exceptions -fno-rtti)
        target_compile_options(smoker_platform PRIVATE -Werror -fno-exceptions -fno-rtti)
        target_compile_options(smoker_core_tests PRIVATE
            -Wall
            -Wextra
            -Wpedantic
            -Wconversion
            -Wsign-conversion
            -Werror
            -fno-exceptions
            -fno-rtti
        )
        target_compile_options(smoker_v0_tests PRIVATE
            -Wall
            -Wextra
            -Wpedantic
            -Wconversion
            -Wsign-conversion
            -Werror
            -fno-exceptions
            -fno-rtti
        )
        target_compile_options(smoker_m12_tests PRIVATE
            -Wall
            -Wextra
            -Wpedantic
            -Wconversion
            -Wsign-conversion
            -Werror
            -fno-exceptions
            -fno-rtti
        )
        target_compile_options(smoker_m13_tests PRIVATE
            -Wall
            -Wextra
            -Wpedantic
            -Wconversion
            -Wsign-conversion
            -Werror
            -fno-exceptions
            -fno-rtti
        )
        target_compile_options(smoker_m14_tests PRIVATE
            -Wall
            -Wextra
            -Wpedantic
            -Wconversion
            -Wsign-conversion
            -Werror
            -fno-exceptions
            -fno-rtti
        )
        target_compile_options(smoker_m15_tests PRIVATE
            -Wall
            -Wextra
            -Wpedantic
            -Wconversion
            -Wsign-conversion
            -Werror
            -fno-exceptions
            -fno-rtti
        )
    endif()

    add_test(NAME smoker_core.domain COMMAND smoker_core_tests)
    add_test(NAME smoker_v0.m2_simulated_control COMMAND smoker_v0_tests m2)
    add_test(NAME smoker_v0.m3_session_timer COMMAND smoker_v0_tests m3)
    add_test(NAME smoker_v0.m4_safety COMMAND smoker_v0_tests m4)
    add_test(NAME smoker_v0.m5_complete_slice COMMAND smoker_v0_tests m5)
    add_test(NAME smoker_v0.m12_transport COMMAND smoker_m12_tests)
    add_test(NAME smoker_v0.m13_ota_policy COMMAND smoker_m13_tests)
    add_test(NAME smoker_v0.m14_history COMMAND smoker_m14_tests)
    add_test(NAME smoker_v0.m15_blynk_projection COMMAND smoker_m15_tests)
endif()
