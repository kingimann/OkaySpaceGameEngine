# Small developer tools. Off by default; enable with -DOKAY_BUILD_TOOLS=ON.
# gen_textures regenerates the procedural starter texture pack in assets/textures/.
add_executable(okay_gen_textures gen_textures.cpp)
target_link_libraries(okay_gen_textures PRIVATE okay::engine)
set_target_properties(okay_gen_textures PROPERTIES OUTPUT_NAME gen_textures)

# The launcher's version question drawn with its real drawing code and look (launcher/OpenProjectUI.cpp), headless (SDL's software renderer on an
# offscreen surface), for the pictures in docs/images/launcher_open/. Needs the launcher's ImGui (so only when the launcher is configured).
#   SDL_VIDEODRIVER=dummy okay_launcher_open_shot older out.bmp        (cases: older, older-missing, unversioned, newer, newer-open-with)
if(TARGET launcher_imgui)
    find_package(Threads REQUIRED)
    add_executable(okay_launcher_open_shot launcher_open_shot.cpp
        ${CMAKE_SOURCE_DIR}/launcher/OpenProjectUI.cpp
        ${CMAKE_SOURCE_DIR}/launcher/VersionsManager.cpp)   # (OpenProjectGate.hpp reuses ChooseEditorForOpen from it)
    target_compile_features(okay_launcher_open_shot PRIVATE cxx_std_17)
    target_include_directories(okay_launcher_open_shot PRIVATE
        ${CMAKE_SOURCE_DIR}/launcher ${CMAKE_SOURCE_DIR}/shared ${CMAKE_SOURCE_DIR}/engine/include)
    target_link_libraries(okay_launcher_open_shot PRIVATE launcher_imgui Threads::Threads okay::crypto)   # (launcher_imgui brings Dear ImGui, its SDL renderer backend and SDL2)
    set_target_properties(okay_launcher_open_shot PROPERTIES OUTPUT_NAME launcher_open_shot)
endif()
