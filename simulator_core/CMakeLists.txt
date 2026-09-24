add_library(simulator_core_lib STATIC
    src/cpu.cpp
    src/debugger.cpp
    src/cli.cpp
)
target_include_directories(simulator_core_lib PUBLIC include)
target_link_libraries(simulator_core_lib PUBLIC isa_spec alu control_unit memory_safety)

add_executable(risc201-sim src/main.cpp)
target_link_libraries(risc201-sim PRIVATE simulator_core_lib)
