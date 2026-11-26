# ---- okay::crypto as its own static library --------------------------
# Our own crypto primitives (docs/platform/crypto.md). A separate target so that code which must not
# link the whole engine (the launcher, shared/AntiCheatCrypto.hpp with -DOKAY_OWN_CRYPTO=ON,
# the crypto tests, the fuzz targets and the benchmark) can use it alone. okay_engine links it
# PUBLIC, so everything that links the engine gets it as before. UNREVIEWED; see docs/platform/crypto.md.
add_library(okay_crypto STATIC
    src/Crypto/Util.cpp
    src/Crypto/Random.cpp
    src/Crypto/ChaCha20.cpp
    src/Crypto/Poly1305.cpp
    src/Crypto/Aead.cpp
    src/Crypto/Blake2b.cpp
    src/Crypto/Sha512.cpp
    src/Crypto/X25519.cpp
    src/Crypto/Ed25519.cpp
)
target_include_directories(okay_crypto PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_compile_features(okay_crypto PUBLIC cxx_std_17)
if(WIN32)
    target_link_libraries(okay_crypto PUBLIC bcrypt)   # BCryptGenRandom
endif()
if(OKAY_OWN_CRYPTO)
    target_compile_definitions(okay_crypto PUBLIC OKAY_OWN_CRYPTO)
endif()
add_library(okay::crypto ALIAS okay_crypto)

# ---- okay::physcore: our own rigid-body physics core (docs/physics/physics_core.md) ----
# Its own static library so its tests and benchmarks build and link in seconds without the whole engine.
# Written only from papers and textbooks (each file says which). okay_engine links it PUBLIC.
add_library(okay_physcore STATIC
    src/Physics/Core/DynamicTree.cpp
    src/Physics/Core/Shape.cpp
    src/Physics/Core/Collide.cpp
    src/Physics/Core/World.cpp
    src/Physics/Core/Solver.cpp
    src/Physics/Core/Joint.cpp
)
target_include_directories(okay_physcore PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_compile_features(okay_physcore PUBLIC cxx_std_17)
add_library(okay::physcore ALIAS okay_physcore)

# okay::js: our own JavaScript engine (ECMA-262; docs/scripting/javascript.md). Standard library only, so the Browser's page
# scripts (okay_browser) and the engine's game scripts (src/JavaScript/JavaScriptVM.cpp in okay_engine) share one copy.
add_library(okay_js STATIC
    src/JavaScript/JsLexer.cpp
    src/JavaScript/JsParser.cpp
    src/JavaScript/JsCompiler.cpp
    src/JavaScript/JsRuntime.cpp
    src/JavaScript/JsInterpreter.cpp
    src/JavaScript/JsLibObject.cpp
    src/JavaScript/JsLibArray.cpp
    src/JavaScript/JsLibString.cpp
    src/JavaScript/JsLibNumber.cpp
    src/JavaScript/JsLibJson.cpp
    src/JavaScript/JsLibCollections.cpp
    src/JavaScript/JsRegExp.cpp
)
target_include_directories(okay_js PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_compile_features(okay_js PUBLIC cxx_std_17)
add_library(okay::js ALIAS okay_js)

# Browser: our own web browser's page engine (HTML reader, CSS, layout, addresses, one tab's navigation, web game packs).
# Standard library, header-only engine helpers (Core/Inflate, Core/Json, Core/PathSafety) and okay_js only, so the launcher, the
# browser program (browser/) and the tests link it without the rest of the engine
# (docs/feature_map/17-launcher-platform.md 17.18).
add_library(okay_browser STATIC
    src/Browser/Url.cpp
    src/Browser/Html.cpp
    src/Browser/Css.cpp
    src/Browser/Plugins.cpp      # round 3: browser plugins (toolbar buttons, page actions, block rules, styles, new-tab sections)
    src/Browser/WebFont.cpp      # round 3: WOFF 1.0 fonts rebuilt into TrueType / OpenType (through Core/Inflate)
    src/Browser/Layout.cpp
    src/Browser/Browser.cpp
    src/Browser/Forms.cpp        # the browser program: form controls' values, the entry list, GET and POST submission
    src/Browser/Profile.cpp      # the browser program: address bar, tabs, history, bookmarks, zoom, downloads, our own pages
    src/Browser/GamePack.cpp     # step 2: the web game pack reader (the native twin of platform/web/okay_web.js)
    src/Browser/GameLaunch.cpp   # step 2: which page elements run a game, per-site folders, the player's command line
    src/Browser/Script.cpp       # round 2: page JavaScript on okay::js (the DOM binding, events, timers, the console)
)
target_include_directories(okay_browser PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_compile_features(okay_browser PUBLIC cxx_std_17)
target_link_libraries(okay_browser PUBLIC okay_js)
add_library(okay::browser ALIAS okay_browser)

# OkaySpace engine library
add_library(okay_engine
    src/Core/Application.cpp
    src/Core/BuildInfo.cpp
    src/Core/CommandLine.cpp
    src/Core/Paths.cpp
    src/Core/Cvar.cpp
    src/Core/Console.cpp
    src/Core/DebugOverlay.cpp
    src/Core/SaveGame.cpp
    src/Core/SaveFile.cpp
    src/Core/TolerantNumbers.cpp
    src/Core/SaveFramework.cpp
    src/Core/SaveFrameworkScene.cpp
    src/Core/SaveFrameworkSync.cpp
    src/Core/GameSaves.cpp
    src/Core/Clipboard.cpp
    src/Core/Scheduler.cpp
    src/Core/Tween.cpp
    src/Core/TimerDebug.cpp
    src/Core/Prefs.cpp
    src/Core/DataAsset.cpp
    src/Core/Undo.cpp
    src/Core/Recovery.cpp
    src/Core/Backup.cpp
    src/Core/AssetManager.cpp
    src/Core/ImportSettings.cpp
    src/Core/ImportSidecar.cpp
    src/Core/AssetDatabase.cpp
    src/Core/AssetDatabaseDeps.cpp
    src/Core/AssetDatabaseSearch.cpp
    src/Core/ThumbnailCache.cpp
    src/Core/Localization.cpp
    src/Input/Input.cpp
    src/Input/InputRecorder.cpp
    src/Input/InputDebug.cpp
    src/Input/Haptics.cpp
    src/Input/GamepadInfo.cpp
    src/Input/ControllerMapping.cpp
    src/Input/PadProfiles.cpp
    src/Input/ActionSet.cpp
    src/Input/InputAsset.cpp
    src/Input/PlayerInput.cpp
    src/Input/InputRuntime.cpp
    src/Input/ActionEditor.cpp
    src/Input/InputActions.cpp
    src/Input/GameInput.cpp
    src/Input/GestureRecognizer.cpp
    src/Input/Gestures.cpp
    src/Input/TouchControls.cpp
    src/Core/DisplaySettings.cpp
    src/Core/DisplayModes.cpp
    src/Render/TextureCacheFiles.cpp
    src/Render/ModelImportProblem.cpp
    src/Render/ConsoleRenderer.cpp
    src/Render/SoftwareRendererShadows.cpp
    src/Render/SoftwareRendererPost.cpp
    src/Render/SoftwareRendererMeshes.cpp
    src/Render/GLRenderer.cpp
    src/Render/GLRendererShaders.cpp
    src/Render/GLRendererPasses.cpp
    src/Render/GLRendererResources.cpp
    src/Render/OkayShader.cpp
    # RHI: the rendering hardware interface (docs/rendering/rhi.md)
    src/Render/RHI/RHI.cpp
    src/Render/RHI/SoftwareDevice.cpp
    src/Render/RHI/GLDevice.cpp
    src/Render/RHI/D3D11Device.cpp
    src/Render/RHI/SurfaceShader.cpp
    src/Render/RHI/BloomPass.cpp
    src/Render/D3D11Renderer.cpp
    src/Render/D3D12Renderer.cpp
    src/Scene/Component.cpp
    src/Scene/GameObject.cpp
    src/Scene/Transform.cpp
    src/Scene/Scene.cpp
    src/Scene/SceneTemplatesBuilders.cpp
    src/Scene/SceneManager.cpp
    src/Scene/ObjectPool.cpp
    src/Scene/SceneSerializer.cpp
    src/Scene/SceneSerializerHelpers.cpp
    src/Scene/SceneSerializerRegistry.cpp
    src/Scene/SceneSerializerWrite.cpp
    src/Scene/SceneSerializerParse.cpp
    src/Scene/ComponentSerializer.cpp
    src/Scene/SceneLocalization.cpp
    src/Components/Camera.cpp
    src/Components/WorldStreamer.cpp
    src/Components/LookAtIK.cpp
    src/Components/SpriteRenderer.cpp
    src/Net/UdpSocket.cpp
    src/Net/AntiCheat.cpp
    src/Net/CheatDetect.cpp
    src/Net/AntiCheatLog.cpp
    src/Net/CheatSim.cpp
    src/Net/SessionTicket.cpp
    src/Net/ClockCheck.cpp
    src/Components/AntiCheatConfig.cpp
    src/Components/Ragdoll.cpp
    src/Components/RagdollShared.cpp
    src/Components/NetworkObject.cpp
    src/Net/NetworkManager.cpp
    # NetworkManager's other parts (batch N1, docs/engine/modularization_plan.md): sync and messages,
    # server authority (lag compensation, anti-cheat checks, movement), the server tick and the client tick.
    src/Net/NetworkManagerSync.cpp
    src/Net/NetworkManagerEvents.cpp
    src/Net/Net.cpp
    src/Net/NetworkManagerMigration.cpp
    src/Net/NetworkManagerOnline.cpp
    src/Net/Rollback.cpp
    src/Net/NetworkManagerAuthority.cpp
    src/Net/NetworkManagerServer.cpp
    src/Net/NetworkManagerClient.cpp
    src/Net/NetBuildSync.cpp
    src/Net/NetQuestSync.cpp
    src/Net/NetDelta.cpp
    src/Net/NetChannels.cpp
    src/Net/NetSim.cpp
    src/Net/RelayServer.cpp
    src/Net/DedicatedServer.cpp
    src/Net/ServerQuery.cpp
    src/Net/ServerBrowser.cpp
    src/Net/MatchResults.cpp
    src/Net/Matchmaker.cpp
    src/Net/NetBackend.cpp
    src/Net/NetTransport.cpp
    src/Net/PhotonTransport.cpp
    src/VisualScript/NodeGraph.cpp
    src/VisualScript/VsNodeRegistry.cpp
    src/VisualScript/VsTypes.cpp
    src/VisualScript/VsApi.cpp
    src/VisualScript/VsBuiltins.cpp
    src/VisualScript/VsVariables.cpp
    src/VisualScript/VsDecl.cpp
    src/VisualScript/VsDebug.cpp
    src/VisualScript/ActionGraph.cpp
    src/Components/VisualScriptComponent.cpp
    src/Components/UIAnimation.cpp
    src/Scripting/OkayScriptVM.cpp
    src/Scripting/OkayScriptVM_Ui.cpp
    src/Scripting/OkayScriptVM_Coroutine.cpp
    src/Scripting/OkayScriptVM_Scene.cpp
    src/Scripting/OkayScriptVM_Alias.cpp
    src/Scripting/OkayScriptVM_Tween.cpp
    src/Scripting/OkayScriptVM_Physics.cpp
    src/Scripting/OkayScriptVM_Math.cpp
    src/Scripting/OkayScriptVM_Network.cpp
    src/Scripting/OkayScriptVM_Inventory.cpp
    src/Scripting/OkayScriptVM_Animation.cpp
    src/Scripting/OkayScriptVM_Navigation.cpp
    src/Scripting/OkayScriptVM_World.cpp
    src/Scripting/OkayScriptVM_Camera.cpp
    src/Scripting/OkayScriptVM_Gameplay.cpp
    src/Scripting/OkayScriptVM_Input.cpp
    src/Scripting/ScriptVM.cpp
    src/Scripting/ScriptApiRegistry.cpp
    src/Scripting/ScriptApiCore.cpp
    src/Scripting/ScriptBindings.cpp
    src/Scripting/ScriptBindingsGenerated.cpp
    src/Script/Lexer.cpp
    src/Script/Parser.cpp
    src/Script/Checker.cpp
    src/Script/CheckerOperators.cpp
    src/Script/CheckerCalls.cpp
    src/Script/CheckerStatements.cpp
    src/Script/CheckerTypes.cpp
    src/Script/CheckerExpressions.cpp
    src/Script/Interpreter.cpp
    src/Script/EngineApi.cpp
    src/Script/EngineApi_Math.cpp
    src/Script/EngineApi_Animation.cpp
    src/Script/EngineApi_Curves.cpp
    src/Script/EngineApi_Story.cpp
    src/Script/EngineApi_World.cpp
    src/Script/EngineApi_Runtime.cpp
    src/Script/EngineApi_Input.cpp
    src/Script/EngineApi_Ui.cpp
    src/Script/EngineApi_Core.cpp
    src/Script/NextScriptVM.cpp
    src/Lua/LuaCompiler.cpp
    src/Lua/LuaState.cpp
    src/Lua/LuaLibBase.cpp
    src/Lua/LuaLibString.cpp
    src/Lua/LuaLibTable.cpp
    src/Lua/LuaLibUtf8.cpp
    src/Lua/LuaScriptVM.cpp
    src/JavaScript/JavaScriptVM.cpp
    src/Scripting/ScriptLanguageGlue.cpp
    src/Components/ScriptComponent.cpp
    src/Components/AnimControl.cpp
    src/Components/ActionList.cpp
    src/Components/ActionListOps.cpp
    src/Inventory/InventoryOps.cpp
    src/Combat/WeaponBook.cpp
    src/Components/MeleeAttacker.cpp
    src/Components/Damage.cpp
    src/Components/RangedWeapon.cpp
    src/Inventory/ItemDatabase.cpp
    src/Inventory/LootTable.cpp
    src/Inventory/InventoryPersistence.cpp
    src/Inventory/Recipe.cpp
    src/Inventory/Equipment.cpp
    src/Inventory/InventoryEvents.cpp
    src/Inventory/NetInventory.cpp
    src/UI/InventoryInput.cpp
    src/Components/UIDocument.cpp
    src/Components/UILayoutGroup.cpp
    src/Components/UIListView.cpp
    src/Components/UIModal.cpp
    src/Components/UIContextMenu.cpp
    src/Components/UIKeybindField.cpp
    src/Components/UITooltip.cpp
    src/Components/UITextBind.cpp
    src/Components/UIBarBind.cpp
    src/Components/UIVars.cpp
    src/Components/UIDataGlue.cpp
    src/Components/UIGameHud.cpp
    src/Components/UIAppParts.cpp
    src/UI/UIGameHudDraw.cpp
    src/UI/UIAppPartsDraw.cpp
    src/Components/UIOwnerData.cpp
    src/Components/UIDamageNumbers.cpp
    src/Components/UIDialogueBox.cpp
    src/Components/UIThemeAsset.cpp
    src/Components/UIDraggable.cpp
    src/Components/Draggable.cpp
    src/Components/Terrain.cpp
    src/Components/Foliage.cpp
    src/Components/ParticleSystem.cpp
    src/Components/Cable.cpp
    src/Components/Decal.cpp
    src/Components/VoxelTerrain.cpp
    src/Components/VoxelEdit.cpp
    src/Components/VoxelFluid.cpp
    src/Components/Character.cpp
    src/Components/CharacterMotion.cpp
    src/Components/CharacterFirstPerson.cpp
    src/Components/ModelAnimatorPose.cpp
    src/Components/ModelAnimatorUpdate.cpp
    src/Components/PlayerView.cpp
    src/Components/PlayerController.cpp
    src/Components/CharacterAnimBackend.cpp
    src/Animation/HumanoidMotion.cpp
    src/Animation/HumanoidLibrary.cpp
    src/Animation/PlayerSetup.cpp
    src/Animation/HumanoidActions.cpp
    src/Animation/HumanoidActionsStates.cpp
    src/Animation/HumanoidActionsRuntime.cpp
    src/Animation/CoreRagdoll.cpp
    src/Animation/DynamicCharacter.cpp
    src/Components/AnimClip.cpp
    src/Components/Sequence.cpp
    src/Components/SequencePlayer.cpp
    src/Components/Spline.cpp
    src/Components/SplineMesh.cpp
    src/Components/LineRenderer.cpp
    src/Components/BuildMode.cpp
    src/Components/Volume.cpp
    src/Components/WindZone.cpp
    src/Components/DialogueGraph.cpp
    src/Components/DialogueRunner.cpp
    src/Components/QuestLog.cpp
    src/Platform/Steam/NullSteamService.cpp
    src/Platform/Steam/Steam.cpp
    src/Components/SteamManager.cpp
    src/Platform/Account/Account.cpp
    src/Physics/Physics2D.cpp
    src/Physics/Physics3D.cpp
    src/Physics/ColliderShapes3D.cpp
    src/Physics/ColliderShapes2D.cpp
    src/Physics/PhysicsToys.cpp
    src/Physics/TriMesh.cpp
    src/Physics/MeshCollider3D.cpp
    src/Audio/AudioMixer.cpp
    src/Audio/SpatialFx.cpp
    src/Audio/AudioClip.cpp
    src/Audio/AudioDecode.cpp
    src/Audio/AudioStream.cpp
    src/Audio/VorbisDecoder.cpp
    src/Audio/Mp3Decoder.cpp
    src/Audio/FlacDecoder.cpp
    src/Audio/WavDecoder.cpp
    src/Audio/WavPlayback.cpp
    src/AI/NavigationGrid.cpp
    src/AI/GridPath2D.cpp
    src/AI/NavMesh.cpp
    src/AI/NavMeshBake.cpp
    src/AI/NavMeshCarve.cpp
    src/AI/NavMeshSurface.cpp
    src/AI/NavMeshAgent.cpp
    src/AI/Navigation.cpp
    src/AI/BehaviorTree.cpp
    src/AI/BehaviorTreeRunner.cpp
    src/AI/Perception.cpp
    src/Core/ImageWrite.cpp
    src/Core/BuildPipeline.cpp
    src/Core/ImageDecode.cpp
    src/Core/ImageDecodeJpeg.cpp
    src/Graphics/Image.cpp
    src/Graphics/Font.cpp
    src/Graphics/TtfFont.cpp
    src/Graphics/TtfRaster.cpp
    src/Graphics/EmbeddedFont.cpp
    src/Debug/FrameHistory.cpp
    src/Debug/GpuTimer.cpp
    src/Debug/RenderStats.cpp
    src/Debug/MemoryTracker.cpp
    src/Debug/DrawCallRecorder.cpp
    src/Debug/ConsoleModel.cpp
    src/Debug/Diagnostics.cpp
    src/Debug/ProjectDiagnostics.cpp
    src/Debug/DebugStatsOverlay.cpp
    src/UI/UIRenderer.cpp
    src/UI/UIScenePass.cpp
    src/UI/UIWidgetsMoreDraw.cpp
    src/UI/UIBatcher.cpp
    src/UI/UIImageScale.cpp
    src/UI/UILayoutUpdate.cpp
    src/UI/UIFrameTransform.cpp
    # Localization tools (docs/engine/localization.md)
    src/Core/LocCheck.cpp
    src/Core/LocTable.cpp
    src/Core/LocScan.cpp
    src/Core/LocPreview.cpp
    # Accessibility (docs/engine/accessibility.md)
    src/Graphics/ColorVision.cpp
    src/Core/Subtitles.cpp
    src/Core/AccessibilitySettings.cpp
    src/UI/FocusGraph.cpp
    src/UI/UIFocus.cpp
    src/Input/RemapModel.cpp
    src/Render/ModelImport.cpp
    src/Render/MeshModifiers.cpp
    src/Render/MeshObj.cpp
    src/Render/MeshUV.cpp
    src/Render/MeshShapes.cpp
    src/Render/ModelImportCharacter.cpp
    src/Render/ModelReaders.cpp
    src/Render/ColladaReader.cpp
    src/Render/Max3dsReader.cpp
    src/Render/BvhReader.cpp
    src/Render/OffReader.cpp
    src/Render/SmdReader.cpp
    src/Render/Md5Reader.cpp
    src/Render/Md5CameraReader.cpp
    src/Render/Ms3dReader.cpp
    src/Render/AseReader.cpp
    src/Render/BlendReader.cpp
    src/Render/BlendReaderBuild.cpp
    src/Render/BlendModifiers.cpp
    src/Render/ThreeMfReader.cpp
    src/Render/AmfReader.cpp
    src/Render/X3dReader.cpp
    src/Render/DxfReader.cpp
    src/Render/LwoReader.cpp
    src/Render/Md2Reader.cpp
    src/Render/Md3Reader.cpp
    src/Render/IrrMeshReader.cpp
    src/Render/B3dReader.cpp
    src/Render/XFileReader.cpp
    src/Render/Ac3dReader.cpp
    src/Render/OgreXmlReader.cpp
    src/Render/ZaeReader.cpp
    src/Render/XglReader.cpp
    src/Render/IrrSceneReader.cpp
    src/Render/NffReader.cpp
    src/Render/IqmReader.cpp
    src/Render/CobReader.cpp
    src/Render/NdoReader.cpp
    src/Render/SibReader.cpp
    src/Render/TerragenReader.cpp
    src/Render/MdcReader.cpp
    src/Render/HmpReader.cpp
    src/Render/CsmReader.cpp
    src/Render/Q3dReader.cpp
    src/Render/RawReader.cpp
    src/Render/PmxReader.cpp
    src/Render/LwsReader.cpp
    src/Render/Gltf1Reader.cpp
    src/Render/OgexReader.cpp
    src/Render/MdlReader.cpp
    src/Render/Q3BspReader.cpp
    src/Modeling/EditTransform.cpp
    src/Modeling/Measure.cpp
    src/Modeling/SelectOps.cpp
    src/Modeling/ScreenSelect.cpp
    src/Modeling/MeshLibrary.cpp
    src/Modeling/Curve.cpp
    src/Modeling/Remesh.cpp
    src/Modeling/PolyMesh.cpp
    src/Modeling/MeshAsset.cpp
    src/Modeling/PolyOps.cpp
    src/Modeling/Modifiers.cpp
    src/Modeling/UVUnwrap.cpp
    src/Modeling/Guided.cpp
    src/Modeling/GuidedFence.cpp
    src/Modeling/GuidedArchitecture.cpp
    src/Modeling/GuidedVehicles.cpp
    src/Modeling/GuidedCharacters.cpp
    src/Modeling/GuidedEnvironment.cpp
    src/Modeling/GuidedProps.cpp
    src/Modeling/GuidedHuman.cpp
    src/Modeling/GuidedRig.cpp
    src/Components/LODGroup.cpp
    src/Render/Visibility.cpp
    src/Render/SurfaceRaycast.cpp
    src/Optimize/MeshSimplify.cpp
    src/Optimize/LodGroup.cpp
    src/Optimize/OcclusionBuffer.cpp
    src/Optimize/PortalCells.cpp
    src/Optimize/BatchPlanner.cpp
    src/Optimize/StreamingGrid.cpp
    src/Optimize/MipBudget.cpp
    src/Texture/Texture.cpp
    src/Texture/Mip.cpp
    src/Texture/Atlas.cpp
    src/Texture/Convert.cpp
    src/Texture/Bc.cpp
    src/Texture/Dds.cpp
    src/Texture/Stream.cpp
    src/Texture/Export.cpp
    src/Render/GpuTexture.cpp
    src/Render/TextureAsset.cpp
    src/Optimize/SceneOptimizationReport.cpp
    src/Testing/TestRegistry.cpp
    src/Testing/PlayTest.cpp
    src/Testing/ObjectPath.cpp
    src/Testing/SceneValidator.cpp
    src/Testing/Regression.cpp
    src/Testing/GoldenImage.cpp
    src/Render/FbxReader.cpp
    src/Render/FbxScene.cpp
    src/Render/FbxSceneClips.cpp
)
# FBX is read only by our own reader (okay::fbx, the three Fbx*.cpp files above); the vendored ufbx was deleted on 2026-11-23.

# Public headers live under engine/include; consumers use #include <Okay.hpp>.
# The UI library's data files (assets/ui/*.okaydata: screens, styles, themes, icons, components) are embedded into the engine, so every
# program has them without shipping the files. Edit the .okaydata file and rebuild; scripts/gen_ui_catalogue.py counts the same files.
set(OKAY_UI_DATA_NAMES ui_screens ui_styles ui_themes ui_icons ui_components)
set(_okay_ui_data_cpp "${CMAKE_CURRENT_BINARY_DIR}/ui_library_data.cpp")
set(_okay_ui_data_text "// Generated by engine/CMakeLists.txt from assets/ui/*.okaydata. Do not edit.\n#include <cstring>\nnamespace okay { namespace uilib {\nconst char* EmbeddedData(const char* name) {\n")
foreach(_n ${OKAY_UI_DATA_NAMES})
    set(_f "${PROJECT_SOURCE_DIR}/assets/ui/${_n}.okaydata")
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${_f}")
    file(READ "${_f}" _c)
    string(APPEND _okay_ui_data_text "    if (!std::strcmp(name, \"${_n}\")) return R\"OKAYUIDATA(${_c})OKAYUIDATA\";\n")
endforeach()
string(APPEND _okay_ui_data_text "    return \"\";\n}\n}}\n")
file(WRITE "${_okay_ui_data_cpp}.new" "${_okay_ui_data_text}")
execute_process(COMMAND ${CMAKE_COMMAND} -E copy_if_different "${_okay_ui_data_cpp}.new" "${_okay_ui_data_cpp}")
target_sources(okay_engine PRIVATE "${_okay_ui_data_cpp}")

target_include_directories(okay_engine PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)

# (No vendored single-header libraries are left: image decoding, fonts and audio are our own, and the
# engine/third_party/stb folder is gone, so there is no include path for it.)
# Image.cpp keeps the warning settings it had while it compiled the vendored stb tables (`-Wno-narrowing`:
# Clang for Emscripten/WASM treats a `char` table initialised with 0xFF as a narrowing ERROR that `-w` does
# not downgrade). Not checked yet whether our own code there still needs them.
set_source_files_properties(src/Graphics/Image.cpp
    PROPERTIES COMPILE_OPTIONS "-w;-Wno-narrowing")
# The built-in UI font (the launcher's Roboto) lives in shared/RobotoFontOwn.h, in our own OKF1 container (shared/OkfDecode.hpp).
set_source_files_properties(src/Graphics/EmbeddedFont.cpp PROPERTIES
    INCLUDE_DIRECTORIES ${CMAKE_CURRENT_SOURCE_DIR}/../shared)
# Only BuildInfo.cpp sees the version string, so a version bump recompiles one file.
set_source_files_properties(src/Core/BuildInfo.cpp PROPERTIES COMPILE_DEFINITIONS "${OKAY_VERSION_DEFINE}")
# The network layer signs its key exchange with #269's ed25519 (shared/AntiCheatCrypto.hpp)
# and runs its clock heartbeat (shared/AntiCheatChecks.hpp); the dedicated server
# files anti-cheat kicks with #270's request builder (shared/AntiCheatBackend.hpp);
# the relay makes its hello cookies with HmacSha256 (shared/AntiCheatChecks.hpp).
set_source_files_properties(src/Net/NetworkManager.cpp src/Net/ClockCheck.cpp src/Net/DedicatedServer.cpp
                            src/Net/RelayServer.cpp src/Net/NetworkManagerSync.cpp src/Net/NetworkManagerAuthority.cpp
                            src/Net/NetworkManagerServer.cpp src/Net/NetworkManagerClient.cpp PROPERTIES
    INCLUDE_DIRECTORIES ${CMAKE_CURRENT_SOURCE_DIR}/../shared)

target_compile_features(okay_engine PUBLIC cxx_std_17)

find_package(Threads REQUIRED)
target_link_libraries(okay_engine PUBLIC Threads::Threads)

# Our own HTTP client, NativeHttp's client on POSIX (libssl is dlopen'ed at run time, nothing linked but libdl).
if(OKAY_OWN_STACK)
    target_compile_definitions(okay_engine PUBLIC OKAY_OWN_STACK_BUILD)   # tests and the startup helper can tell a dogfood build
endif()
if(OKAY_OWN_HTTP AND NOT WIN32 AND NOT APPLE)
    target_compile_definitions(okay_engine PUBLIC OKAY_OWN_HTTP=1)
    target_link_libraries(okay_engine PUBLIC ${CMAKE_DL_LIBS})
endif()

# Networking needs the Windows sockets library when targeting Windows.
if(WIN32)
    target_link_libraries(okay_engine PUBLIC ws2_32 winhttp)   # winhttp: NativeHttp (backend calls without spawning curl)
    # The Direct3D 11 scene renderer (D3D11Renderer.cpp, guarded by _WIN32) needs the
    # D3D runtime + HLSL compiler. Off Windows the file compiles to nothing.
    target_link_libraries(okay_engine PUBLIC d3d11 d3dcompiler)
    # The Direct3D 12 scene renderer (D3D12Renderer.cpp, guarded by _WIN32) needs the
    # D3D12 runtime, DXGI and the COM IIDs (dxguid). Off Windows the file is empty.
    target_link_libraries(okay_engine PUBLIC d3d12 dxgi dxguid)
endif()

# ---- Optional Photon Realtime transport (closed-source SDK) ----------
# Photon's C++ SDK is a per-platform download from dashboard.photonengine.com
# and can't be redistributed, so the adapter (src/Net/PhotonTransport.cpp) is
# an empty TU unless this is turned on with the SDK unpacked somewhere local:
#   cmake -DOKAY_WITH_PHOTON=ON -DOKAY_PHOTON_SDK_DIR=/path/to/photon-sdk ...
# The SDK dir is the one containing LoadBalancing-cpp/, Photon-cpp/ and
# Common-cpp/. See docs/online/photon.md for the full walkthrough.
option(OKAY_WITH_PHOTON "Photon Realtime transport adapter (requires the Photon C++ SDK)" OFF)
set(OKAY_PHOTON_SDK_DIR "" CACHE PATH "Root of the unpacked Photon Realtime C++ SDK")
if(OKAY_WITH_PHOTON)
    target_compile_definitions(okay_engine PUBLIC OKAY_WITH_PHOTON=1)
    if(OKAY_PHOTON_SDK_DIR)
        target_include_directories(okay_engine PRIVATE "${OKAY_PHOTON_SDK_DIR}")
        # Typical layout: <sdk>/<Module>-cpp/lib/lib<Module>-cpp_release_...a —
        # exact names vary by platform/toolchain, so let the user override.
        foreach(_pm LoadBalancing-cpp Photon-cpp Common-cpp)
            file(GLOB _pl "${OKAY_PHOTON_SDK_DIR}/${_pm}/lib/*.a"
                          "${OKAY_PHOTON_SDK_DIR}/${_pm}/lib/*.lib")
            if(_pl)
                list(GET _pl 0 _pl0)
                target_link_libraries(okay_engine PUBLIC "${_pl0}")
            endif()
        endforeach()
    else()
        message(WARNING "OKAY_WITH_PHOTON is ON but OKAY_PHOTON_SDK_DIR is not set; "
                        "the Photon adapter will not compile without the SDK headers.")
    endif()
endif()

# ---- Optional packet encryption (libsodium) --------------------------
# Authenticated encryption for the UDP transport. Enabled when libsodium is found.
#  * Native: discovered via pkg-config (apt: libsodium-dev).
#  * MinGW cross-compile: point -DOKAY_SODIUM_PREFIX at the prebuilt
#    libsodium-win64 dir (the one containing include/sodium.h and lib/libsodium.a).
# Without it a DEVELOPER build still configures: packet encryption is off and the anti-cheat / update / host-identity
# Ed25519 falls back to our own, unreviewed okay::crypto. A RELEASE must never do that: -DOKAY_REQUIRE_SODIUM=ON
# (scripts/build-windows-dist.sh, release.yml, release-local.sh) turns every such case into a configure ERROR.
# ---- Our own crypto (okay::crypto), side by side with libsodium ------
# The primitives in src/Crypto are ALWAYS compiled (and tested); OKAY_OWN_CRYPTO only
# decides whether the network layer uses them instead of libsodium. UNREVIEWED by an
# outside cryptographer: it must not become the default, and libsodium must not be
# removed, before that review (docs/status/own_code_roadmap.md, docs/platform/crypto.md).
target_link_libraries(okay_engine PUBLIC okay_crypto okay_physcore okay_browser)   # (also brings bcrypt on Windows and OKAY_OWN_CRYPTO)
target_link_libraries(okay_engine PRIVATE okayscript_lang)   # the OkayScript lexer (okayscript/, engine-independent)
set(OKAY_SODIUM_IN_BUILD OFF CACHE INTERNAL "libsodium was found and linked into okay_engine")   # reset on every configure; set ON below
if(OKAY_REQUIRE_SODIUM)
    if(OKAY_OWN_CRYPTO OR OKAY_OWN_STACK_CRYPTO_TEST)
        message(FATAL_ERROR "OKAY_REQUIRE_SODIUM=ON (a release build) but OKAY_OWN_CRYPTO / OKAY_OWN_STACK_CRYPTO_TEST is ON: our own crypto is unreviewed and must not ship (CLAUDE.md rule 18)")
    endif()
    if(NOT OKAY_USE_SODIUM)
        message(FATAL_ERROR "OKAY_REQUIRE_SODIUM=ON (a release build) but OKAY_USE_SODIUM=OFF: a release must be built with libsodium")
    endif()
endif()
if(OKAY_OWN_CRYPTO)
    target_compile_definitions(okay_engine PUBLIC OKAY_OWN_CRYPTO OKAY_NET_CRYPTO)
    if(OKAY_OWN_STACK_CRYPTO_TEST)
        target_compile_definitions(okay_engine PUBLIC OKAY_OWN_STACK_CRYPTO_TEST)   # the startup line says "own(test)"
    endif()
    message(STATUS "OkaySpace: packet encryption ENABLED (OUR OWN crypto, unreviewed; OKAY_OWN_CRYPTO=ON)")
elseif(OKAY_USE_SODIUM)
    set(_okay_sodium FALSE)
    if(OKAY_SODIUM_PREFIX)
        # Explicit prefix (e.g. the MinGW prebuilt): reference the static lib directly
        # rather than via find_library, which is fussy across cross toolchains.
        set(_sodium_a "${OKAY_SODIUM_PREFIX}/lib/libsodium.a")
        if(EXISTS "${_sodium_a}")
            set(_sodium_a_exists TRUE)
        else()
            set(_sodium_a_exists FALSE)
        endif()
        if(EXISTS "${OKAY_SODIUM_PREFIX}/include/sodium.h")
            set(_sodium_h_exists TRUE)
        else()
            set(_sodium_h_exists FALSE)
        endif()
        if(_sodium_a_exists AND _sodium_h_exists)
            target_include_directories(okay_engine SYSTEM PUBLIC "${OKAY_SODIUM_PREFIX}/include")
            target_link_libraries(okay_engine PUBLIC "${_sodium_a}")
            target_compile_definitions(okay_engine PUBLIC SODIUM_STATIC=1)  # static .a on Windows
            set(_okay_sodium TRUE)
        elseif(OKAY_REQUIRE_SODIUM)
            message(FATAL_ERROR "OKAY_REQUIRE_SODIUM=ON but OKAY_SODIUM_PREFIX=${OKAY_SODIUM_PREFIX} does not hold lib/libsodium.a and include/sodium.h "
                                "(libsodium.a exists: ${_sodium_a_exists}; sodium.h exists: ${_sodium_h_exists}). A release must not build without libsodium")
        endif()
    else()
        find_package(PkgConfig QUIET)
        if(PkgConfig_FOUND)
            pkg_check_modules(SODIUM QUIET libsodium)
            if(SODIUM_FOUND)
                target_include_directories(okay_engine SYSTEM PRIVATE ${SODIUM_INCLUDE_DIRS})
                target_link_libraries(okay_engine PUBLIC ${SODIUM_LIBRARIES})
                set(_okay_sodium TRUE)
            endif()
        endif()
    endif()
    if(NOT _okay_sodium AND OKAY_REQUIRE_SODIUM)
        message(FATAL_ERROR "OKAY_REQUIRE_SODIUM=ON but libsodium was not found (pkg-config libsodium, or -DOKAY_SODIUM_PREFIX=<dir with lib/libsodium.a>). "
                            "A release must not build without libsodium: it would fall back to our own unreviewed crypto")
    endif()
    if(_okay_sodium)
        target_compile_definitions(okay_engine PUBLIC OKAY_HAVE_SODIUM OKAY_NET_CRYPTO)
        # The built result the release guard reads (scripts/check_release_own_stack.sh): a cache entry that says libsodium is in.
        set(OKAY_SODIUM_IN_BUILD ON CACHE INTERNAL "libsodium was found and linked into okay_engine")
        message(STATUS "OkaySpace: packet encryption ENABLED (libsodium)")
    else()
        set(OKAY_SODIUM_IN_BUILD OFF CACHE INTERNAL "libsodium was found and linked into okay_engine")
        message(STATUS "OkaySpace: libsodium NOT found: packet encryption disabled and Ed25519 (updates, anti-cheat, host identity) uses our own UNREVIEWED okay::crypto. Never for a release (use -DOKAY_REQUIRE_SODIUM=ON)")
    endif()
endif()

# Steam: real Steamworks backend. Enable with -DOKAY_WITH_STEAM=ON and point at
# the SDK with -DSTEAMWORKS_SDK_PATH=/path/to/sdk (Valve's SDK is proprietary
# and cannot be bundled). Without this, the in-memory simulation backend is used.
if(OKAY_WITH_STEAM)
    if(NOT STEAMWORKS_SDK_PATH)
        message(FATAL_ERROR "OKAY_WITH_STEAM requires -DSTEAMWORKS_SDK_PATH=/path/to/steamworks_sdk")
    endif()
    if(WIN32)
        set(_steam_lib "${STEAMWORKS_SDK_PATH}/redistributable_bin/win64/steam_api64.lib")
    elseif(APPLE)
        set(_steam_lib "${STEAMWORKS_SDK_PATH}/redistributable_bin/osx/libsteam_api.dylib")
    else()
        set(_steam_lib "${STEAMWORKS_SDK_PATH}/redistributable_bin/linux64/libsteam_api.so")
    endif()
    target_sources(okay_engine PRIVATE src/Platform/Steam/SteamworksService.cpp)
    target_include_directories(okay_engine SYSTEM PRIVATE "${STEAMWORKS_SDK_PATH}/public")
    target_link_libraries(okay_engine PRIVATE "${_steam_lib}")
    target_compile_definitions(okay_engine PUBLIC OKAY_WITH_STEAM)
    message(STATUS "OkaySpace: Steamworks backend ENABLED (${_steam_lib})")
endif()

# Bake a default Steam App ID into the build (release uses the real id; dev stays
# 480 = Spacewar). Set with -DOKAY_STEAM_APP_ID=<id>.
if(DEFINED OKAY_STEAM_APP_ID)
    target_compile_definitions(okay_engine PUBLIC OKAY_STEAM_APP_ID=${OKAY_STEAM_APP_ID})
    message(STATUS "OkaySpace: default Steam App ID = ${OKAY_STEAM_APP_ID}")
endif()

# The platform seam (windows / events / GL context / timers / clipboard / audio device /
# gamepad): see cmake/OkayPlatform.cmake. Everything that used SDL for those goes through it.
include(${CMAKE_SOURCE_DIR}/cmake/OkayPlatform.cmake)
okay_add_platform_library()
target_link_libraries(okay_engine PUBLIC okay_platform)
# okay::platform::Painter (the 2D drawing surface of a window): the shared part and the sdl2 implementation.
# The software implementation lives in okayui/soft (okay_ui_soft). Own code, not ready, off by default.
target_sources(okay_engine PRIVATE src/Platform/Painter.cpp src/Platform/SdlPainter.cpp src/Platform/GLPainter.cpp)
if(OKAY_PLATFORM_SDL_INCLUDES)
    target_include_directories(okay_engine PRIVATE ${OKAY_PLATFORM_SDL_INCLUDES})
endif()
if(OKAY_PLATFORM_SDL_OPTIONS)
    set_source_files_properties(src/Platform/SdlPainter.cpp PROPERTIES COMPILE_OPTIONS "${OKAY_PLATFORM_SDL_OPTIONS}")
endif()

# The model reader defaults below (OKAY_OWN_<FMT>_DEFAULT) are read by ModelImport.cpp only, so they are put on that one
# file: switching a reader's default rebuilds one file, not the whole engine. Every model reader is our own (Assimp was
# removed on 2026-11-24); OBJ, STL, PLY, glTF and FBX have no switch. The rest are ON by default; a format switched off
# (-DOKAY_OWN_<FMT>=OFF, OKAY_OWN_<FMT>=0 at run time) is not read at all.
if(OKAY_OWN_COLLADA)
    set_property(SOURCE src/Render/ModelImport.cpp APPEND PROPERTY COMPILE_DEFINITIONS OKAY_OWN_COLLADA_DEFAULT)
    message(STATUS "OkaySpace: model import reads .dae with the built-in Collada reader first")
endif()
# Our own 3DS (.3ds) and BVH (.bvh) readers (run-time env OKAY_OWN_3DS=0/1 / OKAY_OWN_BVH=0/1 and SetImportBackend
# override the build default).
if(OKAY_OWN_3DS)
    set_property(SOURCE src/Render/ModelImport.cpp APPEND PROPERTY COMPILE_DEFINITIONS OKAY_OWN_3DS_DEFAULT)
    message(STATUS "OkaySpace: model import reads .3ds with the built-in 3DS reader first")
endif()
if(OKAY_OWN_BVH)
    set_property(SOURCE src/Render/ModelImport.cpp APPEND PROPERTY COMPILE_DEFINITIONS OKAY_OWN_BVH_DEFAULT)
    message(STATUS "OkaySpace: model import reads .bvh with the built-in BVH reader")
endif()
# Our own OFF (.off) reader (env OKAY_OWN_OFF=0/1 and SetImportBackend override the build default).
if(OKAY_OWN_OFF)
    set_property(SOURCE src/Render/ModelImport.cpp APPEND PROPERTY COMPILE_DEFINITIONS OKAY_OWN_OFF_DEFAULT)
    message(STATUS "OkaySpace: model import reads .off with the built-in OFF reader")
endif()
# Our own SMD reader (env OKAY_OWN_SMD=0/1 and SetImportBackend override the build default).
if(OKAY_OWN_SMD)
    set_property(SOURCE src/Render/ModelImport.cpp APPEND PROPERTY COMPILE_DEFINITIONS OKAY_OWN_SMD_DEFAULT)
    message(STATUS "OkaySpace: model import reads .smd with the built-in SMD reader")
endif()
# Our own MD5 reader (env OKAY_OWN_MD5=0/1 and SetImportBackend override the build default).
if(OKAY_OWN_MD5)
    set_property(SOURCE src/Render/ModelImport.cpp APPEND PROPERTY COMPILE_DEFINITIONS OKAY_OWN_MD5_DEFAULT)
    message(STATUS "OkaySpace: model import reads .md5mesh / .md5anim with the built-in MD5 reader")
endif()
# Our own MS3D reader (env OKAY_OWN_MS3D=0/1 and SetImportBackend override the build default).
if(OKAY_OWN_MS3D)
    set_property(SOURCE src/Render/ModelImport.cpp APPEND PROPERTY COMPILE_DEFINITIONS OKAY_OWN_MS3D_DEFAULT)
    message(STATUS "OkaySpace: model import reads .ms3d with the built-in MS3D reader")
endif()
# Our own ASE reader (env OKAY_OWN_ASE=0/1 and SetImportBackend override the build default).
if(OKAY_OWN_ASE)
    set_property(SOURCE src/Render/ModelImport.cpp APPEND PROPERTY COMPILE_DEFINITIONS OKAY_OWN_ASE_DEFAULT)
    message(STATUS "OkaySpace: model import reads .ase with the built-in ASE reader")
endif()
# Our own Blender (.blend) reader (docs/content/model_readers.md).
if(OKAY_OWN_BLEND)
    set_property(SOURCE src/Render/ModelImport.cpp APPEND PROPERTY COMPILE_DEFINITIONS OKAY_OWN_BLEND_DEFAULT)
    message(STATUS "OkaySpace: model import reads .blend with the built-in Blender reader first")
endif()
# Our own 3MF / AMF / X3D / DXF / LWO readers (batch B).
foreach(_fmt 3MF AMF X3D DXF LWO)
    if(OKAY_OWN_${_fmt})
        set_property(SOURCE src/Render/ModelImport.cpp APPEND PROPERTY COMPILE_DEFINITIONS OKAY_OWN_${_fmt}_DEFAULT)
        message(STATUS "OkaySpace: model import reads .${_fmt} with the built-in ${_fmt} reader first")
    endif()
endforeach()

# Our own MD2 / MD3 / IRRMESH / B3D / X readers (batch C), AC3D / OGRE / XGL / IRR / NFF (batch D), Terragen and MDC / HMP / CSM / Q3D / RAW (batch E) IQM / COB / NDO (batch E3) and PMX / LWS (batch E5).
# Guard: every model reader option the root CMakeLists.txt declares ("Read .xyz ... with our own ...") must be in a
# foreach(_fmt ...) list of this file (or have a literal OKAY_OWN_<FMT>_DEFAULT definition), or its default is silently never
# defined (a merge once dropped Terragen from this loop that way). Add a new reader's name to the literal list below.
file(STRINGS "${PROJECT_SOURCE_DIR}/CMakeLists.txt" _okay_own_opts REGEX "^option\\(OKAY_OWN_[A-Z0-9]+ +\"Read ")
file(READ "${CMAKE_CURRENT_LIST_FILE}" _okay_this_file)
string(REGEX MATCHALL "foreach\\(_fmt [A-Z0-9 ]+\\)" _okay_heads "${_okay_this_file}")
set(_okay_looped "")
foreach(_h IN LISTS _okay_heads)
    string(REGEX REPLACE "^foreach\\(_fmt ([A-Z0-9 ]+)\\)$" "\\1" _h "${_h}")
    string(REPLACE " " ";" _h "${_h}")
    list(APPEND _okay_looped ${_h})
endforeach()
foreach(_line IN LISTS _okay_own_opts)
    string(REGEX REPLACE "^option\\(OKAY_OWN_([A-Z0-9]+) .*$" "\\1" _name "${_line}")
    string(FIND "${_okay_this_file}" "COMPILE_DEFINITIONS OKAY_OWN_${_name}_DEFAULT)" _okay_lit)
    if(NOT _name IN_LIST _okay_looped AND _okay_lit EQUAL -1)
        message(FATAL_ERROR "OKAY_OWN_${_name} is an option in CMakeLists.txt but is in no foreach(_fmt ...) list of engine/CMakeLists.txt and has no OKAY_OWN_${_name}_DEFAULT definition: its default would never be applied")
    endif()
endforeach()
foreach(_fmt MD2 MD3 IRRMESH B3D X AC3D OGRE XGL IRR NFF TER MDC HMP CSM Q3D RAW IQM COB NDO PMX LWS OGREBIN ZAE XML LXO GLTF1 OPENGEX MDL Q3BSP SIB)
    if(OKAY_OWN_${_fmt})
        set_property(SOURCE src/Render/ModelImport.cpp APPEND PROPERTY COMPILE_DEFINITIONS OKAY_OWN_${_fmt}_DEFAULT)
        message(STATUS "OkaySpace: model import reads .${_fmt} with the built-in ${_fmt} reader first")
    endif()
endforeach()

# Friendly alias so subprojects can link okay::engine.
add_library(okay::engine ALIAS okay_engine)

# 1.1 Loop and time (docs/engine/loop_and_time.md): Time effects + clocks, Game pause reasons /
# quit guard, the shared GameLoop driver, dice parser. Clock.hpp and RandomExtras'
# templates are header-only.
target_sources(okay_engine PRIVATE
    src/Core/Time.cpp
    src/Core/Game.cpp
    src/Core/GameLoop.cpp
    src/Core/RandomExtras.cpp
)
# ---- Prefab system wave 2 (docs/engine/prefabs.md): nested prefabs, variants, unpack,
# per-record / per-field apply and revert, prefab queries and the asset validator.
target_sources(okay_engine PRIVATE src/Scene/PrefabTools.cpp src/Scene/PrefabResolve.cpp)
# Project tag list and layer names in the project settings file (docs/gameplay/cameras.md).
target_sources(okay_engine PRIVATE src/Scene/ProjectLists.cpp)
# ---- Property reflection (docs/engine/properties.md) ----
# PropertyValue / PropertyDesc, the registry with path access and change hook,
# snapshot / diff / copy, and the built-in component registrations.
target_sources(okay_engine PRIVATE
    src/Scene/Properties.cpp
    src/Scene/PropertyRegistry.cpp
    src/Scene/BuiltinProperties.cpp
    src/Scene/BuiltinPropertiesGameplay.cpp
    src/Scene/BuiltinPropertiesUI.cpp
)

# ---- Core runtime foundations: job system, memory arenas, handles and ids, frame pacing ----
# (docs/engine/core_runtime.md). Appended as a block; Jobs.cpp and StringId.cpp keep their
# threading behind engine/src/Core/CoreThreads.hpp (native Win32 primitives on Windows).
target_sources(okay_engine PRIVATE
    src/Core/Jobs.cpp
    src/Core/StringId.cpp
    src/Core/FramePacing.cpp
)
# Scene text tools for version control (docs/engine/scene_diff_merge.md): canonical form, diff,
# three-way merge and format migration. Text level only: none of these builds a Scene.
target_sources(okay_engine PRIVATE
    src/Scene/SceneText.cpp
    src/Scene/SceneTextDetail.cpp
    src/Scene/SceneDiff.cpp
    src/Scene/SceneMerge.cpp
    src/Scene/SceneMigration.cpp)

# 1.1 Loop and time, second pass (docs/engine/loop_and_time.md): frame recorder / player and the rolling
# frame-time statistics GameLoop feeds. TickPacer.hpp is header-only.
target_sources(okay_engine PRIVATE
    src/Core/FrameRecord.cpp
    src/Core/FrameStats.cpp
    src/Core/FrameOverlay.cpp
    src/Core/TimeSnapshot.cpp
    src/Core/LoopConsole.cpp
    src/Core/ConsoleHost.cpp
)

# The module list the engine knows at run time (okay/Modules/ModuleRegistry.hpp): the manifests embedded as text by
# cmake/OkayModules.cmake, plus the manifest parser and the registry. Every module is on in this stage.
okay_modules_generate("${CMAKE_CURRENT_BINARY_DIR}/modules_enabled.cpp")
get_property(_okay_mod_register GLOBAL PROPERTY OKAY_MODULE_REGISTER_SOURCES)
target_sources(okay_engine PRIVATE
    src/Modules/ModuleManifest.cpp
    src/Modules/ModuleRegistry.cpp
    src/Modules/ModuleMenus.cpp
    src/Render/RenderContributions.cpp
    "${CMAKE_CURRENT_BINARY_DIR}/modules_enabled.cpp"
    ${_okay_mod_register})

# Which module each engine file belongs to (engine/modules/*/module.okaymodule sources=). Keep this last: it reads the
# whole source list. Writes okay_modules.tsv in the build folder; the build itself is unchanged.
okay_modules_group(okay_engine)
