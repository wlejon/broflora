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
    src/validate.cpp
    src/mesh_emit.cpp
    src/prototypes.cpp
)
add_library(broflora::broflora ALIAS broflora)

target_include_directories(broflora PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)
target_compile_features(broflora PUBLIC cxx_std_20)

# OpenMP — the per-plant sim passes (light, vigor, development) carry
# `#pragma omp parallel for`; without OpenMP they run serially. The step is
# irregular tree-growth traversal, so it stays on the CPU — this is how it
# scales across cores. Optional: absent OpenMP the pragmas are ignored.
find_package(OpenMP QUIET)
if(OpenMP_CXX_FOUND)
    target_link_libraries(broflora PUBLIC OpenMP::OpenMP_CXX)
    message(STATUS "broflora: OpenMP enabled (${OpenMP_CXX_FLAGS})")
else()
    message(STATUS "broflora: OpenMP not found, sim passes run serially")
endif()

if(MSVC)
    target_compile_options(broflora PRIVATE /W4 $<$<NOT:$<BOOL:${OpenMP_CXX_FOUND}>>:/wd4068>)
else()
    target_compile_options(broflora PRIVATE -Wall -Wextra)
endif()

# --- Required bromath dependency (sibling header-only library) ---
# Guard against double-add — bro adds bromath before broflora, as does
# bromesh below. First-loader wins.
if(NOT TARGET bromath)
    set(BROMATH_DIR "${CMAKE_SOURCE_DIR}/../bromath" CACHE PATH "bromath sibling")
    if(EXISTS "${BROMATH_DIR}/CMakeLists.txt")
        add_subdirectory("${BROMATH_DIR}" "${CMAKE_BINARY_DIR}/bromath" EXCLUDE_FROM_ALL)
        message(STATUS "broflora: using sibling bromath at ${BROMATH_DIR}")
    else()
        message(FATAL_ERROR "bromath not found at ${BROMATH_DIR}")
    endif()
endif()
target_link_libraries(broflora PUBLIC bromath::bromath)

# --- Required bromesh dependency (sibling library) ---
# broflora emits geometry as bromesh::MeshData and is meaningless without
# bromesh in the picture. Same double-add guard pattern as bromath.
if(NOT TARGET bromesh)
    set(BROMESH_DIR "${CMAKE_SOURCE_DIR}/../bromesh" CACHE PATH "bromesh sibling")
    if(EXISTS "${BROMESH_DIR}/CMakeLists.txt")
        add_subdirectory("${BROMESH_DIR}" "${CMAKE_BINARY_DIR}/bromesh" EXCLUDE_FROM_ALL)
        message(STATUS "broflora: using sibling bromesh at ${BROMESH_DIR}")
    else()
        message(FATAL_ERROR "bromesh not found at ${BROMESH_DIR}")
    endif()
endif()
target_link_libraries(broflora PUBLIC bromesh)

# --- Tests ---
option(BROFLORA_TESTS "Build broflora tests" ${PROJECT_IS_TOP_LEVEL})
if(BROFLORA_TESTS AND CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    enable_testing()
    add_subdirectory(tests)
endif()

# --- Examples ---
# A user-shaped reference for building a world end-to-end. Off by
# default when broflora is consumed as a subdirectory.
option(BROFLORA_EXAMPLES "Build broflora example drivers" ${PROJECT_IS_TOP_LEVEL})
if(BROFLORA_EXAMPLES AND CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    add_executable(broflora_grove examples/grove.cpp)
    target_link_libraries(broflora_grove PRIVATE broflora)
endif()
