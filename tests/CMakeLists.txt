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

if(TARGET broflora_api)
    add_executable(broflora_api_test test_api.cpp)
    target_link_libraries(broflora_api_test PRIVATE broflora_api bronze_runtime_shared)
    target_compile_features(broflora_api_test PRIVATE cxx_std_20)
    if(MSVC)
        target_compile_options(broflora_api_test PRIVATE /W4)
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
    if(WIN32)
        set_tests_properties(broflora_api_test PROPERTIES
            ENVIRONMENT "PATH=$<TARGET_FILE_DIR:bronze_runtime_shared>;$<TARGET_FILE_DIR:broflora_api_test>;$ENV{PATH}")
    endif()
endif()
