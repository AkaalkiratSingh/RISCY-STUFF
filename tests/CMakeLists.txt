add_executable(horizontal_control_unit_test horizontal_control_unit_test.cpp)
target_link_libraries(horizontal_control_unit_test PRIVATE control_unit)
add_test(NAME HorizontalControlUnitTest COMMAND horizontal_control_unit_test)

add_executable(control_unit_alu_integration control_unit_alu_integration.cpp)
target_link_libraries(control_unit_alu_integration PRIVATE control_unit alu)
add_test(NAME ControlUnitAluIntegration COMMAND control_unit_alu_integration)

add_executable(vertical_control_unit_test vertical_control_unit_test.cpp)
target_link_libraries(vertical_control_unit_test PRIVATE simulator_core_lib assembler_lib)
target_compile_definitions(vertical_control_unit_test PRIVATE EXAMPLES_DIR="${CMAKE_SOURCE_DIR}/examples")
add_test(NAME VerticalControlUnitTest COMMAND vertical_control_unit_test)

add_executable(cpu_integration_test cpu_integration_test.cpp)
target_link_libraries(cpu_integration_test PRIVATE simulator_core_lib assembler_lib)
add_test(NAME CpuIntegrationTest COMMAND cpu_integration_test)

add_executable(decode_test decode_test.cpp)
target_link_libraries(decode_test PRIVATE simulator_core_lib assembler_lib)
add_test(NAME DecodeTest COMMAND decode_test)

add_executable(pass1_test pass1_test.cpp)
target_link_libraries(pass1_test PRIVATE assembler_lib)
add_test(NAME Pass1Test COMMAND pass1_test)

add_executable(pass2_test pass2_test.cpp)
target_link_libraries(pass2_test PRIVATE assembler_lib)
add_test(NAME Pass2Test COMMAND pass2_test)

add_executable(encoder_test encoder_test.cpp)
target_link_libraries(encoder_test PRIVATE assembler_lib)
add_test(NAME EncoderTest COMMAND encoder_test)

add_executable(disassembler_test disassembler_test.cpp)
target_link_libraries(disassembler_test PRIVATE assembler_lib)
add_test(NAME DisassemblerTest COMMAND disassembler_test)

add_executable(test_alu test_alu.cpp)
target_link_libraries(test_alu PRIVATE alu)
add_test(NAME AluTest COMMAND test_alu)

add_executable(memory_safety_test memory_safety_test.cpp)
target_link_libraries(memory_safety_test PRIVATE simulator_core_lib)
add_test(NAME MemorySafetyTest COMMAND memory_safety_test)

add_executable(control_decode_test control_decode_test.cpp)
target_link_libraries(control_decode_test PRIVATE simulator_core_lib)
add_test(NAME ControlDecodeTest COMMAND control_decode_test)

add_executable(pipeline_test pipeline_test.cpp)
target_link_libraries(pipeline_test PRIVATE simulator_core_lib assembler_lib)
add_test(NAME PipelineTest COMMAND pipeline_test)

add_executable(literals_test literals_test.cpp)
target_link_libraries(literals_test PRIVATE assembler_lib)
add_test(NAME LiteralsTest COMMAND literals_test)

add_executable(preprocessor_test preprocessor_test.cpp)
target_link_libraries(preprocessor_test PRIVATE assembler_lib)
add_test(NAME PreprocessorTest COMMAND preprocessor_test)

add_executable(debugger_test debugger_test.cpp)
target_link_libraries(debugger_test PRIVATE simulator_core_lib assembler_lib)
add_test(NAME DebuggerTest COMMAND debugger_test)

add_executable(micro_trace_test micro_trace_test.cpp)
target_link_libraries(micro_trace_test PRIVATE simulator_core_lib)
add_test(NAME MicroTraceTest COMMAND micro_trace_test)

add_test(NAME MicroCliTest COMMAND ${CMAKE_COMMAND}
    -DSIM=$<TARGET_FILE:risc201-sim> -P ${CMAKE_CURRENT_SOURCE_DIR}/micro_cli_test.cmake)

add_test(NAME ControlRomListings COMMAND risc201-rom --check ${CMAKE_SOURCE_DIR}/docs)
