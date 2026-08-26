cmake_minimum_required(VERSION 3.24)
project(cpp-limit-order-book VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(LOB_BUILD_TESTS "Build GoogleTest correctness suite" OFF)
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

add_library(lob src/version.cpp)
add_library(lob::lob ALIAS lob)
target_include_directories(lob PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)
lob_set_warnings(lob)
lob_set_sanitizers(lob)
