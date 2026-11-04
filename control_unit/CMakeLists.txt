add_library(control_unit STATIC
    src/horizontal_control_unit.cpp
    src/vertical_control_unit.cpp
)
target_include_directories(control_unit PUBLIC include)
target_link_libraries(control_unit PUBLIC isa_spec utils_lib)

add_executable(risc201-rom src/export_control_rom.cpp)
target_link_libraries(risc201-rom PRIVATE control_unit)
add_custom_target(export_control_rom
    COMMAND $<TARGET_FILE:risc201-rom> ${CMAKE_SOURCE_DIR}/docs
    DEPENDS risc201-rom
    VERBATIM)
