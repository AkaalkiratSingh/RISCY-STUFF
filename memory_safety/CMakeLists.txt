add_library(memory_safety STATIC
    src/stack_guard.cpp
    src/visualizer.cpp
)
target_include_directories(memory_safety PUBLIC include)
target_link_libraries(memory_safety PUBLIC isa_spec)
