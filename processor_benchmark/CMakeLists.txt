add_executable(risc201-pipeline-sweep pipeline_sweep.cpp)
target_link_libraries(risc201-pipeline-sweep PRIVATE simulator_core_lib assembler_lib)

add_test(NAME PipelineSweepTest
         COMMAND risc201-pipeline-sweep --check ${CMAKE_CURRENT_SOURCE_DIR})
