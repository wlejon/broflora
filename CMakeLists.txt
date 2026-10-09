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
    src/leaf_cluster.cpp
    src/sdf_mesh.cpp
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

# --- Dependencies ---
# Resolved by cmake/bro_deps.cmake, like every repo in the bro ecosystem: an
# existing target (bro adds these before broflora, and its pins win) first;
# otherwise the ../<name> working tree beside the top-level project; otherwise
# the commit pinned here, fetched at configure. Leaves first: bromath before
# bromesh, so the diamond resolves once.
include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/bro_deps.cmake)

bro_dependency(bromath GITHUB wlejon/bromath REF 8b511fbd69c014bf0584d2b42e6c96a8cf720f6a TARGET bromath::bromath)
target_link_libraries(broflora PUBLIC bromath::bromath)

# broflora emits geometry as bromesh::MeshData and is meaningless without
# bromesh in the picture.
bro_dependency(bromesh GITHUB wlejon/bromesh REF fe7fdc1801f96d246d34e3f33b706ba9359cd217)
target_link_libraries(broflora PUBLIC bromesh)

# bronze, the JavaScript runtime the api binds and the compiler for src/api/js
# (it brings brass, its code generator). The api is bound across bronze's C++
# embed boundary, so it has to build against the bronze the surrounding tree
# builds against.
bro_dependency(bronze GITHUB wlejon/bronze REF 15ce4cf5fc25d8afe03882985ae95adcba46a6a9 TARGET bronze_runtime_shared
               OPTIONS BRONZE_BUILD_SHARED_RUNTIME=ON BRONZE_BUILD_TESTS=OFF)

# --- Standalone Bronze JavaScript API ---
add_subdirectory(src/api)

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
