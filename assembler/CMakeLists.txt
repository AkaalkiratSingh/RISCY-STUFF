add_library(assembler_lib STATIC
    src/assembler.cpp
    src/disassembler.cpp
    src/preprocessor.cpp
)
target_include_directories(assembler_lib PUBLIC include)
target_link_libraries(assembler_lib PUBLIC isa_spec)

# CLI tool: risc201-asm
add_executable(risc201-asm src/main_asm.cpp)
target_link_libraries(risc201-asm PRIVATE assembler_lib)

# CLI tool: risc201-disasm
add_executable(risc201-disasm src/main_disasm.cpp)
target_link_libraries(risc201-disasm PRIVATE assembler_lib)