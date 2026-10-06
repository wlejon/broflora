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

# --- Siblings: bromath (header-only) + bromesh ---
# Each resolves like every repo in the bro ecosystem: an existing target (bro
# adds both before broflora; first loader wins); otherwise a checkout beside the
# top-level project (../<name>, overridable with -D<NAME>_DIR=<path>);
# otherwise the top-level project's third_party/<name> submodule (`git
# submodule update --init --recursive`). third_party/ is flat: it carries every
# sibling broflora needs, transitive ones included. bromath is resolved first
# so bromesh finds the target and the diamond resolves once.
function(broflora_sibling name what)
    if(TARGET ${name})
        return()
    endif()
    string(TOUPPER "${name}" _upper)
    set(${_upper}_DIR "${CMAKE_SOURCE_DIR}/../${name}" CACHE PATH
        "Path to standalone ${name} repo (set to a missing path to force submodule fallback)")
    if(EXISTS "${${_upper}_DIR}/CMakeLists.txt")
        message(STATUS "broflora: ${name} from ${${_upper}_DIR}")
        add_subdirectory("${${_upper}_DIR}" "${CMAKE_BINARY_DIR}/${name}" EXCLUDE_FROM_ALL)
    elseif(EXISTS "${CMAKE_SOURCE_DIR}/third_party/${name}/CMakeLists.txt")
        message(STATUS "broflora: ${name} from ${CMAKE_SOURCE_DIR}/third_party/${name} (submodule)")
        add_subdirectory("${CMAKE_SOURCE_DIR}/third_party/${name}" "${CMAKE_BINARY_DIR}/${name}" EXCLUDE_FROM_ALL)
    else()
        message(FATAL_ERROR
            "broflora needs ${name} (${what}): not found at ${${_upper}_DIR} or "
            "${CMAKE_SOURCE_DIR}/third_party/${name}. Clone https://github.com/wlejon/${name} next "
            "to this repo, run `git submodule update --init --recursive`, or override with "
            "-D${_upper}_DIR=...")
    endif()
endfunction()

broflora_sibling(bromath "math headers")
target_link_libraries(broflora PUBLIC bromath::bromath)

# broflora emits geometry as bromesh::MeshData and is meaningless without
# bromesh in the picture.
broflora_sibling(bromesh "mesh geometry")
target_link_libraries(broflora PUBLIC bromesh)

# --- bronze, the JavaScript runtime the api binds and the compiler for src/api/js ---
# A checkout beside this one (../bronze, with ../brass beside it — bronze
# resolves and add_subdirectory()s its own code generator), the layout of a
# bro tree and of CI (bronze's .github/actions/checkout-toolchain), or an
# explicit -DBRONZE_DIR. No vendored fallback: the api is bound across
# bronze's C++ embed boundary, so it has to build against the bronze the
# surrounding tree builds against. Under bro this block is skipped, bro having
# added bronze already.
if(NOT TARGET bronze_runtime_shared)
    set(BRONZE_BUILD_SHARED_RUNTIME ON CACHE BOOL "" FORCE)
    set(BRONZE_BUILD_TESTS OFF CACHE BOOL "" FORCE)
    if(DEFINED BRONZE_DIR AND EXISTS "${BRONZE_DIR}/CMakeLists.txt")
        # Explicit BRONZE_DIR supplied
    elseif(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../bronze/CMakeLists.txt")
        set(BRONZE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../bronze" CACHE PATH "bronze source tree")
    elseif(EXISTS "${CMAKE_SOURCE_DIR}/../bronze/CMakeLists.txt")
        # Beside the top-level project: this repo is a third_party/ submodule
        # of the project being built.
        set(BRONZE_DIR "${CMAKE_SOURCE_DIR}/../bronze" CACHE PATH "bronze source tree")
    endif()

    if(NOT BRONZE_DIR OR NOT EXISTS "${BRONZE_DIR}/CMakeLists.txt")
        message(FATAL_ERROR
            "bronze not found: check it out beside this repository (../bronze, with ../brass beside it) "
            "or pass -DBRONZE_DIR=<path>.")
    endif()

    add_subdirectory("${BRONZE_DIR}" "${CMAKE_BINARY_DIR}/bronze-build" EXCLUDE_FROM_ALL)
endif()

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
