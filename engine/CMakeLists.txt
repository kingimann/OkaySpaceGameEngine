# OkaySpace engine library
add_library(okay_engine
    src/Core/Application.cpp
    src/Input/Input.cpp
    src/Render/ConsoleRenderer.cpp
    src/Scene/Component.cpp
    src/Scene/GameObject.cpp
    src/Scene/Transform.cpp
    src/Scene/Scene.cpp
    src/Components/Camera.cpp
    src/Components/SpriteRenderer.cpp
)

# Public headers live under engine/include; consumers use #include <Okay.hpp>.
target_include_directories(okay_engine PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)

target_compile_features(okay_engine PUBLIC cxx_std_17)

find_package(Threads REQUIRED)
target_link_libraries(okay_engine PUBLIC Threads::Threads)

# Friendly alias so subprojects can link okay::engine.
add_library(okay::engine ALIAS okay_engine)
