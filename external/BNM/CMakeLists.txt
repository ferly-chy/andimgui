cmake_minimum_required(VERSION 3.22...3.28)

project(BNM
    VERSION 2.6.0
    DESCRIPTION "High-performance symbolic Android IL2CPP modding library"
    LANGUAGES CXX
)

#Configuration Options
option(BNM_LINK_LOG "Link against Android log library" ON)
option(BNM_ENABLE_LTO "Enable Link Time Optimization" ON)
option(BNM_HARDENING "Enable security hardening flags" ON)
option(BNM_ENABLE_EXTENDED_CLASS_INIT_CONSENSUS "Enable extended Class::Init consensus diagnostics and safe Path 1 confirmation" ON)
option(BNM_ALLOW_NON_P1_CLASS_INIT_CONSENSUS "Allow extended Class::Init consensus to select a non-Path 1 target" OFF)
option(BNM_ENABLE_RESOLVER_DIAGNOSTICS "Enable structured resolver diagnostic logs" OFF)

#C++ Standard Configuration
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

#Source and Header Files
file(GLOB_RECURSE BNM_SOURCES src/*.cpp)
file(GLOB_RECURSE BNM_HEADERS include/BNM/*.hpp)

# Define Library
add_library(BNM STATIC ${BNM_SOURCES})

# Include Directories
target_include_directories(BNM
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/external/include>
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/external/spdlog/include>
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/external/simdutf/include>
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/external
        ${CMAKE_CURRENT_SOURCE_DIR}/src/private
)

target_sources(BNM PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/external/simdutf/src/simdutf.cpp
)

target_include_directories(BNM SYSTEM PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/external/simdutf/src
)

# Compiler Flags and Modern Features
target_compile_options(BNM PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:
        -Wall -Wextra -Wpedantic -Werror=return-type
        -fvisibility=hidden
        -fvisibility-inlines-hidden
        -ffunction-sections -fdata-sections
        -O3
        # Suppress inevitable warnings from IL2CPP headers and non-standard modding patterns
        -Wno-zero-length-array
        -Wno-dollar-in-identifier-extension
        -Wno-gnu-anonymous-struct
        -Wno-nested-anon-types
        -Wno-variadic-macros
        -Wno-vla-extension
        -Wno-unused-parameter
    >
)

# Security Hardening
if(BNM_HARDENING)
    target_compile_options(BNM PRIVATE
        $<$<CXX_COMPILER_ID:GNU,Clang>:
            -fstack-protector-strong
            -D_FORTIFY_SOURCE=2
        >
    )
endif()

if(BNM_ENABLE_EXTENDED_CLASS_INIT_CONSENSUS)
    target_compile_definitions(BNM PRIVATE BNM_ENABLE_EXTENDED_CLASS_INIT_CONSENSUS)
endif()

if(BNM_ALLOW_NON_P1_CLASS_INIT_CONSENSUS)
    target_compile_definitions(BNM PRIVATE BNM_ALLOW_NON_P1_CLASS_INIT_CONSENSUS)
endif()

if(BNM_ENABLE_RESOLVER_DIAGNOSTICS)
    target_compile_definitions(BNM PRIVATE BNM_ENABLE_RESOLVER_DIAGNOSTICS)
endif()

# Link Time Optimization (LTO)
if(BNM_ENABLE_LTO)
    include(CheckIPOSupported)
    check_ipo_supported(RESULT ipo_supported OUTPUT ipo_error)
    if(ipo_supported)
        set_target_properties(BNM PROPERTIES INTERPROCEDURAL_OPTIMIZATION TRUE)
    else()
        message(STATUS "LTO/IPO is not supported: ${ipo_error}")
    endif()
endif()

# Linker Flags
if(ANDROID)
    target_link_options(BNM PRIVATE "-Wl,--gc-sections")
    if(BNM_LINK_LOG)
        target_link_libraries(BNM PUBLIC log)
    endif()
endif()

# Export Include Directories for consumers
set_target_properties(BNM PROPERTIES BNM_INCLUDE_DIRECTORIES "${CMAKE_CURRENT_SOURCE_DIR}/include")
