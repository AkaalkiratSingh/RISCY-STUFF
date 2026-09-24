add_library(control_unit STATIC
    src/control_unit.cpp
)
target_include_directories(control_unit PUBLIC include)
target_link_libraries(control_unit PUBLIC isa_spec)
