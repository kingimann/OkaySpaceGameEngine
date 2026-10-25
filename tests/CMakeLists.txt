add_executable(test_math test_math.cpp)
target_link_libraries(test_math PRIVATE okay::engine)
add_test(NAME math COMMAND test_math)

add_executable(test_scene test_scene.cpp)
target_link_libraries(test_scene PRIVATE okay::engine)
add_test(NAME scene COMMAND test_scene)

add_executable(test_serialize test_serialize.cpp)
target_link_libraries(test_serialize PRIVATE okay::engine)
add_test(NAME serialize COMMAND test_serialize)

# Robustness: parent cycles (Transform::SetParent + scene/prefab/creation loaders)
add_executable(test_parent_cycles test_parent_cycles.cpp)
target_link_libraries(test_parent_cycles PRIVATE okay::engine)
add_test(NAME parent_cycles COMMAND test_parent_cycles)

add_executable(test_instantiate test_instantiate.cpp)
target_link_libraries(test_instantiate PRIVATE okay::engine)
add_test(NAME instantiate COMMAND test_instantiate)

# Robustness: scene record counts / indices are validated on load
add_executable(test_scene_records test_scene_records.cpp)
target_link_libraries(test_scene_records PRIVATE okay::engine)
add_test(NAME scene_records COMMAND test_scene_records)
set_tests_properties(scene_records PROPERTIES TIMEOUT 300)

add_executable(test_remove test_remove.cpp)
target_link_libraries(test_remove PRIVATE okay::engine)
add_test(NAME remove_component COMMAND test_remove)

add_executable(test_audio test_audio.cpp)
target_link_libraries(test_audio PRIVATE okay::engine)
add_test(NAME audio COMMAND test_audio)

add_executable(test_layers test_layers.cpp)
target_link_libraries(test_layers PRIVATE okay::engine)
add_test(NAME physics_layers COMMAND test_layers)

# Robustness: a NaN body can't poison statics / other bodies
add_executable(test_physics_nan test_physics_nan.cpp)
target_link_libraries(test_physics_nan PRIVATE okay::engine)
add_test(NAME physics_nan COMMAND test_physics_nan)

add_executable(test_physics3d test_physics3d.cpp)
target_link_libraries(test_physics3d PRIVATE okay::engine)
add_test(NAME physics3d COMMAND test_physics3d)

# Robustness: removing colliders / bodies / components from contact callbacks
add_executable(test_physics_callback_removal test_physics_callback_removal.cpp)
target_link_libraries(test_physics_callback_removal PRIVATE okay::engine)
add_test(NAME physics_callback_removal COMMAND test_physics_callback_removal)

# Robustness: a handler removing the OTHER collider stops the rest of that dispatch
add_executable(test_physics_dispatch_removal test_physics_dispatch_removal.cpp)
target_link_libraries(test_physics_dispatch_removal PRIVATE okay::engine)
add_test(NAME physics_dispatch_removal COMMAND test_physics_dispatch_removal)

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

# Robustness: Scheduler callbacks that Invoke / Cancel / Clear during Update
add_executable(test_scheduler_reentry test_scheduler_reentry.cpp)
target_link_libraries(test_scheduler_reentry PRIVATE okay::engine)
add_test(NAME scheduler_reentry COMMAND test_scheduler_reentry)

add_executable(test_gameplay test_gameplay.cpp)
target_link_libraries(test_gameplay PRIVATE okay::engine)
add_test(NAME gameplay COMMAND test_gameplay)

add_executable(test_prefs test_prefs.cpp)
target_link_libraries(test_prefs PRIVATE okay::engine)
add_test(NAME prefs COMMAND test_prefs)

# Robustness: Prefs escaping/precision, TweenSequence cycles, Mat4::Inverse, UB-free hashes
add_executable(test_prefs_misc test_prefs_misc.cpp)
target_link_libraries(test_prefs_misc PRIVATE okay::engine)
add_test(NAME prefs_misc COMMAND test_prefs_misc)

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

# Robustness: OBJ / glTF / image loaders reject malformed input
add_executable(test_loaders_robust test_loaders_robust.cpp)
target_link_libraries(test_loaders_robust PRIVATE okay::engine)
add_test(NAME loaders_robust COMMAND test_loaders_robust)
set_tests_properties(loaders_robust PROPERTIES TIMEOUT 120)   # a glTF node cycle used to hang the import

add_executable(test_text test_text.cpp)
target_link_libraries(test_text PRIVATE okay::engine)
add_test(NAME text COMMAND test_text)

add_executable(test_ttf test_ttf.cpp)
target_link_libraries(test_ttf PRIVATE okay::engine)
add_test(NAME ttf COMMAND test_ttf)

add_executable(test_block_builder test_block_builder.cpp)
target_link_libraries(test_block_builder PRIVATE okay::engine)
add_test(NAME block_builder COMMAND test_block_builder)

add_executable(test_structure_builder test_structure_builder.cpp)
target_link_libraries(test_structure_builder PRIVATE okay::engine)
add_test(NAME structure_builder COMMAND test_structure_builder)

add_executable(test_builder_mode test_builder_mode.cpp)
target_link_libraries(test_builder_mode PRIVATE okay::engine)
add_test(NAME builder_mode COMMAND test_builder_mode)

add_executable(test_build_mode test_build_mode.cpp)
target_link_libraries(test_build_mode PRIVATE okay::engine)
add_test(NAME build_mode COMMAND test_build_mode)

add_executable(test_loading_screen test_loading_screen.cpp)
target_link_libraries(test_loading_screen PRIVATE okay::engine)
add_test(NAME loading_screen COMMAND test_loading_screen)

add_executable(test_inventory_ui test_inventory_ui.cpp)
target_link_libraries(test_inventory_ui PRIVATE okay::engine)
add_test(NAME inventory_ui COMMAND test_inventory_ui)

add_executable(test_grid_inventory test_grid_inventory.cpp)
target_link_libraries(test_grid_inventory PRIVATE okay::engine)
add_test(NAME grid_inventory COMMAND test_grid_inventory)

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

add_executable(test_particles_fx test_particles_fx.cpp)
target_link_libraries(test_particles_fx PRIVATE okay::engine)
add_test(NAME particles_fx COMMAND test_particles_fx)

add_executable(test_profiler test_profiler.cpp)
target_link_libraries(test_profiler PRIVATE okay::engine)
add_test(NAME profiler COMMAND test_profiler)

add_executable(test_datapack test_datapack.cpp)
target_link_libraries(test_datapack PRIVATE okay::engine)
add_test(NAME datapack COMMAND test_datapack)

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

add_executable(test_meshedit test_meshedit.cpp)
target_link_libraries(test_meshedit PRIVATE okay::engine)
add_test(NAME meshedit COMMAND test_meshedit)

add_executable(test_player test_player.cpp)
target_link_libraries(test_player PRIVATE okay::engine)
add_test(NAME player COMMAND test_player)

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

add_executable(test_physics_queries_ext test_physics_queries_ext.cpp)
target_link_libraries(test_physics_queries_ext PRIVATE okay::engine)
add_test(NAME physics_queries_ext COMMAND test_physics_queries_ext)

add_executable(test_particles_tilemap test_particles_tilemap.cpp)
target_link_libraries(test_particles_tilemap PRIVATE okay::engine)
add_test(NAME particles_tilemap COMMAND test_particles_tilemap)

add_executable(test_net test_net.cpp)
target_link_libraries(test_net PRIVATE okay::engine)
add_test(NAME net COMMAND test_net)

add_executable(test_net_auth test_net_auth.cpp)
target_link_libraries(test_net_auth PRIVATE okay::engine)
add_test(NAME net_auth COMMAND test_net_auth)

add_executable(test_net_crypto test_net_crypto.cpp)
target_link_libraries(test_net_crypto PRIVATE okay::engine)
add_test(NAME net_crypto COMMAND test_net_crypto)

add_executable(test_net_aoi test_net_aoi.cpp)
target_link_libraries(test_net_aoi PRIVATE okay::engine)
add_test(NAME net_aoi COMMAND test_net_aoi)

add_executable(test_matchmaking test_matchmaking.cpp)
target_link_libraries(test_matchmaking PRIVATE okay::engine)
add_test(NAME matchmaking COMMAND test_matchmaking)

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

add_executable(test_cinemachine test_cinemachine.cpp)
target_link_libraries(test_cinemachine PRIVATE okay::engine)
add_test(NAME cinemachine COMMAND test_cinemachine)

add_executable(test_shaders test_shaders.cpp)
target_link_libraries(test_shaders PRIVATE okay::engine)
add_test(NAME shaders COMMAND test_shaders)

add_executable(test_uishape test_uishape.cpp)
target_link_libraries(test_uishape PRIVATE okay::engine)
add_test(NAME uishape COMMAND test_uishape)

add_executable(test_ui_system test_ui_system.cpp)
target_link_libraries(test_ui_system PRIVATE okay::engine)
add_test(NAME ui_system COMMAND test_ui_system)

add_executable(test_scenemanager test_scenemanager.cpp)
target_link_libraries(test_scenemanager PRIVATE okay::engine)
add_test(NAME scenemanager COMMAND test_scenemanager)

add_executable(test_script_net test_script_net.cpp)
target_link_libraries(test_script_net PRIVATE okay::engine)
add_test(NAME script_net COMMAND test_script_net)

add_executable(test_net_relay test_net_relay.cpp)
target_link_libraries(test_net_relay PRIVATE okay::engine)
add_test(NAME net_relay COMMAND test_net_relay)

add_executable(test_net_rpc test_net_rpc.cpp)
target_link_libraries(test_net_rpc PRIVATE okay::engine)
add_test(NAME net_rpc COMMAND test_net_rpc)

add_executable(test_net_sync test_net_sync.cpp)
target_link_libraries(test_net_sync PRIVATE okay::engine)
add_test(NAME net_sync COMMAND test_net_sync)

add_executable(test_net_spawn test_net_spawn.cpp)
target_link_libraries(test_net_spawn PRIVATE okay::engine)
add_test(NAME net_spawn COMMAND test_net_spawn)

add_executable(test_animclip test_animclip.cpp)
target_link_libraries(test_animclip PRIVATE okay::engine)
add_test(NAME animclip COMMAND test_animclip)

add_executable(test_script_anim test_script_anim.cpp)
target_link_libraries(test_script_anim PRIVATE okay::engine)
add_test(NAME script_anim COMMAND test_script_anim)

add_executable(test_steam_script test_steam_script.cpp)
target_link_libraries(test_steam_script PRIVATE okay::engine)
add_test(NAME steam_script COMMAND test_steam_script)

add_executable(test_net_rooms test_net_rooms.cpp)
target_link_libraries(test_net_rooms PRIVATE okay::engine)
add_test(NAME net_rooms COMMAND test_net_rooms)

add_executable(test_terrain test_terrain.cpp)
target_link_libraries(test_terrain PRIVATE okay::engine)
add_test(NAME terrain COMMAND test_terrain)

add_executable(test_voxel test_voxel.cpp)
target_link_libraries(test_voxel PRIVATE okay::engine)
add_test(NAME voxel COMMAND test_voxel)

add_executable(test_pausemenu test_pausemenu.cpp)
target_link_libraries(test_pausemenu PRIVATE okay::engine)
add_test(NAME pausemenu COMMAND test_pausemenu)

add_executable(test_worldstreamer test_worldstreamer.cpp)
target_link_libraries(test_worldstreamer PRIVATE okay::engine)
add_test(NAME worldstreamer COMMAND test_worldstreamer)

add_executable(test_destructible test_destructible.cpp)
target_link_libraries(test_destructible PRIVATE okay::engine)
add_test(NAME destructible COMMAND test_destructible)

add_executable(test_shadows test_shadows.cpp)
target_link_libraries(test_shadows PRIVATE okay::engine)
add_test(NAME shadows COMMAND test_shadows)

add_executable(test_script_debug test_script_debug.cpp)
target_link_libraries(test_script_debug PRIVATE okay::engine)
add_test(NAME script_debug COMMAND test_script_debug)

add_executable(test_ui_extra test_ui_extra.cpp)
target_link_libraries(test_ui_extra PRIVATE okay::engine)
add_test(NAME ui_extra COMMAND test_ui_extra)

add_executable(test_lighting test_lighting.cpp)
target_link_libraries(test_lighting PRIVATE okay::engine)
add_test(NAME lighting COMMAND test_lighting)

add_executable(test_scene_pointer test_scene_pointer.cpp)
target_link_libraries(test_scene_pointer PRIVATE okay::engine)
add_test(NAME scene_pointer COMMAND test_scene_pointer)

add_executable(test_light_shadows test_light_shadows.cpp)
target_link_libraries(test_light_shadows PRIVATE okay::engine)
add_test(NAME light_shadows COMMAND test_light_shadows)

add_executable(test_modifiers test_modifiers.cpp)
target_link_libraries(test_modifiers PRIVATE okay::engine)
add_test(NAME modifiers COMMAND test_modifiers)

add_executable(test_uv test_uv.cpp)
target_link_libraries(test_uv PRIVATE okay::engine)
add_test(NAME uv COMMAND test_uv)

add_executable(test_savedata test_savedata.cpp)
target_link_libraries(test_savedata PRIVATE okay::engine)
add_test(NAME savedata COMMAND test_savedata)

add_executable(test_animator test_animator.cpp)
target_link_libraries(test_animator PRIVATE okay::engine)
add_test(NAME animator COMMAND test_animator)

add_executable(test_dataasset test_dataasset.cpp)
target_link_libraries(test_dataasset PRIVATE okay::engine)
add_test(NAME dataasset COMMAND test_dataasset)

add_executable(test_item_dragdrop test_item_dragdrop.cpp)
target_link_libraries(test_item_dragdrop PRIVATE okay::engine)
add_test(NAME item_dragdrop COMMAND test_item_dragdrop)

add_executable(test_csharp_style_script test_csharp_style_script.cpp)
target_link_libraries(test_csharp_style_script PRIVATE okay::engine)
add_test(NAME csharp_style_script COMMAND test_csharp_style_script)

add_executable(test_uidocument test_uidocument.cpp)
target_link_libraries(test_uidocument PRIVATE okay::engine)
add_test(NAME uidocument COMMAND test_uidocument)

add_executable(test_collider_fit test_collider_fit.cpp)
target_link_libraries(test_collider_fit PRIVATE okay::engine)
add_test(NAME collider_fit COMMAND test_collider_fit)

add_executable(test_clicktomove test_clicktomove.cpp)
target_link_libraries(test_clicktomove PRIVATE okay::engine)
add_test(NAME clicktomove COMMAND test_clicktomove)

add_executable(test_rpg test_rpg.cpp)
target_link_libraries(test_rpg PRIVATE okay::engine)
add_test(NAME rpg COMMAND test_rpg)

add_executable(test_stance test_stance.cpp)
target_link_libraries(test_stance PRIVATE okay::engine)
add_test(NAME stance COMMAND test_stance)

add_executable(test_uiwidgets test_uiwidgets.cpp)
target_link_libraries(test_uiwidgets PRIVATE okay::engine)
add_test(NAME uiwidgets COMMAND test_uiwidgets)

add_executable(test_uicapture_look test_uicapture_look.cpp)
target_link_libraries(test_uicapture_look PRIVATE okay::engine)
add_test(NAME uicapture_look COMMAND test_uicapture_look)

add_executable(test_ui_layering test_ui_layering.cpp)
target_link_libraries(test_ui_layering PRIVATE okay::engine)
add_test(NAME ui_layering COMMAND test_ui_layering)

add_executable(test_ui_layer_table test_ui_layer_table.cpp)
target_link_libraries(test_ui_layer_table PRIVATE okay::engine)
target_compile_definitions(test_ui_layer_table PRIVATE OKAY_SOURCE_DIR="${PROJECT_SOURCE_DIR}")
add_test(NAME ui_layer_table COMMAND test_ui_layer_table)

add_executable(test_jump test_jump.cpp)
target_link_libraries(test_jump PRIVATE okay::engine)
add_test(NAME jump COMMAND test_jump)

add_executable(test_fp_hand test_fp_hand.cpp)
target_link_libraries(test_fp_hand PRIVATE okay::engine)
add_test(NAME fp_hand COMMAND test_fp_hand)

# The account service core is header-only, so this test just needs the engine's
# public include path and C++17 — no engine link required.
add_executable(test_account test_account.cpp)
target_compile_features(test_account PRIVATE cxx_std_17)
target_include_directories(test_account PRIVATE ${CMAKE_SOURCE_DIR}/engine/include)
add_test(NAME account COMMAND test_account)

# Exercises the okay::Account facade and the account_* OkayScript builtins, so
# this one links the engine.
add_executable(test_account_script test_account_script.cpp)
target_link_libraries(test_account_script PRIVATE okay::engine)
add_test(NAME account_script COMMAND test_account_script)

add_executable(test_worldtoscreen test_worldtoscreen.cpp)
target_link_libraries(test_worldtoscreen PRIVATE okay::engine)
add_test(NAME worldtoscreen COMMAND test_worldtoscreen)

add_executable(test_worldcanvas test_worldcanvas.cpp)
target_link_libraries(test_worldcanvas PRIVATE okay::engine)
add_test(NAME worldcanvas COMMAND test_worldcanvas)

add_executable(test_vehicle test_vehicle.cpp)
target_link_libraries(test_vehicle PRIVATE okay::engine)
add_test(NAME vehicle COMMAND test_vehicle)

add_executable(test_survival test_survival.cpp)
target_link_libraries(test_survival PRIVATE okay::engine)
add_test(NAME survival COMMAND test_survival)

add_executable(test_minimap test_minimap.cpp)
target_link_libraries(test_minimap PRIVATE okay::engine)
add_test(NAME minimap COMMAND test_minimap)

add_executable(test_daynight test_daynight.cpp)
target_link_libraries(test_daynight PRIVATE okay::engine)
add_test(NAME daynight COMMAND test_daynight)

add_executable(test_npc_crafting test_npc_crafting.cpp)
target_link_libraries(test_npc_crafting PRIVATE okay::engine)
add_test(NAME npc_crafting COMMAND test_npc_crafting)

add_executable(test_script_ui test_script_ui.cpp)
target_link_libraries(test_script_ui PRIVATE okay::engine)
add_test(NAME script_ui COMMAND test_script_ui)

# OkayUI renders through SDL_Renderer, so this test drives a headless SOFTWARE
# renderer and reads pixels back. Only built when SDL2 (hence okay_ui) is present.
if(TARGET okay_ui)
    add_executable(test_okayui test_okayui.cpp)
    target_link_libraries(test_okayui PRIVATE okay_ui)
    add_test(NAME okayui COMMAND test_okayui)
endif()

add_executable(test_character_parts test_character_parts.cpp)
target_link_libraries(test_character_parts PRIVATE okay::engine)
add_test(NAME character_parts COMMAND test_character_parts)

add_executable(test_animator_edit test_animator_edit.cpp)
target_link_libraries(test_animator_edit PRIVATE okay::engine)
add_test(NAME animator_edit COMMAND test_animator_edit)

add_executable(test_pause_menu test_pause_menu.cpp)
target_link_libraries(test_pause_menu PRIVATE okay::engine)
add_test(NAME pause_menu COMMAND test_pause_menu)

add_executable(test_char_anim test_char_anim.cpp)
target_link_libraries(test_char_anim PRIVATE okay::engine)
add_test(NAME char_anim COMMAND test_char_anim)

add_executable(test_char_state_anim test_char_state_anim.cpp)
target_link_libraries(test_char_state_anim PRIVATE okay::engine)
add_test(NAME char_state_anim COMMAND test_char_state_anim)

add_executable(test_char_blend test_char_blend.cpp)
target_link_libraries(test_char_blend PRIVATE okay::engine)
add_test(NAME char_blend COMMAND test_char_blend)

add_executable(test_char_anim_events test_char_anim_events.cpp)
target_link_libraries(test_char_anim_events PRIVATE okay::engine)
add_test(NAME char_anim_events COMMAND test_char_anim_events)

add_executable(test_fp_punch test_fp_punch.cpp)
target_link_libraries(test_fp_punch PRIVATE okay::engine)
add_test(NAME fp_punch COMMAND test_fp_punch)

add_executable(test_char_editor_parts test_char_editor_parts.cpp)
target_link_libraries(test_char_editor_parts PRIVATE okay::engine)
add_test(NAME char_editor_parts COMMAND test_char_editor_parts)

add_executable(test_voxel_dig test_voxel_dig.cpp)
target_link_libraries(test_voxel_dig PRIVATE okay::engine)
add_test(NAME voxel_dig COMMAND test_voxel_dig)

add_executable(test_api_expand test_api_expand.cpp)
target_link_libraries(test_api_expand PRIVATE okay::engine)
add_test(NAME api_expand COMMAND test_api_expand)

add_executable(test_modeling test_modeling.cpp)
target_link_libraries(test_modeling PRIVATE okay::engine)
add_test(NAME modeling COMMAND test_modeling)

add_executable(test_physics_explode test_physics_explode.cpp)
target_link_libraries(test_physics_explode PRIVATE okay::engine)
add_test(NAME physics_explode COMMAND test_physics_explode)

add_executable(test_subtree test_subtree.cpp)
target_link_libraries(test_subtree PRIVATE okay::engine)
add_test(NAME subtree COMMAND test_subtree)

add_executable(test_camera_cine test_camera_cine.cpp)
target_link_libraries(test_camera_cine PRIVATE okay::engine)
add_test(NAME camera_cine COMMAND test_camera_cine)

add_executable(test_player_fixes test_player_fixes.cpp)
target_link_libraries(test_player_fixes PRIVATE okay::engine)
add_test(NAME player_fixes COMMAND test_player_fixes)

add_executable(test_terrain_ops test_terrain_ops.cpp)
target_link_libraries(test_terrain_ops PRIVATE okay::engine)
add_test(NAME terrain_ops COMMAND test_terrain_ops)

add_executable(test_npc_ai test_npc_ai.cpp)
target_link_libraries(test_npc_ai PRIVATE okay::engine)
add_test(NAME npc_ai COMMAND test_npc_ai)

add_executable(test_mp_controllers test_mp_controllers.cpp)
target_link_libraries(test_mp_controllers PRIVATE okay::engine)
add_test(NAME mp_controllers COMMAND test_mp_controllers)

add_executable(test_net_player_spawn test_net_player_spawn.cpp)
target_link_libraries(test_net_player_spawn PRIVATE okay::engine)
add_test(NAME net_player_spawn COMMAND test_net_player_spawn)

add_executable(test_anim_layers test_anim_layers.cpp)
target_link_libraries(test_anim_layers PRIVATE okay::engine)
add_test(NAME anim_layers COMMAND test_anim_layers)

add_executable(test_obj_import test_obj_import.cpp)
target_link_libraries(test_obj_import PRIVATE okay::engine)
add_test(NAME obj_import COMMAND test_obj_import)

add_executable(test_gltf_import test_gltf_import.cpp)
target_link_libraries(test_gltf_import PRIVATE okay::engine)
add_test(NAME gltf_import COMMAND test_gltf_import)

add_executable(test_skinning test_skinning.cpp)
target_link_libraries(test_skinning PRIVATE okay::engine)
add_test(NAME skinning COMMAND test_skinning)

add_executable(test_physics_joints test_physics_joints.cpp)
target_link_libraries(test_physics_joints PRIVATE okay::engine)
add_test(NAME physics_joints COMMAND test_physics_joints)

add_executable(test_math_extra test_math_extra.cpp)
target_link_libraries(test_math_extra PRIVATE okay::engine)
add_test(NAME math_extra COMMAND test_math_extra)

add_executable(test_2d_expand test_2d_expand.cpp)
target_link_libraries(test_2d_expand PRIVATE okay::engine)
add_test(NAME 2d_expand COMMAND test_2d_expand)

add_executable(test_phys_math test_phys_math.cpp)
target_link_libraries(test_phys_math PRIVATE okay::engine)
add_test(NAME phys_math COMMAND test_phys_math)

add_executable(test_angular2d test_angular2d.cpp)
target_link_libraries(test_angular2d PRIVATE okay::engine)
add_test(NAME angular2d COMMAND test_angular2d)

add_executable(test_angular3d test_angular3d.cpp)
target_link_libraries(test_angular3d PRIVATE okay::engine)
add_test(NAME angular3d COMMAND test_angular3d)

add_executable(test_hinge2d test_hinge2d.cpp)
target_link_libraries(test_hinge2d PRIVATE okay::engine)
add_test(NAME hinge2d COMMAND test_hinge2d)

add_executable(test_hinge3d test_hinge3d.cpp)
target_link_libraries(test_hinge3d PRIVATE okay::engine)
add_test(NAME hinge3d COMMAND test_hinge3d)

add_executable(test_net_backend test_net_backend.cpp)
target_link_libraries(test_net_backend PRIVATE okay::engine)
add_test(NAME net_backend COMMAND test_net_backend)

add_executable(test_anim_ik test_anim_ik.cpp)
target_link_libraries(test_anim_ik PRIVATE okay::engine)
add_test(NAME anim_ik COMMAND test_anim_ik)

add_executable(test_humanoid_template test_humanoid_template.cpp)
target_link_libraries(test_humanoid_template PRIVATE okay::engine)
add_test(NAME humanoid_template COMMAND test_humanoid_template)

add_executable(test_controllers_ik test_controllers_ik.cpp)
target_link_libraries(test_controllers_ik PRIVATE okay::engine)
add_test(NAME controllers_ik COMMAND test_controllers_ik)

add_executable(test_ecs test_ecs.cpp)
target_link_libraries(test_ecs PRIVATE okay::engine)
add_test(NAME ecs COMMAND test_ecs)

add_executable(test_anim_playback test_anim_playback.cpp)
target_link_libraries(test_anim_playback PRIVATE okay::engine)
add_test(NAME anim_playback COMMAND test_anim_playback)

add_executable(test_editor_plugins test_editor_plugins.cpp)
target_link_libraries(test_editor_plugins PRIVATE okay::engine)
target_compile_definitions(test_editor_plugins PRIVATE OKAY_SOURCE_DIR="${PROJECT_SOURCE_DIR}")
add_test(NAME editor_plugins COMMAND test_editor_plugins)

add_executable(test_anim_state_machine test_anim_state_machine.cpp)
target_link_libraries(test_anim_state_machine PRIVATE okay::engine)
add_test(NAME anim_state_machine COMMAND test_anim_state_machine)

add_executable(test_ik_core test_ik_core.cpp)
target_link_libraries(test_ik_core PRIVATE okay::engine)
add_test(NAME ik_core COMMAND test_ik_core)

add_executable(test_vs_nodes test_vs_nodes.cpp)
target_link_libraries(test_vs_nodes PRIVATE okay::engine)
add_test(NAME vs_nodes COMMAND test_vs_nodes)

add_executable(test_anim_clip_tools test_anim_clip_tools.cpp)
target_link_libraries(test_anim_clip_tools PRIVATE okay::engine)
add_test(NAME anim_clip_tools COMMAND test_anim_clip_tools)

add_executable(test_anim_core test_anim_core.cpp)
target_link_libraries(test_anim_core PRIVATE okay::engine)
add_test(NAME anim_core COMMAND test_anim_core)

add_executable(test_model_import_rig test_model_import_rig.cpp)
target_link_libraries(test_model_import_rig PRIVATE okay::engine)
add_test(NAME model_import_rig COMMAND test_model_import_rig)

add_executable(test_anim_sm_layers test_anim_sm_layers.cpp)
target_link_libraries(test_anim_sm_layers PRIVATE okay::engine)
add_test(NAME anim_sm_layers COMMAND test_anim_sm_layers)

# Robustness: AnimStateMachine layers survive `states` reallocating
add_executable(test_anim_sm_realloc test_anim_sm_realloc.cpp)
target_link_libraries(test_anim_sm_realloc PRIVATE okay::engine)
add_test(NAME anim_sm_realloc COMMAND test_anim_sm_realloc)

add_executable(test_foot_planting test_foot_planting.cpp)
target_link_libraries(test_foot_planting PRIVATE okay::engine)
add_test(NAME foot_planting COMMAND test_foot_planting)

add_executable(test_blend_shapes test_blend_shapes.cpp)
target_link_libraries(test_blend_shapes PRIVATE okay::engine)
add_test(NAME blend_shapes COMMAND test_blend_shapes)

add_executable(test_frustum_culling test_frustum_culling.cpp)
target_link_libraries(test_frustum_culling PRIVATE okay::engine)
add_test(NAME frustum_culling COMMAND test_frustum_culling)

add_executable(test_vs_pin_defaults test_vs_pin_defaults.cpp)
target_link_libraries(test_vs_pin_defaults PRIVATE okay::engine)
add_test(NAME vs_pin_defaults COMMAND test_vs_pin_defaults)

# C# / OkayScript: classes, generics, lambdas, coroutines, components.
add_executable(test_okayscript_csharp test_okayscript_csharp.cpp)
target_link_libraries(test_okayscript_csharp PRIVATE okay::engine)
add_test(NAME okayscript_csharp COMMAND test_okayscript_csharp)

# Built-in animations on imported rigs: humanoid bone matching (Mixamo / UE / Rigify).
add_executable(test_humanoid_retarget test_humanoid_retarget.cpp)
target_link_libraries(test_humanoid_retarget PRIVATE okay::engine)
add_test(NAME humanoid_retarget COMMAND test_humanoid_retarget)

add_executable(test_character_model_setup test_character_model_setup.cpp)
target_link_libraries(test_character_model_setup PRIVATE okay::engine)
add_test(NAME character_model_setup COMMAND test_character_model_setup)

add_executable(test_scene_defaults test_scene_defaults.cpp)
target_link_libraries(test_scene_defaults PRIVATE okay::engine)
add_test(NAME scene_defaults COMMAND test_scene_defaults)

add_executable(test_vs_wave4 test_vs_wave4.cpp)
target_link_libraries(test_vs_wave4 PRIVATE okay::engine)
add_test(NAME vs_wave4 COMMAND test_vs_wave4)

add_executable(test_ui_animation test_ui_animation.cpp)
target_link_libraries(test_ui_animation PRIVATE okay::engine)
add_test(NAME ui_animation COMMAND test_ui_animation)

add_executable(test_action_graph test_action_graph.cpp)
target_link_libraries(test_action_graph PRIVATE okay::engine)
add_test(NAME action_graph COMMAND test_action_graph)

add_executable(test_scene_cameras test_scene_cameras.cpp)
target_link_libraries(test_scene_cameras PRIVATE okay::engine)
add_test(NAME scene_cameras COMMAND test_scene_cameras)

add_executable(test_dedicated_server test_dedicated_server.cpp)
target_link_libraries(test_dedicated_server PRIVATE okay::engine)
add_test(NAME dedicated_server COMMAND test_dedicated_server)

add_executable(test_launcher_join test_launcher_join.cpp)
target_link_libraries(test_launcher_join PRIVATE okay::engine)
add_test(NAME launcher_join COMMAND test_launcher_join)

add_executable(test_net_authority test_net_authority.cpp)
target_link_libraries(test_net_authority PRIVATE okay::engine)
add_test(NAME net_authority COMMAND test_net_authority)

add_executable(test_match_results test_match_results.cpp)
target_link_libraries(test_match_results PRIVATE okay::engine)
add_test(NAME match_results COMMAND test_match_results)

add_executable(test_matchmaker test_matchmaker.cpp)
target_link_libraries(test_matchmaker PRIVATE okay::engine)
add_test(NAME matchmaker COMMAND test_matchmaker)

add_executable(test_ui_templates test_ui_templates.cpp)
target_link_libraries(test_ui_templates PRIVATE okay::engine)
add_test(NAME ui_templates COMMAND test_ui_templates)

add_executable(test_gltf_cubicspline test_gltf_cubicspline.cpp)
target_link_libraries(test_gltf_cubicspline PRIVATE okay::engine)
add_test(NAME gltf_cubicspline COMMAND test_gltf_cubicspline)

add_executable(test_crash_reports test_crash_reports.cpp)
target_include_directories(test_crash_reports PRIVATE ${CMAKE_SOURCE_DIR}/shared)
add_test(NAME crash_reports COMMAND test_crash_reports)

# The launcher's official game (shared/OfficialGame.hpp): header-only, so no engine link.
add_executable(test_official_game test_official_game.cpp)
target_include_directories(test_official_game PRIVATE ${CMAKE_SOURCE_DIR}/shared ${CMAKE_SOURCE_DIR}/engine/include)
add_test(NAME official_game COMMAND test_official_game)
# Publishing it from the launcher's Admin page (same header).
add_executable(test_official_publish test_official_publish.cpp)
target_include_directories(test_official_publish PRIVATE ${CMAKE_SOURCE_DIR}/shared ${CMAKE_SOURCE_DIR}/engine/include)
add_test(NAME official_publish COMMAND test_official_publish)
# ...and scripts/publish-official-game.sh, against a stub curl (no network).
if(UNIX)
    add_test(NAME publish_official_cleanup COMMAND bash ${CMAKE_CURRENT_SOURCE_DIR}/scripts/test_publish_official_cleanup.sh)
    add_test(NAME publish_supabase_checks COMMAND bash ${CMAKE_CURRENT_SOURCE_DIR}/scripts/test_publish_supabase_checks.sh)
endif()

# The launcher's chat JSON (Messages / Servers): header-only, no engine link.
add_executable(test_chat_json test_chat_json.cpp)
target_compile_features(test_chat_json PRIVATE cxx_std_17)
target_include_directories(test_chat_json PRIVATE ${CMAKE_SOURCE_DIR}/engine/include)
add_test(NAME chat_json COMMAND test_chat_json)
# ...and the friends / profile JSON (Friends.hpp).
add_executable(test_friends_json test_friends_json.cpp)
target_compile_features(test_friends_json PRIVATE cxx_std_17)
target_include_directories(test_friends_json PRIVATE ${CMAKE_SOURCE_DIR}/engine/include)
add_test(NAME friends_json COMMAND test_friends_json)

add_executable(test_tween test_tween.cpp)
target_link_libraries(test_tween PRIVATE okay::engine)
add_test(NAME tween COMMAND test_tween)

add_executable(test_editor_tabs test_editor_tabs.cpp)
target_include_directories(test_editor_tabs PRIVATE ${CMAKE_SOURCE_DIR}/editor)
add_test(NAME editor_tabs COMMAND test_editor_tabs)

add_executable(test_ui_tabs test_ui_tabs.cpp)
target_link_libraries(test_ui_tabs PRIVATE okay::engine)
add_test(NAME ui_tabs COMMAND test_ui_tabs)

add_executable(test_shooter25d test_shooter25d.cpp)
target_link_libraries(test_shooter25d PRIVATE okay::engine)
add_test(NAME shooter25d COMMAND test_shooter25d)

add_executable(test_script_semantics test_script_semantics.cpp)
target_link_libraries(test_script_semantics PRIVATE okay::engine)
add_test(NAME script_semantics COMMAND test_script_semantics)

add_executable(test_okayscript_next test_okayscript_next.cpp)
target_link_libraries(test_okayscript_next PRIVATE okay::engine)
add_test(NAME okayscript_next COMMAND test_okayscript_next)

add_executable(test_okayscript_next_csharp test_okayscript_next_csharp.cpp)
target_link_libraries(test_okayscript_next_csharp PRIVATE okay::engine)
add_test(NAME okayscript_next_csharp COMMAND test_okayscript_next_csharp)

add_executable(test_vs_acceptance test_vs_acceptance.cpp)
target_link_libraries(test_vs_acceptance PRIVATE okay::engine)
add_test(NAME vs_acceptance COMMAND test_vs_acceptance)

add_executable(test_vs_types test_vs_types.cpp)
target_link_libraries(test_vs_types PRIVATE okay::engine)
target_compile_definitions(test_vs_types PRIVATE OKAY_SOURCE_DIR="${PROJECT_SOURCE_DIR}")
add_test(NAME vs_types COMMAND test_vs_types)

add_executable(test_vs_debug test_vs_debug.cpp)
target_link_libraries(test_vs_debug PRIVATE okay::engine)
add_test(NAME vs_debug COMMAND test_vs_debug)

add_executable(test_game_fiber test_game_fiber.cpp)
target_include_directories(test_game_fiber PRIVATE ${CMAKE_SOURCE_DIR}/editor)
add_test(NAME game_fiber COMMAND test_game_fiber)

add_executable(test_edit_transform test_edit_transform.cpp)
target_link_libraries(test_edit_transform PRIVATE okay::engine)
add_test(NAME edit_transform COMMAND test_edit_transform)

add_executable(test_polymesh test_polymesh.cpp)
target_link_libraries(test_polymesh PRIVATE okay::engine)
add_test(NAME polymesh COMMAND test_polymesh)

add_executable(test_mesh_asset test_mesh_asset.cpp)
target_link_libraries(test_mesh_asset PRIVATE okay::engine)
add_test(NAME mesh_asset COMMAND test_mesh_asset)

add_executable(test_poly_ops test_poly_ops.cpp)
target_link_libraries(test_poly_ops PRIVATE okay::engine)
add_test(NAME poly_ops COMMAND test_poly_ops)

add_executable(test_locomotion test_locomotion.cpp)
target_link_libraries(test_locomotion PRIVATE okay::engine)
add_test(NAME locomotion COMMAND test_locomotion)

# QA regression tests for PR #250 (fail until the reported bugs are fixed)
add_executable(test_qa_pr250 test_qa_pr250.cpp)
target_link_libraries(test_qa_pr250 PRIVATE okay::engine)
add_test(NAME qa_pr250 COMMAND test_qa_pr250)

# A scene saved crouched / prone reloads in that stance (not stood up inside low cover)
add_executable(test_stance_save test_stance_save.cpp)
target_link_libraries(test_stance_save PRIVATE okay::engine)
add_test(NAME stance_save COMMAND test_stance_save)

# The relay is internet-facing: same-session forwarding, random slots, caps, fuzz
add_executable(test_relay_security test_relay_security.cpp)
target_link_libraries(test_relay_security PRIVATE okay::engine)
add_test(NAME relay_security COMMAND test_relay_security)

# Script file APIs stay inside the game's folders (no absolute paths, no "..")
add_executable(test_script_paths test_script_paths.cpp)
target_link_libraries(test_script_paths PRIVATE okay::engine)
add_test(NAME script_paths COMMAND test_script_paths)

# Golden images for the engine UI renderer, drawn on SDL's software renderer
# (needs SDL2; skipped quietly without it).
find_package(SDL2 QUIET)
if(SDL2_FOUND)
    if(TARGET SDL2::SDL2)
        set(OKAY_TEST_SDL_LIB SDL2::SDL2)
    else()
        set(OKAY_TEST_SDL_LIB ${SDL2_LIBRARIES})
    endif()
    add_executable(test_ui_renderer_golden test_ui_renderer_golden.cpp)
    target_link_libraries(test_ui_renderer_golden PRIVATE okay::engine ${OKAY_TEST_SDL_LIB})
    target_include_directories(test_ui_renderer_golden PRIVATE ${SDL2_INCLUDE_DIRS})
    target_compile_definitions(test_ui_renderer_golden PRIVATE OKAY_SOURCE_DIR="${PROJECT_SOURCE_DIR}")
    add_test(NAME ui_renderer_golden COMMAND test_ui_renderer_golden)
endif()
