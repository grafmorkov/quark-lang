cmake_minimum_required(VERSION 3.20)

project(Quark VERSION 1.0 LANGUAGES CXX)

# C++ standard
set(CMAKE_CXX_STANDARD 17)
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
    src/backend/ccodegen.cpp
)

target_include_directories(libquark
    PUBLIC
        ${PROJECT_SOURCE_DIR}/include
        ${PROJECT_SOURCE_DIR}/include/utils
        ${PROJECT_SOURCE_DIR}/include/third_party/
)

# Executable file Quark (main.cpp)
add_executable(quark
    src/main.cpp
)

target_link_libraries(quark
    PRIVATE libquark
)