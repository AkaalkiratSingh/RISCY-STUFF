add_library(alu STATIC
    src/alu.cpp
)
target_include_directories(alu PUBLIC include)
target_link_libraries(alu PUBLIC isa_spec)
add_executable(test_alu ../tests/test_alu.cpp)
target_link_libraries(test_alu PRIVATE alu)
