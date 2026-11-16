# Small developer tools. Off by default; enable with -DOKAY_BUILD_TOOLS=ON.
# gen_textures regenerates the procedural starter texture pack in assets/textures/.
add_executable(okay_gen_textures gen_textures.cpp)
target_link_libraries(okay_gen_textures PRIVATE okay::engine)
set_target_properties(okay_gen_textures PROPERTIES OUTPUT_NAME gen_textures)
# guided_pictures renders the Guided modeling pictures headlessly (docs/images/guided_modeling/) and writes the shipped examples.
add_executable(guided_pictures guided_pictures.cpp)
target_link_libraries(guided_pictures PRIVATE okay::engine)
