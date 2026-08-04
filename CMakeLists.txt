cmake_minimum_required(VERSION 3.20)

project(Quanta VERSION 0.37 LANGUAGES CXX)
include(FetchContent)

# C++ standard
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Embed the stdlib (.qu sources) into the compiler binary.
set(STD_EMBED_HEADER "${CMAKE_CURRENT_BINARY_DIR}/generated/quanta_std_embedded.h")
file(GLOB_RECURSE STD_QU_FILES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/std/*.qu")

add_custom_command(
    OUTPUT "${STD_EMBED_HEADER}"
    COMMAND ${CMAKE_COMMAND}
        -DQUANTA_STD_DIR=${CMAKE_CURRENT_SOURCE_DIR}/std
        -DOUTPUT_HEADER=${STD_EMBED_HEADER}
        -P ${CMAKE_CURRENT_SOURCE_DIR}/scripts/embed_std.cmake
    DEPENDS ${STD_QU_FILES}
    COMMENT "Embedding stdlib into the compiler"
    VERBATIM
)

# Library libquanta (C++ files)
add_library(libquanta
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
    "${STD_EMBED_HEADER}"
)

set_target_properties(libquanta PROPERTIES
        PREFIX ""
)

FetchContent_Declare(arena-alloc
    GIT_REPOSITORY https://github.com/grafmorkov/quanta-alloc.git
    GIT_TAG v1.0.1)
FetchContent_MakeAvailable(arena-alloc)

add_library(quanta-alloc INTERFACE)
target_include_directories(quanta-alloc INTERFACE ${arena-alloc_SOURCE_DIR})

target_include_directories(libquanta
    PUBLIC
        ${PROJECT_SOURCE_DIR}/include
        ${PROJECT_SOURCE_DIR}/include/utils
        ${CMAKE_CURRENT_BINARY_DIR}/generated
)
target_compile_definitions(libquanta PUBLIC
    QUANTA_ROOT="${PROJECT_SOURCE_DIR}"
)
target_link_libraries(libquanta PUBLIC quanta-alloc)

# Executable file Quanta (main.cpp)
add_executable(qu
    src/main.cpp
)
target_link_libraries(qu
    PRIVATE libquanta
)
