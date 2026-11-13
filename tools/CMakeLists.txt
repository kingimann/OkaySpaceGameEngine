# Small developer tools. Off by default; enable with -DOKAY_BUILD_TOOLS=ON.
# gen_textures regenerates the procedural starter texture pack in assets/textures/.
add_executable(okay_gen_textures gen_textures.cpp)
target_link_libraries(okay_gen_textures PRIVATE okay::engine)
set_target_properties(okay_gen_textures PROPERTIES OUTPUT_NAME gen_textures)

# okay_ac_tool: the anti-cheat build tool (code baseline after the link, signing the module rules). See docs/anticheat.md.
add_executable(okay_ac_tool okay_ac_tool.cpp)
target_include_directories(okay_ac_tool PRIVATE ${CMAKE_SOURCE_DIR}/shared ${CMAKE_SOURCE_DIR}/engine/include)
