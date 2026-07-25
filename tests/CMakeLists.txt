# ---- Unit tests ----

add_executable(test_hash unit/test_hash.c $<TARGET_OBJECTS:elfr_common>)
add_test(NAME test_hash COMMAND test_hash)

add_executable(test_manifest unit/test_manifest.c $<TARGET_OBJECTS:elfrc_core> $<TARGET_OBJECTS:elfr_common>)
target_link_libraries(test_manifest PRIVATE PkgConfig::YAML)
add_test(NAME test_manifest COMMAND test_manifest)

add_executable(test_container unit/test_container.c $<TARGET_OBJECTS:elfrc_core> $<TARGET_OBJECTS:elfr_common>)
target_link_libraries(test_container PRIVATE PkgConfig::YAML)
add_test(NAME test_container COMMAND test_container)

# ---- Integration test: elfrc -> ELF object -> libelfr, end to end ----

set(EXAMPLE_DIR ${CMAKE_SOURCE_DIR}/examples/basic)
set(EXAMPLE_RESOURCES_O ${CMAKE_CURRENT_BINARY_DIR}/example_resources.o)

add_custom_command(
    OUTPUT ${EXAMPLE_RESOURCES_O}
    COMMAND $<TARGET_FILE:elfrc> ${EXAMPLE_DIR}/resources.elfrc -o ${EXAMPLE_RESOURCES_O}
    DEPENDS elfrc
            ${EXAMPLE_DIR}/resources.elfrc
            ${EXAMPLE_DIR}/assets/logo.png
            ${EXAMPLE_DIR}/assets/config.json
            ${EXAMPLE_DIR}/assets/font/Roboto.ttf
            ${EXAMPLE_DIR}/assets/shaders/main.spv
            ${EXAMPLE_DIR}/assets/locale/en.json
            ${EXAMPLE_DIR}/assets/locale/fr.json
    COMMENT "elfrc: compiling examples/basic/resources.elfrc"
)
add_custom_target(example_resources_obj DEPENDS ${EXAMPLE_RESOURCES_O})

add_executable(test_roundtrip integration/test_roundtrip.c ${EXAMPLE_RESOURCES_O})
add_dependencies(test_roundtrip example_resources_obj)
target_link_libraries(test_roundtrip PRIVATE elfr_static)
add_test(NAME test_roundtrip COMMAND test_roundtrip ${EXAMPLE_DIR})

# Also build the example's own demo binary, so it's exercised by every
# build (not just `ctest`).
add_executable(example_basic ${EXAMPLE_DIR}/main.c ${EXAMPLE_RESOURCES_O})
add_dependencies(example_basic example_resources_obj)
target_link_libraries(example_basic PRIVATE elfr_static)
