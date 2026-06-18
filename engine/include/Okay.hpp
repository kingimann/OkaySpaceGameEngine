#pragma once
/// OkaySpaceGameEngine — single public header.
/// Include this to pull in the whole engine API.
///
///     #include <Okay.hpp>
///     using namespace okay;

// Math
#include "okay/Math/Math.hpp"

// Core
#include "okay/Core/Log.hpp"
#include "okay/Core/Time.hpp"
#include "okay/Core/Application.hpp"

// Input
#include "okay/Input/Input.hpp"

// Rendering
#include "okay/Render/Color.hpp"
#include "okay/Render/Renderer.hpp"
#include "okay/Render/ConsoleRenderer.hpp"

// Scene graph
#include "okay/Scene/Component.hpp"
#include "okay/Scene/Transform.hpp"
#include "okay/Scene/GameObject.hpp"
#include "okay/Scene/Scene.hpp"

// Built-in components
#include "okay/Components/Camera.hpp"
#include "okay/Components/SpriteRenderer.hpp"
