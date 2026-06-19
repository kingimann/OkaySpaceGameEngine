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

add_executable(test_physics3d test_physics3d.cpp)
target_link_libraries(test_physics3d PRIVATE okay::engine)
add_test(NAME physics3d COMMAND test_physics3d)

add_executable(test_scriptapi test_scriptapi.cpp)
target_link_libraries(test_scriptapi PRIVATE okay::engine)
add_test(NAME scriptapi COMMAND test_scriptapi)

add_executable(test_material test_material.cpp)
target_link_libraries(test_material PRIVATE okay::engine)
add_test(NAME material COMMAND test_material)

add_executable(test_actionlist test_actionlist.cpp)
target_link_libraries(test_actionlist PRIVATE okay::engine)
add_test(NAME actionlist COMMAND test_actionlist)

add_executable(test_charctrl test_charctrl.cpp)
target_link_libraries(test_charctrl PRIVATE okay::engine)
add_test(NAME charctrl COMMAND test_charctrl)

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

add_executable(test_script_components test_script_components.cpp)
target_link_libraries(test_script_components PRIVATE okay::engine)
add_test(NAME script_components COMMAND test_script_components)

add_executable(test_scene_load test_scene_load.cpp)
target_link_libraries(test_scene_load PRIVATE okay::engine)
add_test(NAME scene_load COMMAND test_scene_load)

add_executable(test_script_physics test_script_physics.cpp)
target_link_libraries(test_script_physics PRIVATE okay::engine)
add_test(NAME script_physics COMMAND test_script_physics)

add_executable(test_script_events test_script_events.cpp)
target_link_libraries(test_script_events PRIVATE okay::engine)
add_test(NAME script_events COMMAND test_script_events)

add_executable(test_assets test_assets.cpp)
target_link_libraries(test_assets PRIVATE okay::engine)
add_test(NAME assets COMMAND test_assets)

add_executable(test_tilemap_collision test_tilemap_collision.cpp)
target_link_libraries(test_tilemap_collision PRIVATE okay::engine)
add_test(NAME tilemap_collision COMMAND test_tilemap_collision)

add_executable(test_particles_serialize test_particles_serialize.cpp)
target_link_libraries(test_particles_serialize PRIVATE okay::engine)
add_test(NAME particles_serialize COMMAND test_particles_serialize)

add_executable(test_timescale test_timescale.cpp)
target_link_libraries(test_timescale PRIVATE okay::engine)
add_test(NAME timescale COMMAND test_timescale)

add_executable(test_gravity test_gravity.cpp)
target_link_libraries(test_gravity PRIVATE okay::engine)
add_test(NAME gravity COMMAND test_gravity)

add_executable(test_master_volume test_master_volume.cpp)
target_link_libraries(test_master_volume PRIVATE okay::engine)
add_test(NAME master_volume COMMAND test_master_volume)

add_executable(test_script_arrays test_script_arrays.cpp)
target_link_libraries(test_script_arrays PRIVATE okay::engine)
add_test(NAME script_arrays COMMAND test_script_arrays)

add_executable(test_script_lang2 test_script_lang2.cpp)
target_link_libraries(test_script_lang2 PRIVATE okay::engine)
add_test(NAME script_lang2 COMMAND test_script_lang2)

add_executable(test_ui test_ui.cpp)
target_link_libraries(test_ui PRIVATE okay::engine)
add_test(NAME ui COMMAND test_ui)

add_executable(test_mesh3d test_mesh3d.cpp)
target_link_libraries(test_mesh3d PRIVATE okay::engine)
add_test(NAME mesh3d COMMAND test_mesh3d)

add_executable(test_software_renderer test_software_renderer.cpp)
target_link_libraries(test_software_renderer PRIVATE okay::engine)
add_test(NAME software_renderer COMMAND test_software_renderer)

add_executable(test_script_maps test_script_maps.cpp)
target_link_libraries(test_script_maps PRIVATE okay::engine)
add_test(NAME script_maps COMMAND test_script_maps)

add_executable(test_script_lang3 test_script_lang3.cpp)
target_link_libraries(test_script_lang3 PRIVATE okay::engine)
add_test(NAME script_lang3 COMMAND test_script_lang3)

add_executable(test_script_util test_script_util.cpp)
target_link_libraries(test_script_util PRIVATE okay::engine)
add_test(NAME script_util COMMAND test_script_util)

add_executable(test_script_lib test_script_lib.cpp)
target_link_libraries(test_script_lib PRIVATE okay::engine)
add_test(NAME script_lib COMMAND test_script_lib)

add_executable(test_script_camera test_script_camera.cpp)
target_link_libraries(test_script_camera PRIVATE okay::engine)
add_test(NAME script_camera COMMAND test_script_camera)

add_executable(test_script_fx test_script_fx.cpp)
target_link_libraries(test_script_fx PRIVATE okay::engine)
add_test(NAME script_fx COMMAND test_script_fx)

add_executable(test_script_objects test_script_objects.cpp)
target_link_libraries(test_script_objects PRIVATE okay::engine)
add_test(NAME script_objects COMMAND test_script_objects)

add_executable(test_script_tilemap test_script_tilemap.cpp)
target_link_libraries(test_script_tilemap PRIVATE okay::engine)
add_test(NAME script_tilemap COMMAND test_script_tilemap)

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

add_executable(test_follow test_follow.cpp)
target_link_libraries(test_follow PRIVATE okay::engine)
add_test(NAME follow COMMAND test_follow)

add_executable(test_ui_system test_ui_system.cpp)
target_link_libraries(test_ui_system PRIVATE okay::engine)
add_test(NAME ui_system COMMAND test_ui_system)

add_executable(test_scenemanager test_scenemanager.cpp)
target_link_libraries(test_scenemanager PRIVATE okay::engine)
add_test(NAME scenemanager COMMAND test_scenemanager)

add_executable(test_script_net test_script_net.cpp)
target_link_libraries(test_script_net PRIVATE okay::engine)
add_test(NAME script_net COMMAND test_script_net)

add_executable(test_steam_script test_steam_script.cpp)
target_link_libraries(test_steam_script PRIVATE okay::engine)
add_test(NAME steam_script COMMAND test_steam_script)

add_executable(test_uidocument test_uidocument.cpp)
target_link_libraries(test_uidocument PRIVATE okay::engine)
add_test(NAME uidocument COMMAND test_uidocument)
