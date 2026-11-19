# okay-platform-selftest: opt-in check program for the own okay::platform backend (OFF by default).
# Uses only okay::platform; see docs/try_it/11-own-platform-layer-window-input-audio-de.md.
add_executable(okay-platform-selftest main.cpp)
target_link_libraries(okay-platform-selftest PRIVATE okay::platform)
target_include_directories(okay-platform-selftest PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(okay-platform-selftest PRIVATE cxx_std_17)
set_target_properties(okay-platform-selftest PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
