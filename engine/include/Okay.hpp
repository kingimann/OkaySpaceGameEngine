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
#include "okay/Core/DataAsset.hpp"
#include "okay/Core/SaveData.hpp"
#include "okay/Core/Application.hpp"

// Input
#include "okay/Input/Input.hpp"
#include "okay/Input/Cursor.hpp"

// Rendering
#include "okay/Render/Color.hpp"
#include "okay/Render/Renderer.hpp"
#include "okay/Render/ConsoleRenderer.hpp"
#include "okay/Render/Mesh.hpp"
#include "okay/Render/Lighting.hpp"
#include "okay/Render/SoftwareRenderer.hpp"
#include "okay/Components/Light.hpp"

// Scene graph
#include "okay/Scene/Component.hpp"
#include "okay/Scene/Transform.hpp"
#include "okay/Scene/Layers.hpp"
#include "okay/Scene/GameObject.hpp"
#include "okay/Scene/Scene.hpp"
#include "okay/Scene/SceneSerializer.hpp"
#include "okay/Scene/SceneManager.hpp"

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
#include "okay/Physics/Rigidbody3D.hpp"
#include "okay/Physics/Collider3D.hpp"
#include "okay/Physics/Physics3D.hpp"
#include "okay/Physics/ColliderFit.hpp"

// Animation
#include "okay/Animation/AnimationCurve.hpp"
#include "okay/Animation/AnimationClip.hpp"
#include "okay/Components/Animator.hpp"

// More components
#include "okay/Components/ParticleSystem.hpp"
#include "okay/Components/Terrain.hpp"
#include "okay/Components/Character.hpp"
#include "okay/Render/Material.hpp"
#include "okay/Components/Tilemap.hpp"
#include "okay/Components/TilemapCollider2D.hpp"

// In-game UI
#include "okay/Components/UIButton.hpp"
#include "okay/Components/UIAnchor.hpp"
#include "okay/Components/UIImage.hpp"
#include "okay/Components/UINavigation.hpp"
#include "okay/Components/UIPanel.hpp"
#include "okay/Components/UIProgressBar.hpp"
#include "okay/Components/UIRadialProgress.hpp"
#include "okay/Components/Minimap.hpp"
#include "okay/Components/Crosshair.hpp"
#include "okay/Components/UISlider.hpp"
#include "okay/Components/UIStepper.hpp"
#include "okay/Components/UIRating.hpp"
#include "okay/Components/WorldUI.hpp"
#include "okay/Components/UIToggle.hpp"
#include "okay/Components/UITabs.hpp"
#include "okay/Components/UIScrollView.hpp"
#include "okay/Components/UILayoutGroup.hpp"
#include "okay/Components/UIInputField.hpp"
#include "okay/Components/UIDropdown.hpp"
#include "okay/Components/UITooltip.hpp"
#include "okay/Components/UITextBind.hpp"
#include "okay/Components/UIDraggable.hpp"
#include "okay/Components/Draggable.hpp"
#include "okay/Components/UIElement.hpp"
#include "okay/Components/Canvas.hpp"
#include "okay/Components/EventSystem.hpp"
#include "okay/Components/UIDocument.hpp"

// Gameplay behaviours (no-scripting motion/lifetime helpers)
#include "okay/Components/Mover.hpp"
#include "okay/Components/Spinner.hpp"
#include "okay/Components/Lifetime.hpp"
#include "okay/Components/Stats.hpp"
#include "okay/Components/Inventory.hpp"
#include "okay/Components/TurnManager.hpp"
#include "okay/Components/CameraFollow.hpp"
#include "okay/Components/DollyPath.hpp"
#include "okay/Components/VirtualCamera.hpp"
#include "okay/Components/CinemachineBrain.hpp"

// AI
#include "okay/AI/Pathfinding.hpp"

// Starter scene templates
#include "okay/Scene/SceneTemplates.hpp"

// Audio
#include "okay/Audio/AudioClip.hpp"
#include "okay/Components/AudioSource.hpp"
#include "okay/Components/ActionList.hpp"
#include "okay/Components/CharacterController2D.hpp"
#include "okay/Components/CharacterController3D.hpp"
#include "okay/Components/FirstPersonController.hpp"
#include "okay/Components/ThirdPersonController.hpp"
#include "okay/Components/VehicleController.hpp"
#include "okay/Components/VehicleController2D.hpp"
#include "okay/Components/SurvivalStats.hpp"
#include "okay/Components/SurvivalComponents.hpp"
#include "okay/Components/SurvivalAfflictions.hpp"
#include "okay/Components/SurvivalSystems.hpp"
#include "okay/Components/SurvivalZone.hpp"
#include "okay/Components/Consumables.hpp"
#include "okay/Components/DayNightCycle.hpp"
#include "okay/Components/NPCController.hpp"
#include "okay/Components/Crafting.hpp"
#include "okay/Components/MeleeAttacker.hpp"
#include "okay/Components/Spawner.hpp"
#include "okay/Components/CraftingMenu.hpp"
#include "okay/Components/ThirdPersonShooterController.hpp"
#include "okay/Components/TopDownController.hpp"
#include "okay/Components/FreeRoamController.hpp"
#include "okay/Components/ClickToMoveController.hpp"
#include "okay/Components/FollowTarget2D.hpp"
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
#include "okay/Platform/Steam/Steam.hpp"
#include "okay/Components/SteamManager.hpp"

// Player accounts (sign-in/register; local backend by default, online when an
// auth server is configured)
#include "okay/Platform/Account/AccountService.hpp"
#include "okay/Platform/Account/Account.hpp"
