cmake_minimum_required(VERSION 3.20)

project(Quant VERSION 0.37 LANGUAGES CXX)
include(FetchContent)

# C++ standard
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

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
    DEPENDS ${STD_QU_FILES}
    COMMENT "Embedding stdlib into the compiler"
    VERBATIM
)

# Library libquant (C++ files)
add_library(libquant
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

set_target_properties(libquant PROPERTIES
        PREFIX ""
)

FetchContent_Declare(arena-alloc
    GIT_REPOSITORY https://github.com/grafmorkov/quant-alloc.git
    GIT_TAG v1.0.2)
FetchContent_MakeAvailable(arena-alloc)

add_library(quant-alloc INTERFACE)
target_include_directories(quant-alloc INTERFACE ${arena-alloc_SOURCE_DIR})

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

# Executable file Quant (main.cpp)
add_executable(qu
    src/main.cpp
)
target_link_libraries(qu
    PRIVATE libquant
)
