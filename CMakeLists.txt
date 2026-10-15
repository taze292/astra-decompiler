cmake_minimum_required(VERSION 3.20)
project(AstraDecompiler VERSION 0.5.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/luau/Common/include/Luau/Bytecode.h")
    set(ASTRA_DEFAULT_LUAU "${CMAKE_CURRENT_SOURCE_DIR}/luau")
else()
    set(ASTRA_DEFAULT_LUAU "${CMAKE_CURRENT_SOURCE_DIR}/luau-master/luau-master")
endif()
set(ASTRA_LUAU_DIR "${ASTRA_DEFAULT_LUAU}" CACHE PATH "Local, untracked Luau source checkout")
if(NOT EXISTS "${ASTRA_LUAU_DIR}/Common/include/Luau/Bytecode.h")
    message(FATAL_ERROR "Set ASTRA_LUAU_DIR to a Luau checkout. See README.md.")
endif()

set(ASTRA_INTERNAL_INCLUDE_DIRS
    "${CMAKE_CURRENT_SOURCE_DIR}/src/bytecode"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/analysis"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/reconstruction")

add_library(AstraDecompiler
    src/api/Decompiler.cpp
    src/api/Options.cpp
    src/api/Batch.cpp
    src/bytecode/Bytecode.cpp
    src/analysis/ControlFlow.cpp
    src/analysis/Exports.cpp
    src/reconstruction/Analysis.cpp
    src/reconstruction/Expressions.cpp
    src/reconstruction/Lowering.cpp
    src/reconstruction/Optimize.cpp
    src/reconstruction/Predicates.cpp
    src/reconstruction/Render.cpp
    src/reconstruction/Structure.cpp
    src/reconstruction/Symbols.cpp)
target_include_directories(AstraDecompiler
    PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include"
    PRIVATE ${ASTRA_INTERNAL_INCLUDE_DIRS} "${ASTRA_LUAU_DIR}/Common/include")
if(MSVC)
    target_compile_options(AstraDecompiler PRIVATE /W4 /EHsc)
else()
    target_compile_options(AstraDecompiler PRIVATE -Wall -Wextra -Wpedantic)
endif()

find_package(Threads REQUIRED)
target_link_libraries(AstraDecompiler PRIVATE Threads::Threads)

add_executable(astra
    src/cli/Main.cpp
    src/cli/WebSocketServer.cpp
    src/cli/BatchFiles.cpp)
target_link_libraries(astra PRIVATE AstraDecompiler)
if(WIN32)
    target_link_libraries(astra PRIVATE ws2_32 bcrypt)
endif()

include(cmake/DeveloperTools.cmake)
include(cmake/Tests.cmake)
