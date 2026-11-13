# The sandbox the file-writing tests run in (temp folder as working and save folder; guard against writes into the source tree)
add_executable(test_sandbox test_sandbox.cpp)
target_link_libraries(test_sandbox PRIVATE okay::engine)
add_test(NAME sandbox COMMAND test_sandbox)

add_executable(test_math test_math.cpp)
target_link_libraries(test_math PRIVATE okay::engine)
add_test(NAME math COMMAND test_math)

add_executable(test_scene test_scene.cpp)
target_link_libraries(test_scene PRIVATE okay::engine)
add_test(NAME scene COMMAND test_scene)

# Scene::StructureVersion changes only when the hierarchy does (animation caches key on it)
add_executable(test_structure_version test_structure_version.cpp)
target_link_libraries(test_structure_version PRIVATE okay::engine)
add_test(NAME structure_version COMMAND test_structure_version)

# scripts/versions_index.py maintains the release feed's versions.json (release channels).
find_program(OKAY_PYTHON NAMES python3 python)
if(OKAY_PYTHON)
    add_test(NAME versions_index COMMAND ${OKAY_PYTHON} -I ${CMAKE_SOURCE_DIR}/scripts/versions_index.py --self-test)
endif()

add_executable(test_serialize test_serialize.cpp)
target_link_libraries(test_serialize PRIVATE okay::engine)
add_test(NAME serialize COMMAND test_serialize)

# Grid inventory: drag rotation/cancel, enforced weight, stack merge/split, Validate()
add_executable(test_grid_inventory_fixes test_grid_inventory_fixes.cpp)
target_link_libraries(test_grid_inventory_fixes PRIVATE okay::engine)
add_test(NAME grid_inventory_fixes COMMAND test_grid_inventory_fixes)

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

# Safe loading: bad / cut-short / newer documents never damage the live scene
add_executable(test_scene_safe_load test_scene_safe_load.cpp)
target_link_libraries(test_scene_safe_load PRIVATE okay::engine)
target_compile_definitions(test_scene_safe_load PRIVATE OKAY_SOURCE_DIR="${PROJECT_SOURCE_DIR}")
add_test(NAME scene_safe_load COMMAND test_scene_safe_load)

add_executable(test_remove test_remove.cpp)
target_link_libraries(test_remove PRIVATE okay::engine)
add_test(NAME remove_component COMMAND test_remove)

add_executable(test_audio test_audio.cpp)
target_link_libraries(test_audio PRIVATE okay::engine)
add_test(NAME audio COMMAND test_audio)

add_executable(test_audio_mixer test_audio_mixer.cpp)
target_link_libraries(test_audio_mixer PRIVATE okay::engine)
add_test(NAME audio_mixer COMMAND test_audio_mixer)

add_executable(test_spatial_audio test_spatial_audio.cpp)
target_link_libraries(test_spatial_audio PRIVATE okay::engine)
target_compile_definitions(test_spatial_audio PRIVATE OKAY_SOURCE_DIR="${PROJECT_SOURCE_DIR}")
add_test(NAME spatial_audio COMMAND test_spatial_audio)

add_executable(test_audio_formats test_audio_formats.cpp)
target_link_libraries(test_audio_formats PRIVATE okay::engine)
target_compile_definitions(test_audio_formats PRIVATE OKAY_SOURCE_DIR="${PROJECT_SOURCE_DIR}")
add_test(NAME audio_formats COMMAND test_audio_formats)

# Stereo clips and placement (balance for 2D, mono-then-pan for 3D).
add_executable(test_audio_stereo test_audio_stereo.cpp)
target_link_libraries(test_audio_stereo PRIVATE okay::engine)
add_test(NAME audio_stereo COMMAND test_audio_stereo)

# Streaming audio: streamed vs whole-file decode, seek, loop, memory bound, underruns, hostile files.
add_executable(test_audio_stream test_audio_stream.cpp)
target_link_libraries(test_audio_stream PRIVATE okay::engine)
target_compile_definitions(test_audio_stream PRIVATE OKAY_SOURCE_DIR="${PROJECT_SOURCE_DIR}")
add_test(NAME audio_stream COMMAND test_audio_stream)
# Own Ogg Vorbis decoder: SNR vs libvorbis references, Ogg framing, headers, truncation, bit-flip and noise fuzzing.
add_executable(test_vorbis_decoder test_vorbis_decoder.cpp)
target_link_libraries(test_vorbis_decoder PRIVATE okay::engine)
target_compile_definitions(test_vorbis_decoder PRIVATE OKAY_SOURCE_DIR="${PROJECT_SOURCE_DIR}")
add_test(NAME vorbis_decoder COMMAND test_vorbis_decoder)

# Own MP3 decoder: SNR vs ffmpeg references, gapless/tags/resync, truncation, bit-flip and noise fuzzing.
add_executable(test_mp3_decoder test_mp3_decoder.cpp)
target_link_libraries(test_mp3_decoder PRIVATE okay::engine)
target_compile_definitions(test_mp3_decoder PRIVATE OKAY_SOURCE_DIR="${PROJECT_SOURCE_DIR}")
add_test(NAME mp3_decoder COMMAND test_mp3_decoder)

add_executable(test_audio_music test_audio_music.cpp)
target_link_libraries(test_audio_music PRIVATE okay::engine)
add_test(NAME audio_music COMMAND test_audio_music)

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

# MeshCollider3D: real triangle-mesh collision (BVH): rays, overlaps, casts, bodies, edits, perf
add_executable(test_meshcollider3d test_meshcollider3d.cpp)
target_link_libraries(test_meshcollider3d PRIVATE okay::engine)
add_test(NAME meshcollider3d COMMAND test_meshcollider3d)

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
add_executable(test_scheduler_hardening test_scheduler_hardening.cpp)
target_link_libraries(test_scheduler_hardening PRIVATE okay::engine)
add_test(NAME scheduler_hardening COMMAND test_scheduler_hardening)
add_executable(test_scheduler_owner_clock test_scheduler_owner_clock.cpp)
target_link_libraries(test_scheduler_owner_clock PRIVATE okay::engine)
add_test(NAME scheduler_owner_clock COMMAND test_scheduler_owner_clock)
add_executable(test_scheduler_scale test_scheduler_scale.cpp)
target_link_libraries(test_scheduler_scale PRIVATE okay::engine)
add_test(NAME scheduler_scale COMMAND test_scheduler_scale)

add_executable(test_gameplay test_gameplay.cpp)
target_link_libraries(test_gameplay PRIVATE okay::engine)
add_test(NAME gameplay COMMAND test_gameplay)

add_executable(test_prefs test_prefs.cpp)
target_link_libraries(test_prefs PRIVATE okay::engine)
add_test(NAME prefs COMMAND test_prefs)

# Inventory core correctness: no dupes, no item loss, maxStack respected
add_executable(test_inventory_core test_inventory_core.cpp)
target_link_libraries(test_inventory_core PRIVATE okay::engine)
add_test(NAME inventory_core COMMAND test_inventory_core)

# Robustness: Prefs escaping/precision, TweenSequence cycles, Mat4::Inverse, UB-free hashes
add_executable(test_prefs_misc test_prefs_misc.cpp)
target_link_libraries(test_prefs_misc PRIVATE okay::engine)
add_test(NAME prefs_misc COMMAND test_prefs_misc)

# Inventory wiring: nearest chest/bench opens, who the player is, slots = capacity, craft buttons not saved
add_executable(test_inventory_wiring test_inventory_wiring.cpp)
target_link_libraries(test_inventory_wiring PRIVATE okay::engine)
add_test(NAME inventory_wiring COMMAND test_inventory_wiring)

# InventoryOps + InventoryInput: the one path the player and the editor Play view share
add_executable(test_inventory_ops test_inventory_ops.cpp)
target_link_libraries(test_inventory_ops PRIVATE okay::engine)
add_test(NAME inventory_ops COMMAND test_inventory_ops)

# Item definitions: .okayitems database, legacy names, per-instance data, def-driven stack/icon/rarity/use
add_executable(test_item_database test_item_database.cpp)
target_link_libraries(test_item_database PRIVATE okay::engine)
add_test(NAME item_database COMMAND test_item_database)

# Item, loot and recipe files: round trip, plain-English missing-item checks, loot roll distribution
add_executable(test_item_loot_recipes test_item_loot_recipes.cpp)
target_link_libraries(test_item_loot_recipes PRIVATE okay::engine)
add_test(NAME item_loot_recipes COMMAND test_item_loot_recipes)

# Inventory gameplay: pickups/drops, using the held item, encumbrance, loot tables
add_executable(test_inventory_gameplay test_inventory_gameplay.cpp)
target_link_libraries(test_inventory_gameplay PRIVATE okay::engine)
add_test(NAME inventory_gameplay COMMAND test_inventory_gameplay)

# Inventories across scenes + save/load, one crafting system (books, craft time, benches, unlocks), def-based sorting
add_executable(test_inventory_persist_craft test_inventory_persist_craft.cpp)
target_link_libraries(test_inventory_persist_craft PRIVATE okay::engine)
add_test(NAME inventory_persist_craft COMMAND test_inventory_persist_craft)
# Equipment: drag to equip, sockets, armor/defence, worn backpacks, save/load and scene carry
add_executable(test_inventory_equipment test_inventory_equipment.cpp)
target_link_libraries(test_inventory_equipment PRIVATE okay::engine)
add_test(NAME inventory_equipment COMMAND test_inventory_equipment)
# Equipment / pickups with Procedural Object Holding: main hand in the hands, hold-in-hands pickups
add_executable(test_inventory_holding test_inventory_holding.cpp)
target_link_libraries(test_inventory_holding PRIVATE okay::engine)
add_test(NAME inventory_holding COMMAND test_inventory_holding)
# Inventory scripting: inv_* calls, events to scripts / visual scripts / C++, ActionList give / take / has_item
add_executable(test_inventory_script test_inventory_script.cpp)
target_link_libraries(test_inventory_script PRIVATE okay::engine)
add_test(NAME inventory_script COMMAND test_inventory_script)

add_executable(test_pathfinding test_pathfinding.cpp)
target_link_libraries(test_pathfinding PRIVATE okay::engine)
add_test(NAME pathfinding COMMAND test_pathfinding)

add_executable(test_easing test_easing.cpp)
target_link_libraries(test_easing PRIVATE okay::engine)
add_test(NAME easing COMMAND test_easing)
add_executable(test_spring test_spring.cpp)
target_link_libraries(test_spring PRIVATE okay::engine)
add_test(NAME spring COMMAND test_spring)

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
add_executable(test_bitmap_font test_bitmap_font.cpp)
target_link_libraries(test_bitmap_font PRIVATE okay::engine)
add_test(NAME bitmap_font COMMAND test_bitmap_font)

add_executable(test_ttf test_ttf.cpp)
target_link_libraries(test_ttf PRIVATE okay::engine)
add_test(NAME ttf COMMAND test_ttf)

# Our own TrueType reader / rasterizer: synthetic fonts, DejaVu references, fuzzed files.
add_executable(test_ttf_rasterizer test_ttf_rasterizer.cpp)
target_link_libraries(test_ttf_rasterizer PRIVATE okay::engine)
target_compile_definitions(test_ttf_rasterizer PRIVATE OKAY_SOURCE_DIR="${PROJECT_SOURCE_DIR}")
add_test(NAME ttf_rasterizer COMMAND test_ttf_rasterizer)
set_tests_properties(ttf_rasterizer PROPERTIES TIMEOUT 120)

add_executable(test_block_builder test_block_builder.cpp)
target_link_libraries(test_block_builder PRIVATE okay::engine)
add_test(NAME block_builder COMMAND test_block_builder)

add_executable(test_structure_builder test_structure_builder.cpp)
target_link_libraries(test_structure_builder PRIVATE okay::engine)
add_test(NAME structure_builder COMMAND test_structure_builder)

# Builds are paid in full or not at all; a full-bag demolish refund drops a pickup
add_executable(test_inventory_build_charge test_inventory_build_charge.cpp)
target_link_libraries(test_inventory_build_charge PRIVATE okay::engine)
add_test(NAME inventory_build_charge COMMAND test_inventory_build_charge)

add_executable(test_builder_mode test_builder_mode.cpp)
target_link_libraries(test_builder_mode PRIVATE okay::engine)
add_test(NAME builder_mode COMMAND test_builder_mode)

add_executable(test_build_mode test_build_mode.cpp)
target_link_libraries(test_build_mode PRIVATE okay::engine)
add_test(NAME build_mode COMMAND test_build_mode)

# Generalised I/O links on a Logic Source: named outputs, inputs, delay, once, save/load, prefab overrides
add_executable(test_io_links test_io_links.cpp)
target_link_libraries(test_io_links PRIVATE okay::engine)
add_test(NAME io_links COMMAND test_io_links)

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

add_executable(test_profiler_export test_profiler_export.cpp)
target_link_libraries(test_profiler_export PRIVATE okay::engine)
add_test(NAME profiler_export COMMAND test_profiler_export)

add_executable(test_log test_log.cpp)
target_link_libraries(test_log PRIVATE okay::engine)
add_test(NAME log COMMAND test_log)

# Core wave 6, 1.10: $OKAY_LOG vs Configure, log file cap / flush / failing disk, sink recursion, profiler bounds, threads and trace validity
add_executable(test_core_log_profiler test_core_log_profiler.cpp)
target_link_libraries(test_core_log_profiler PRIVATE okay::engine)
add_test(NAME core_log_profiler COMMAND test_core_log_profiler)

# Profiling & debugging data layers (okay/Debug): frame history, GPU/render stats, memory, frame debugger, console, overlay
add_executable(test_debug_frame_history test_debug_frame_history.cpp)
target_link_libraries(test_debug_frame_history PRIVATE okay::engine)
add_test(NAME debug_frame_history COMMAND test_debug_frame_history)

add_executable(test_debug_gpu_stats test_debug_gpu_stats.cpp)
target_link_libraries(test_debug_gpu_stats PRIVATE okay::engine)
add_test(NAME debug_gpu_stats COMMAND test_debug_gpu_stats)

add_executable(test_debug_memory test_debug_memory.cpp)
target_link_libraries(test_debug_memory PRIVATE okay::engine)
add_test(NAME debug_memory COMMAND test_debug_memory)

# The one test that turns the opt-in operator new/delete hooks on (OKAY_ENABLE_MEMORY_HOOKS, defined in the file itself)
add_executable(test_debug_memory_hooks test_debug_memory_hooks.cpp)
target_link_libraries(test_debug_memory_hooks PRIVATE okay::engine)
add_test(NAME debug_memory_hooks COMMAND test_debug_memory_hooks)

add_executable(test_debug_draw_calls test_debug_draw_calls.cpp)
target_link_libraries(test_debug_draw_calls PRIVATE okay::engine)
add_test(NAME debug_draw_calls COMMAND test_debug_draw_calls)

add_executable(test_debug_console test_debug_console.cpp)
target_link_libraries(test_debug_console PRIVATE okay::engine)
add_test(NAME debug_console COMMAND test_debug_console)

add_executable(test_diagnostics test_diagnostics.cpp)
target_link_libraries(test_diagnostics PRIVATE okay::engine)
add_test(NAME diagnostics COMMAND test_diagnostics)

add_executable(test_debug_overlay test_debug_overlay.cpp)
target_link_libraries(test_debug_overlay PRIVATE okay::engine)
add_test(NAME debug_overlay COMMAND test_debug_overlay)

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

add_executable(test_stop_settle test_stop_settle.cpp)
target_link_libraries(test_stop_settle PRIVATE okay::engine)
add_test(NAME stop_settle COMMAND test_stop_settle)

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

add_executable(test_net_guard test_net_guard.cpp)
target_include_directories(test_net_guard PRIVATE ${CMAKE_SOURCE_DIR}/tests ${CMAKE_SOURCE_DIR}/launcher)
add_test(NAME net_guard COMMAND test_net_guard)

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


# Old scenes naming the removed Lua / Mono C# backends still load and say "no longer supported".
add_executable(test_script_removed_langs test_script_removed_langs.cpp)
target_link_libraries(test_script_removed_langs PRIVATE okay::engine)
add_test(NAME script_removed_langs COMMAND test_script_removed_langs)

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

# Remote players' gait: proxies get the speed they really move at, one anim writer
add_executable(test_net_proxy_gait test_net_proxy_gait.cpp)
target_link_libraries(test_net_proxy_gait PRIVATE okay::engine)
add_test(NAME net_proxy_gait COMMAND test_net_proxy_gait)

add_executable(test_net_anim_actions test_net_anim_actions.cpp)
target_link_libraries(test_net_anim_actions PRIVATE okay::engine)
add_test(NAME net_anim_actions COMMAND test_net_anim_actions)

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

add_executable(test_foliage test_foliage.cpp)
target_link_libraries(test_foliage PRIVATE okay::engine)
add_test(NAME foliage COMMAND test_foliage)

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

# Undo/redo and recovery: command based undo stack (groups, merge, caps, dirty tracking),
# autosave + crash recovery, rolling file backups
add_executable(test_undo test_undo.cpp)
target_link_libraries(test_undo PRIVATE okay::engine)
add_test(NAME undo COMMAND test_undo)

add_executable(test_recovery test_recovery.cpp)
target_link_libraries(test_recovery PRIVATE okay::engine)
add_test(NAME recovery COMMAND test_recovery)

add_executable(test_backup test_backup.cpp)
target_link_libraries(test_backup PRIVATE okay::engine)
add_test(NAME backup COMMAND test_backup)

add_executable(test_animator test_animator.cpp)
target_link_libraries(test_animator PRIVATE okay::engine)
add_test(NAME animator COMMAND test_animator)

add_executable(test_dataasset test_dataasset.cpp)
target_link_libraries(test_dataasset PRIVATE okay::engine)
add_test(NAME dataasset COMMAND test_dataasset)

# Core wave 6, 1.14: DataAsset BOM / numbers / typed getters / safe Invalidate, DataPack damage and size cases
add_executable(test_core_data test_core_data.cpp)
target_link_libraries(test_core_data PRIVATE okay::engine)
add_test(NAME core_data COMMAND test_core_data)

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

add_executable(test_runtime_ui test_runtime_ui.cpp)
target_link_libraries(test_runtime_ui PRIVATE okay::engine)
target_compile_definitions(test_runtime_ui PRIVATE OKAY_TEST_OUTPUT_DIR="${CMAKE_CURRENT_BINARY_DIR}")
add_test(NAME runtime_ui COMMAND test_runtime_ui)

# File > Examples: the UI Gallery builds and saves itself into the project (ImGui-free part of the editor).
add_executable(test_example_scenes test_example_scenes.cpp ${PROJECT_SOURCE_DIR}/editor/EditorState.cpp ${PROJECT_SOURCE_DIR}/shared/FileDialog.cpp ${PROJECT_SOURCE_DIR}/editor/ExampleScenes.cpp)
target_include_directories(test_example_scenes PRIVATE ${PROJECT_SOURCE_DIR}/editor)
target_compile_definitions(test_example_scenes PRIVATE OKAY_TEST_OUTPUT_DIR="${CMAKE_CURRENT_BINARY_DIR}")
target_link_libraries(test_example_scenes PRIVATE okay::engine)
add_test(NAME example_scenes COMMAND test_example_scenes)

add_executable(test_ui_layering test_ui_layering.cpp)
target_link_libraries(test_ui_layering PRIVATE okay::engine)
add_test(NAME ui_layering COMMAND test_ui_layering)

add_executable(test_ui_layer_table test_ui_layer_table.cpp)
target_link_libraries(test_ui_layer_table PRIVATE okay::engine)
target_compile_definitions(test_ui_layer_table PRIVATE OKAY_SOURCE_DIR="${PROJECT_SOURCE_DIR}")
add_test(NAME ui_layer_table COMMAND test_ui_layer_table)

add_executable(test_ui_renderer_options test_ui_renderer_options.cpp)
target_link_libraries(test_ui_renderer_options PRIVATE okay::engine)
add_test(NAME ui_renderer_options COMMAND test_ui_renderer_options)

add_executable(test_ui_render_bench test_ui_render_bench.cpp)
target_link_libraries(test_ui_render_bench PRIVATE okay::engine)
target_compile_definitions(test_ui_render_bench PRIVATE OKAY_TEST_OUTPUT_DIR="${CMAKE_CURRENT_BINARY_DIR}")
add_test(NAME ui_render_bench COMMAND test_ui_render_bench)

add_executable(test_ui_layout_containers test_ui_layout_containers.cpp)
target_link_libraries(test_ui_layout_containers PRIVATE okay::engine)
add_test(NAME ui_layout_containers COMMAND test_ui_layout_containers)

# UIFrame conversion and mirroring; also drives the editor's (ImGui-free) undo stack.
add_executable(test_ui_frame test_ui_frame.cpp ${PROJECT_SOURCE_DIR}/editor/EditorState.cpp ${PROJECT_SOURCE_DIR}/shared/FileDialog.cpp)
target_include_directories(test_ui_frame PRIVATE ${PROJECT_SOURCE_DIR}/editor)
target_link_libraries(test_ui_frame PRIVATE okay::engine)
add_test(NAME ui_frame COMMAND test_ui_frame)

# The editor's hooks into the core systems (Play on GameLoop, Pause / Step, Duplicate, tag lists); links the ImGui-free EditorState.
add_executable(test_editor_hooks test_editor_hooks.cpp ${PROJECT_SOURCE_DIR}/editor/EditorState.cpp ${PROJECT_SOURCE_DIR}/editor/ProjectUpgrade.cpp ${PROJECT_SOURCE_DIR}/shared/FileDialog.cpp)   # EditorState cancels queued built-in dialogs on scene rebuilds (#732)
target_include_directories(test_editor_hooks PRIVATE ${PROJECT_SOURCE_DIR}/editor)
target_link_libraries(test_editor_hooks PRIVATE okay::engine)
add_test(NAME editor_hooks COMMAND test_editor_hooks)

# The Hierarchy and Inspector logic (editor/InspectorModel, EditUndo; ImGui-free): search filter, drag-and-drop reparenting, mixed values, copy / paste / reset, prefab changes, one undo step per edit.
add_executable(test_inspector_model test_inspector_model.cpp ${PROJECT_SOURCE_DIR}/editor/InspectorModel.cpp ${PROJECT_SOURCE_DIR}/editor/EditorState.cpp ${PROJECT_SOURCE_DIR}/shared/FileDialog.cpp)
target_include_directories(test_inspector_model PRIVATE ${PROJECT_SOURCE_DIR}/editor)
target_link_libraries(test_inspector_model PRIVATE okay::engine)
add_test(NAME inspector_model COMMAND test_inspector_model)

# The editor's undo (EditorState + SceneUndo.hpp on okay::UndoStack): every kind of edit does / undoes / redoes to the same scene text, merging, delete restores GUIDs, limits, new scene / Play, saved tracking.
add_executable(test_editor_undo test_editor_undo.cpp ${PROJECT_SOURCE_DIR}/editor/EditorState.cpp ${PROJECT_SOURCE_DIR}/shared/FileDialog.cpp)
target_include_directories(test_editor_undo PRIVATE ${PROJECT_SOURCE_DIR}/editor)
target_link_libraries(test_editor_undo PRIVATE okay::engine)
add_test(NAME editor_undo COMMAND test_editor_undo)

# Prefab mode (EditorState): open a prefab on its own, edit, save, go back; nested levels, per-level undo, instance refresh, Play refused.
add_executable(test_prefab_mode test_prefab_mode.cpp ${PROJECT_SOURCE_DIR}/editor/EditorState.cpp ${PROJECT_SOURCE_DIR}/shared/FileDialog.cpp)
target_include_directories(test_prefab_mode PRIVATE ${PROJECT_SOURCE_DIR}/editor)
target_link_libraries(test_prefab_mode PRIVATE okay::engine)
add_test(NAME prefab_mode COMMAND test_prefab_mode)

# The editor's prefab actions (editor/PrefabEdit; ImGui-free): override detection, per-field and per-component Revert / Apply, Apply updating the
# other instances, nested prefabs and variants, objects added to or lost from an instance, Create Variant, and undo / redo that moves the prefab files too.
add_executable(test_prefab_editor test_prefab_editor.cpp ${PROJECT_SOURCE_DIR}/editor/PrefabEdit.cpp ${PROJECT_SOURCE_DIR}/editor/InspectorModel.cpp ${PROJECT_SOURCE_DIR}/editor/EditorState.cpp ${PROJECT_SOURCE_DIR}/shared/FileDialog.cpp)
target_include_directories(test_prefab_editor PRIVATE ${PROJECT_SOURCE_DIR}/editor)
target_link_libraries(test_prefab_editor PRIVATE okay::engine)
add_test(NAME prefab_editor COMMAND test_prefab_editor)

# Save, load and unsaved changes in the editor (ImGui-free EditorState): atomic saves, all-or-nothing Open and New Project, Play edits never saved.
add_executable(test_project_save test_project_save.cpp ${PROJECT_SOURCE_DIR}/editor/EditorState.cpp ${PROJECT_SOURCE_DIR}/shared/FileDialog.cpp)
target_link_libraries(test_project_save PRIVATE okay::engine)
add_test(NAME project_save COMMAND test_project_save)

# The project's own state (project.okayproj, build settings, profiles, tags, layers): every field set, written, read back; old files load.
# The build pipeline's pure logic (okay/Core/BuildPipeline.hpp): layout planner, data-folder lookup, dependency walk,
# build manifest, report, zip, and the Build Settings text. The end-to-end export check is tests/export/verify_layout.sh.
add_executable(test_build_pipeline test_build_pipeline.cpp)
target_link_libraries(test_build_pipeline PRIVATE okay::engine)
add_test(NAME build_pipeline COMMAND test_build_pipeline)

add_executable(test_project_state test_project_state.cpp)
target_link_libraries(test_project_state PRIVATE okay::engine)
add_test(NAME project_state COMMAND test_project_state)

# Upgrading a project to the editor's version (editor/ProjectUpgrade.*, ImGui-free): backup, check and rewrite of scenes and prefabs, stamp, restore, failure injection at every step, and the gate that asks before anything is touched.
add_executable(test_project_upgrade test_project_upgrade.cpp ${PROJECT_SOURCE_DIR}/editor/ProjectUpgrade.cpp)
target_include_directories(test_project_upgrade PRIVATE ${PROJECT_SOURCE_DIR}/editor)
target_link_libraries(test_project_upgrade PRIVATE okay::engine)
add_test(NAME project_upgrade COMMAND test_project_upgrade)

# The Project panel's logic (editor/ProjectBrowser.*, ImGui-free): folder model, safe create / rename / move / copy / delete with undo, references rewritten, clipboard, failure injection at every file step.
add_executable(test_project_panel test_project_panel.cpp ${PROJECT_SOURCE_DIR}/editor/ProjectBrowser.cpp)
target_include_directories(test_project_panel PRIVATE ${PROJECT_SOURCE_DIR}/editor)
target_link_libraries(test_project_panel PRIVATE okay::engine)
add_test(NAME project_panel COMMAND test_project_panel)

# The Project panel on the asset database and the thumbnail cache: listing / search / filters / used-by / info from the database, incremental refresh (5,000 assets timed), thumbnails by GUID + hash (hit, miss, invalidate, disk, frame budget), rename / move / delete keep GUIDs and badge broken references.
add_executable(test_project_panel_db test_project_panel_db.cpp ${PROJECT_SOURCE_DIR}/editor/ProjectBrowser.cpp)
target_include_directories(test_project_panel_db PRIVATE ${PROJECT_SOURCE_DIR}/editor)
target_link_libraries(test_project_panel_db PRIVATE okay::engine)
add_test(NAME project_panel_db COMMAND test_project_panel_db)

# Import settings that really change behaviour: texture Filter / Wrap / Mipmaps (software sampler + sidecar read by the OpenGL path), model Normals / Smoothing angle / Import animations.
add_executable(test_import_settings_real test_import_settings_real.cpp)
target_link_libraries(test_import_settings_real PRIVATE okay::engine)
add_test(NAME import_settings_real COMMAND test_import_settings_real)

# Save coverage: every component type the engine has survives a scene save and load (and the Enabled switch with it); two colliders / audio sources on one object both do.
add_executable(test_save_coverage test_save_coverage.cpp)
target_link_libraries(test_save_coverage PRIVATE okay::engine)
add_test(NAME save_coverage COMMAND test_save_coverage)

# Two of every serializable component, plus prefab apply, undo deltas, diff/merge, copy and export.
add_executable(test_duplicate_components test_duplicate_components.cpp)
target_link_libraries(test_duplicate_components PRIVATE okay::engine)
target_compile_definitions(test_duplicate_components PRIVATE OKAY_SOURCE_DIR="${PROJECT_SOURCE_DIR}")
add_test(NAME duplicate_components COMMAND test_duplicate_components)

add_executable(test_ui_overlays test_ui_overlays.cpp)
target_link_libraries(test_ui_overlays PRIVATE okay::engine)
add_test(NAME ui_overlays COMMAND test_ui_overlays)

add_executable(test_ui_nav_touch test_ui_nav_touch.cpp)
target_link_libraries(test_ui_nav_touch PRIVATE okay::engine)
add_test(NAME ui_nav_touch COMMAND test_ui_nav_touch)

add_executable(test_jump test_jump.cpp)
target_link_libraries(test_jump PRIVATE okay::engine)
add_test(NAME jump COMMAND test_jump)

add_executable(test_fp_hand test_fp_hand.cpp)
target_link_libraries(test_fp_hand PRIVATE okay::engine)
add_test(NAME fp_hand COMMAND test_fp_hand)

add_executable(test_run_feel test_run_feel.cpp)
target_link_libraries(test_run_feel PRIVATE okay::engine)
add_test(NAME run_feel COMMAND test_run_feel)

add_executable(test_view_ground test_view_ground.cpp)
target_link_libraries(test_view_ground PRIVATE okay::engine)
add_test(NAME view_ground COMMAND test_view_ground)

add_executable(test_view_upgrade test_view_upgrade.cpp)
target_link_libraries(test_view_upgrade PRIVATE okay::engine)
add_test(NAME view_upgrade COMMAND test_view_upgrade)

add_executable(test_player_controller test_player_controller.cpp)
target_link_libraries(test_player_controller PRIVATE okay::engine)
add_test(NAME player_controller COMMAND test_player_controller)

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

add_executable(test_volumes test_volumes.cpp)
target_link_libraries(test_volumes PRIVATE okay::engine)
add_test(NAME volumes COMMAND test_volumes)

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

add_executable(test_character_feel test_character_feel.cpp)
target_link_libraries(test_character_feel PRIVATE okay::engine)
add_test(NAME character_feel COMMAND test_character_feel)

add_executable(test_animator_edit test_animator_edit.cpp)
target_link_libraries(test_animator_edit PRIVATE okay::engine)
add_test(NAME animator_edit COMMAND test_animator_edit)

add_executable(test_pause_menu test_pause_menu.cpp)
target_link_libraries(test_pause_menu PRIVATE okay::engine)
add_test(NAME pause_menu COMMAND test_pause_menu)

add_executable(test_ui_input_guard test_ui_input_guard.cpp)
target_link_libraries(test_ui_input_guard PRIVATE okay::engine)
add_test(NAME ui_input_guard COMMAND test_ui_input_guard)

# Display settings model: window modes, resolutions, vsync, quality preset (no window needed)
add_executable(test_display_settings test_display_settings.cpp)
target_link_libraries(test_display_settings PRIVATE okay::engine)
add_test(NAME display_settings COMMAND test_display_settings)

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

add_executable(test_physics2d_casts test_physics2d_casts.cpp)
target_link_libraries(test_physics2d_casts PRIVATE okay::engine)
target_compile_definitions(test_physics2d_casts PRIVATE OKAY_SOURCE_DIR="${PROJECT_SOURCE_DIR}")
add_test(NAME physics2d_casts COMMAND test_physics2d_casts)

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

# Clip editing tools behind the Animation window: per-key easing (file + scene
# round-trips, older files), retiming, insert / remove time, mirror, pose values.
add_executable(test_anim_edit test_anim_edit.cpp)
target_link_libraries(test_anim_edit PRIVATE okay::engine)
add_test(NAME anim_edit COMMAND test_anim_edit)

add_executable(test_obj_import test_obj_import.cpp)
target_link_libraries(test_obj_import PRIVATE okay::engine)
add_test(NAME obj_import COMMAND test_obj_import)

add_executable(test_gltf_import test_gltf_import.cpp)
target_link_libraries(test_gltf_import PRIVATE okay::engine)
add_test(NAME gltf_import COMMAND test_gltf_import)

add_executable(test_glb_container test_glb_container.cpp)
target_link_libraries(test_glb_container PRIVATE okay::engine)
add_test(NAME glb_container COMMAND test_glb_container)

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

add_executable(test_ik_state_weight test_ik_state_weight.cpp)
target_link_libraries(test_ik_state_weight PRIVATE okay::engine)
add_test(NAME ik_state_weight COMMAND test_ik_state_weight)

# Cutscene sequences: the .okayseq text, evaluation, the SequencePlayer's events and actions
add_executable(test_sequence test_sequence.cpp)
target_link_libraries(test_sequence PRIVATE okay::engine)
target_compile_definitions(test_sequence PRIVATE OKAY_SOURCE_DIR="${PROJECT_SOURCE_DIR}")
add_test(NAME sequence COMMAND test_sequence)

# Localization: .okayloc / CSV round trips, fallback, plurals, settings, localized widgets, scripts, fonts
add_executable(test_localization test_localization.cpp)
target_link_libraries(test_localization PRIVATE okay::engine)
add_test(NAME localization COMMAND test_localization)

# Localization tools: translation grid, CSV / JSON, checker, key scanner, previews, font coverage
add_executable(test_loc_tools test_loc_tools.cpp)
target_link_libraries(test_loc_tools PRIVATE okay::engine)
add_test(NAME loc_tools COMMAND test_loc_tools)

# The Localization window's path onto those tools (editor/LocalizationTools, ImGui-free): a missing
# key, a placeholder mismatch and a pseudo-locale preview, the same functions DrawLocalizationWindow calls.
add_executable(test_loc_window test_loc_window.cpp ${PROJECT_SOURCE_DIR}/editor/LocalizationTools.cpp)
target_include_directories(test_loc_window PRIVATE ${PROJECT_SOURCE_DIR}/editor)
target_link_libraries(test_loc_window PRIVATE okay::engine)
add_test(NAME loc_window COMMAND test_loc_window)

# Localization runtime, second pass: keys, language matching, plural rules, number/select placeholders, CSV
add_executable(test_loc_runtime test_loc_runtime.cpp)
target_link_libraries(test_loc_runtime PRIVATE okay::engine)
add_test(NAME loc_runtime COMMAND test_loc_runtime)

# Accessibility: colour vision, safe palette, subtitles and captions, settings
add_executable(test_accessibility test_accessibility.cpp)
target_link_libraries(test_accessibility PRIVATE okay::engine)
add_test(NAME accessibility COMMAND test_accessibility)

# Focus graph (gamepad / keyboard UI navigation), the scene adapter, and the controls-screen model
add_executable(test_focus_remap test_focus_remap.cpp)
target_link_libraries(test_focus_remap PRIVATE okay::engine)
add_test(NAME focus_remap COMMAND test_focus_remap)

add_executable(test_ik_core test_ik_core.cpp)
target_link_libraries(test_ik_core PRIVATE okay::engine)
add_test(NAME ik_core COMMAND test_ik_core)

# IK Target, Aim Offset (recoil), and Pose Capture.
add_executable(test_anim_ik_components test_anim_ik_components.cpp)
target_link_libraries(test_anim_ik_components PRIVATE okay::engine)
add_test(NAME anim_ik_components COMMAND test_anim_ik_components)

# Animator getters, layer weight, root motion, per-kind IK, and the matching Engine API nodes.
add_executable(test_anim_ik_tools test_anim_ik_tools.cpp)
target_link_libraries(test_anim_ik_tools PRIVATE okay::engine)
add_test(NAME anim_ik_tools COMMAND test_anim_ik_tools)

# Look-At IK shares, clamp and eyes; Limb IK stretch.
add_executable(test_ik_look_stretch test_ik_look_stretch.cpp)
target_link_libraries(test_ik_look_stretch PRIVATE okay::engine)
add_test(NAME ik_look_stretch COMMAND test_ik_look_stretch)

# Foot IK toe/heel probes on a step, Look-At IK distance fade, Full Body IK stretch.
add_executable(test_ik_probes test_ik_probes.cpp)
target_link_libraries(test_ik_probes PRIVATE okay::engine)
add_test(NAME ik_probes COMMAND test_ik_probes)

# Look-At IK on imported rigs: the head's forward axis read off the rest pose.
add_executable(test_ik_rigs test_ik_rigs.cpp)
target_link_libraries(test_ik_rigs PRIVATE okay::engine)
target_compile_definitions(test_ik_rigs PRIVATE OKAY_SOURCE_DIR="${PROJECT_SOURCE_DIR}")
add_test(NAME ik_rigs COMMAND test_ik_rigs)

add_executable(test_vs_nodes test_vs_nodes.cpp)
target_link_libraries(test_vs_nodes PRIVATE okay::engine)
add_test(NAME vs_nodes COMMAND test_vs_nodes)
add_executable(test_vs_inventory test_vs_inventory.cpp)
target_link_libraries(test_vs_inventory PRIVATE okay::engine)
target_compile_definitions(test_vs_inventory PRIVATE OKAY_SOURCE_DIR="${PROJECT_SOURCE_DIR}")
add_test(NAME vs_inventory COMMAND test_vs_inventory)

# The Visual Script editor's graph model (save / load / paste), without drawing:
# it needs the Dear ImGui headers the editor header includes, nothing linked.
# With the editor in the build those come from its ImGui (nothing fetched
# twice); only a tests-only configure fetches the same OKAY_IMGUI_TAG itself.
# Offline, point -DFETCHCONTENT_SOURCE_DIR_IMGUI=<a local imgui checkout> at one.
if(TARGET okay_imgui)
    set(OKAY_VS_IMGUI_INCLUDE $<TARGET_PROPERTY:okay_imgui,INTERFACE_INCLUDE_DIRECTORIES>)
else()
    include(FetchContent)
    FetchContent_Declare(imgui GIT_REPOSITORY https://github.com/ocornut/imgui.git GIT_TAG ${OKAY_IMGUI_TAG})
    FetchContent_MakeAvailable(imgui)
    set(OKAY_VS_IMGUI_INCLUDE ${imgui_SOURCE_DIR})
endif()
add_executable(test_vs_editor_model test_vs_editor_model.cpp)
target_include_directories(test_vs_editor_model PRIVATE ${CMAKE_SOURCE_DIR}/editor ${OKAY_VS_IMGUI_INCLUDE})
target_link_libraries(test_vs_editor_model PRIVATE okay::engine)
target_compile_definitions(test_vs_editor_model PRIVATE OKAY_SOURCE_DIR="${PROJECT_SOURCE_DIR}")
add_test(NAME vs_editor_model COMMAND test_vs_editor_model)

add_executable(test_vs_names test_vs_names.cpp)
target_include_directories(test_vs_names PRIVATE ${CMAKE_SOURCE_DIR}/editor ${OKAY_VS_IMGUI_INCLUDE})
target_link_libraries(test_vs_names PRIVATE okay::engine)
add_test(NAME vs_names COMMAND test_vs_names)

add_executable(test_vs_palette_categories test_vs_palette_categories.cpp)
target_include_directories(test_vs_palette_categories PRIVATE ${CMAKE_SOURCE_DIR}/editor ${OKAY_VS_IMGUI_INCLUDE})
target_link_libraries(test_vs_palette_categories PRIVATE okay::engine)
target_compile_definitions(test_vs_palette_categories PRIVATE OKAY_SOURCE_DIR="${PROJECT_SOURCE_DIR}")
add_test(NAME vs_palette_categories COMMAND test_vs_palette_categories)

add_executable(test_vs_palette_levels test_vs_palette_levels.cpp)
target_include_directories(test_vs_palette_levels PRIVATE ${CMAKE_SOURCE_DIR}/editor ${OKAY_VS_IMGUI_INCLUDE})
target_link_libraries(test_vs_palette_levels PRIVATE okay::engine)
add_test(NAME vs_palette_levels COMMAND test_vs_palette_levels)

add_executable(test_vs_templates test_vs_templates.cpp)
target_include_directories(test_vs_templates PRIVATE ${CMAKE_SOURCE_DIR}/editor ${OKAY_VS_IMGUI_INCLUDE})
target_link_libraries(test_vs_templates PRIVATE okay::engine)
target_compile_definitions(test_vs_templates PRIVATE OKAY_SOURCE_DIR="${PROJECT_SOURCE_DIR}")
add_test(NAME vs_templates COMMAND test_vs_templates)

add_executable(test_vs_flow_behaviour test_vs_flow_behaviour.cpp)
target_include_directories(test_vs_flow_behaviour PRIVATE ${CMAKE_SOURCE_DIR}/editor ${OKAY_VS_IMGUI_INCLUDE})
target_link_libraries(test_vs_flow_behaviour PRIVATE okay::engine)
target_compile_definitions(test_vs_flow_behaviour PRIVATE OKAY_SOURCE_DIR="${PROJECT_SOURCE_DIR}")
add_test(NAME vs_flow_behaviour COMMAND test_vs_flow_behaviour)

add_executable(test_vs_node_docs test_vs_node_docs.cpp)
target_include_directories(test_vs_node_docs PRIVATE ${CMAKE_SOURCE_DIR}/editor ${OKAY_VS_IMGUI_INCLUDE})
target_link_libraries(test_vs_node_docs PRIVATE okay::engine)
add_test(NAME vs_node_docs COMMAND test_vs_node_docs)

add_executable(test_vs_clarity test_vs_clarity.cpp)
target_include_directories(test_vs_clarity PRIVATE ${CMAKE_SOURCE_DIR}/editor ${OKAY_VS_IMGUI_INCLUDE})
target_link_libraries(test_vs_clarity PRIVATE okay::engine)
add_test(NAME vs_clarity COMMAND test_vs_clarity)

add_executable(test_doc_recovery test_doc_recovery.cpp)
target_include_directories(test_doc_recovery PRIVATE ${CMAKE_SOURCE_DIR}/editor)
target_link_libraries(test_doc_recovery PRIVATE okay::engine)
add_test(NAME doc_recovery COMMAND test_doc_recovery)

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

add_executable(test_clip_graph test_clip_graph.cpp)
target_link_libraries(test_clip_graph PRIVATE okay::engine)
target_compile_definitions(test_clip_graph PRIVATE OKAY_SOURCE_DIR="${PROJECT_SOURCE_DIR}")
add_test(NAME clip_graph COMMAND test_clip_graph)

add_executable(test_strafe_blend test_strafe_blend.cpp)
target_link_libraries(test_strafe_blend PRIVATE okay::engine)
target_compile_definitions(test_strafe_blend PRIVATE OKAY_SOURCE_DIR="${PROJECT_SOURCE_DIR}")
add_test(NAME strafe_blend COMMAND test_strafe_blend)

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

add_executable(test_vs_variables test_vs_variables.cpp)
target_link_libraries(test_vs_variables PRIVATE okay::engine)
target_compile_definitions(test_vs_variables PRIVATE OKAY_SOURCE_DIR="${PROJECT_SOURCE_DIR}")
add_test(NAME vs_variables COMMAND test_vs_variables)

add_executable(test_vs_refs test_vs_refs.cpp)
target_link_libraries(test_vs_refs PRIVATE okay::engine)
add_test(NAME vs_refs COMMAND test_vs_refs)

add_executable(test_vs_choices test_vs_choices.cpp)
target_include_directories(test_vs_choices PRIVATE ${CMAKE_SOURCE_DIR}/editor ${OKAY_VS_IMGUI_INCLUDE})
target_link_libraries(test_vs_choices PRIVATE okay::engine)
add_test(NAME vs_choices COMMAND test_vs_choices)

# C# / OkayScript: classes, generics, lambdas, coroutines, components.
add_executable(test_okayscript_csharp test_okayscript_csharp.cpp)
target_link_libraries(test_okayscript_csharp PRIVATE okay::engine)
add_test(NAME okayscript_csharp COMMAND test_okayscript_csharp)

# Built-in animations on imported rigs: humanoid bone matching (Mixamo / UE / Rigify).
add_executable(test_humanoid_retarget test_humanoid_retarget.cpp)
target_link_libraries(test_humanoid_retarget PRIVATE okay::engine)
add_test(NAME humanoid_retarget COMMAND test_humanoid_retarget)

add_executable(test_humanoid_bones test_humanoid_bones.cpp)
target_link_libraries(test_humanoid_bones PRIVATE okay::engine)
add_test(NAME humanoid_bones COMMAND test_humanoid_bones)

add_executable(test_character_model_setup test_character_model_setup.cpp)
target_link_libraries(test_character_model_setup PRIVATE okay::engine)
add_test(NAME character_model_setup COMMAND test_character_model_setup)

# Imported (linear) clips keep their shape through Save + Reopen.
add_executable(test_modelanim_save test_modelanim_save.cpp)
target_link_libraries(test_modelanim_save PRIVATE okay::engine)
add_test(NAME modelanim_save COMMAND test_modelanim_save)

# Foot IK on a retargeted imported rig: walking doesn't sink the feet.
add_executable(test_footik_drift test_footik_drift.cpp)
target_link_libraries(test_footik_drift PRIVATE okay::engine)
target_compile_definitions(test_footik_drift PRIVATE OKAY_SOURCE_DIR="${PROJECT_SOURCE_DIR}")
add_test(NAME footik_drift COMMAND test_footik_drift)

# Imported characters: template foot IK, model swap, state crossfades, uneven frames.
add_executable(test_imported_glitches test_imported_glitches.cpp)
target_link_libraries(test_imported_glitches PRIVATE okay::engine)
target_compile_definitions(test_imported_glitches PRIVATE OKAY_SOURCE_DIR="${PROJECT_SOURCE_DIR}")
add_test(NAME imported_glitches COMMAND test_imported_glitches)

add_executable(test_make_playable test_make_playable.cpp)
target_link_libraries(test_make_playable PRIVATE okay::engine)
target_compile_definitions(test_make_playable PRIVATE OKAY_SOURCE_DIR="${PROJECT_SOURCE_DIR}")
add_test(NAME make_playable COMMAND test_make_playable)

add_executable(test_gait_continuity test_gait_continuity.cpp)
target_link_libraries(test_gait_continuity PRIVATE okay::engine)
add_test(NAME gait_continuity COMMAND test_gait_continuity)

add_executable(test_player_setup test_player_setup.cpp)
target_link_libraries(test_player_setup PRIVATE okay::engine)
target_compile_definitions(test_player_setup PRIVATE OKAY_SOURCE_DIR="${PROJECT_SOURCE_DIR}")
add_test(NAME player_setup COMMAND test_player_setup)

add_executable(test_destroy_unstarted test_destroy_unstarted.cpp)
target_link_libraries(test_destroy_unstarted PRIVATE okay::engine)
add_test(NAME destroy_unstarted COMMAND test_destroy_unstarted)

add_executable(test_head_stability test_head_stability.cpp)
target_link_libraries(test_head_stability PRIVATE okay::engine)
target_compile_definitions(test_head_stability PRIVATE OKAY_SOURCE_DIR="${PROJECT_SOURCE_DIR}")
add_test(NAME head_stability COMMAND test_head_stability)

add_executable(test_clear_spawn test_clear_spawn.cpp)
target_link_libraries(test_clear_spawn PRIVATE okay::engine)
add_test(NAME clear_spawn COMMAND test_clear_spawn)

add_executable(test_play_look test_play_look.cpp)
target_link_libraries(test_play_look PRIVATE okay::engine)
add_test(NAME play_look COMMAND test_play_look)

add_executable(test_gap_stuck test_gap_stuck.cpp)
target_link_libraries(test_gap_stuck PRIVATE okay::engine)
add_test(NAME gap_stuck COMMAND test_gap_stuck)

add_executable(test_anim_jitter test_anim_jitter.cpp)
target_link_libraries(test_anim_jitter PRIVATE okay::engine)
target_compile_definitions(test_anim_jitter PRIVATE OKAY_SOURCE_DIR="${PROJECT_SOURCE_DIR}")
add_test(NAME anim_jitter COMMAND test_anim_jitter)

add_executable(test_rig_sweep test_rig_sweep.cpp)
target_link_libraries(test_rig_sweep PRIVATE okay::engine)
target_compile_definitions(test_rig_sweep PRIVATE OKAY_SOURCE_DIR="${PROJECT_SOURCE_DIR}")
add_test(NAME rig_sweep COMMAND test_rig_sweep)

add_executable(test_starter_script test_starter_script.cpp)
target_link_libraries(test_starter_script PRIVATE okay::engine)
add_test(NAME starter_script COMMAND test_starter_script)

add_executable(test_mixamo_locomotion test_mixamo_locomotion.cpp)
target_link_libraries(test_mixamo_locomotion PRIVATE okay::engine)
target_compile_definitions(test_mixamo_locomotion PRIVATE OKAY_SOURCE_DIR="${PROJECT_SOURCE_DIR}")
add_test(NAME mixamo_locomotion COMMAND test_mixamo_locomotion)

add_executable(test_scene_defaults test_scene_defaults.cpp)
target_link_libraries(test_scene_defaults PRIVATE okay::engine)
add_test(NAME scene_defaults COMMAND test_scene_defaults)

add_executable(test_vs_wave4 test_vs_wave4.cpp)
target_link_libraries(test_vs_wave4 PRIVATE okay::engine)
add_test(NAME vs_wave4 COMMAND test_vs_wave4)

add_executable(test_vs_runtime test_vs_runtime.cpp)
target_link_libraries(test_vs_runtime PRIVATE okay::engine)
target_compile_definitions(test_vs_runtime PRIVATE OKAY_SOURCE_DIR="${PROJECT_SOURCE_DIR}")
add_test(NAME vs_runtime COMMAND test_vs_runtime)

add_executable(test_vs_gameplay test_vs_gameplay.cpp)
target_link_libraries(test_vs_gameplay PRIVATE okay::engine)
target_compile_definitions(test_vs_gameplay PRIVATE OKAY_SOURCE_DIR="${PROJECT_SOURCE_DIR}")
add_test(NAME vs_gameplay COMMAND test_vs_gameplay)

add_executable(test_vs_node_behavior test_vs_node_behavior.cpp)
target_link_libraries(test_vs_node_behavior PRIVATE okay::engine)
target_compile_definitions(test_vs_node_behavior PRIVATE OKAY_SOURCE_DIR="${PROJECT_SOURCE_DIR}")
add_test(NAME vs_node_behavior COMMAND test_vs_node_behavior)

add_executable(test_vs_playground test_vs_playground.cpp)
target_link_libraries(test_vs_playground PRIVATE okay::engine)
target_compile_definitions(test_vs_playground PRIVATE OKAY_SOURCE_DIR="${PROJECT_SOURCE_DIR}")

add_executable(test_vs_logic_round test_vs_logic_round.cpp)
target_link_libraries(test_vs_logic_round PRIVATE okay::engine)
add_test(NAME vs_logic_round COMMAND test_vs_logic_round)

add_executable(test_vs_smart_logic test_vs_smart_logic.cpp)
target_link_libraries(test_vs_smart_logic PRIVATE okay::engine)
add_test(NAME vs_smart_logic COMMAND test_vs_smart_logic)

add_executable(test_vs_flow_playground test_vs_flow_playground.cpp)
target_link_libraries(test_vs_flow_playground PRIVATE okay::engine)
target_compile_definitions(test_vs_flow_playground PRIVATE OKAY_SOURCE_DIR="${PROJECT_SOURCE_DIR}")
add_test(NAME vs_flow_playground COMMAND test_vs_flow_playground)

add_executable(test_vs_registry_effect test_vs_registry_effect.cpp)
target_include_directories(test_vs_registry_effect PRIVATE ${CMAKE_SOURCE_DIR}/editor ${OKAY_VS_IMGUI_INCLUDE})
target_link_libraries(test_vs_registry_effect PRIVATE okay::engine)
target_compile_definitions(test_vs_registry_effect PRIVATE OKAY_SOURCE_DIR="${PROJECT_SOURCE_DIR}")
add_test(NAME vs_registry_effect COMMAND test_vs_registry_effect)

add_executable(test_vs_node_effect test_vs_node_effect.cpp)
target_include_directories(test_vs_node_effect PRIVATE ${CMAKE_SOURCE_DIR}/editor ${OKAY_VS_IMGUI_INCLUDE})
target_link_libraries(test_vs_node_effect PRIVATE okay::engine)
add_test(NAME vs_node_effect COMMAND test_vs_node_effect)

add_executable(test_vs_gameplay_library test_vs_gameplay_library.cpp)
target_link_libraries(test_vs_gameplay_library PRIVATE okay::engine)
target_compile_definitions(test_vs_gameplay_library PRIVATE OKAY_SOURCE_DIR="${PROJECT_SOURCE_DIR}")
add_test(NAME vs_gameplay_library COMMAND test_vs_gameplay_library)

add_executable(test_vs_logic_library test_vs_logic_library.cpp)
target_link_libraries(test_vs_logic_library PRIVATE okay::engine)
add_test(NAME vs_logic_library COMMAND test_vs_logic_library)
add_test(NAME vs_playground COMMAND test_vs_playground)

add_executable(test_vs_script_coverage test_vs_script_coverage.cpp)
target_link_libraries(test_vs_script_coverage PRIVATE okay::engine)
target_compile_definitions(test_vs_script_coverage PRIVATE OKAY_SOURCE_DIR="${PROJECT_SOURCE_DIR}")
add_test(NAME vs_script_coverage COMMAND test_vs_script_coverage)

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

add_executable(test_net_anticheat test_net_anticheat.cpp)
target_link_libraries(test_net_anticheat PRIVATE okay::engine)
add_test(NAME net_anticheat COMMAND test_net_anticheat)

# Anti-cheat plan phase 1: always-on holes, OkayScript handlers, uninstall, RPC rate limit
add_executable(test_net_anticheat_ph1 test_net_anticheat_ph1.cpp)
target_link_libraries(test_net_anticheat_ph1 PRIVATE okay::engine)
add_test(NAME net_anticheat_ph1 COMMAND test_net_anticheat_ph1)
# Anti-cheat plan phase 2: relay-hosted games get the server checks; the relay
# forwards only client<->host, keeps codes from being taken over, caps rates
add_executable(test_net_anticheat_ph2 test_net_anticheat_ph2.cpp)
target_link_libraries(test_net_anticheat_ph2 PRIVATE okay::engine)
add_test(NAME net_anticheat_ph2 COMMAND test_net_anticheat_ph2)
# Anti-cheat plan phase 3: client listens only to its server, sealed Join with a
# version, AEAD associated data, bounded reliable de-dup, pinned host keys, clock heartbeat
add_executable(test_net_anticheat_ph3 test_net_anticheat_ph3.cpp)
target_link_libraries(test_net_anticheat_ph3 PRIVATE okay::engine)
add_test(NAME net_anticheat_ph3 COMMAND test_net_anticheat_ph3)
# The account token goes only to hosts that check sign-ins
add_executable(test_net_token_policy test_net_token_policy.cpp)
target_link_libraries(test_net_token_policy PRIVATE okay::engine)
add_test(NAME net_token_policy COMMAND test_net_token_policy)
# Anti-cheat plan phase 4: server-only SyncVars, fire rate / ammo budgets,
# hit claims checked against the server's view, kicks filed for review
add_executable(test_net_anticheat_ph4 test_net_anticheat_ph4.cpp)
target_link_libraries(test_net_anticheat_ph4 PRIVATE okay::engine)
add_test(NAME net_anticheat_ph4 COMMAND test_net_anticheat_ph4)
# Anti-cheat plan 5a: the opt-in fixed network tick
add_executable(test_net_tick test_net_tick.cpp)
target_link_libraries(test_net_tick PRIVATE okay::engine)
add_test(NAME net_tick COMMAND test_net_tick)
# Net plan 5b: server-authoritative movement (inputs, not positions)
add_executable(test_net_auth_move test_net_auth_move.cpp)
target_link_libraries(test_net_auth_move PRIVATE okay::engine)
add_test(NAME net_auth_move COMMAND test_net_auth_move)
# Net plan 5c: client prediction and reconciliation (through a delaying proxy)
add_executable(test_net_predict test_net_predict.cpp)
target_link_libraries(test_net_predict PRIVATE okay::engine)
add_test(NAME net_predict COMMAND test_net_predict)
# Net plan 5d: the remote interpolation buffer (through a jittery proxy)
add_executable(test_net_interp test_net_interp.cpp)
target_link_libraries(test_net_interp PRIVATE okay::engine)
add_test(NAME net_interp COMMAND test_net_interp)
# Net plan 5e: lag compensation (hit claims and RewindRaycast against the pose history)
add_executable(test_net_lagcomp test_net_lagcomp.cpp)
target_link_libraries(test_net_lagcomp PRIVATE okay::engine)
add_test(NAME net_lagcomp COMMAND test_net_lagcomp)

# Anti-cheat as a game uses it: AntiCheatConfig / NetValidator components and the VS nodes
add_executable(test_net_anticheat_components test_net_anticheat_components.cpp)
target_link_libraries(test_net_anticheat_components PRIVATE okay::engine)
add_test(NAME net_anticheat_components COMMAND test_net_anticheat_components)

# Anti-cheat P2: signed session tickets, replay window, encryption required
add_executable(test_net_anticheat_p2 test_net_anticheat_p2.cpp)
target_link_libraries(test_net_anticheat_p2 PRIVATE okay::engine)
add_test(NAME net_anticheat_p2 COMMAND test_net_anticheat_p2)

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

# The launcher's "Delete project" safety checks (shared/ProjectDelete.hpp).
add_executable(test_project_delete test_project_delete.cpp)
target_compile_features(test_project_delete PRIVATE cxx_std_17)
target_include_directories(test_project_delete PRIVATE ${CMAKE_SOURCE_DIR}/shared)
add_test(NAME project_delete COMMAND test_project_delete)

# New-project rules shared by the editor and the launcher: name, location, folder choice, skeleton, atomic writes.
add_executable(test_project_create test_project_create.cpp)
target_link_libraries(test_project_create PRIVATE okay::engine)
add_test(NAME project_create COMMAND test_project_create)

# The project's engine version (shared/ProjectVersion.hpp): parse / compare / Decide, the engine_version line in project.okayproj, the launcher's Versions layout. Header-only: no engine link.
add_executable(test_project_version test_project_version.cpp)
target_compile_features(test_project_version PRIVATE cxx_std_17)
target_include_directories(test_project_version PRIVATE ${CMAKE_SOURCE_DIR}/shared)
add_test(NAME project_version COMMAND test_project_version)

# The launcher's Versions page logic (launcher/VersionsManager.cpp, VersionsNet.cpp): versions.json, install / remove / prune on a temp folder with an injected downloader,
# keeping the current editor on update, the Latest / LTS channel, project badges and "Open with" command lines, the `--versions` hand-off. No SDL, ImGui, engine link or network.
find_package(Threads REQUIRED)
add_executable(test_launcher_versions test_launcher_versions.cpp
    ${PROJECT_SOURCE_DIR}/launcher/VersionsManager.cpp ${PROJECT_SOURCE_DIR}/launcher/VersionsNet.cpp)
target_compile_features(test_launcher_versions PRIVATE cxx_std_17)
target_include_directories(test_launcher_versions PRIVATE ${PROJECT_SOURCE_DIR}/launcher ${PROJECT_SOURCE_DIR}/shared ${PROJECT_SOURCE_DIR}/engine/include)
target_link_libraries(test_launcher_versions PRIVATE Threads::Threads)
add_test(NAME launcher_versions COMMAND test_launcher_versions)

# The self-update decisions (shared/SelfUpdate.hpp, launcher/UpdaterCore.hpp): state file, start-up verdict, retry rule, self-check, rollback,
# all-or-nothing swap, hostile versions.json. Temp folders only; no window, no network.
add_executable(test_self_update test_self_update.cpp ${PROJECT_SOURCE_DIR}/launcher/VersionsManager.cpp)
target_compile_features(test_self_update PRIVATE cxx_std_17)
target_include_directories(test_self_update PRIVATE ${PROJECT_SOURCE_DIR}/launcher ${PROJECT_SOURCE_DIR}/shared ${PROJECT_SOURCE_DIR}/engine/include)
target_link_libraries(test_self_update PRIVATE Threads::Threads)
if(WIN32)
    target_link_libraries(test_self_update PRIVATE shell32)
endif()
add_test(NAME self_update COMMAND test_self_update)

# Editor/launcher self-update: wait for the old process, swap, relaunch once.
add_executable(test_app_restart test_app_restart.cpp)
target_compile_features(test_app_restart PRIVATE cxx_std_17)
target_include_directories(test_app_restart PRIVATE ${CMAKE_SOURCE_DIR}/shared ${CMAKE_CURRENT_SOURCE_DIR})
find_package(Threads REQUIRED)
target_link_libraries(test_app_restart PRIVATE Threads::Threads)
if(WIN32)
    target_link_libraries(test_app_restart PRIVATE shell32)
endif()
add_test(NAME app_restart COMMAND test_app_restart)

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

# The hosted password-reset page (supabase/auth-pages/reset-password): its pure logic, under node (skipped when node is absent).
find_program(OKAY_NODE node)
if(OKAY_NODE)
    add_test(NAME password_reset_page COMMAND ${OKAY_NODE} ${CMAKE_CURRENT_SOURCE_DIR}/scripts/test_password_reset_page.js)
endif()

# The launcher's chat JSON (Messages / Servers): header-only, no engine link.
add_executable(test_chat_json test_chat_json.cpp)
target_compile_features(test_chat_json PRIVATE cxx_std_17)
target_include_directories(test_chat_json PRIVATE ${CMAKE_SOURCE_DIR}/engine/include)
add_test(NAME chat_json COMMAND test_chat_json)
# Server-side blocks + privacy: rule parsing, missing-table detection, settings, pending changes.
add_executable(test_blocks_privacy test_blocks_privacy.cpp)
target_compile_features(test_blocks_privacy PRIVATE cxx_std_17)
target_include_directories(test_blocks_privacy PRIVATE ${CMAKE_SOURCE_DIR}/engine/include)
add_test(NAME blocks_privacy COMMAND test_blocks_privacy)
# Kick / ban / unban / promote logic for servers and group chats (shared/MemberModeration.hpp): permissions, requests, error mapping.
add_executable(test_member_moderation test_member_moderation.cpp)
target_compile_features(test_member_moderation PRIVATE cxx_std_17)
target_include_directories(test_member_moderation PRIVATE ${CMAKE_SOURCE_DIR}/engine/include ${PROJECT_SOURCE_DIR}/shared)
add_test(NAME member_moderation COMMAND test_member_moderation)
# Typing indicator / read receipts: missing-table detection (docs/sql/chat_receipts.sql).
add_executable(test_chat_receipts test_chat_receipts.cpp)
target_compile_features(test_chat_receipts PRIVATE cxx_std_17)
target_include_directories(test_chat_receipts PRIVATE ${CMAKE_SOURCE_DIR}/engine/include)
add_test(NAME chat_receipts COMMAND test_chat_receipts)
# ...and the friends / profile JSON (Friends.hpp).
add_executable(test_friends_json test_friends_json.cpp)
target_compile_features(test_friends_json PRIVATE cxx_std_17)
target_include_directories(test_friends_json PRIVATE ${CMAKE_SOURCE_DIR}/engine/include)
add_test(NAME friends_json COMMAND test_friends_json)

# Anti-cheat (opt-in): the build's integrity manifest + ed25519, and the runtime
# checks' logic. Linked to the engine so a libsodium build tests that backend too.
add_executable(test_anticheat_integrity test_anticheat_integrity.cpp)
target_link_libraries(test_anticheat_integrity PRIVATE okay::engine)
target_include_directories(test_anticheat_integrity PRIVATE ${CMAKE_SOURCE_DIR}/shared)
add_test(NAME anticheat_integrity COMMAND test_anticheat_integrity)
# The same test compiled two more ways WITHOUT the engine, so shared/AntiCheatCrypto.hpp is proven to work where the
# engine is not linked: (a) with -DOKAY_OWN_CRYPTO against okay::crypto alone (Ed25519 and SHA-512 from our own library,
# the port only as the reference the test compares with), (b) with neither libsodium nor okay::crypto (the portable port).
add_executable(test_anticheat_integrity_ownlib test_anticheat_integrity.cpp)
target_link_libraries(test_anticheat_integrity_ownlib PRIVATE okay::crypto)
target_include_directories(test_anticheat_integrity_ownlib PRIVATE ${CMAKE_SOURCE_DIR}/shared)
target_compile_definitions(test_anticheat_integrity_ownlib PRIVATE OKAY_OWN_CRYPTO)
add_test(NAME anticheat_integrity_ownlib COMMAND test_anticheat_integrity_ownlib)
add_executable(test_anticheat_integrity_portable test_anticheat_integrity.cpp)
target_include_directories(test_anticheat_integrity_portable PRIVATE ${CMAKE_SOURCE_DIR}/shared ${CMAKE_SOURCE_DIR}/engine/include)
add_test(NAME anticheat_integrity_portable COMMAND test_anticheat_integrity_portable)
add_executable(test_anticheat_checks test_anticheat_checks.cpp)
target_link_libraries(test_anticheat_checks PRIVATE okay::engine)
target_include_directories(test_anticheat_checks PRIVATE ${CMAKE_SOURCE_DIR}/shared)
add_test(NAME anticheat_checks COMMAND test_anticheat_checks)
# -DOKAY_AC_TRUSTED_KEYS validation (cmake/OkayAntiCheatKeys.cmake): good lists pass,
# anything but exactly 64 hex digits per key fails the configure.
set(_okay_ac_k64 "d75a980182b10ab7d54bfed3c964073a0ee172f3daa62325af021a68f707511a")
add_test(NAME anticheat_keys_ok COMMAND ${CMAKE_COMMAND} "-DKEYS=${_okay_ac_k64}" -P ${CMAKE_CURRENT_SOURCE_DIR}/anticheat_keys_check.cmake)
add_test(NAME anticheat_keys_two_ok COMMAND ${CMAKE_COMMAND} "-DKEYS=${_okay_ac_k64},${_okay_ac_k64}" -P ${CMAKE_CURRENT_SOURCE_DIR}/anticheat_keys_check.cmake)
string(SUBSTRING "${_okay_ac_k64}" 0 63 _okay_ac_k63)
string(SUBSTRING "${_okay_ac_k64}" 0 62 _okay_ac_k62)
foreach(_bad "${_okay_ac_k63}" "${_okay_ac_k64}0" "${_okay_ac_k64},${_okay_ac_k63}" "${_okay_ac_k64}," "zz${_okay_ac_k62}")
    string(MD5 _id "${_bad}")
    add_test(NAME anticheat_keys_bad_${_id} COMMAND ${CMAKE_COMMAND} "-DKEYS=${_bad}" -P ${CMAKE_CURRENT_SOURCE_DIR}/anticheat_keys_check.cmake)
    set_tests_properties(anticheat_keys_bad_${_id} PROPERTIES WILL_FAIL TRUE)
endforeach()
# Anti-cheat P4: the salted hardware hash + the backend's request building / parsing.
add_executable(test_anticheat_backend test_anticheat_backend.cpp)
target_link_libraries(test_anticheat_backend PRIVATE okay::engine)
target_include_directories(test_anticheat_backend PRIVATE ${CMAKE_SOURCE_DIR}/shared)
add_test(NAME anticheat_backend COMMAND test_anticheat_backend)
# User reports (docs/sql/reports.sql): requests, honest outcomes, the moderator list...
add_executable(test_user_reports test_user_reports.cpp)
target_link_libraries(test_user_reports PRIVATE okay::engine)
target_include_directories(test_user_reports PRIVATE ${CMAKE_SOURCE_DIR}/shared)
add_test(NAME user_reports COMMAND test_user_reports)
# ...and docs/sql/anticheat.sql on a LOCAL Postgres (skipped unless
# OKAY_SQL_TEST_PGHOST names one; see the script).
if(UNIX)
    add_test(NAME anticheat_sql COMMAND bash ${CMAKE_CURRENT_SOURCE_DIR}/scripts/test_anticheat_sql.sh)
    set_tests_properties(anticheat_sql PROPERTIES SKIP_RETURN_CODE 77)
    add_test(NAME reports_sql COMMAND bash ${CMAKE_CURRENT_SOURCE_DIR}/scripts/test_reports_sql.sh)
    set_tests_properties(reports_sql PROPERTIES SKIP_RETURN_CODE 77)

    add_test(NAME blocks_sql COMMAND bash ${CMAKE_CURRENT_SOURCE_DIR}/scripts/test_blocks_sql.sh)
    set_tests_properties(blocks_sql PROPERTIES SKIP_RETURN_CODE 77)
    # scripts/sign-windows.sh (Authenticode signing of the Windows programs) with a throwaway certificate; needs osslsigncode, openssl and MinGW.
    add_test(NAME sign_windows COMMAND bash ${CMAKE_CURRENT_SOURCE_DIR}/scripts/test_sign_windows.sh)
    set_tests_properties(sign_windows PROPERTIES SKIP_RETURN_CODE 77)
    # docs/sql/member_moderation.sql (kick / ban / roles) on a LOCAL Postgres, same opt-in as above.
    add_test(NAME member_moderation_sql COMMAND bash ${CMAKE_CURRENT_SOURCE_DIR}/scripts/test_member_moderation_sql.sh)
    set_tests_properties(member_moderation_sql PROPERTIES SKIP_RETURN_CODE 77)
    add_test(NAME chat_receipts_sql COMMAND bash ${CMAKE_CURRENT_SOURCE_DIR}/scripts/test_chat_receipts_sql.sh)
    set_tests_properties(chat_receipts_sql PROPERTIES SKIP_RETURN_CODE 77)
endif()

add_executable(test_tween test_tween.cpp)
target_link_libraries(test_tween PRIVATE okay::engine)
add_test(NAME tween COMMAND test_tween)
add_executable(test_tween_edges test_tween_edges.cpp)
target_link_libraries(test_tween_edges PRIVATE okay::engine)
add_test(NAME tween_edges COMMAND test_tween_edges)
add_executable(test_tween_features test_tween_features.cpp)
target_link_libraries(test_tween_features PRIVATE okay::engine)
add_test(NAME tween_features COMMAND test_tween_features)
add_executable(test_tween_long_run test_tween_long_run.cpp)
target_link_libraries(test_tween_long_run PRIVATE okay::engine)
add_test(NAME tween_long_run COMMAND test_tween_long_run)
add_executable(test_tween_sequencing test_tween_sequencing.cpp)
target_link_libraries(test_tween_sequencing PRIVATE okay::engine)
add_test(NAME tween_sequencing COMMAND test_tween_sequencing)
add_executable(test_timer_debug test_timer_debug.cpp)
target_link_libraries(test_timer_debug PRIVATE okay::engine)
add_test(NAME timer_debug COMMAND test_timer_debug)
add_executable(test_timers_audit test_timers_audit.cpp)
target_link_libraries(test_timers_audit PRIVATE okay::engine)
add_test(NAME timers_audit COMMAND test_timers_audit)
add_executable(test_timers_fifth test_timers_fifth.cpp)
target_link_libraries(test_timers_fifth PRIVATE okay::engine)
add_test(NAME timers_fifth COMMAND test_timers_fifth)

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

add_executable(test_anim_script_control test_anim_script_control.cpp)
target_link_libraries(test_anim_script_control PRIVATE okay::engine)
add_test(NAME anim_script_control COMMAND test_anim_script_control)

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

# Robustness: Mesh::PropRand without signed overflow (UBSan), same values
add_executable(test_mesh_variant_hash test_mesh_variant_hash.cpp)
target_link_libraries(test_mesh_variant_hash PRIVATE okay::engine)
add_test(NAME mesh_variant_hash COMMAND test_mesh_variant_hash)

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

# The relay client trusts nothing on its socket: welcome timeout, nonce echo, refusal codes
add_executable(test_net_relay_hardening test_net_relay_hardening.cpp)
target_link_libraries(test_net_relay_hardening PRIVATE okay::engine)
add_test(NAME net_relay_hardening COMMAND test_net_relay_hardening)

# The packet wire format is little-endian on every host: pinned byte for byte
add_executable(test_net_packet_layout test_net_packet_layout.cpp)
target_link_libraries(test_net_packet_layout PRIVATE okay::engine)
add_test(NAME net_packet_layout COMMAND test_net_packet_layout)

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
    target_compile_definitions(test_ui_renderer_golden PRIVATE OKAY_TEST_OUTPUT_DIR="${CMAKE_CURRENT_BINARY_DIR}")
    add_test(NAME ui_renderer_golden COMMAND test_ui_renderer_golden)

    # Accessibility settings in rendered pixels (text size, colour filter, high contrast)
    add_executable(test_accessibility_render test_accessibility_render.cpp)
    target_link_libraries(test_accessibility_render PRIVATE okay::engine ${OKAY_TEST_SDL_LIB})
    target_include_directories(test_accessibility_render PRIVATE ${SDL2_INCLUDE_DIRS})
    add_test(NAME accessibility_render COMMAND test_accessibility_render)

    add_executable(test_display_screen test_display_screen.cpp)
    target_link_libraries(test_display_screen PRIVATE okay::engine ${OKAY_TEST_SDL_LIB})
    target_include_directories(test_display_screen PRIVATE ${SDL2_INCLUDE_DIRS})
    add_test(NAME display_screen COMMAND test_display_screen)
    add_executable(test_ui_focus_render test_ui_focus_render.cpp)
    target_link_libraries(test_ui_focus_render PRIVATE okay::engine ${OKAY_TEST_SDL_LIB})
    target_include_directories(test_ui_focus_render PRIVATE ${SDL2_INCLUDE_DIRS})
    add_test(NAME ui_focus_render COMMAND test_ui_focus_render)
else()
    message(WARNING "SDL2 not found: skipping the UI renderer golden-image test (ui_renderer_golden).")
endif()

# Untrusted network input + thread safety of the shared net state (also run under ASan/UBSan/TSan)
add_executable(test_net_hardening test_net_hardening.cpp)
target_link_libraries(test_net_hardening PRIVATE okay::engine)
add_test(NAME net_hardening COMMAND test_net_hardening)
add_executable(test_net_races test_net_races.cpp)
target_link_libraries(test_net_races PRIVATE okay::engine)
add_test(NAME net_races COMMAND test_net_races)
add_executable(test_net_multiplayer_fixes test_net_multiplayer_fixes.cpp)
target_link_libraries(test_net_multiplayer_fixes PRIVATE okay::engine)
add_test(NAME net_multiplayer_fixes COMMAND test_net_multiplayer_fixes)

# The launcher's voice session (header-only, SDL): its net thread's failure text vs the per-frame Error() read. Needs SDL2.
if(SDL2_FOUND)
    add_executable(test_voice_races test_voice_races.cpp)
    target_link_libraries(test_voice_races PRIVATE okay::engine ${OKAY_TEST_SDL_LIB})
    target_include_directories(test_voice_races PRIVATE ${SDL2_INCLUDE_DIRS})
    add_test(NAME voice_races COMMAND test_voice_races)
endif()

# Procedural object holding: hands on the grips (built-in + imported rigs), reach, drop, save/load.
add_executable(test_object_holding test_object_holding.cpp)
target_link_libraries(test_object_holding PRIVATE okay::engine)
target_compile_definitions(test_object_holding PRIVATE OKAY_SOURCE_DIR="${PROJECT_SOURCE_DIR}")
add_test(NAME object_holding COMMAND test_object_holding)

# Input actions: bindings, gamepad sticks / triggers, rebinding saved per player, keybind fields
add_executable(test_input_actions test_input_actions.cpp)
target_link_libraries(test_input_actions PRIVATE okay::engine)
add_test(NAME input_actions COMMAND test_input_actions)

# Touch: gesture recogniser (synthetic finger streams) and on-screen controls driving the action map.
add_executable(test_touch_controls test_touch_controls.cpp)
target_link_libraries(test_touch_controls PRIVATE okay::engine)
target_compile_definitions(test_touch_controls PRIVATE OKAY_SOURCE_DIR="${PROJECT_SOURCE_DIR}")
add_test(NAME touch_controls COMMAND test_touch_controls)

# Input: F-keys / nav keys / left-right modifiers, input contexts that swallow keys, gamepad dead zones,
# response curves, trigger thresholds and several controllers
add_executable(test_input_keys_gamepad test_input_keys_gamepad.cpp)
target_link_libraries(test_input_keys_gamepad PRIVATE okay::engine)
add_test(NAME input_keys_gamepad COMMAND test_input_keys_gamepad)

# Save games: slots, whole-scene state, script variables, deferred load, autosave
add_executable(test_save_game test_save_game.cpp)
target_link_libraries(test_save_game PRIVATE okay::engine)
add_test(NAME save_game COMMAND test_save_game)

add_executable(test_ragdoll test_ragdoll.cpp)
target_link_libraries(test_ragdoll PRIVATE okay::engine)
add_test(NAME ragdoll COMMAND test_ragdoll)

# Fixed-step physics: same result at any frame rate, catch-up limit, render interpolation
add_executable(test_fixed_step test_fixed_step.cpp)
target_link_libraries(test_fixed_step PRIVATE okay::engine)
add_test(NAME fixed_step COMMAND test_fixed_step)

# Post-processing, sky, fog and weather: settings round trip, presets, the weather rig, sky / fog / tonemap maths
add_executable(test_post_sky test_post_sky.cpp)
target_link_libraries(test_post_sky PRIVATE okay::engine)
add_test(NAME post_sky COMMAND test_post_sky)
# Which lighting / post / fog / sky switches each renderer actually draws
add_executable(test_renderer_features test_renderer_features.cpp)
target_link_libraries(test_renderer_features PRIVATE okay::engine)
add_test(NAME renderer_features COMMAND test_renderer_features)
# Navigation mesh: bake (walls, doorways, slopes, steps, bridges), queries, agents, files, records
add_executable(test_navmesh test_navmesh.cpp)
target_link_libraries(test_navmesh PRIVATE okay::engine)
add_test(NAME navmesh COMMAND test_navmesh)
# Navigation areas (modifier, costs, masks) and off-mesh links (jump / drop / ladder): bake, path search, crossing, scene file
add_executable(test_navmesh_areas test_navmesh_areas.cpp)
target_link_libraries(test_navmesh_areas PRIVATE okay::engine)
add_test(NAME navmesh_areas COMMAND test_navmesh_areas)
# Navigation obstacle carving: footprint strips, polygon subtraction, portals, stationary timing, agents replanning, scene file, validator
add_executable(test_navmesh_carving test_navmesh_carving.cpp)
target_link_libraries(test_navmesh_carving PRIVATE okay::engine)
add_test(NAME navmesh_carving COMMAND test_navmesh_carving)
# Dialogue graphs: parse / write, traversal (lines, choices, conditions, sets, events,
# items, quests), validation, the runner's Advance / Choose, the StoryState record
add_executable(test_dialogue test_dialogue.cpp)
target_link_libraries(test_dialogue PRIVATE okay::engine)
add_test(NAME dialogue COMMAND test_dialogue)

# Quests: objectives from every source, prerequisites, rewards once, scene + save-game round trips
add_executable(test_quests test_quests.cpp)
target_link_libraries(test_quests PRIVATE okay::engine)
add_test(NAME quests COMMAND test_quests)

# Behavior trees: file round trip, composites, decorators, guards, blackboard, perception, navigation tasks, scripts, scene record
add_executable(test_behavior_tree test_behavior_tree.cpp)
target_link_libraries(test_behavior_tree PRIVATE okay::engine)
add_test(NAME behavior_tree COMMAND test_behavior_tree)

# Splines, spline meshes / instancers / followers, line and trail renderers: evaluation, records, scripts
add_executable(test_splines test_splines.cpp)
target_link_libraries(test_splines PRIVATE okay::engine)
add_test(NAME splines COMMAND test_splines)

# Scheduler: global (persistent) timers, per-task options, cancel by tag / owner;
# Tween: callback order, re-entrancy, hitches, lifetime
add_executable(test_timers_tweens test_timers_tweens.cpp)
target_link_libraries(test_timers_tweens PRIVATE okay::engine)
add_test(NAME timers_tweens COMMAND test_timers_tweens)

add_executable(test_random test_random.cpp)
target_link_libraries(test_random PRIVATE okay::engine)
add_test(NAME random COMMAND test_random)

add_executable(test_eventbus test_eventbus.cpp)
target_link_libraries(test_eventbus PRIVATE okay::engine)
add_test(NAME eventbus COMMAND test_eventbus)

add_executable(test_utf8 test_utf8.cpp)
target_link_libraries(test_utf8 PRIVATE okay::engine)
add_test(NAME utf8 COMMAND test_utf8)

add_executable(test_time_pause test_time_pause.cpp)
target_link_libraries(test_time_pause PRIVATE okay::engine)
add_test(NAME time_pause COMMAND test_time_pause)

add_executable(test_scene_lifecycle test_scene_lifecycle.cpp)
target_link_libraries(test_scene_lifecycle PRIVATE okay::engine)
add_test(NAME scene_lifecycle COMMAND test_scene_lifecycle)

# Scene lifecycle audit: event order across a swap, loads from callbacks, two loads at once, failed loads, DontDestroyOnLoad order, unloading the camera's scene
add_executable(test_scene_lifecycle_audit test_scene_lifecycle_audit.cpp)
target_link_libraries(test_scene_lifecycle_audit PRIVATE okay::engine)
add_test(NAME scene_lifecycle_audit COMMAND test_scene_lifecycle_audit)

add_executable(test_durable_saves test_durable_saves.cpp)
target_link_libraries(test_durable_saves PRIVATE okay::engine)
add_test(NAME durable_saves COMMAND test_durable_saves)

# Saves and prefs hardening: the shared atomic write, SaveFile checksum and backup, Prefs recovery, SaveGame listing
add_executable(test_save_hardening test_save_hardening.cpp)
target_link_libraries(test_save_hardening PRIVATE okay::engine)
add_test(NAME save_hardening COMMAND test_save_hardening)

# Save game framework: typed blocks, slot files + headers, migration chain, forward
# compatibility, sync backends, menu model (no scene needed).
add_executable(test_save_framework test_save_framework.cpp)
target_link_libraries(test_save_framework PRIVATE okay::engine)
add_test(NAME save_framework COMMAND test_save_framework)

# Save framework on a Scene: Saveable capture/apply, dynamic spawn, destroyed objects,
# checkpoints, quick save, autosave ring, continue.
add_executable(test_save_scene test_save_scene.cpp)
target_link_libraries(test_save_scene PRIVATE okay::engine)
add_test(NAME save_scene COMMAND test_save_scene)

# Save framework without code: Saveable in the scene file, GameSaves, autosave, script / visual-script /
# Action List routes, the Save Slots screen.
add_executable(test_save_nocode test_save_nocode.cpp)
target_link_libraries(test_save_nocode PRIVATE okay::engine)
add_test(NAME save_nocode COMMAND test_save_nocode)

# Script builtins batch 1: stats, hierarchy, light setters, WaitUntil/WaitWhile
add_executable(test_script_batch1 test_script_batch1.cpp)
target_link_libraries(test_script_batch1 PRIVATE okay::engine)
add_test(NAME script_batch1 COMMAND test_script_batch1)

add_executable(test_assetmanager test_assetmanager.cpp)
target_link_libraries(test_assetmanager PRIVATE okay::engine)
add_test(NAME assetmanager COMMAND test_assetmanager)

# Asset database: GUID metas, move detection, dependency graph, import settings, thumbnails, search
add_executable(test_asset_database test_asset_database.cpp)
target_link_libraries(test_asset_database PRIVATE okay::engine)
target_compile_definitions(test_asset_database PRIVATE OKAY_SOURCE_DIR="${PROJECT_SOURCE_DIR}")
add_test(NAME asset_database COMMAND test_asset_database)

# Core audit regressions: math edge cases, ECS/NetWorld hardening, Camera degenerate
# viewports, Layers names, DataAsset load, input binding persistence.
add_executable(test_core_audit test_core_audit.cpp)
target_link_libraries(test_core_audit PRIVATE okay::engine)
add_test(NAME core_audit COMMAND test_core_audit)

# Core math extras: Quat::Normalized NaN, Color::Parse (hex and "r,g,b")
add_executable(test_core_math_extra test_core_math_extra.cpp)
target_link_libraries(test_core_math_extra PRIVATE okay::engine)
add_test(NAME core_math_extra COMMAND test_core_math_extra)

# Core audit: terminal input session (pty based, so POSIX only).
if(UNIX)
    add_executable(test_core_audit_input test_core_audit_input.cpp)
    target_link_libraries(test_core_audit_input PRIVATE okay::engine)
    add_test(NAME core_audit_input COMMAND test_core_audit_input)
endif()

# Visual-script nodes for music, camera and particles
add_executable(test_vs_media test_vs_media.cpp)
target_link_libraries(test_vs_media PRIVATE okay::engine)
add_test(NAME vs_media COMMAND test_vs_media)
add_executable(test_vs_tween test_vs_tween.cpp)
target_link_libraries(test_vs_tween PRIVATE okay::engine)
add_test(NAME vs_tween COMMAND test_vs_tween)

# Script navmesh queries: nav_path_exists / nav_path_length / nav_point_walkable / nav_nearest_point
add_executable(test_script_nav test_script_nav.cpp)
target_link_libraries(test_script_nav PRIVATE okay::engine)
add_test(NAME script_nav COMMAND test_script_nav)

# Math: Mat3, Mat4::Decompose, geometry (Ray/Plane/Sphere/AABB/OBB/Frustum/Capsule), Vec2i/Vec3i, noise, curves, colour spaces, Rng streams
add_executable(test_math_geometry test_math_geometry.cpp)
target_link_libraries(test_math_geometry PRIVATE okay::engine)
add_test(NAME math_geometry COMMAND test_math_geometry)

# Core fixes: Application quit handling, strict Rect::Overlaps, SmoothStep edges, ToEuler near the poles
add_executable(test_core_fixes test_core_fixes.cpp)
target_link_libraries(test_core_fixes PRIVATE okay::engine)
add_test(NAME core_fixes COMMAND test_core_fixes)

# Core tools: CommandLine, Paths, BuildInfo, Stopwatch, StateMachine
add_executable(test_core_cli test_core_cli.cpp)
target_link_libraries(test_core_cli PRIVATE okay::engine)
target_compile_definitions(test_core_cli PRIVATE OKAY_ENGINE_VERSION_FOR_TEST="${PROJECT_VERSION}")
add_test(NAME core_cli COMMAND test_core_cli)

# Core console: EventBus extras, Cvars, Console, debug overlay lines
add_executable(test_core_console test_core_console.cpp)
target_link_libraries(test_core_console PRIVATE okay::engine)
add_test(NAME core_console COMMAND test_core_console)
# Core wave 6, 1.18: CommandLine defaults, Cvar extremes and flags (read-only, cheat, restart required), Console nesting, StateMachine exit
add_executable(test_core_utils_audit test_core_utils_audit.cpp)
target_link_libraries(test_core_utils_audit PRIVATE okay::engine)
add_test(NAME core_utils_audit COMMAND test_core_utils_audit)
# Localization formatting + plural rules, typed Prefs, FixedTimestep (docs/localization_prefs_time.md)
add_executable(test_loc_prefs_time test_loc_prefs_time.cpp)
target_link_libraries(test_loc_prefs_time PRIVATE okay::engine)
add_test(NAME loc_prefs_time COMMAND test_loc_prefs_time)

add_executable(test_ecs_commands test_ecs_commands.cpp)
target_link_libraries(test_ecs_commands PRIVATE okay::engine)
add_test(NAME ecs_commands COMMAND test_ecs_commands)

# Core wave 6, 1.9: Subscription::Active, throwing handlers, SystemScheduler changes while running, entity id wrap, SceneBridge
add_executable(test_core_events_ecs test_core_events_ecs.cpp)
target_link_libraries(test_core_events_ecs PRIVATE okay::engine)
add_test(NAME core_events_ecs COMMAND test_core_events_ecs)

# Multi-frame scene loads (progress, additive load + unload by handle), ObjectPool, RequireComponent rules
add_executable(test_scene_load_pool test_scene_load_pool.cpp)
target_link_libraries(test_scene_load_pool PRIVATE okay::engine)
add_test(NAME scene_load_pool COMMAND test_scene_load_pool)

# Persistent object ids, sibling order / enabled flags, duplicate components, component serializer registry, prefab overrides
add_executable(test_scene_ids_prefabs test_scene_ids_prefabs.cpp)
target_link_libraries(test_scene_ids_prefabs PRIVATE okay::engine)
add_test(NAME scene_ids_prefabs COMMAND test_scene_ids_prefabs)

# Scene serializer audit: key bindings in the one-token records, several components of one type, asset paths, number and text round trips
add_executable(test_scene_serializer_audit test_scene_serializer_audit.cpp)
target_link_libraries(test_scene_serializer_audit PRIVATE okay::engine)
add_test(NAME scene_serializer_audit COMMAND test_scene_serializer_audit)

add_executable(test_layers_tags_cameras test_layers_tags_cameras.cpp)
target_link_libraries(test_layers_tags_cameras PRIVATE okay::engine)
add_test(NAME layers_tags_cameras COMMAND test_layers_tags_cameras)

add_executable(test_scene_hierarchy_search test_scene_hierarchy_search.cpp)
target_link_libraries(test_scene_hierarchy_search PRIVATE okay::engine)
add_test(NAME scene_hierarchy_search COMMAND test_scene_hierarchy_search)

# Scene graph (1.3): a 100000-deep chain, Transform audit and helpers, scene / GameObject audit,
# project tag list and layer names, spawning (Instantiate / Duplicate / unique names), Mover / Spinner / Lifetime
add_executable(test_deep_hierarchy test_deep_hierarchy.cpp)
target_link_libraries(test_deep_hierarchy PRIVATE okay::engine)
add_test(NAME deep_hierarchy COMMAND test_deep_hierarchy)

add_executable(test_transform_graph test_transform_graph.cpp)
target_link_libraries(test_transform_graph PRIVATE okay::engine)
add_test(NAME transform_graph COMMAND test_transform_graph)

add_executable(test_scene_graph_audit test_scene_graph_audit.cpp)
target_link_libraries(test_scene_graph_audit PRIVATE okay::engine)
add_test(NAME scene_graph_audit COMMAND test_scene_graph_audit)

add_executable(test_layers_tags_project test_layers_tags_project.cpp)
target_link_libraries(test_layers_tags_project PRIVATE okay::engine)
add_test(NAME layers_tags_project COMMAND test_layers_tags_project)

add_executable(test_scene_spawn test_scene_spawn.cpp)
target_link_libraries(test_scene_spawn PRIVATE okay::engine)
add_test(NAME scene_spawn COMMAND test_scene_spawn)

add_executable(test_mover_spinner_lifetime test_mover_spinner_lifetime.cpp)
target_link_libraries(test_mover_spinner_lifetime PRIVATE okay::engine)
add_test(NAME mover_spinner_lifetime COMMAND test_mover_spinner_lifetime)

# Scene graph (1.3, wave 4): world math (singular inverse, shear, negative scale), a cached active state and linear
# destroy on deep / wide hierarchies, Clear and loads from inside callbacks, saved component order, audit and additions
add_executable(test_transform_world test_transform_world.cpp)
target_link_libraries(test_transform_world PRIVATE okay::engine)
add_test(NAME transform_world COMMAND test_transform_world)

add_executable(test_scene_graph_perf test_scene_graph_perf.cpp)
target_link_libraries(test_scene_graph_perf PRIVATE okay::engine)
add_test(NAME scene_graph_perf COMMAND test_scene_graph_perf)
set_tests_properties(scene_graph_perf PROPERTIES TIMEOUT 240)   # the old per-object detach took minutes at these sizes

add_executable(test_scene_reentrant test_scene_reentrant.cpp)
target_link_libraries(test_scene_reentrant PRIVATE okay::engine)
add_test(NAME scene_reentrant COMMAND test_scene_reentrant)

add_executable(test_component_order test_component_order.cpp)
target_link_libraries(test_component_order PRIVATE okay::engine)
add_test(NAME component_order COMMAND test_component_order)

add_executable(test_scene_graph_wave4 test_scene_graph_wave4.cpp)
target_link_libraries(test_scene_graph_wave4 PRIVATE okay::engine)
add_test(NAME scene_graph_wave4 COMMAND test_scene_graph_wave4)

# Scene graph (1.3, wave 5): world-cache check against a plain walk, constant-cost child-list removal, look-at variants,
# GetOrAddComponent / ForEachDescendant, FindByPath with duplicates, Stats on a huge scene, tag / layer names in other languages
add_executable(test_scene_graph_wave5 test_scene_graph_wave5.cpp)
target_link_libraries(test_scene_graph_wave5 PRIVATE okay::engine)
add_test(NAME scene_graph_wave5 COMMAND test_scene_graph_wave5)
set_tests_properties(scene_graph_wave5 PROPERTIES TIMEOUT 240)   # 300000 children, 100000-deep chains

add_executable(test_object_pool_wave5 test_object_pool_wave5.cpp)
target_link_libraries(test_object_pool_wave5 PRIVATE okay::engine)
add_test(NAME object_pool_wave5 COMMAND test_object_pool_wave5)

# Object pooling audit: idle objects not saved, Shrink, Scene::Despawn, resets, Spawner usePool, script pool calls
add_executable(test_object_pool_audit test_object_pool_audit.cpp)
target_link_libraries(test_object_pool_audit PRIVATE okay::engine)
add_test(NAME object_pool_audit COMMAND test_object_pool_audit)

# Loading Screen with a background load: progress, kept across the swap, failures, scenes saved before keep loading in one go
add_executable(test_loading_screen_async test_loading_screen_async.cpp)
target_link_libraries(test_loading_screen_async PRIVATE okay::engine)
add_test(NAME loading_screen_async COMMAND test_loading_screen_async)

# Optimization tools (27): mesh simplification, LOD chains, LOD group selection
add_executable(test_optimize_simplify test_optimize_simplify.cpp)
target_link_libraries(test_optimize_simplify PRIVATE okay::engine)
add_test(NAME optimize_simplify COMMAND test_optimize_simplify)

# Optimization tools (27): CPU occlusion buffer and portal / cell visibility
add_executable(test_optimize_occlusion test_optimize_occlusion.cpp)
target_link_libraries(test_optimize_occlusion PRIVATE okay::engine)
add_test(NAME optimize_occlusion COMMAND test_optimize_occlusion)

# Optimization tools (27): draw call batching planner and static mesh merge
add_executable(test_optimize_batching test_optimize_batching.cpp)
target_link_libraries(test_optimize_batching PRIVATE okay::engine)
add_test(NAME optimize_batching COMMAND test_optimize_batching)

# Optimization tools (27): streaming cell grid and texture mip budget
add_executable(test_optimize_streaming test_optimize_streaming.cpp)
target_link_libraries(test_optimize_streaming PRIVATE okay::engine)
add_test(NAME optimize_streaming COMMAND test_optimize_streaming)

# Optimization tools (27): scene optimization report (JSON / text)
add_executable(test_camera_stack test_camera_stack.cpp)
target_link_libraries(test_camera_stack PRIVATE okay::engine)
add_test(NAME camera_stack COMMAND test_camera_stack)

add_executable(test_visibility_lod test_visibility_lod.cpp)
target_link_libraries(test_visibility_lod PRIVATE okay::engine)
add_test(NAME visibility_lod COMMAND test_visibility_lod)

add_executable(test_gltf_pbr_maps test_gltf_pbr_maps.cpp)
target_link_libraries(test_gltf_pbr_maps PRIVATE okay::engine)
add_test(NAME gltf_pbr_maps COMMAND test_gltf_pbr_maps)

add_executable(test_optimize_report test_optimize_report.cpp)
target_link_libraries(test_optimize_report PRIVATE okay::engine)
add_test(NAME optimize_report COMMAND test_optimize_report)

# ---- Automated testing (feature map area 33) --------------------------------
# 33.1 play-mode tests: the registry / runner, then PlayTest (headless frame stepping,
# input injection, log capture) on procedurally built scenes.
add_executable(test_testing_registry test_testing_registry.cpp)
target_link_libraries(test_testing_registry PRIVATE okay::engine)
target_compile_definitions(test_testing_registry PRIVATE OKAY_TEST_OUTPUT_DIR="${CMAKE_CURRENT_BINARY_DIR}")
add_test(NAME testing_registry COMMAND test_testing_registry)

add_executable(test_playtest test_playtest.cpp)
target_link_libraries(test_playtest PRIVATE okay::engine)
target_compile_definitions(test_playtest PRIVATE OKAY_TEST_OUTPUT_DIR="${CMAKE_CURRENT_BINARY_DIR}")
add_test(NAME playtest COMMAND test_playtest)

# 33.2 scene validation: missing assets, cameras, duplicate names, scripts, references, transforms
add_executable(test_scene_validator test_scene_validator.cpp)
target_link_libraries(test_scene_validator PRIVATE okay::engine)
target_compile_definitions(test_scene_validator PRIVATE OKAY_TEST_OUTPUT_DIR="${CMAKE_CURRENT_BINARY_DIR}")
add_test(NAME scene_validator COMMAND test_scene_validator)

# Core wave 6, 1.17: the two validators agree (asset rule, damaged packs, fonts) and the semantic one carries the file-level checks
add_executable(test_core_validation test_core_validation.cpp)
target_link_libraries(test_core_validation PRIVATE okay::engine)
add_test(NAME core_validation COMMAND test_core_validation)

# 33.3 regression helpers: golden text/binary files, the scene digest, golden images
# (the software renderer drawn into memory and compared with tests/golden/regression/cube.png;
#  refresh it on purpose with OKAY_UPDATE_GOLDEN=1 test_regression).
add_executable(test_regression test_regression.cpp)
target_link_libraries(test_regression PRIVATE okay::engine)
target_compile_definitions(test_regression PRIVATE OKAY_SOURCE_DIR="${PROJECT_SOURCE_DIR}"
                                                  OKAY_TEST_OUTPUT_DIR="${CMAKE_CURRENT_BINARY_DIR}")
add_test(NAME regression COMMAND test_regression)

# 33.1 the command-line runner: okay-playtest runs every OKAY_PLAYTEST linked into it. This one
# carries the framework's own play-tests (tests/playtest/selftest_playtests.cpp); a game makes its
# own by linking its tests with okay::engine and calling okay::testing::RunMain from main().
add_executable(okay-playtest playtest/playtest_main.cpp playtest/selftest_playtests.cpp)
target_link_libraries(okay-playtest PRIVATE okay::engine)
target_compile_definitions(okay-playtest PRIVATE OKAY_SOURCE_DIR="${PROJECT_SOURCE_DIR}")
set_target_properties(okay-playtest PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
add_test(NAME playtest_runner COMMAND okay-playtest --junit ${CMAKE_CURRENT_BINARY_DIR}/okay-playtest-junit.xml
                                                      --text ${CMAKE_CURRENT_BINARY_DIR}/okay-playtest.txt)
add_test(NAME playtest_runner_list COMMAND okay-playtest --list)
set_tests_properties(playtest_runner_list PROPERTIES PASS_REGULAR_EXPRESSION "selftest\\.script_moves_object")

# 1.1 Loop and time (docs/loop_and_time.md): GameLoop, Time effects + clocks, Game pause reasons /
# quit guard, Random extras, Application on the shared loop. Deterministic: fake frame times,
# fixed seeds, no sleeping except the Application config checks (a few tens of milliseconds).
add_executable(test_game_loop test_game_loop.cpp)
target_link_libraries(test_game_loop PRIVATE okay::engine)
add_test(NAME game_loop COMMAND test_game_loop)

add_executable(test_clock test_clock.cpp)
target_link_libraries(test_clock PRIVATE okay::engine)
add_test(NAME clock COMMAND test_clock)

add_executable(test_time_extras test_time_extras.cpp)
target_link_libraries(test_time_extras PRIVATE okay::engine)
add_test(NAME time_extras COMMAND test_time_extras)

add_executable(test_game_state test_game_state.cpp)
target_link_libraries(test_game_state PRIVATE okay::engine)
add_test(NAME game_state COMMAND test_game_state)

add_executable(test_random_extras test_random_extras.cpp)
target_link_libraries(test_random_extras PRIVATE okay::engine)
add_test(NAME random_extras COMMAND test_random_extras)

# 1.1 fourth pass (docs/loop_and_time.md): Random / RandomExtras audit, SampleIndices, PoissonDisk2D, saving a bag or table
add_executable(test_random_wave4 test_random_wave4.cpp)
target_link_libraries(test_random_wave4 PRIVATE okay::engine)
add_test(NAME random_wave4 COMMAND test_random_wave4)

# 1.1 fifth pass: Random::Shared() is thread safe (std::thread tests; header-only, also built standalone under a thread sanitizer)
add_executable(test_random_threads test_random_threads.cpp)
target_link_libraries(test_random_threads PRIVATE okay::engine)
add_test(NAME random_threads COMMAND test_random_threads)

add_executable(test_application_loop test_application_loop.cpp)
target_link_libraries(test_application_loop PRIVATE okay::engine)
add_test(NAME application_loop COMMAND test_application_loop)
# ---- Prefab system wave 2 (docs/prefab_system.md): nested prefabs, variants, per-field apply / revert
add_executable(test_prefab_nested test_prefab_nested.cpp)
target_link_libraries(test_prefab_nested PRIVATE okay::engine)
add_test(NAME prefab_nested COMMAND test_prefab_nested)
add_executable(test_prefab_variants test_prefab_variants.cpp)
target_link_libraries(test_prefab_variants PRIVATE okay::engine)
add_test(NAME prefab_variants COMMAND test_prefab_variants)

add_executable(test_prefab_apply_revert test_prefab_apply_revert.cpp)
target_link_libraries(test_prefab_apply_revert PRIVATE okay::engine)
add_test(NAME prefab_apply_revert COMMAND test_prefab_apply_revert)
# Property reflection: values and text round trips, registry paths, change hook,
# snapshot / diff / copy, and a cross-check of every registered property against the scene serializer
add_executable(test_property_registry test_property_registry.cpp)
target_link_libraries(test_property_registry PRIVATE okay::engine)
add_test(NAME property_registry COMMAND test_property_registry)
# ---- Core runtime foundations (docs/core_runtime.md) ----
# Job system: pool, task graph, ParallelFor / ParallelReduce, cancellation, shutdown, main-thread queue.
# Takes an optional repeat count: `test_jobs 20` runs the threaded suites 20 times to shake out races.
add_executable(test_jobs test_jobs.cpp)
target_link_libraries(test_jobs PRIVATE okay::engine)
add_test(NAME jobs COMMAND test_jobs 3)

# FrameArena, PoolAllocator, SlotMap / Handle, StringId.
add_executable(test_arena_handles test_arena_handles.cpp)
target_link_libraries(test_arena_handles PRIVATE okay::engine)
add_test(NAME arena_handles COMMAND test_arena_handles)


# Frame delta clamp, frame timer and frame limiter.
add_executable(test_frame_pacing test_frame_pacing.cpp)
target_link_libraries(test_frame_pacing PRIVATE okay::engine)
add_test(NAME frame_pacing COMMAND test_frame_pacing)

# ---- Scene text tools for version control (docs/scene_diff_merge.md) ----------------
# Canonical form, structured diff, three-way merge and format migration. They share
# scene_text_testkit.hpp (seeded random scene generator, messy-layout perturbation, edit
# helpers). Property tests read OKAY_TEST_SEEDS to run over more random seeds.
add_executable(test_scene_canonical test_scene_canonical.cpp)
target_link_libraries(test_scene_canonical PRIVATE okay::engine)
add_test(NAME scene_canonical COMMAND test_scene_canonical)

add_executable(test_scene_diff test_scene_diff.cpp)
target_link_libraries(test_scene_diff PRIVATE okay::engine)
add_test(NAME scene_diff COMMAND test_scene_diff)

add_executable(test_scene_merge test_scene_merge.cpp)
target_link_libraries(test_scene_merge PRIVATE okay::engine)
add_test(NAME scene_merge COMMAND test_scene_merge)

add_executable(test_scene_migration test_scene_migration.cpp)
target_link_libraries(test_scene_migration PRIVATE okay::engine)
target_compile_definitions(test_scene_migration PRIVATE OKAY_SOURCE_DIR="${PROJECT_SOURCE_DIR}")
add_test(NAME scene_migration COMMAND test_scene_migration)

# 1.1 Loop and time, second pass: audit regressions, frame stepping, frame recorder / player, capture
# mode, frame statistics, the server tick pacer and the player-style loop harness. All deterministic:
# made-up frame times, no sleeping (the record tests write a few small files in the build directory).
add_executable(test_loop_audit2 test_loop_audit2.cpp)
target_link_libraries(test_loop_audit2 PRIVATE okay::engine)
add_test(NAME loop_audit2 COMMAND test_loop_audit2)

add_executable(test_frame_step test_frame_step.cpp)
target_link_libraries(test_frame_step PRIVATE okay::engine)
add_test(NAME frame_step COMMAND test_frame_step)

add_executable(test_capture_mode test_capture_mode.cpp)
target_link_libraries(test_capture_mode PRIVATE okay::engine)
add_test(NAME capture_mode COMMAND test_capture_mode)

add_executable(test_frame_record test_frame_record.cpp)
target_link_libraries(test_frame_record PRIVATE okay::engine)
add_test(NAME frame_record COMMAND test_frame_record)

add_executable(test_frame_stats test_frame_stats.cpp)
target_link_libraries(test_frame_stats PRIVATE okay::engine)
add_test(NAME frame_stats COMMAND test_frame_stats)

add_executable(test_tick_pacer test_tick_pacer.cpp)
target_link_libraries(test_tick_pacer PRIVATE okay::engine)
add_test(NAME tick_pacer COMMAND test_tick_pacer)

add_executable(test_loop_player_style test_loop_player_style.cpp)
target_link_libraries(test_loop_player_style PRIVATE okay::engine)
add_test(NAME loop_player_style COMMAND test_loop_player_style)

# 1.1 third pass (docs/loop_and_time.md): GameLoop / Game / Application edge cases, timers and clock
# ramps, the loop's save snapshot, frame-stat export and console commands.
add_executable(test_loop_audit3 test_loop_audit3.cpp)
target_link_libraries(test_loop_audit3 PRIVATE okay::engine)
add_test(NAME loop_audit3 COMMAND test_loop_audit3)

# 1.1 fourth pass: TogglePause, session clocks, RunFrames, debug scale presets, edge cases of Time / GameLoop / TickPacer / Utf8
add_executable(test_loop_audit4 test_loop_audit4.cpp)
target_link_libraries(test_loop_audit4 PRIVATE okay::engine)
add_test(NAME loop_audit4 COMMAND test_loop_audit4)

# 1.1 fifth pass: script access to the clocks, real FPS, TimeSnapshot ramps, replay pauses and seeds, console arguments
add_executable(test_loop_audit5 test_loop_audit5.cpp)
target_link_libraries(test_loop_audit5 PRIVATE okay::engine)
add_test(NAME loop_audit5 COMMAND test_loop_audit5)

add_executable(test_script_time test_script_time.cpp)
target_link_libraries(test_script_time PRIVATE okay::engine)
add_test(NAME script_time COMMAND test_script_time)

add_executable(test_frame_overlay test_frame_overlay.cpp)
target_link_libraries(test_frame_overlay PRIVATE okay::engine)
add_test(NAME frame_overlay COMMAND test_frame_overlay)

add_executable(test_countdown test_countdown.cpp)
target_link_libraries(test_countdown PRIVATE okay::engine)
add_test(NAME countdown COMMAND test_countdown)

add_executable(test_time_snapshot test_time_snapshot.cpp)
target_link_libraries(test_time_snapshot PRIVATE okay::engine)
add_test(NAME time_snapshot COMMAND test_time_snapshot)

add_executable(test_dev_log test_dev_log.cpp)
target_include_directories(test_dev_log PRIVATE ${CMAKE_SOURCE_DIR}/shared)
target_link_libraries(test_dev_log PRIVATE okay::engine)
add_test(NAME dev_log COMMAND test_dev_log)

add_executable(test_image_write test_image_write.cpp)
target_link_libraries(test_image_write PRIVATE okay::engine)
add_test(NAME image_write COMMAND test_image_write)

# Our own image decoder (Core/ImageDecode): PNG / JPEG / BMP / TGA / GIF / HDR / PSD fixtures vs reference pixels
# (tests/data/images), the inflater, and truncated / bit-flipped / absurd input.
add_executable(test_image_decoder test_image_decoder.cpp)
target_compile_definitions(test_image_decoder PRIVATE OKAY_SOURCE_DIR="${PROJECT_SOURCE_DIR}")
target_link_libraries(test_image_decoder PRIVATE okay::engine)
add_test(NAME image_decoder COMMAND test_image_decoder)
set_tests_properties(image_decoder PROPERTIES TIMEOUT 300)

# shared/FileDialog: our own open / save / folder dialogs (replaced the vendored tinyfiledialogs): path and glob helpers,
# argument lists for zenity / kdialog / osascript (no injection), result parsing, extension enforcing, the built-in browser's
# model and queue, and a run through fake helper programs on PATH. No window.
add_executable(test_file_dialog test_file_dialog.cpp ${PROJECT_SOURCE_DIR}/shared/FileDialog.cpp)
target_compile_features(test_file_dialog PRIVATE cxx_std_17)
target_include_directories(test_file_dialog PRIVATE ${PROJECT_SOURCE_DIR}/shared)
add_test(NAME file_dialog COMMAND test_file_dialog)

# Accessibility settings that change behaviour: reduced motion and flashing, colour filter on a software
# frame, text size layout, and the Settings screen controls that set and save them
add_executable(test_accessibility_wired test_accessibility_wired.cpp)
target_link_libraries(test_accessibility_wired PRIVATE okay::engine)
add_test(NAME accessibility_wired COMMAND test_accessibility_wired)
add_executable(test_accessibility_remaining test_accessibility_remaining.cpp)
target_link_libraries(test_accessibility_remaining PRIVATE okay::engine)
add_test(NAME accessibility_remaining COMMAND test_accessibility_remaining)
add_executable(test_ui_focus test_ui_focus.cpp)
target_link_libraries(test_ui_focus PRIVATE okay::engine)
add_test(NAME ui_focus COMMAND test_ui_focus)
# okay::platform (engine/include/okay/Platform): the seam over SDL2 and our own backends.
# platform: registry, timers, audio devices, controllers on each compiled-in backend (no
# display needed; SDL's dummy audio driver, ALSA's null device) + the own evdev mapping.
# platform_window_*: windows, GL clear colour, keyboard / mouse / text / clipboard on a real
# X server (Xvfb) with xdotool injecting the input. They skip (exit 77) without Xvfb / xdotool.
if(NOT WIN32 AND NOT EMSCRIPTEN)
    add_executable(test_platform test_platform.cpp)
    target_link_libraries(test_platform PRIVATE okay::platform)
    target_include_directories(test_platform PRIVATE ${CMAKE_SOURCE_DIR}/engine/src)
    if(TARGET SDL2::SDL2)
        target_link_libraries(test_platform PRIVATE SDL2::SDL2)   # the Key / SDL scancode numbering check
    endif()
    add_test(NAME platform COMMAND test_platform)
    # OKAY_PLATFORM_BACKEND in the environment overrides the build's default backend at run time.
    foreach(_okay_pb sdl2 own)
        add_test(NAME platform_default_env_${_okay_pb} COMMAND test_platform --print-default)
        set_tests_properties(platform_default_env_${_okay_pb} PROPERTIES
            ENVIRONMENT "OKAY_PLATFORM_BACKEND=${_okay_pb}" PASS_REGULAR_EXPRESSION "default=${_okay_pb}")
    endforeach()

    add_executable(test_platform_window test_platform_window.cpp)
    target_link_libraries(test_platform_window PRIVATE okay::platform)
    if(TARGET SDL2::SDL2)
        target_link_libraries(test_platform_window PRIVATE SDL2::SDL2)   # pushes touch / natural-scroll events through the sdl2 backend
    endif()
    target_include_directories(test_platform_window PRIVATE ${CMAKE_SOURCE_DIR}/engine/src)
    foreach(_okay_pb sdl2 own)
        add_test(NAME platform_window_${_okay_pb}
                 COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/run_under_xvfb.sh $<TARGET_FILE:test_platform_window> ${_okay_pb})
        set_tests_properties(platform_window_${_okay_pb} PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 120)
    endforeach()
endif()

# The software okay::platform::Painter (okayui/soft) on a real own-backend window under Xvfb: what it draws is what
# the X server holds. Own code, not ready, off by default; the test only checks it.
if(NOT WIN32 AND NOT EMSCRIPTEN AND TARGET okay_ui_soft AND CMAKE_SYSTEM_NAME STREQUAL "Linux")
    add_executable(test_platform_painter_window test_platform_painter_window.cpp)
    target_link_libraries(test_platform_painter_window PRIVATE okay_ui_soft)
    target_include_directories(test_platform_painter_window PRIVATE ${CMAKE_SOURCE_DIR}/engine/src)
    add_test(NAME platform_painter_window
             COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/run_under_xvfb.sh $<TARGET_FILE:test_platform_painter_window>)
    set_tests_properties(platform_painter_window PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 120)
endif()

# The controller -> Input bridge (Input/PlatformGamepad.hpp) with a fake okay::platform::Gamepad.
add_executable(test_platform_gamepad test_platform_gamepad.cpp)
target_link_libraries(test_platform_gamepad PRIVATE okay::engine)
add_test(NAME platform_gamepad COMMAND test_platform_gamepad)
# okay::crypto (our own, UNREVIEWED crypto primitives; docs/crypto.md): RFC known-answer vectors,
# tamper tests, edge cases. tests/crypto_rfc_vectors.hpp is extracted from the RFC text.
add_executable(test_crypto test_crypto.cpp)
target_link_libraries(test_crypto PRIVATE okay::crypto)
add_test(NAME crypto COMMAND test_crypto)

# The embedded fonts in our own OKF1 container (shared/OkfDecode.hpp): decoded bytes must equal the original TrueType files
# (pinned by size and SHA-256 in assets/fonts/embedded_fonts.sha256), the decoder's every token form and damaged input.
# EmbeddedFont.cpp is compiled in directly, so this needs only okay::crypto (for SHA-256), not the engine.
add_executable(test_embedded_font test_embedded_font.cpp ${CMAKE_SOURCE_DIR}/engine/src/Graphics/EmbeddedFont.cpp)
target_link_libraries(test_embedded_font PRIVATE okay::crypto)
target_include_directories(test_embedded_font PRIVATE ${CMAKE_SOURCE_DIR}/shared ${CMAKE_SOURCE_DIR}/engine/include)
target_compile_definitions(test_embedded_font PRIVATE OKAY_SOURCE_DIR="${PROJECT_SOURCE_DIR}")
add_test(NAME embedded_font COMMAND test_embedded_font)
add_test(NAME embedded_font_generator COMMAND python3 ${PROJECT_SOURCE_DIR}/scripts/gen_embedded_fonts.py --check)

# The internal arithmetic: the 51-bit and the portable 16-bit field against each other, the Barrett scalar
# reduction against a bit-by-bit reference (includes engine/src/Crypto headers; needs __int128).
add_executable(test_crypto_field test_crypto_field.cpp)
target_include_directories(test_crypto_field PRIVATE ${CMAKE_SOURCE_DIR}/engine/include)
add_test(NAME crypto_field COMMAND test_crypto_field)
# The whole primitive test again with the portable 16-bit field forced on (what MSVC and WebAssembly use),
# so the fallback cannot rot: the same sources compiled with OKAY_CRYPTO_FE16.
add_executable(test_crypto_fe16 test_crypto.cpp
    ${CMAKE_SOURCE_DIR}/engine/src/Crypto/Util.cpp ${CMAKE_SOURCE_DIR}/engine/src/Crypto/Random.cpp
    ${CMAKE_SOURCE_DIR}/engine/src/Crypto/ChaCha20.cpp ${CMAKE_SOURCE_DIR}/engine/src/Crypto/Poly1305.cpp
    ${CMAKE_SOURCE_DIR}/engine/src/Crypto/Aead.cpp ${CMAKE_SOURCE_DIR}/engine/src/Crypto/Blake2b.cpp
    ${CMAKE_SOURCE_DIR}/engine/src/Crypto/Sha256.cpp ${CMAKE_SOURCE_DIR}/engine/src/Crypto/Sha512.cpp
    ${CMAKE_SOURCE_DIR}/engine/src/Crypto/X25519.cpp ${CMAKE_SOURCE_DIR}/engine/src/Crypto/Ed25519.cpp)
target_include_directories(test_crypto_fe16 PRIVATE ${CMAKE_SOURCE_DIR}/engine/include)
target_compile_definitions(test_crypto_fe16 PRIVATE OKAY_CRYPTO_FE16)
if(WIN32)
    target_link_libraries(test_crypto_fe16 PRIVATE bcrypt)
endif()
add_test(NAME crypto_fe16 COMMAND test_crypto_fe16)

# Constant-time audit 1 of 2: valgrind marks every secret undefined and reports any branch or address that
# depends on one (docs/crypto_review_package.md section 4). Built only when the valgrind headers exist; the
# ctest runs it under valgrind and is skipped when valgrind itself is missing.
find_path(OKAY_VALGRIND_INCLUDE valgrind/memcheck.h)
find_program(OKAY_VALGRIND valgrind)
if(OKAY_VALGRIND_INCLUDE AND NOT WIN32)
    set(_okay_ct_srcs "")
    foreach(_f IN ITEMS Util Random ChaCha20 Poly1305 Aead Blake2b Sha256 Sha512 X25519 Ed25519)
        list(APPEND _okay_ct_srcs ${CMAKE_SOURCE_DIR}/engine/src/Crypto/${_f}.cpp)
    endforeach()
    add_executable(crypto_ct_valgrind crypto_ct_valgrind.cpp ${_okay_ct_srcs})
    target_include_directories(crypto_ct_valgrind PRIVATE ${CMAKE_SOURCE_DIR}/engine/include ${OKAY_VALGRIND_INCLUDE})
    target_compile_definitions(crypto_ct_valgrind PRIVATE OKAY_CRYPTO_CTGRIND)
    target_compile_options(crypto_ct_valgrind PRIVATE -g)
    if(OKAY_VALGRIND)
        add_test(NAME crypto_ct_valgrind COMMAND ${OKAY_VALGRIND} -q $<TARGET_FILE:crypto_ct_valgrind>)
    endif()
endif()

# Fuzz target. Default: a built-in mutator drives it for a few hundred iterations (a quick
# smoke test; run `fuzz_crypto_smoke 100000` under -DOKAY_SANITIZE=address,undefined for more, or
# scripts/crypto_fuzz_sanitized.sh).
# -DOKAY_CRYPTO_LIBFUZZER=ON (clang) builds the real libFuzzer binaries `fuzz_crypto` and `fuzz_net_crypto` instead.
option(OKAY_CRYPTO_LIBFUZZER "Build the okay::crypto libFuzzer target (clang -fsanitize=fuzzer)" OFF)
if(OKAY_CRYPTO_LIBFUZZER)
    add_executable(fuzz_crypto fuzz_crypto.cpp)
    target_link_libraries(fuzz_crypto PRIVATE okay::crypto)
    target_compile_definitions(fuzz_crypto PRIVATE OKAY_CRYPTO_LIBFUZZER)
    target_compile_options(fuzz_crypto PRIVATE -fsanitize=fuzzer-no-link,address,undefined)
    target_link_options(fuzz_crypto PRIVATE -fsanitize=fuzzer,address,undefined)
else()
    add_executable(fuzz_crypto_smoke fuzz_crypto.cpp)
    target_link_libraries(fuzz_crypto_smoke PRIVATE okay::crypto)
    add_test(NAME crypto_fuzz_smoke COMMAND fuzz_crypto_smoke 300)
endif()
# Fuzz target for what parses attacker bytes around the primitives: key exchange, sealed-message open and the replay
# window, session tickets, mutated valid signatures and AEAD messages (fuzz_net_crypto.cpp). Compiled against okay::crypto
# alone (SessionTicket.cpp and the header-only SecureChannel are built in), so it needs neither libsodium nor the engine.
set(_okay_fuzznet_defs OKAY_OWN_CRYPTO OKAY_NET_CRYPTO)
if(OKAY_CRYPTO_LIBFUZZER)
    add_executable(fuzz_net_crypto fuzz_net_crypto.cpp ${CMAKE_SOURCE_DIR}/engine/src/Net/SessionTicket.cpp)
    target_link_libraries(fuzz_net_crypto PRIVATE okay::crypto)
    target_compile_definitions(fuzz_net_crypto PRIVATE OKAY_CRYPTO_LIBFUZZER ${_okay_fuzznet_defs})
    target_compile_options(fuzz_net_crypto PRIVATE -fsanitize=fuzzer-no-link,address,undefined)
    target_link_options(fuzz_net_crypto PRIVATE -fsanitize=fuzzer,address,undefined)
else()
    add_executable(fuzz_net_crypto_smoke fuzz_net_crypto.cpp ${CMAKE_SOURCE_DIR}/engine/src/Net/SessionTicket.cpp)
    target_link_libraries(fuzz_net_crypto_smoke PRIVATE okay::crypto)
    target_compile_definitions(fuzz_net_crypto_smoke PRIVATE ${_okay_fuzznet_defs})
    add_test(NAME crypto_fuzz_net_smoke COMMAND fuzz_net_crypto_smoke 300)
endif()

# Project Wycheproof vectors (Apache-2.0, copied unmodified into tests/wycheproof/): every X25519, Ed25519, ChaCha20-Poly1305,
# XChaCha20-Poly1305, HMAC (built on our SHA-2), HKDF and ChaCha20 counter vector. OKAY_WYCHEPROOF_DIR points the test at another
# copy (e.g. a fresh clone's testvectors_v1/). With libsodium installed (found below) every X25519 and Ed25519 vector is also
# compared with it.
add_executable(test_crypto_wycheproof test_crypto_wycheproof.cpp)
target_link_libraries(test_crypto_wycheproof PRIVATE okay::crypto)
target_compile_definitions(test_crypto_wycheproof PRIVATE OKAY_WYCHEPROOF_DEFAULT_DIR="${CMAKE_CURRENT_SOURCE_DIR}/wycheproof")
add_test(NAME crypto_wycheproof COMMAND test_crypto_wycheproof)

# Cross-checks against other implementations (both directions, thousands of random cases).
# 1) libsodium, in process (only when it is installed; independent of OKAY_USE_SODIUM / OKAY_OWN_CRYPTO).
find_package(PkgConfig QUIET)
set(_okay_xc_sodium FALSE)
if(PkgConfig_FOUND AND NOT CMAKE_CROSSCOMPILING)   # (a cross build would find the HOST's libsodium)
    pkg_check_modules(OKAY_XC_SODIUM QUIET libsodium)
    if(OKAY_XC_SODIUM_FOUND)
        set(_okay_xc_sodium TRUE)
    endif()
endif()
if(_okay_xc_sodium)
    target_link_libraries(test_crypto_wycheproof PRIVATE ${OKAY_XC_SODIUM_LIBRARIES})
    target_include_directories(test_crypto_wycheproof SYSTEM PRIVATE ${OKAY_XC_SODIUM_INCLUDE_DIRS})
    target_compile_definitions(test_crypto_wycheproof PRIVATE OKAY_WY_SODIUM)
    add_executable(test_crypto_sodium_xcheck test_crypto_sodium_xcheck.cpp)
    target_link_libraries(test_crypto_sodium_xcheck PRIVATE okay::crypto ${OKAY_XC_SODIUM_LIBRARIES})
    target_include_directories(test_crypto_sodium_xcheck SYSTEM PRIVATE ${OKAY_XC_SODIUM_INCLUDE_DIRS})
    add_test(NAME crypto_sodium_xcheck COMMAND test_crypto_sodium_xcheck)
    # The large differential test (libsodium and the TweetNaCl-derived port; structured edge-case inputs). The ctest is a smoke run;
    # `OKAY_DIFF_N=1000000 build/tests/crypto_difftest` is the ten-minute run (see docs/crypto_review_package.md).
    add_executable(crypto_difftest crypto_difftest.cpp)
    target_link_libraries(crypto_difftest PRIVATE okay::crypto ${OKAY_XC_SODIUM_LIBRARIES})
    target_include_directories(crypto_difftest SYSTEM PRIVATE ${OKAY_XC_SODIUM_INCLUDE_DIRS})
    target_include_directories(crypto_difftest PRIVATE ${CMAKE_SOURCE_DIR}/shared ${CMAKE_SOURCE_DIR}/engine/include)
    add_test(NAME crypto_difftest_smoke COMMAND crypto_difftest 1500)
endif()
# 2) Python `cryptography` (OpenSSL) and hashlib, through a line-protocol front end to our code.
add_executable(crypto_xcheck_tool crypto_xcheck_tool.cpp)
target_link_libraries(crypto_xcheck_tool PRIVATE okay::crypto)
set(_okay_xc_python "")
foreach(_py IN ITEMS python3 /usr/bin/python3 python)
    if(NOT _okay_xc_python)
        execute_process(COMMAND ${_py} -c "from cryptography.hazmat.primitives.ciphers.aead import ChaCha20Poly1305; from cryptography.hazmat.primitives.asymmetric.ed25519 import Ed25519PrivateKey"
                        RESULT_VARIABLE _rc OUTPUT_QUIET ERROR_QUIET)
        if(_rc EQUAL 0)
            set(_okay_xc_python ${_py})
        endif()
    endif()
endforeach()
if(_okay_xc_python)
    add_test(NAME crypto_python_xcheck
             COMMAND ${_okay_xc_python} ${CMAKE_CURRENT_SOURCE_DIR}/scripts/crypto_xcheck.py $<TARGET_FILE:crypto_xcheck_tool> 1500)
else()
    message(STATUS "OkaySpace tests: python 'cryptography' not found; crypto_python_xcheck skipped")
endif()

# Benchmark (not a test): microseconds per operation, next to libsodium when it is installed.
add_executable(bench_crypto bench_crypto.cpp)
target_link_libraries(bench_crypto PRIVATE okay::crypto)
if(_okay_xc_sodium)
    target_link_libraries(bench_crypto PRIVATE ${OKAY_XC_SODIUM_LIBRARIES})
    target_include_directories(bench_crypto SYSTEM PRIVATE ${OKAY_XC_SODIUM_INCLUDE_DIRS})
    target_compile_definitions(bench_crypto PRIVATE OKAY_BENCH_SODIUM)
endif()

# Constant-time audit 2 of 2: a fixed-versus-random statistical timing test. NOT run by ctest at full length (it needs
# a quiet machine and millions of measurements); the ctest is a short smoke run that only checks that the
# harness runs. See the header of crypto_timing.cpp and docs/crypto_review_package.md section 4.
add_executable(crypto_timing crypto_timing.cpp)
target_link_libraries(crypto_timing PRIVATE okay::crypto)
add_test(NAME crypto_timing_smoke COMMAND crypto_timing 300 X25519Base)
if(_okay_xc_sodium)   # reference rows: libsodium under the same harness, to tell CPU/measurement effects from ours
    target_link_libraries(crypto_timing PRIVATE ${OKAY_XC_SODIUM_LIBRARIES})
    target_include_directories(crypto_timing SYSTEM PRIVATE ${OKAY_XC_SODIUM_INCLUDE_DIRS})
    target_compile_definitions(crypto_timing PRIVATE OKAY_TIMING_SODIUM)
endif()

# The existing SecureChannel test, compiled once per backend IN THE SAME BUILD (tests/test_net_crypto_*.cpp
# rename the class so the two copies never meet at link time). With -DOKAY_OWN_CRYPTO=ON the engine's
# own net code (net_*, net_auth, dedicated_server, ...) runs on okay::crypto as well.
add_executable(test_net_crypto_own test_net_crypto_own.cpp)
target_link_libraries(test_net_crypto_own PRIVATE okay::crypto)
add_test(NAME net_crypto_own COMMAND test_net_crypto_own)
if(_okay_xc_sodium)
    add_executable(test_net_crypto_sodium test_net_crypto_sodium.cpp)
    target_link_libraries(test_net_crypto_sodium PRIVATE okay::crypto ${OKAY_XC_SODIUM_LIBRARIES})
    target_include_directories(test_net_crypto_sodium SYSTEM PRIVATE ${OKAY_XC_SODIUM_INCLUDE_DIRS})
    add_test(NAME net_crypto_sodium COMMAND test_net_crypto_sodium)
endif()

# Our own model readers (OBJ+MTL, STL, PLY) + the import seam: fixtures in tests/data/models,
# malformed input, a fuzz sweep, the backend switch, and (with OKAY_USE_ASSIMP) both backends compared.
add_executable(test_model_readers test_model_readers.cpp)
target_link_libraries(test_model_readers PRIVATE okay::engine)
target_compile_definitions(test_model_readers PRIVATE OKAY_SOURCE_DIR="${PROJECT_SOURCE_DIR}")
add_test(NAME model_readers COMMAND test_model_readers WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})
set_tests_properties(model_readers PROPERTIES TIMEOUT 300)
add_executable(test_collada_reader test_collada_reader.cpp)
target_link_libraries(test_collada_reader PRIVATE okay::engine)
target_compile_definitions(test_collada_reader PRIVATE OKAY_SOURCE_DIR="${PROJECT_SOURCE_DIR}")
add_test(NAME collada_reader COMMAND test_collada_reader WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})

add_executable(test_model_readers_fuzz test_model_readers_fuzz.cpp)
target_link_libraries(test_model_readers_fuzz PRIVATE okay::engine)
target_compile_definitions(test_model_readers_fuzz PRIVATE OKAY_SOURCE_DIR="${PROJECT_SOURCE_DIR}")
add_test(NAME model_readers_fuzz COMMAND test_model_readers_fuzz WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})
set_tests_properties(model_readers_fuzz PROPERTIES TIMEOUT 300)

# "Download my data" / "Delete my account": client request building, confirmation
# gating, export assembly, error mapping (engine/include/okay/Platform/Account/AccountData.hpp).
add_executable(test_account_data test_account_data.cpp)
target_compile_features(test_account_data PRIVATE cxx_std_17)
target_include_directories(test_account_data PRIVATE ${CMAKE_SOURCE_DIR}/engine/include)
add_test(NAME account_data COMMAND test_account_data)

# The delete-account Edge Function's logic against a fake client (needs Node 22+;
# skipped without it). Same file also runs under Deno.
find_program(OKAY_NODE node)
if(OKAY_NODE)
    add_test(NAME delete_account_function
             COMMAND ${OKAY_NODE} --experimental-strip-types --test
                     ${CMAKE_SOURCE_DIR}/supabase/functions/delete-account/delete_account_test.ts)
endif()

# Our own FBX reader (okay::fbx), side by side with ufbx: fixtures from scripts/gen_fbx_fixtures.py
add_executable(test_fbx_reader test_fbx_reader.cpp)
target_link_libraries(test_fbx_reader PRIVATE okay::engine)
target_compile_definitions(test_fbx_reader PRIVATE OKAY_SOURCE_DIR="${PROJECT_SOURCE_DIR}")
add_test(NAME fbx_reader COMMAND test_fbx_reader)

# Hostile FBX files through the importer (ufbx default path and our reader): deep node chains, animation counts, the whole-file bake budget.
add_executable(test_fbx_import_limits test_fbx_import_limits.cpp)
target_link_libraries(test_fbx_import_limits PRIVATE okay::engine)
add_test(NAME fbx_import_limits COMMAND test_fbx_import_limits)

# Hostile-input tests for okay::fbx. Standalone (no engine library) so it can run under sanitizers:
#   cmake -DOKAY_SANITIZE=address,undefined ...   (CI's sanitizer job), OKAY_FBX_FUZZ_ITERS=20000 for a longer fuzz.
add_executable(test_fbx_hostile test_fbx_hostile.cpp
    ${PROJECT_SOURCE_DIR}/engine/src/Render/FbxReader.cpp
    ${PROJECT_SOURCE_DIR}/engine/src/Render/FbxScene.cpp)
target_include_directories(test_fbx_hostile PRIVATE ${PROJECT_SOURCE_DIR}/engine/include)
target_compile_definitions(test_fbx_hostile PRIVATE OKAY_SOURCE_DIR="${PROJECT_SOURCE_DIR}")
add_test(NAME fbx_hostile COMMAND test_fbx_hostile)

# The editor's layout maths (editor/EditorLayoutMath.hpp, UI-library free): overlay anchoring, label placement and crowd fading, Inspector density, toolbar planning, maximize bookkeeping, view preferences.
add_executable(test_editor_layout test_editor_layout.cpp)
target_include_directories(test_editor_layout PRIVATE ${CMAKE_SOURCE_DIR}/editor)
add_test(NAME editor_layout COMMAND test_editor_layout)

add_executable(test_net_reconnect test_net_reconnect.cpp)
target_link_libraries(test_net_reconnect PRIVATE okay::engine)
add_test(NAME net_reconnect COMMAND test_net_reconnect)

add_executable(test_net_respawn_anim test_net_respawn_anim.cpp)
target_link_libraries(test_net_respawn_anim PRIVATE okay::engine)
add_test(NAME net_respawn_anim COMMAND test_net_respawn_anim)

add_executable(test_net_lossy test_net_lossy.cpp)
target_link_libraries(test_net_lossy PRIVATE okay::engine)
add_test(NAME net_lossy COMMAND test_net_lossy)

add_executable(test_net_fuzz test_net_fuzz.cpp)
target_link_libraries(test_net_fuzz PRIVATE okay::engine)
add_test(NAME net_fuzz COMMAND test_net_fuzz)

add_executable(test_net_rejoin test_net_rejoin.cpp)
target_link_libraries(test_net_rejoin PRIVATE okay::engine)
add_test(NAME net_rejoin COMMAND test_net_rejoin)


add_executable(test_bughunt_round3 test_bughunt_round3.cpp)
target_link_libraries(test_bughunt_round3 PRIVATE okay::engine)
add_test(NAME bughunt_round3 COMMAND test_bughunt_round3)

# Our own 3DS and BVH readers (okay/Render/ModelData.hpp): fixtures from scripts/gen_3ds_fixtures.py and
# scripts/gen_bvh_fixtures.py in tests/data/models, checked by hand-computed values and through the import seam.
add_executable(test_3ds_reader test_3ds_reader.cpp)
target_link_libraries(test_3ds_reader PRIVATE okay::engine)
target_compile_definitions(test_3ds_reader PRIVATE OKAY_SOURCE_DIR="${PROJECT_SOURCE_DIR}")
add_test(NAME 3ds_reader COMMAND test_3ds_reader WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})
add_executable(test_bvh_reader test_bvh_reader.cpp)
target_link_libraries(test_bvh_reader PRIVATE okay::engine)
target_compile_definitions(test_bvh_reader PRIVATE OKAY_SOURCE_DIR="${PROJECT_SOURCE_DIR}")
add_test(NAME bvh_reader COMMAND test_bvh_reader WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})
# Hostile input + mutation fuzz for the 3DS and BVH readers. Standalone (no engine library, only the two reader files)
# so it can also be built by hand under -fsanitize=address,undefined,float-cast-overflow; OKAY_3DS_FUZZ_ITERS raises the sweep.
add_executable(test_3ds_bvh_hostile test_3ds_bvh_hostile.cpp
    ${PROJECT_SOURCE_DIR}/engine/src/Render/Max3dsReader.cpp
    ${PROJECT_SOURCE_DIR}/engine/src/Render/BvhReader.cpp)
target_include_directories(test_3ds_bvh_hostile PRIVATE ${PROJECT_SOURCE_DIR}/engine/include)
target_compile_definitions(test_3ds_bvh_hostile PRIVATE OKAY_SOURCE_DIR="${PROJECT_SOURCE_DIR}")
add_test(NAME 3ds_bvh_hostile COMMAND test_3ds_bvh_hostile WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})
set_tests_properties(3ds_bvh_hostile PROPERTIES TIMEOUT 300)

# Real-file corpus check for the own FBX / COLLADA / 3DS / BVH readers. Needs OKAY_MODEL_CORPUS (a directory of
# real exporter files, not in this repo; docs/model_corpus.md); without it the test prints "skipped" and passes.
add_executable(test_model_corpus test_model_corpus.cpp)
target_link_libraries(test_model_corpus PRIVATE okay::engine)
target_compile_definitions(test_model_corpus PRIVATE OKAY_SOURCE_DIR="${PROJECT_SOURCE_DIR}")
add_test(NAME model_corpus COMMAND test_model_corpus WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})
set_tests_properties(model_corpus PROPERTIES TIMEOUT 600)

# Game clipboard: sanitize, input-field copy/paste, text-script and visual-script nodes.
add_executable(test_clipboard test_clipboard.cpp)
target_link_libraries(test_clipboard PRIVATE okay::engine)
target_include_directories(test_clipboard PRIVATE ${CMAKE_SOURCE_DIR}/editor)
target_compile_definitions(test_clipboard PRIVATE OKAY_SOURCE_DIR="${PROJECT_SOURCE_DIR}")
add_test(NAME clipboard COMMAND test_clipboard)

# Our own HTTP/1.1 client (docs/own_http_client.md). The pure layer (parser, builder, redirects) is tested and
# fuzzed over in-memory streams; the transports (TCP, CONNECT proxy, OpenSSL loaded with dlopen) are tested against
# local servers, `openssl s_server` and throw-away certificates from tests/scripts/gen_http_test_certs.sh.
# Header-only: neither links the engine. The TLS cases skip (and say so) when libssl or the openssl program is missing.
add_executable(test_own_http test_own_http.cpp)
target_include_directories(test_own_http PRIVATE ${CMAKE_SOURCE_DIR}/engine/include)
add_test(NAME own_http COMMAND test_own_http)
if(UNIX AND NOT APPLE)
    add_executable(test_own_http_net test_own_http_net.cpp)
    target_include_directories(test_own_http_net PRIVATE ${CMAKE_SOURCE_DIR}/engine/include)
    target_link_libraries(test_own_http_net PRIVATE ${CMAKE_DL_LIBS} Threads::Threads)
    add_test(NAME own_http_net COMMAND test_own_http_net ${CMAKE_CURRENT_SOURCE_DIR}/scripts/gen_http_test_certs.sh ${CMAKE_SOURCE_DIR})
    set_tests_properties(own_http_net PROPERTIES TIMEOUT 240 ENVIRONMENT "ASAN_OPTIONS=detect_leaks=0")
endif()
