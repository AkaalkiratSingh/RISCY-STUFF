add_executable(pass1_test pass1_test.cpp)
target_link_libraries(pass1_test PRIVATE assembler_lib)
add_test(NAME Pass1Test COMMAND pass1_test)