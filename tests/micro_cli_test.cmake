file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/micro_cli.hex" "E0000000\n")
execute_process(COMMAND "${SIM}" "${CMAKE_CURRENT_BINARY_DIR}/micro_cli.hex" --micro-trace --vertical
    RESULT_VARIABLE result OUTPUT_VARIABLE output)
if(NOT result EQUAL 1 OR NOT output MATCHES "ILLEGAL_INSTRUCTION pc=0" OR NOT output MATCHES "microPC=0" OR NOT output MATCHES "signals: halt trap")
    message(FATAL_ERROR "Illegal opcode trace failed: ${result} ${output}")
endif()
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/micro_cli.hex" "F8000000\n")
execute_process(COMMAND "${SIM}" "${CMAKE_CURRENT_BINARY_DIR}/micro_cli.hex" --micro-trace --trace
    RESULT_VARIABLE result OUTPUT_VARIABLE output)
if(NOT result EQUAL 0 OR NOT output MATCHES "microPC=.*END" OR NOT output MATCHES "retired 0")
    message(FATAL_ERROR "HALT trace failed: ${result} ${output}")
endif()
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/micro_cli_input.txt" "q\n")
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/micro_cli.hex" "0C400007\nF8000000\n")
execute_process(COMMAND "${SIM}" "${CMAKE_CURRENT_BINARY_DIR}/micro_cli.hex" --micro-step
    INPUT_FILE "${CMAKE_CURRENT_BINARY_DIR}/micro_cli_input.txt"
    RESULT_VARIABLE result OUTPUT_VARIABLE output)
if(NOT result EQUAL 2 OR NOT output MATCHES "Enter: next microcode row")
    message(FATAL_ERROR "Interactive stop failed: ${result} ${output}")
endif()
