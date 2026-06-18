# Runnable feature examples.
add_executable(netdemo netdemo/main.cpp)
target_link_libraries(netdemo PRIVATE okay::engine)
set_target_properties(netdemo PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
