add_library(utils_lib STATIC
    src/utils.cpp
)
target_include_directories(utils_lib PUBLIC include)
target_link_libraries(utils_lib PUBLIC isa_spec)
