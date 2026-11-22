# okay-texture: command line front end of okay::tex (docs/rendering/own_texture_library.md). Always built (no graphics deps);
# Windows builds ship it as Tools\okay-texture.exe.
add_executable(okay-texture main.cpp)
okay_win_version(okay-texture "OkaySpace Texture Tool" "okay-texture.exe")
target_compile_features(okay-texture PRIVATE cxx_std_17)
target_link_libraries(okay-texture PRIVATE okay::engine)
set_target_properties(okay-texture PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
