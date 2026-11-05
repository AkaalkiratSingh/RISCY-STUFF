add_library(alu STATIC src/alu.cpp)

target_include_directories(alu PUBLIC 
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../isa_spec
)

target_link_libraries(alu PUBLIC isa_spec)