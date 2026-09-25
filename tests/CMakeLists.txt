add_executable(broflora_test
    test_main.cpp
    test_smoke.cpp
    test_pipe_model.cpp
    test_light.cpp
    test_spawning.cpp
    test_seeding.cpp
    test_branch_length.cpp
    test_orientation.cpp
    test_mesh_emit.cpp
    test_prototypes.cpp
    test_regression.cpp
    test_spatial_hash.cpp
    test_step_observer.cpp
    test_terrain.cpp
    test_remove_plant.cpp
    test_leaf_cluster.cpp
)

target_link_libraries(broflora_test PRIVATE broflora)
target_compile_features(broflora_test PRIVATE cxx_std_20)

if(MSVC)
    target_compile_options(broflora_test PRIVATE /W4)
else()
    target_compile_options(broflora_test PRIVATE -Wall -Wextra)
endif()

add_test(NAME broflora_test COMMAND broflora_test)

add_executable(test_sdf_mesh test_sdf_mesh.cpp)
target_link_libraries(test_sdf_mesh PRIVATE broflora bromesh)
target_compile_features(test_sdf_mesh PRIVATE cxx_std_20)

if(MSVC)
    target_compile_options(test_sdf_mesh PRIVATE /W4)
else()
    target_compile_options(test_sdf_mesh PRIVATE -Wall -Wextra)
endif()

add_test(NAME test_sdf_mesh COMMAND test_sdf_mesh)

# The api test installs bro.flora into a bronze realm and drives it. The
# JavaScript half is js/flora.js compiled by the bronze CLI, which needs a
# brass code generator for the target (BRASS_HOST_BACKEND; x86_64 and AArch64
# have one). Where there is none the api carries a no-op entry instead
# (src/api/CMakeLists.txt) and there is nothing for the test to see: it is
# not registered there rather than registered to fail.
if(TARGET broflora_api AND NOT BROFLORA_API_COMPILED_JS)
    message(STATUS "broflora: broflora_api_test not registered (no compiled JS on this target)")
endif()
if(TARGET broflora_api AND BROFLORA_API_COMPILED_JS)
    add_executable(broflora_api_test test_api.cpp)
    target_link_libraries(broflora_api_test PRIVATE broflora_api bronze_runtime_shared)
    target_compile_features(broflora_api_test PRIVATE cxx_std_20)
    if(MSVC)
        target_compile_options(broflora_api_test PRIVATE /W4)
        target_link_options(broflora_api_test PRIVATE /FORCE:MULTIPLE)
    else()
        target_compile_options(broflora_api_test PRIVATE -Wall -Wextra)
    endif()
    # bronze's shared runtime is built into BRONZE_SHARED_RUNTIME_DIR, not
    # beside this executable, and the PE loader only looks beside the .exe
    # (0xc0000135 + a modal "dll was not found" otherwise). Stage it next to
    # the test as a post-build step and put both dirs on ctest's PATH.
    if(WIN32)
        add_custom_command(TARGET broflora_api_test POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                $<TARGET_FILE:bronze_runtime_shared>
                $<TARGET_FILE_DIR:broflora_api_test>
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                $<TARGET_LINKER_FILE:bronze_runtime_shared>
                $<TARGET_FILE_DIR:broflora_api_test>
        )
    endif()
    add_test(NAME broflora_api_test COMMAND broflora_api_test)
    # The same test with bronze's collector flipping at every allocation: a
    # raw Value held across an allocating embed call in a binding crashes or
    # misreads here where the normal run happens to pass.
    add_test(NAME broflora_api_test_gcstress COMMAND broflora_api_test)
    if(WIN32)
        set_tests_properties(broflora_api_test PROPERTIES
            ENVIRONMENT "PATH=$<TARGET_FILE_DIR:bronze_runtime_shared>;$<TARGET_FILE_DIR:broflora_api_test>;$ENV{PATH}")
        set_tests_properties(broflora_api_test_gcstress PROPERTIES
            ENVIRONMENT "BRONZE_GC_STRESS=1;PATH=$<TARGET_FILE_DIR:bronze_runtime_shared>;$<TARGET_FILE_DIR:broflora_api_test>;$ENV{PATH}")
    else()
        set_tests_properties(broflora_api_test_gcstress PROPERTIES ENVIRONMENT "BRONZE_GC_STRESS=1")
    endif()
endif()
