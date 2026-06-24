# OkaySpace reference relay — a small console UDP forwarder for NAT traversal.
# No SDL/graphics deps; links only the engine's networking core.
add_executable(okayspace-relay main.cpp)
target_compile_features(okayspace-relay PRIVATE cxx_std_17)
target_link_libraries(okayspace-relay PRIVATE okay::engine)
set_target_properties(okayspace-relay PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
