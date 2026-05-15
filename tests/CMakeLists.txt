add_executable(broflora_test
    test_main.cpp
    test_smoke.cpp
    test_pipe_model.cpp
    test_light.cpp
    test_spawning.cpp
    test_seeding.cpp
)

target_link_libraries(broflora_test PRIVATE broflora)
target_compile_features(broflora_test PRIVATE cxx_std_20)

if(MSVC)
    target_compile_options(broflora_test PRIVATE /W4)
else()
    target_compile_options(broflora_test PRIVATE -Wall -Wextra)
endif()

add_test(NAME broflora_test COMMAND broflora_test)
