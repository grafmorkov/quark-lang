cmake_minimum_required(VERSION 3.20)

project(Quant VERSION 0.42 LANGUAGES CXX)
include(FetchContent)

# Build type default
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE "RelWithDebInfo" CACHE STRING "Build type" FORCE)
  set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS
        "Debug" "Release" "RelWithDebInfo" "MinSizeRel")
endif()

# C++ standard
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Output all binaries into one place, easier for scripts/CI to find qu
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)

# Project-wide options
option(QUANT_WARNINGS_AS_ERRORS "Treat warnings as errors" OFF)
option(QUANT_ENABLE_LTO "Enable interprocedural optimization in Release builds" ON)
option(QUANT_ENABLE_ASAN "Enable AddressSanitizer/UBSan in Debug builds" OFF)
option(QUANT_ENABLE_TESTS "Build tests" OFF)

# Compiler warnings / flags, branched per compiler
add_library(quant_compiler_flags INTERFACE)

if(MSVC)
  target_compile_options(quant_compiler_flags INTERFACE
        /W4
        /permissive-
        /Zc:__cplusplus
        $<$<CONFIG:Debug>:/RTC1>
    )
  if(QUANT_WARNINGS_AS_ERRORS)
    target_compile_options(quant_compiler_flags INTERFACE /WX)
  endif()
elseif(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
  target_compile_options(quant_compiler_flags INTERFACE
        -Wall -Wextra -Wpedantic
        -Wshadow -Wconversion -Wsign-conversion
        -Wnon-virtual-dtor -Woverloaded-virtual
        $<$<CONFIG:Debug>:-Og -g>
        $<$<CONFIG:Release>:-O3>
    )
  if(QUANT_WARNINGS_AS_ERRORS)
    target_compile_options(quant_compiler_flags INTERFACE -Werror)
  endif()
  if(QUANT_ENABLE_ASAN)
    target_compile_options(quant_compiler_flags INTERFACE
            $<$<CONFIG:Debug>:-fsanitize=address,undefined -fno-omit-frame-pointer>)
    target_link_options(quant_compiler_flags INTERFACE
            $<$<CONFIG:Debug>:-fsanitize=address,undefined>)
  endif()
endif()

# LTO only for optimized configs, and only if the toolchain actually supports it
if(QUANT_ENABLE_LTO)
  include(CheckIPOSupported)
  check_ipo_supported(RESULT ipo_supported OUTPUT ipo_error)
  if(ipo_supported)
    set(CMAKE_INTERPROCEDURAL_OPTIMIZATION_RELEASE ON)
    set(CMAKE_INTERPROCEDURAL_OPTIMIZATION_MINSIZEREL ON)
  else()
    message(STATUS "LTO not supported by this toolchain: ${ipo_error}")
  endif()
endif()

# Embed the stdlib (.qu sources) into the compiler binary.
set(STD_EMBED_HEADER "${CMAKE_CURRENT_BINARY_DIR}/generated/quant_std_embedded.h")
file(GLOB_RECURSE STD_QU_FILES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/std/*.qu")

add_custom_command(
    OUTPUT "${STD_EMBED_HEADER}"
    COMMAND ${CMAKE_COMMAND}
        -DQUANT_STD_DIR=${CMAKE_CURRENT_SOURCE_DIR}/std
        -DOUTPUT_HEADER=${STD_EMBED_HEADER}
        -P ${CMAKE_CURRENT_SOURCE_DIR}/scripts/embed_std.cmake
    DEPENDS ${STD_QU_FILES} ${CMAKE_CURRENT_SOURCE_DIR}/scripts/embed_std.cmake
    COMMENT "Embedding stdlib into the compiler"
    VERBATIM
)

# Explicit target so other targets can depend on it without pulling the header
# into a sources list (cleaner separation, avoids surprises with IDE file lists).
add_custom_target(quant_std_embed DEPENDS "${STD_EMBED_HEADER}")

# Library libquant (C++ files)
add_library(libquant
    src/frontend/ast.cpp
    src/frontend/lexer.cpp
    src/frontend/parser.cpp
    src/semantic/semantic.cpp
    src/semantic/symbol_table.cpp
    src/utils/logger.cpp
    src/ir/ir_gen.cpp
    src/ir/ir_dump.cpp
    src/modules/modules.cpp
    src/backend/fasmcodegen.cpp
    src/backend/x86_64.cpp
    src/backend/aarch_64.cpp
    src/backend/aarch64_isel.cpp
    src/backend/isel.cpp
    src/backend/elf_writer.cpp
    src/backend/pe_writer.cpp
    src/backend/native_backend.cpp
    src/utils/file_manager.cpp
    src/utils/options.cpp
    src/utils/errors.cpp
    src/linker/linker.cpp
    src/support/type_context.cpp
    src/support/symbol_path.cpp
)
add_dependencies(libquant quant_std_embed)

set_target_properties(libquant PROPERTIES
    PREFIX ""
    POSITION_INDEPENDENT_CODE ON
)

target_link_libraries(libquant PUBLIC quant_compiler_flags)

# Dependencies
FetchContent_Declare(arena-alloc
    GIT_REPOSITORY https://github.com/grafmorkov/quant-alloc.git
    GIT_TAG        v1.0.2
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(arena-alloc)

add_library(quant-alloc INTERFACE)
# SYSTEM keeps third-party headers from triggering your -Wall/-Wextra/-Wpedantic
target_include_directories(quant-alloc SYSTEM INTERFACE ${arena-alloc_SOURCE_DIR})

target_include_directories(libquant
    PUBLIC
        ${PROJECT_SOURCE_DIR}/include
        ${PROJECT_SOURCE_DIR}/include/utils
        ${CMAKE_CURRENT_BINARY_DIR}/generated
)
target_compile_definitions(libquant PUBLIC
    QUANT_ROOT="${PROJECT_SOURCE_DIR}"
)
target_link_libraries(libquant PUBLIC quant-alloc)

# Executable qu (main.cpp)
add_executable(qu
    src/main.cpp
)
target_link_libraries(qu
    PRIVATE libquant quant_compiler_flags
)

# Install rules
include(GNUInstallDirs)
install(TARGETS qu
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

# Tests (optional)
if(QUANT_ENABLE_TESTS)
  enable_testing()
  add_subdirectory(tests)
endif()
