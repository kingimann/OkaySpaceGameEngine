add_executable(test_math test_math.cpp)
target_link_libraries(test_math PRIVATE okay::engine)
add_test(NAME math COMMAND test_math)

add_executable(test_scene test_scene.cpp)
target_link_libraries(test_scene PRIVATE okay::engine)
add_test(NAME scene COMMAND test_scene)

add_executable(test_net test_net.cpp)
target_link_libraries(test_net PRIVATE okay::engine)
add_test(NAME net COMMAND test_net)

add_executable(test_visualscript test_visualscript.cpp)
target_link_libraries(test_visualscript PRIVATE okay::engine)
add_test(NAME visualscript COMMAND test_visualscript)

add_executable(test_scripting test_scripting.cpp)
target_link_libraries(test_scripting PRIVATE okay::engine)
add_test(NAME scripting COMMAND test_scripting)

# Built only when the optional Lua backend is enabled.
if(OKAY_WITH_LUA)
    add_executable(test_lua test_lua.cpp)
    target_link_libraries(test_lua PRIVATE okay::engine)
    add_test(NAME lua COMMAND test_lua)
endif()

# Built only when the optional C# (Mono) backend is enabled.
if(OKAY_WITH_CSHARP)
    add_executable(test_csharp test_csharp.cpp)
    target_link_libraries(test_csharp PRIVATE okay::engine)
    add_test(NAME csharp COMMAND test_csharp)
endif()
