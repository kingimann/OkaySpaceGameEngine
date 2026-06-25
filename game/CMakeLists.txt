# OkaySurvival — generator for the standalone survival game's scene/config files.
# A console tool (no graphics deps); run it to emit game.okayscene + game.okayconfig,
# then ship them beside the player runtime.
add_executable(okaysurvival_gen okaysurvival_gen.cpp)
target_link_libraries(okaysurvival_gen PRIVATE okay::engine)
