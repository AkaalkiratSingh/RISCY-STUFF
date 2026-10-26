add_library(tui_lib STATIC
    src/terminal.cpp
    src/screen.cpp
    src/tui.cpp
)

target_include_directories(tui_lib PUBLIC include)
target_link_libraries(tui_lib PUBLIC
    simulator_core_lib
    assembler_lib
    isa_spec
    utils_lib
)

add_executable(risc201-tui src/main.cpp)
target_link_libraries(risc201-tui PRIVATE
    tui_lib
    simulator_core_lib
    assembler_lib
    utils_lib
)
