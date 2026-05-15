cmake_minimum_required(VERSION 3.24)
project(broflora LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(MSVC)
    add_compile_options(/MP /permissive- /utf-8)
    # Static CRT only when building standalone (not consumed as a subdirectory)
    if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
        set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
    endif()
endif()

# --- broflora library ---

add_library(broflora STATIC
    src/world.cpp
    src/light.cpp
    src/vigor.cpp
    src/development.cpp
    src/spawning.cpp
    src/senescence.cpp
)
add_library(broflora::broflora ALIAS broflora)

target_include_directories(broflora PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)
target_compile_features(broflora PUBLIC cxx_std_20)

if(MSVC)
    target_compile_options(broflora PRIVATE /W4)
else()
    target_compile_options(broflora PRIVATE -Wall -Wextra)
endif()

# --- Optional bromesh boundary ---
# When a sibling ../bromesh checkout exists, gate the mesh-emit module on
# it via BROFLORA_HAS_BROMESH. Core library never depends on bromesh.
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../bromesh/CMakeLists.txt")
    target_compile_definitions(broflora PUBLIC BROFLORA_HAS_BROMESH=1)
    message(STATUS "broflora: sibling ../bromesh detected; mesh-emit boundary enabled")
else()
    message(STATUS "broflora: ../bromesh not found; mesh-emit will be no-op")
endif()

# --- Tests ---
option(BROFLORA_TESTS "Build broflora tests" ${PROJECT_IS_TOP_LEVEL})
if(BROFLORA_TESTS AND CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    enable_testing()
    add_subdirectory(tests)
endif()
