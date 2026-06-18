add_executable(test_math test_math.cpp)
target_link_libraries(test_math PRIVATE okay::engine)
add_test(NAME math COMMAND test_math)

add_executable(test_scene test_scene.cpp)
target_link_libraries(test_scene PRIVATE okay::engine)
add_test(NAME scene COMMAND test_scene)

add_executable(test_serialize test_serialize.cpp)
target_link_libraries(test_serialize PRIVATE okay::engine)
add_test(NAME serialize COMMAND test_serialize)

add_executable(test_instantiate test_instantiate.cpp)
target_link_libraries(test_instantiate PRIVATE okay::engine)
add_test(NAME instantiate COMMAND test_instantiate)

add_executable(test_remove test_remove.cpp)
target_link_libraries(test_remove PRIVATE okay::engine)
add_test(NAME remove_component COMMAND test_remove)

add_executable(test_audio test_audio.cpp)
target_link_libraries(test_audio PRIVATE okay::engine)
add_test(NAME audio COMMAND test_audio)

add_executable(test_layers test_layers.cpp)
target_link_libraries(test_layers PRIVATE okay::engine)
add_test(NAME physics_layers COMMAND test_layers)

add_executable(test_transform_input test_transform_input.cpp)
target_link_libraries(test_transform_input PRIVATE okay::engine)
add_test(NAME transform_input COMMAND test_transform_input)

add_executable(test_core_features test_core_features.cpp)
target_link_libraries(test_core_features PRIVATE okay::engine)
add_test(NAME core_features COMMAND test_core_features)

add_executable(test_gameplay test_gameplay.cpp)
target_link_libraries(test_gameplay PRIVATE okay::engine)
add_test(NAME gameplay COMMAND test_gameplay)

add_executable(test_prefs test_prefs.cpp)
target_link_libraries(test_prefs PRIVATE okay::engine)
add_test(NAME prefs COMMAND test_prefs)

add_executable(test_pathfinding test_pathfinding.cpp)
target_link_libraries(test_pathfinding PRIVATE okay::engine)
add_test(NAME pathfinding COMMAND test_pathfinding)

add_executable(test_easing test_easing.cpp)
target_link_libraries(test_easing PRIVATE okay::engine)
add_test(NAME easing COMMAND test_easing)

add_executable(test_templates test_templates.cpp)
target_link_libraries(test_templates PRIVATE okay::engine)
add_test(NAME templates COMMAND test_templates)

add_executable(test_image test_image.cpp)
target_link_libraries(test_image PRIVATE okay::engine)
add_test(NAME image COMMAND test_image)

add_executable(test_text test_text.cpp)
target_link_libraries(test_text PRIVATE okay::engine)
add_test(NAME text COMMAND test_text)

add_executable(test_spriteanim test_spriteanim.cpp)
target_link_libraries(test_spriteanim PRIVATE okay::engine)
add_test(NAME spriteanim COMMAND test_spriteanim)

add_executable(test_wav test_wav.cpp)
target_link_libraries(test_wav PRIVATE okay::engine)
add_test(NAME wav COMMAND test_wav)

add_executable(test_script_spawn test_script_spawn.cpp)
target_link_libraries(test_script_spawn PRIVATE okay::engine)
add_test(NAME script_spawn COMMAND test_script_spawn)

add_executable(test_physics test_physics.cpp)
target_link_libraries(test_physics PRIVATE okay::engine)
add_test(NAME physics COMMAND test_physics)

add_executable(test_3d test_3d.cpp)
target_link_libraries(test_3d PRIVATE okay::engine)
add_test(NAME 3d COMMAND test_3d)

add_executable(test_animation test_animation.cpp)
target_link_libraries(test_animation PRIVATE okay::engine)
add_test(NAME animation COMMAND test_animation)

add_executable(test_queries test_queries.cpp)
target_link_libraries(test_queries PRIVATE okay::engine)
add_test(NAME physics_queries COMMAND test_queries)

add_executable(test_particles_tilemap test_particles_tilemap.cpp)
target_link_libraries(test_particles_tilemap PRIVATE okay::engine)
add_test(NAME particles_tilemap COMMAND test_particles_tilemap)

add_executable(test_net test_net.cpp)
target_link_libraries(test_net PRIVATE okay::engine)
add_test(NAME net COMMAND test_net)

add_executable(test_visualscript test_visualscript.cpp)
target_link_libraries(test_visualscript PRIVATE okay::engine)
add_test(NAME visualscript COMMAND test_visualscript)

add_executable(test_scripting test_scripting.cpp)
target_link_libraries(test_scripting PRIVATE okay::engine)
add_test(NAME scripting COMMAND test_scripting)

add_executable(test_steam test_steam.cpp)
target_link_libraries(test_steam PRIVATE okay::engine)
add_test(NAME steam COMMAND test_steam)

add_executable(test_playfab test_playfab.cpp)
target_link_libraries(test_playfab PRIVATE okay::engine)
add_test(NAME playfab COMMAND test_playfab)

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
