cmake_minimum_required(VERSION 3.20)

project(OAG_Universal_Core_V1 LANGUAGES CXX)

option(OAG_BUILD_HOST_TESTS "Build host-side OAG core tests" ON)

add_library(oag_core STATIC
    src/device/device_registry.cpp
    src/mapping/logical_slot_manager.cpp
    src/mapping/pass_through_mapping.cpp
)

target_include_directories(oag_core
    PUBLIC
        ${CMAKE_CURRENT_LIST_DIR}/include
)

target_compile_features(oag_core PUBLIC cxx_std_17)

target_compile_options(oag_core PRIVATE
    -Wall
    -Wextra
    -Wpedantic
    -Werror
)

if(OAG_BUILD_HOST_TESTS)
    enable_testing()

    add_executable(oag_core_tests
        tests/host/test_core.cpp
    )

    target_link_libraries(oag_core_tests PRIVATE oag_core)
    target_compile_features(oag_core_tests PRIVATE cxx_std_17)

    add_test(NAME oag_core_tests COMMAND oag_core_tests)
endif()
