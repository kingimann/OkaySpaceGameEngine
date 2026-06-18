add_executable(okayspace-launcher main.cpp)
target_compile_features(okayspace-launcher PRIVATE cxx_std_17)
set_target_properties(okayspace-launcher PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
