cmake_minimum_required(VERSION 3.24)
project(cpp-limit-order-book VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(LOB_BUILD_TESTS "Build GoogleTest correctness suite" ON)
option(LOB_BUILD_BENCH "Build benchmark harness + Google Benchmark" OFF)
option(LOB_ENABLE_SANITIZERS "Enable ASan + UBSan (Debug-friendly)" OFF)

function(lob_set_warnings target)
    target_compile_options(${target} PRIVATE
        -Wall -Wextra -Wpedantic -Werror
        -Wshadow -Wconversion -Wsign-conversion
    )
endfunction()

function(lob_set_sanitizers target)
    if(LOB_ENABLE_SANITIZERS)
        target_compile_options(${target} PRIVATE
            -fsanitize=address,undefined -fno-omit-frame-pointer)
        target_link_options(${target} PRIVATE
            -fsanitize=address,undefined)
    endif()
endfunction()

add_library(lob
    src/version.cpp
    src/engine.cpp
)
add_library(lob::lob ALIAS lob)
target_include_directories(lob PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)
lob_set_warnings(lob)
lob_set_sanitizers(lob)

include(FetchContent)

if(LOB_BUILD_TESTS)
    include(CTest)
    enable_testing()
    set(BUILD_GMOCK OFF CACHE BOOL "" FORCE)
    set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
    FetchContent_Declare(
        googletest
        GIT_REPOSITORY https://github.com/google/googletest.git
        GIT_TAG        v1.15.2
        GIT_SHALLOW    TRUE
    )
    FetchContent_MakeAvailable(googletest)
    add_executable(lob_tests tests/test_engine.cpp)
    target_link_libraries(lob_tests PRIVATE lob::lob GTest::gtest_main)
    lob_set_warnings(lob_tests)
    lob_set_sanitizers(lob_tests)
    add_test(NAME lob_tests COMMAND lob_tests)
endif()
