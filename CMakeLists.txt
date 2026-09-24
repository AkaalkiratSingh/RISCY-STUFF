cmake_minimum_required(VERSION 3.16)
project(risc201 CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Debug)
endif()

add_compile_options(-Wall -Wextra)

# isa_spec is a header-only shared contract used by every module.
add_library(isa_spec INTERFACE)
target_include_directories(isa_spec INTERFACE ${CMAKE_SOURCE_DIR}/isa_spec)

enable_testing()

add_subdirectory(alu)
add_subdirectory(control_unit)
add_subdirectory(assembler)
add_subdirectory(memory_safety)
add_subdirectory(simulator_core)

if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/tests/CMakeLists.txt)
  add_subdirectory(tests)
endif()
