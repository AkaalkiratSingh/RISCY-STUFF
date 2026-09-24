add_library(alu STATIC
    src/alu.cpp
)
target_include_directories(alu PUBLIC include)
target_link_libraries(alu PUBLIC isa_spec)
