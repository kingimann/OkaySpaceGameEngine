add_executable(test_math test_math.cpp)
target_link_libraries(test_math PRIVATE okay::engine)
add_test(NAME math COMMAND test_math)

add_executable(test_scene test_scene.cpp)
target_link_libraries(test_scene PRIVATE okay::engine)
add_test(NAME scene COMMAND test_scene)
