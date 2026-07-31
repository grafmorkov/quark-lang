cmake_minimum_required(VERSION 3.20)

project(Quark VERSION 1.0 LANGUAGES CXX)
include(FetchContent)

# C++ standard
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Library libquark (C++ files)
add_library(libquark
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
    src/backend/native_backend.cpp
    src/utils/file_manager.cpp
    src/utils/options.cpp
    src/utils/errors.cpp
    src/linker/linker.cpp
    src/support/type_context.cpp
    src/support/symbol_path.cpp
)
FetchContent_Declare(arena-alloc
    GIT_REPOSITORY https://github.com/grafmorkov/quark-alloc.git
    GIT_TAG v1.0.0)
FetchContent_MakeAvailable(arena-alloc)

add_library(quark-alloc INTERFACE)
target_include_directories(quark-alloc INTERFACE ${arena-alloc_SOURCE_DIR})

target_include_directories(libquark
    PUBLIC
        ${PROJECT_SOURCE_DIR}/include
        ${PROJECT_SOURCE_DIR}/include/utils
)
target_compile_definitions(libquark PUBLIC
    QUARK_ROOT="${PROJECT_SOURCE_DIR}"
)
target_link_libraries(libquark PUBLIC quark-alloc)

# Executable file Quark (main.cpp)
add_executable(quark
    src/main.cpp
)
target_link_libraries(quark
    PRIVATE libquark
)
