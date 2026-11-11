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
    src/Core/SaveFramework.cpp
    src/Core/SaveFrameworkScene.cpp
    src/Core/SaveFrameworkSync.cpp
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
    src/Core/AssetDatabase.cpp
    src/Core/AssetDatabaseDeps.cpp
    src/Core/AssetDatabaseSearch.cpp
    src/Core/ThumbnailCache.cpp
    src/Core/Localization.cpp
    src/Input/Input.cpp
    src/Input/InputActions.cpp
    src/Input/GameInput.cpp
    src/Core/DisplaySettings.cpp
    src/Render/ConsoleRenderer.cpp
    src/Render/GLRenderer.cpp
    src/Render/D3D11Renderer.cpp
    src/Render/D3D12Renderer.cpp
    src/Scene/Component.cpp
    src/Scene/GameObject.cpp
    src/Scene/Transform.cpp
    src/Scene/Scene.cpp
    src/Scene/SceneManager.cpp
    src/Scene/ObjectPool.cpp
    src/Scene/SceneSerializer.cpp
    src/Scene/ComponentSerializer.cpp
    src/Scene/SceneLocalization.cpp
    src/Components/Camera.cpp
    src/Components/LookAtIK.cpp
    src/Components/SpriteRenderer.cpp
    src/Net/UdpSocket.cpp
    src/Net/AntiCheat.cpp
    src/Net/SessionTicket.cpp
    src/Net/ClockCheck.cpp
    src/Components/AntiCheatConfig.cpp
    src/Components/Ragdoll.cpp
    src/Components/NetworkObject.cpp
    src/Net/NetworkManager.cpp
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
    src/VisualScript/VsTypes.cpp
    src/VisualScript/VsApi.cpp
    src/VisualScript/VsBuiltins.cpp
    src/VisualScript/VsVariables.cpp
    src/VisualScript/VsDebug.cpp
    src/VisualScript/ActionGraph.cpp
    src/Components/VisualScriptComponent.cpp
    src/Components/UIAnimation.cpp
    src/Scripting/OkayScriptVM.cpp
    src/Scripting/ScriptVM.cpp
    src/Script/Lexer.cpp
    src/Script/Parser.cpp
    src/Script/Checker.cpp
    src/Script/Interpreter.cpp
    src/Script/EngineApi.cpp
    src/Script/NextScriptVM.cpp
    src/Components/ScriptComponent.cpp
    src/Components/AnimControl.cpp
    src/Components/ActionList.cpp
    src/Inventory/InventoryOps.cpp
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
    src/Components/UIDamageNumbers.cpp
    src/Components/UIDialogueBox.cpp
    src/Components/UIThemeAsset.cpp
    src/Components/UIDraggable.cpp
    src/Components/Draggable.cpp
    src/Components/Terrain.cpp
    src/Components/VoxelTerrain.cpp
    src/Components/Character.cpp
    src/Components/ModelAnimatorPose.cpp
    src/Components/PlayerView.cpp
    src/Components/PlayerController.cpp
    src/Components/CharacterAnimBackend.cpp
    src/Animation/HumanoidMotion.cpp
    src/Animation/HumanoidLibrary.cpp
    src/Animation/PlayerSetup.cpp
    src/Animation/HumanoidActions.cpp
    src/Components/AnimClip.cpp
    src/Components/Sequence.cpp
    src/Components/SequencePlayer.cpp
    src/Components/Spline.cpp
    src/Components/SplineMesh.cpp
    src/Components/LineRenderer.cpp
    src/Components/BuildMode.cpp
    src/Components/DialogueGraph.cpp
    src/Components/DialogueRunner.cpp
    src/Components/QuestLog.cpp
    src/Platform/Steam/NullSteamService.cpp
    src/Platform/Steam/Steam.cpp
    src/Components/SteamManager.cpp
    src/Platform/Account/Account.cpp
    src/Physics/Physics2D.cpp
    src/Physics/Physics3D.cpp
    src/Audio/AudioMixer.cpp
    src/Audio/AudioClip.cpp
    src/Audio/AudioDecode.cpp
    src/Audio/VorbisDecoder.cpp
    src/Audio/Mp3Decoder.cpp
    src/AI/Pathfinding.cpp
    src/AI/NavMesh.cpp
    src/AI/NavMeshBake.cpp
    src/AI/NavMeshSurface.cpp
    src/AI/NavMeshAgent.cpp
    src/AI/Navigation.cpp
    src/AI/BehaviorTree.cpp
    src/AI/BehaviorTreeRunner.cpp
    src/Core/ImageWrite.cpp
    src/Graphics/Image.cpp
    src/Graphics/Font.cpp
    src/Graphics/TtfFont.cpp
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
    src/UI/UIBatcher.cpp
    src/UI/UILayoutUpdate.cpp
    # Localization tools (docs/localization_tools.md)
    src/Core/LocCheck.cpp
    src/Core/LocTable.cpp
    src/Core/LocScan.cpp
    src/Core/LocPreview.cpp
    # Accessibility (docs/accessibility.md)
    src/Graphics/ColorVision.cpp
    src/Core/Subtitles.cpp
    src/Core/AccessibilitySettings.cpp
    src/UI/FocusGraph.cpp
    src/Input/RemapModel.cpp
    src/Render/ModelImport.cpp
    src/Render/ModelReaders.cpp
    src/Modeling/EditTransform.cpp
    src/Modeling/PolyMesh.cpp
    src/Modeling/MeshAsset.cpp
    src/Modeling/PolyOps.cpp
    src/Modeling/Modifiers.cpp
    src/Modeling/UVUnwrap.cpp
    src/Components/LODGroup.cpp
    src/Render/Visibility.cpp
    src/Optimize/MeshSimplify.cpp
    src/Optimize/LodGroup.cpp
    src/Optimize/OcclusionBuffer.cpp
    src/Optimize/PortalCells.cpp
    src/Optimize/BatchPlanner.cpp
    src/Optimize/StreamingGrid.cpp
    src/Optimize/MipBudget.cpp
    src/Optimize/SceneOptimizationReport.cpp
    src/Testing/TestRegistry.cpp
    src/Testing/PlayTest.cpp
    src/Testing/ObjectPath.cpp
    src/Testing/SceneValidator.cpp
    src/Testing/Regression.cpp
    src/Testing/GoldenImage.cpp
    third_party/ufbx/ufbx.c
)

# Public headers live under engine/include; consumers use #include <Okay.hpp>.
target_include_directories(okay_engine PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)

# Vendored single-header libraries (stb_image), used only by
# the Image translation unit. Kept PRIVATE so it doesn't leak to consumers.
target_include_directories(okay_engine PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/stb
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/ufbx
)
# ufbx (FBX importer, MIT/public domain) is a single C file: no warnings, optimised
# even in debug builds (parsing big FBX files unoptimised is painfully slow).
if(MSVC)
    set_source_files_properties(third_party/ufbx/ufbx.c PROPERTIES COMPILE_OPTIONS "/w;/O2")
else()
    set_source_files_properties(third_party/ufbx/ufbx.c PROPERTIES COMPILE_OPTIONS "-w;-O2")
endif()
# Silence warnings from the third-party single-header libraries. `-Wno-narrowing`
# is required in addition to `-w`: vendored tables (stb) may initialise `char`
# arrays with 0xFF, which Clang (Emscripten/WASM) treats as a hard narrowing ERROR
# that `-w` does not downgrade, even though GCC/MinGW accept it.
set_source_files_properties(src/Graphics/Image.cpp src/Graphics/TtfFont.cpp
    PROPERTIES COMPILE_OPTIONS "-w;-Wno-narrowing")
# The built-in UI font (the launcher's Roboto) lives in shared/RobotoFont.h.
set_source_files_properties(src/Graphics/EmbeddedFont.cpp PROPERTIES
    INCLUDE_DIRECTORIES ${CMAKE_CURRENT_SOURCE_DIR}/../shared)
# Only BuildInfo.cpp sees the version string, so a version bump recompiles one file.
set_source_files_properties(src/Core/BuildInfo.cpp PROPERTIES COMPILE_DEFINITIONS "${OKAY_VERSION_DEFINE}")
# The network layer signs its key exchange with #269's ed25519 (shared/AntiCheatCrypto.hpp)
# and runs its clock heartbeat (shared/AntiCheatChecks.hpp); the dedicated server
# files anti-cheat kicks with #270's request builder (shared/AntiCheatBackend.hpp);
# the relay makes its hello cookies with HmacSha256 (shared/AntiCheatChecks.hpp).
set_source_files_properties(src/Net/NetworkManager.cpp src/Net/ClockCheck.cpp src/Net/DedicatedServer.cpp
                            src/Net/RelayServer.cpp PROPERTIES
    INCLUDE_DIRECTORIES ${CMAKE_CURRENT_SOURCE_DIR}/../shared)

target_compile_features(okay_engine PUBLIC cxx_std_17)

find_package(Threads REQUIRED)
target_link_libraries(okay_engine PUBLIC Threads::Threads)

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
# Common-cpp/. See docs/photon.md for the full walkthrough.
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
# Without it the transport still builds and runs — just unencrypted (the
# NetworkManager encryption toggle becomes a no-op), so it's a soft dependency.
if(OKAY_USE_SODIUM)
    set(_okay_sodium FALSE)
    if(OKAY_SODIUM_PREFIX)
        # Explicit prefix (e.g. the MinGW prebuilt): reference the static lib directly
        # rather than via find_library, which is fussy across cross toolchains.
        set(_sodium_a "${OKAY_SODIUM_PREFIX}/lib/libsodium.a")
        if(EXISTS "${_sodium_a}" AND EXISTS "${OKAY_SODIUM_PREFIX}/include/sodium.h")
            target_include_directories(okay_engine SYSTEM PUBLIC "${OKAY_SODIUM_PREFIX}/include")
            target_link_libraries(okay_engine PUBLIC "${_sodium_a}")
            target_compile_definitions(okay_engine PUBLIC SODIUM_STATIC=1)  # static .a on Windows
            set(_okay_sodium TRUE)
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
    if(_okay_sodium)
        target_compile_definitions(okay_engine PUBLIC OKAY_HAVE_SODIUM)
        message(STATUS "OkaySpace: packet encryption ENABLED (libsodium)")
    else()
        message(STATUS "OkaySpace: libsodium not found; packet encryption disabled")
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

# ---- Optional model import via Assimp (FBX / DAE / STL / PLY / 3DS / ...) ----
# The built-in loaders already cover OBJ + glTF with no dependency; Assimp adds the
# long tail of formats. FetchContent DOWNLOADS Assimp at configure time, so this is
# OFF by default to keep the default build self-contained (no network needed). Turn it
# on with -DOKAY_USE_ASSIMP=ON; ModelImport.cpp then routes those formats through it.
if(OKAY_USE_ASSIMP)
    include(FetchContent)
    # Build only what we need — keep it lean and quiet. Static, so shipped builds
    # don't need an extra assimp DLL beside the exes; assimp builds its own zlib.
    set(BUILD_SHARED_LIBS                        OFF CACHE BOOL "" FORCE)
    set(ASSIMP_BUILD_ZLIB                        ON  CACHE BOOL "" FORCE)
    set(ASSIMP_BUILD_TESTS                       OFF CACHE BOOL "" FORCE)
    set(ASSIMP_INSTALL                           OFF CACHE BOOL "" FORCE)
    set(ASSIMP_BUILD_ASSIMP_TOOLS                OFF CACHE BOOL "" FORCE)
    set(ASSIMP_BUILD_ALL_IMPORTERS_BY_DEFAULT    OFF CACHE BOOL "" FORCE)
    set(ASSIMP_BUILD_ALL_EXPORTERS_BY_DEFAULT    OFF CACHE BOOL "" FORCE)
    set(ASSIMP_NO_EXPORT                         ON  CACHE BOOL "" FORCE)
    set(ASSIMP_WARNINGS_AS_ERRORS                OFF CACHE BOOL "" FORCE)
    set(ASSIMP_BUILD_FBX_IMPORTER       ON CACHE BOOL "" FORCE)
    set(ASSIMP_BUILD_OBJ_IMPORTER       ON CACHE BOOL "" FORCE)
    set(ASSIMP_BUILD_GLTF_IMPORTER      ON CACHE BOOL "" FORCE)
    set(ASSIMP_BUILD_COLLADA_IMPORTER   ON CACHE BOOL "" FORCE)
    set(ASSIMP_BUILD_STL_IMPORTER       ON CACHE BOOL "" FORCE)
    set(ASSIMP_BUILD_PLY_IMPORTER       ON CACHE BOOL "" FORCE)
    set(ASSIMP_BUILD_3DS_IMPORTER       ON CACHE BOOL "" FORCE)
    set(ASSIMP_BUILD_BLEND_IMPORTER     ON CACHE BOOL "" FORCE)
    set(ASSIMP_BUILD_X_IMPORTER         ON CACHE BOOL "" FORCE)   # DirectX .x
    set(ASSIMP_BUILD_MD5_IMPORTER       ON CACHE BOOL "" FORCE)   # Doom 3
    set(ASSIMP_BUILD_SMD_IMPORTER       ON CACHE BOOL "" FORCE)   # Source engine
    set(ASSIMP_BUILD_MS3D_IMPORTER      ON CACHE BOOL "" FORCE)   # MilkShape 3D
    set(ASSIMP_BUILD_LWO_IMPORTER       ON CACHE BOOL "" FORCE)   # LightWave
    set(ASSIMP_BUILD_DXF_IMPORTER       ON CACHE BOOL "" FORCE)   # AutoCAD DXF
    set(ASSIMP_BUILD_OFF_IMPORTER       ON CACHE BOOL "" FORCE)   # Object File Format
    set(ASSIMP_BUILD_AC_IMPORTER        ON CACHE BOOL "" FORCE)   # AC3D
    set(ASSIMP_BUILD_B3D_IMPORTER       ON CACHE BOOL "" FORCE)   # Blitz3D
    # ...and the rest of the long tail, so "any model file" just imports.
    foreach(_fmt 3MF AMF ASE X3D OGRE OPENGEX Q3D Q3BSP IRR IRRMESH MD2 MD3 MDL HMP MDC NFF RAW COB CSM
                 LWS BVH TERRAGEN SIB MMD IQM NDO XGL HMP)
        set(ASSIMP_BUILD_${_fmt}_IMPORTER ON CACHE BOOL "" FORCE)
    endforeach()
    FetchContent_Declare(assimp
        GIT_REPOSITORY https://github.com/assimp/assimp.git
        GIT_TAG        v5.4.3
        GIT_SHALLOW    TRUE)
    FetchContent_MakeAvailable(assimp)
    target_link_libraries(okay_engine PUBLIC assimp)
    target_compile_definitions(okay_engine PUBLIC OKAY_HAVE_ASSIMP)
    target_sources(okay_engine PRIVATE src/Render/ModelImportAssimp.cpp)   # the only file that includes Assimp
    message(STATUS "OkaySpace: Assimp model import ENABLED (DAE/STL/PLY/3DS/Blend/X/3MF/AMF/X3D/MD2/MD3/MD5/MDL/SMD/MS3D/LWO/DXF/OFF/AC/B3D/BVH/PMX/...)")
endif()

# Our own STL / PLY readers are always built. With Assimp also on, this picks which one
# a format uses by default (docs/model_import.md); SetImportBackend() changes it at run time.
if(OKAY_PREFER_OWN_READERS)
    target_compile_definitions(okay_engine PRIVATE OKAY_PREFER_OWN_READERS_DEFAULT)
    message(STATUS "OkaySpace: model import prefers the built-in STL/PLY readers over Assimp")
endif()

# Friendly alias so subprojects can link okay::engine.
add_library(okay::engine ALIAS okay_engine)

# 1.1 Loop and time (docs/loop_and_time.md): Time effects + clocks, Game pause reasons /
# quit guard, the shared GameLoop driver, dice parser. Clock.hpp and RandomExtras'
# templates are header-only.
target_sources(okay_engine PRIVATE
    src/Core/Time.cpp
    src/Core/Game.cpp
    src/Core/GameLoop.cpp
    src/Core/RandomExtras.cpp
)
# ---- Prefab system wave 2 (docs/prefab_system.md): nested prefabs, variants, unpack,
# per-record / per-field apply and revert, prefab queries and the asset validator.
target_sources(okay_engine PRIVATE src/Scene/PrefabTools.cpp)
# Project tag list and layer names in the project settings file (docs/layers_tags_cameras.md).
target_sources(okay_engine PRIVATE src/Scene/ProjectLists.cpp)
# ---- Property reflection (docs/properties.md) ----
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
# (docs/core_runtime.md). Appended as a block; Jobs.cpp and StringId.cpp keep their
# threading behind engine/src/Core/CoreThreads.hpp (native Win32 primitives on Windows).
target_sources(okay_engine PRIVATE
    src/Core/Jobs.cpp
    src/Core/StringId.cpp
    src/Core/FramePacing.cpp
)
# Scene text tools for version control (docs/scene_diff_merge.md): canonical form, diff,
# three-way merge and format migration. Text level only: none of these builds a Scene.
target_sources(okay_engine PRIVATE
    src/Scene/SceneText.cpp
    src/Scene/SceneTextDetail.cpp
    src/Scene/SceneDiff.cpp
    src/Scene/SceneMerge.cpp
    src/Scene/SceneMigration.cpp)

# 1.1 Loop and time, second pass (docs/loop_and_time.md): frame recorder / player and the rolling
# frame-time statistics GameLoop feeds. TickPacer.hpp is header-only.
target_sources(okay_engine PRIVATE
    src/Core/FrameRecord.cpp
    src/Core/FrameStats.cpp
    src/Core/FrameOverlay.cpp
    src/Core/TimeSnapshot.cpp
    src/Core/LoopConsole.cpp
)
