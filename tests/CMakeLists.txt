add_executable(control_unit_test control_unit_test.cpp)
target_link_libraries(control_unit_test PRIVATE control_unit)
add_test(NAME ControlUnitTest COMMAND control_unit_test)

add_executable(cpu_integration_test cpu_integration_test.cpp)
target_link_libraries(cpu_integration_test PRIVATE simulator_core_lib assembler_lib)
add_test(NAME CpuIntegrationTest COMMAND cpu_integration_test)

add_executable(pass1_test pass1_test.cpp)
target_link_libraries(pass1_test PRIVATE assembler_lib)
add_test(NAME Pass1Test COMMAND pass1_test)

add_executable(pass2_test pass2_test.cpp)
target_link_libraries(pass2_test PRIVATE assembler_lib)
add_test(NAME Pass2Test COMMAND pass2_test)

add_executable(encoder_test encoder_test.cpp)
target_link_libraries(encoder_test PRIVATE assembler_lib)
add_test(NAME EncoderTest COMMAND encoder_test)

add_test(NAME AluTest COMMAND test_alu)

add_executable(memory_safety_test memory_safety_test.cpp)
target_link_libraries(memory_safety_test PRIVATE simulator_core_lib)
add_test(NAME MemorySafetyTest COMMAND memory_safety_test)
