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
#include "okay/Core/Random.hpp"
#include "okay/Core/EventBus.hpp"
#include "okay/Core/Scheduler.hpp"
#include "okay/Core/Application.hpp"

// Input
#include "okay/Input/Input.hpp"

// Rendering
#include "okay/Render/Color.hpp"
#include "okay/Render/Renderer.hpp"
#include "okay/Render/ConsoleRenderer.hpp"
#include "okay/Render/Mesh.hpp"

// Scene graph
#include "okay/Scene/Component.hpp"
#include "okay/Scene/Transform.hpp"
#include "okay/Scene/GameObject.hpp"
#include "okay/Scene/Scene.hpp"
#include "okay/Scene/SceneSerializer.hpp"

// Built-in components
#include "okay/Components/Camera.hpp"
#include "okay/Components/SpriteRenderer.hpp"
#include "okay/Components/MeshRenderer.hpp"

// 2D physics
#include "okay/Physics/Rigidbody2D.hpp"
#include "okay/Physics/Collider2D.hpp"
#include "okay/Physics/Physics2D.hpp"

// Animation
#include "okay/Animation/AnimationCurve.hpp"
#include "okay/Animation/AnimationClip.hpp"
#include "okay/Components/Animator.hpp"

// Multiplayer networking
#include "okay/Net/Packet.hpp"
#include "okay/Net/UdpSocket.hpp"
#include "okay/Net/NetworkManager.hpp"

// Visual scripting (node graphs)
#include "okay/VisualScript/VsValue.hpp"
#include "okay/VisualScript/NodeGraph.hpp"
#include "okay/VisualScript/Nodes.hpp"
#include "okay/Components/VisualScriptComponent.hpp"

// Text scripting (OkayScript built-in; Lua / C# optional)
#include "okay/Scripting/ScriptVM.hpp"
#include "okay/Scripting/OkayScriptVM.hpp"
#include "okay/Components/ScriptComponent.hpp"

// Steam platform integration (simulation backend by default)
#include "okay/Platform/Steam/SteamService.hpp"
#include "okay/Components/SteamManager.hpp"

// PlayFab LiveOps integration (simulation backend by default)
#include "okay/Platform/PlayFab/PlayFabService.hpp"
#include "okay/Components/PlayFabManager.hpp"
