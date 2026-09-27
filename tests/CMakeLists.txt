add_executable(pass1_test pass1_test.cpp)
target_link_libraries(pass1_test PRIVATE assembler_lib)
add_test(NAME Pass1Test COMMAND pass1_test)

add_executable(pass2_test pass2_test.cpp)
target_link_libraries(pass2_test PRIVATE assembler_lib)
add_test(NAME Pass2Test COMMAND pass2_test)

add_executable(encoder_test encoder_test.cpp)
target_link_libraries(encoder_test PRIVATE assembler_lib)
add_test(NAME EncoderTest COMMAND encoder_test)