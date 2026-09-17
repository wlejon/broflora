## ── Compile JS flora with bronze AOT at build time ─────────────────────────

# bronze's code generator emits x86_64 only, so on Apple Silicon js/flora.js
# is not compiled and a no-op entry stands in: the natives still register,
# but nothing mounts `bro.flora`. BROFLORA_API_COMPILED_JS tells the tests
# which of the two they are looking at.
if(APPLE AND (CMAKE_OSX_ARCHITECTURES MATCHES "arm64" OR (NOT CMAKE_OSX_ARCHITECTURES AND CMAKE_SYSTEM_PROCESSOR MATCHES "arm64")))
    set(BROFLORA_COMPILED_JS_OBJECTS "${CMAKE_CURRENT_SOURCE_DIR}/bronze_js_stubs_arm64.cpp")
    set(BROFLORA_API_COMPILED_JS OFF PARENT_SCOPE)
    message(STATUS "broflora: api built with the arm64 no-op JS entry (bronze emits x86_64 only)")
else()
    set(BROFLORA_API_COMPILED_JS ON PARENT_SCOPE)
    set(GEN_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated")
    file(MAKE_DIRECTORY "${GEN_DIR}")

    set(FLORA_GLOBALS "${CMAKE_CURRENT_SOURCE_DIR}/flora.globals")
    set(BROFLORA_COMPILED_JS_OBJECTS "")

    function(bronze_compile_js JS_FILE ENTRY_SYMBOL)
        set(INPUT  "${CMAKE_CURRENT_SOURCE_DIR}/js/${JS_FILE}.js")
        set(OUTPUT "${GEN_DIR}/${JS_FILE}.o")
        add_custom_command(
            OUTPUT  "${OUTPUT}"
            COMMAND $<TARGET_FILE:bronze> build "${INPUT}" --emit-obj --host-globals "${FLORA_GLOBALS}" --entry-symbol=${ENTRY_SYMBOL} -o "${OUTPUT}"
            DEPENDS bronze "${INPUT}" "${FLORA_GLOBALS}"
            COMMENT "bronze compile js/${JS_FILE}.js -> ${JS_FILE}.o (${ENTRY_SYMBOL})"
            VERBATIM
        )
        set_source_files_properties("${OUTPUT}" PROPERTIES EXTERNAL_OBJECT TRUE GENERATED TRUE)
        list(APPEND BROFLORA_COMPILED_JS_OBJECTS "${OUTPUT}")
        set(BROFLORA_COMPILED_JS_OBJECTS "${BROFLORA_COMPILED_JS_OBJECTS}" PARENT_SCOPE)
    endfunction()

    bronze_compile_js(flora bronze_flora_main)
endif()

## ── Static Library ──────────────────────────────────────────────────────────

set(BROFLORA_API_SOURCES
    api.cpp
    native_flora.cpp
    native_flora_emit.cpp
)

add_library(broflora_api STATIC
    ${BROFLORA_API_SOURCES}
    ${BROFLORA_COMPILED_JS_OBJECTS}
)

add_library(broflora::api ALIAS broflora_api)

target_include_directories(broflora_api PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/..>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/../../include>
    $<INSTALL_INTERFACE:include>
)

target_link_libraries(broflora_api PUBLIC
    broflora
    bromath::bromath
    bromesh
    bronze_runtime_shared
)

find_package(OpenMP QUIET)
if(OpenMP_CXX_FOUND)
    target_link_libraries(broflora_api PUBLIC OpenMP::OpenMP_CXX)
endif()

target_compile_features(broflora_api PUBLIC cxx_std_20)

if(MSVC)
    target_compile_options(broflora_api PRIVATE /MP /permissive- /utf-8 /W4)
else()
    target_compile_options(broflora_api PRIVATE -Wall -Wextra)
endif()
