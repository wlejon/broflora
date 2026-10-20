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
    src/foliage_emit.cpp
    src/sway.cpp
    src/clumps.cpp
    src/tubes.cpp
    src/prototypes.cpp
    src/presets.cpp
    src/leaf_cluster.cpp
    src/sdf_mesh.cpp
    src/world_io.cpp
    src/herbs.cpp
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
# existing target (bro adds these before broflora, and its choice wins) first;
# otherwise the ../<name> working tree beside the top-level project; otherwise
# the head of its main, fetched at configure. Leaves first: bromath before
# bromesh, so the diamond resolves once.
include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/bro_deps.cmake)

bro_dependency(bromath GITHUB wlejon/bromath TARGET bromath::bromath)
target_link_libraries(broflora PUBLIC bromath::bromath)

# BROFLORA_SIM_HASH: the hash of the step's source (its translation units,
# the headers they read, bromath's headers, and the serializer), which keys
# growCached's cache of grown worlds. Only what a step computes with is in
# it, so an edit to emit or clumps keeps the cache warm.
get_target_property(_bromath_inc bromath::bromath INTERFACE_INCLUDE_DIRECTORIES)
set(_bromath_headers "")
foreach(_d IN LISTS _bromath_inc)
    string(REGEX REPLACE "^\\$<BUILD_INTERFACE:(.*)>$" "\\1" _d "${_d}")
    if(_d MATCHES "^\\$<" OR NOT IS_DIRECTORY "${_d}")
        continue()
    endif()
    file(GLOB_RECURSE _h CONFIGURE_DEPENDS "${_d}/bromath/*.h")
    list(APPEND _bromath_headers ${_h})
endforeach()
list(SORT _bromath_headers)
set(_sim_sources)
foreach(_f world light vigor development spawning senescence world_io)
    list(APPEND _sim_sources "${CMAKE_CURRENT_SOURCE_DIR}/src/${_f}.cpp")
endforeach()
foreach(_f world module environment plant orientation light vigor development spawning senescence world_io)
    list(APPEND _sim_sources "${CMAKE_CURRENT_SOURCE_DIR}/include/broflora/${_f}.h")
endforeach()
file(GLOB _sim_internal CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/src/internal_*.h")
list(SORT _sim_internal)
list(APPEND _sim_sources ${_sim_internal} ${_bromath_headers})
set(_sim_dir "${CMAKE_CURRENT_BINARY_DIR}/generated")
add_custom_command(
    OUTPUT "${_sim_dir}/sim_hash.stamp"
    BYPRODUCTS "${_sim_dir}/broflora/sim_hash.h"
    COMMAND ${CMAKE_COMMAND} "-DOUT=${_sim_dir}/broflora/sim_hash.h" "-DSTAMP=${_sim_dir}/sim_hash.stamp"
            "-DFILES=${_sim_sources}" -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/sim_hash.cmake"
    DEPENDS ${_sim_sources} "${CMAKE_CURRENT_SOURCE_DIR}/cmake/sim_hash.cmake"
    COMMENT "broflora: hashing the step's source"
    VERBATIM)
target_sources(broflora PRIVATE "${_sim_dir}/sim_hash.stamp")
target_include_directories(broflora PRIVATE "${_sim_dir}")

# broflora emits geometry as bromesh::MeshData and is meaningless without
# bromesh in the picture.
bro_dependency(bromesh GITHUB wlejon/bromesh)
target_link_libraries(broflora PUBLIC bromesh)

# bronze, the JavaScript runtime the api binds and the compiler for src/api/js
# (it brings brass, its code generator). The api is bound across bronze's C++
# embed boundary, so it has to build against the bronze the surrounding tree
# builds against.
bro_dependency(bronze GITHUB wlejon/bronze TARGET bronze_runtime_shared
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
