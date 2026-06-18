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
#include "okay/Core/Prefs.hpp"
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

// Graphics assets
#include "okay/Graphics/Image.hpp"
#include "okay/Graphics/Font.hpp"

// Built-in components
#include "okay/Components/Camera.hpp"
#include "okay/Components/SpriteRenderer.hpp"
#include "okay/Components/SpriteAnimator.hpp"
#include "okay/Components/MeshRenderer.hpp"
#include "okay/Components/TextRenderer.hpp"

// 2D physics
#include "okay/Physics/Rigidbody2D.hpp"
#include "okay/Physics/Collider2D.hpp"
#include "okay/Physics/Physics2D.hpp"

// Animation
#include "okay/Animation/AnimationCurve.hpp"
#include "okay/Animation/AnimationClip.hpp"
#include "okay/Components/Animator.hpp"

// More components
#include "okay/Components/ParticleSystem.hpp"
#include "okay/Components/Tilemap.hpp"
#include "okay/Components/TilemapCollider2D.hpp"

// In-game UI
#include "okay/Components/UIButton.hpp"
#include "okay/Components/UIPanel.hpp"
#include "okay/Components/UIProgressBar.hpp"

// Gameplay behaviours (no-scripting motion/lifetime helpers)
#include "okay/Components/Mover.hpp"
#include "okay/Components/Spinner.hpp"
#include "okay/Components/Lifetime.hpp"
#include "okay/Components/CameraFollow.hpp"

// AI
#include "okay/AI/Pathfinding.hpp"

// Starter scene templates
#include "okay/Scene/SceneTemplates.hpp"

// Audio
#include "okay/Audio/AudioClip.hpp"
#include "okay/Components/AudioSource.hpp"
#include "okay/Audio/AudioMixer.hpp"

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
