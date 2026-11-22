# Runnable feature examples.
add_executable(netdemo netdemo/main.cpp)
target_link_libraries(netdemo PRIVATE okay::engine)
set_target_properties(netdemo PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

add_executable(physicsdemo physicsdemo/main.cpp)
target_link_libraries(physicsdemo PRIVATE okay::engine)
set_target_properties(physicsdemo PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

# Writes examples/streaming_assets (pictures, rock.obj, the scene): make_streaming_assets <folder>
add_executable(make_streaming_assets streaming_assets/make_scene.cpp)
target_link_libraries(make_streaming_assets PRIVATE okay::engine)
set_target_properties(make_streaming_assets PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
