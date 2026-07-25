cmake_minimum_required(VERSION 3.16)
project(elfr C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE RelWithDebInfo)
endif()

add_compile_options(-Wall -Wextra)

find_package(PkgConfig REQUIRED)
pkg_check_modules(YAML REQUIRED IMPORTED_TARGET yaml-0.1)

include_directories(${CMAKE_SOURCE_DIR}/include)

# OBJECT library: its object files are compiled once and embedded directly
# into every target below (via $<TARGET_OBJECTS:...>), so libelfr.a /
# libelfr.so are self-contained and consumers never need to separately
# link elfr_common themselves.
add_library(elfr_common OBJECT
    src/common/fnv1a.c
    src/common/crc64.c
    src/common/byte_buf.c
)
set_target_properties(elfr_common PROPERTIES POSITION_INDEPENDENT_CODE ON)

# OBJECT library holding elfrc's internals (manifest parsing, container
# building, ELF emission), embedded into the elfrc executable and also
# linked directly into the unit tests so they can exercise these functions
# without shelling out to the CLI.
add_library(elfrc_core OBJECT
    src/elfrc/manifest_yaml.c
    src/elfrc/container_build.c
    src/elfrc/elf_writer.c
)
target_link_libraries(elfrc_core PRIVATE PkgConfig::YAML)

add_executable(elfrc
    src/elfrc/main.c
    $<TARGET_OBJECTS:elfrc_core>
    $<TARGET_OBJECTS:elfr_common>
)
target_link_libraries(elfrc PRIVATE PkgConfig::YAML)

add_library(elfr_shared SHARED
    src/libelfr/erf_reader.c
    src/libelfr/erf_lookup.c
    $<TARGET_OBJECTS:elfr_common>
)
set_target_properties(elfr_shared PROPERTIES OUTPUT_NAME elfr)

add_library(elfr_static STATIC
    src/libelfr/erf_reader.c
    src/libelfr/erf_lookup.c
    $<TARGET_OBJECTS:elfr_common>
)
set_target_properties(elfr_static PROPERTIES OUTPUT_NAME elfr)

add_executable(elfresdump
    src/elfresdump/main.c
)
target_link_libraries(elfresdump PRIVATE elfr_static)

install(DIRECTORY include/elfr DESTINATION include)
install(TARGETS elfr_shared elfr_static elfrc elfresdump
    RUNTIME DESTINATION bin
    LIBRARY DESTINATION lib
    ARCHIVE DESTINATION lib
)

enable_testing()
add_subdirectory(tests)
