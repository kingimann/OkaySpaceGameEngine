cmake_minimum_required(VERSION 3.16)
project(OkaySpaceGameEngine
    VERSION 2.903.4
    DESCRIPTION "A modern C++ game engine"
    LANGUAGES C CXX)   # C enabled for vendored single-file C libs (ufbx)

# Stamp the version into binaries (used by the editor's self-updater).
# The version string is only compiled into the editor and launcher (their targets
# add OKAY_ENGINE_VERSION), so a version bump doesn't change every file's compile
# flags and the release build's compiler cache keeps hitting.
include(${CMAKE_SOURCE_DIR}/cmake/OkayWinVersion.cmake)   # okay_win_version(): Windows exe name / version / manifest
set(OKAY_VERSION_DEFINE OKAY_ENGINE_VERSION="${PROJECT_VERSION}")

# ---- Global settings --------------------------------------------------
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE "Release" CACHE STRING "Build type" FORCE)
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(OKAY_BUILD_SANDBOX  "Build the sandbox demo game"       ON)
option(OKAY_BUILD_TESTS    "Build the unit tests"              ON)
option(OKAY_BUILD_LAUNCHER "Build the self-updating launcher"  ON)
option(OKAY_BUILD_EDITOR   "Build the desktop GUI editor (needs SDL2+OpenGL)" OFF)
option(OKAY_ENABLE_TEST_HOOKS "Compile scripted input and screenshots (OKAY_TEST_SCRIPT, shared/TestHooks.hpp) into the launcher and editor for headless test runs. Never for a release." OFF)
option(OKAY_BUILD_PLAYER   "Build the standalone game player runtime (needs SDL2)" OFF)
option(OKAY_USE_SODIUM     "Authenticated packet encryption via libsodium"     ON)
option(OKAY_REQUIRE_SODIUM "RELEASE builds: configuring FAILS unless libsodium is found and OKAY_OWN_CRYPTO is OFF (never silently fall back to our own unreviewed crypto; CLAUDE.md rule 18)" OFF)
if(OKAY_ENABLE_TEST_HOOKS AND OKAY_REQUIRE_SODIUM)
    message(FATAL_ERROR "OKAY_ENABLE_TEST_HOOKS (scripted input) must never be on in a release build (OKAY_REQUIRE_SODIUM marks one)")
endif()
option(OKAY_OWN_CRYPTO    "Network encryption via our OWN okay::crypto instead of libsodium (UNREVIEWED; keep OFF in shipped builds)" OFF)
option(OKAY_USE_ASSIMP     "Import FBX/DAE/STL/... via Assimp (FetchContent; downloads at configure time)" OFF)
option(OKAY_PREFER_OWN_READERS "With OKAY_USE_ASSIMP: read STL/PLY with our own readers first (Assimp stays the fallback); no effect without Assimp" ON)
# The UI toolkit the launcher and editor use for the screens that have an OkayUI port. Dear ImGui stays the default until
# OkayUI is proven; a person can still choose OkayUI in Settings, or with the OKAY_UI_BACKEND environment variable.
set(OKAY_UI_DEFAULT_BACKEND "imgui" CACHE STRING "Which UI toolkit the launcher and editor start with: imgui (default) or okayui (preview)")
set_property(CACHE OKAY_UI_DEFAULT_BACKEND PROPERTY STRINGS imgui okayui)
if(NOT OKAY_UI_DEFAULT_BACKEND STREQUAL "imgui" AND NOT OKAY_UI_DEFAULT_BACKEND STREQUAL "okayui")
    message(FATAL_ERROR "OKAY_UI_DEFAULT_BACKEND must be imgui or okayui (got '${OKAY_UI_DEFAULT_BACKEND}')")
endif()
option(OKAY_BUILD_OKAYUI_TESTS "Build the headless OkayUI software backend, its tests (behaviour, golden images, fuzz) and demo (small; needs OKAY_BUILD_TESTS and SDL2)" ON)
# Our own model readers are the DEFAULT since 2026-11-17 (owner: "start switching them so i can test them out"). Assimp and ufbx
# stay compiled in as the fallback: a file our reader refuses goes to them, OKAY_OWN_<FMT>=0 switches one format back and
# OKAY_THIRD_PARTY=1 (okay/Core/ThirdParty.hpp) switches every format back at run time. NDO stays OFF: that reader is a seam that
# refuses every file. An existing build folder keeps the old OFF values in its CMakeCache.txt: configure a fresh one.
# Since 2026-11-23 (owner: "i want our stuff to be on by default, all of them") the last four readers (glTF 1.0, OpenGEX, MDL,
# Q3BSP), our own WAV decoder and our own HTTP client (Linux; Windows uses WinHTTP) are ON by default too. Still OFF: NDO (a seam
# that refuses every file), OKAY_OWN_CRYPTO (owner rule 18), OKAY_OWN_STACK (a dogfood switch releases refuse) and the two extra
# OkayUI programs (OKAY_LAUNCHER_ON_OKAYUI / OKAY_EDITOR_ON_OKAYUI: not built by CI yet; docs/status/own_code_roadmap.md).
option(OKAY_OWN_COLLADA   "Read .dae with our own Collada reader by default (Assimp, when built, stays the fallback; without Assimp this is what lets .dae load at all; run-time env OKAY_OWN_COLLADA=0/1 and SetImportBackend override)" ON)
option(OKAY_OWN_3DS       "Read .3ds with our own 3DS reader by default (Assimp, when built, stays the fallback; without Assimp this is what lets .3ds load at all; run-time env OKAY_OWN_3DS=0/1 and SetImportBackend override)" ON)
option(OKAY_OWN_BVH       "Read .bvh motion capture with our own BVH reader by default (run-time env OKAY_OWN_BVH=0/1 and SetImportBackend override)" ON)
option(OKAY_OWN_WAV       "Load .wav with our own WAV decoder (PCM, float, A-law, mu-law, MS and IMA ADPCM) by default (run-time env OKAY_OWN_WAV=0/1 overrides)" ON)
option(OKAY_OWN_OFF       "Read .off meshes with our own OFF reader by default (run-time env OKAY_OWN_OFF=0/1 and SetImportBackend override)" ON)
option(OKAY_OWN_SMD       "Read .smd (Valve Studiomdl Data) with our own SMD reader by default (run-time env OKAY_OWN_SMD=0/1 and SetImportBackend override)" ON)
option(OKAY_OWN_MD5       "Read .md5mesh / .md5anim (id Tech 4 / Doom 3) with our own MD5 reader by default (run-time env OKAY_OWN_MD5=0/1 and SetImportBackend override)" ON)
option(OKAY_OWN_MS3D       "Read .ms3d (MilkShape 3D) with our own MS3D reader by default (run-time env OKAY_OWN_MS3D=0/1 and SetImportBackend override)" ON)
option(OKAY_OWN_ASE       "Read .ase (3ds Max ASCII Scene Export) with our own ASE reader by default (run-time env OKAY_OWN_ASE=0/1 and SetImportBackend override)" ON)
option(OKAY_OWN_BLEND     "Read .blend with our own Blender reader by default (Assimp, when built, stays the fallback; without Assimp this is what lets .blend load at all; run-time env OKAY_OWN_BLEND=0/1 and SetImportBackend override)" ON)
option(OKAY_OWN_HTTP      "Allow our own HTTP/1.1 client (system OpenSSL loaded at run time) on Linux for NativeHttp instead of spawning curl; still needs OKAY_OWN_HTTP=1 in the environment at run time, curl stays the fallback (docs/platform/own_http.md)" ON)
option(OKAY_OWN_3MF       "Read .3mf with our own 3MF reader by default (Assimp, when built, stays the fallback; run-time env OKAY_OWN_3MF=0/1 and SetImportBackend override)" ON)
option(OKAY_OWN_AMF       "Read .amf with our own AMF reader by default (Assimp, when built, stays the fallback; run-time env OKAY_OWN_AMF=0/1 and SetImportBackend override)" ON)
option(OKAY_OWN_X3D       "Read .x3d with our own X3D reader by default (Assimp, when built, stays the fallback; run-time env OKAY_OWN_X3D=0/1 and SetImportBackend override)" ON)
option(OKAY_OWN_DXF       "Read .dxf with our own DXF reader by default (Assimp, when built, stays the fallback; run-time env OKAY_OWN_DXF=0/1 and SetImportBackend override)" ON)
option(OKAY_OWN_LWO       "Read .lwo with our own LWO reader by default (Assimp, when built, stays the fallback; run-time env OKAY_OWN_LWO=0/1 and SetImportBackend override)" ON)
option(OKAY_OWN_MD2       "Read .md2 (Quake II models) with our own MD2 reader by default (Assimp, when built, stays the fallback; run-time env OKAY_OWN_MD2=0/1 and SetImportBackend override)" ON)
option(OKAY_OWN_MD3       "Read .md3 (Quake III Arena models) with our own MD3 reader by default (Assimp, when built, stays the fallback; run-time env OKAY_OWN_MD3=0/1 and SetImportBackend override)" ON)
option(OKAY_OWN_IRRMESH   "Read .irrmesh (Irrlicht meshes) with our own IRRMESH reader by default (Assimp, when built, stays the fallback; run-time env OKAY_OWN_IRRMESH=0/1 and SetImportBackend override)" ON)
option(OKAY_OWN_B3D       "Read .b3d (Blitz3D models) with our own B3D reader by default (Assimp, when built, stays the fallback; run-time env OKAY_OWN_B3D=0/1 and SetImportBackend override)" ON)
option(OKAY_OWN_X         "Read .x (DirectX models) with our own X reader by default (Assimp, when built, stays the fallback; run-time env OKAY_OWN_X=0/1 and SetImportBackend override)" ON)
option(OKAY_OWN_AC3D     "Read .ac / .acc / .ac3d (AC3D models) with our own AC3D reader by default (Assimp, when built, stays the fallback; run-time env OKAY_OWN_AC3D=0/1 and SetImportBackend override)" ON)
option(OKAY_OWN_OGRE     "Read .mesh.xml (OGRE XML meshes) with our own OGRE reader by default (Assimp, when built, stays the fallback; run-time env OKAY_OWN_OGRE=0/1 and SetImportBackend override)" ON)
option(OKAY_OWN_XGL      "Read .xgl / .zgl (XGL scenes) with our own XGL reader by default (Assimp, when built, stays the fallback; run-time env OKAY_OWN_XGL=0/1 and SetImportBackend override)" ON)
option(OKAY_OWN_IRR      "Read .irr (Irrlicht scenes) with our own IRR reader by default (Assimp, when built, stays the fallback; run-time env OKAY_OWN_IRR=0/1 and SetImportBackend override)" ON)
option(OKAY_OWN_NFF      "Read .nff / .enff (Neutral File Format) with our own NFF reader by default (Assimp, when built, stays the fallback; run-time env OKAY_OWN_NFF=0/1 and SetImportBackend override)" ON)
option(OKAY_OWN_IQM      "Read .iqm (Inter-Quake Models) with our own IQM reader by default (Assimp, when built, stays the fallback; run-time env OKAY_OWN_IQM=0/1 and SetImportBackend override)" ON)
option(OKAY_OWN_COB      "Read .cob (Caligari trueSpace) with our own COB reader by default (Assimp, when built, stays the fallback; run-time env OKAY_OWN_COB=0/1 and SetImportBackend override)" ON)
option(OKAY_OWN_NDO      "Read .ndo (Nendo; the reader is a header-only seam and refuses every file) with our own NDO reader by default (Assimp, when built, stays the fallback; run-time env OKAY_OWN_NDO=0/1 and SetImportBackend override)" OFF)
option(OKAY_OWN_MDC      "Read .mdc (Return to Castle Wolfenstein models) with our own MDC reader by default (Assimp, when built, stays the fallback; run-time env OKAY_OWN_MDC=0/1 and SetImportBackend override)" ON)
option(OKAY_OWN_HMP      "Read .hmp (3D GameStudio HMP7 terrain) with our own HMP reader by default (Assimp, when built, stays the fallback; run-time env OKAY_OWN_HMP=0/1 and SetImportBackend override)" ON)
option(OKAY_OWN_CSM      "Read .csm (Character Studio motion capture) with our own CSM reader by default (Assimp, when built, stays the fallback; run-time env OKAY_OWN_CSM=0/1 and SetImportBackend override)" ON)
option(OKAY_OWN_TER      "Read .ter (Terragen terrain) with our own Terragen reader by default (Assimp, when built, stays the fallback; run-time env OKAY_OWN_TER=0/1 and SetImportBackend override)" ON)
option(OKAY_OWN_Q3D      "Read .q3o / .q3s (Quick3D) with our own Q3D reader by default (Assimp, when built, stays the fallback; run-time env OKAY_OWN_Q3D=0/1 and SetImportBackend override)" ON)
option(OKAY_OWN_RAW      "Read .raw (plain triangle text) with our own RAW reader by default (Assimp, when built, stays the fallback; run-time env OKAY_OWN_RAW=0/1 and SetImportBackend override)" ON)
option(OKAY_OWN_PMX      "Read .pmx (MikuMikuDance models) with our own PMX reader by default (Assimp, when built, stays the fallback; run-time env OKAY_OWN_PMX=0/1 and SetImportBackend override)" ON)
option(OKAY_OWN_OGREBIN  "Read .mesh (OGRE binary meshes) with our own OGRE binary reader by default (Assimp, when built, stays the fallback; run-time env OKAY_OWN_OGREBIN=0/1 and SetImportBackend override)" ON)
option(OKAY_OWN_ZAE      "Read .zae (COLLADA in a ZIP archive) with our own ZAE reader by default (Assimp, when built, stays the fallback; run-time env OKAY_OWN_ZAE=0/1 and SetImportBackend override)" ON)
option(OKAY_OWN_XML      "Read .xml (an Irrlicht scene or mesh, COLLADA, XGL or OGRE XML, told apart by the root element) with our own readers by default (Assimp, when built, stays the fallback; run-time env OKAY_OWN_XML=0/1 and SetImportBackend override)" ON)
option(OKAY_OWN_LXO      "Read .lxo (Modo objects) with our own LWO reader by default (Assimp, when built, stays the fallback; run-time env OKAY_OWN_LXO=0/1 and SetImportBackend override)" ON)
option(OKAY_OWN_OPENGEX  "Read .ogex (Open Game Engine Exchange) with our own OpenGEX reader by default (Assimp, when built, stays the fallback; run-time env OKAY_OWN_OPENGEX=0/1 and SetImportBackend override)" ON)
option(OKAY_OWN_MDL      "Read .mdl (Quake 1 and Half-Life 1 models) with our own MDL reader by default (Assimp, when built, stays the fallback; run-time env OKAY_OWN_MDL=0/1 and SetImportBackend override)" ON)
option(OKAY_OWN_Q3BSP    "Read .bsp / .pk3 (Quake III Arena maps) with our own Q3BSP reader by default (Assimp, when built, stays the fallback; run-time env OKAY_OWN_Q3BSP=0/1 and SetImportBackend override)" ON)
option(OKAY_OWN_GLTF1    "Read glTF 1.0 files (.gltf / .glb of version 1) with our own glTF 1.0 reader by default (without it they are refused: .gltf / .glb never go to Assimp; run-time env OKAY_OWN_GLTF1=0/1 and SetImportBackend(\".gltf1\") override)" ON)
option(OKAY_OWN_LWS      "Read .lws / .mot (LightWave scenes and motions) with our own LWS reader by default (Assimp, when built, stays the fallback; run-time env OKAY_OWN_LWS=0/1 and SetImportBackend override)" ON)
option(OKAY_OWN_FBX       "Use our own FBX reader (okay::fbx) instead of ufbx by default (ufbx stays compiled in; run-time env OKAY_OWN_FBX=0/1 overrides)" ON)
option(OKAY_BUILD_PLATFORM_SELFTEST "Build okay-platform-selftest: tries the own okay::platform backend (window, input, audio, pad, clipboard) and prints what worked. OFF by default" OFF)
option(OKAY_BUILD_TOOLS    "Build developer tools (texture generator, ...)"    OFF)
option(OKAY_BUILD_XBOX_UWP "EXPERIMENTAL, UNVERIFIED Xbox One Developer Mode (UWP) export: its tests, the editor entry and (on a UWP toolchain) the app. platform/xbox_uwp, docs/platform/xbox_uwp.md" OFF)

# ---- One switch to dogfood the whole own stack (docs/status/own_stack_rollout.md) -------------------------------------
# -DOKAY_OWN_STACK=ON turns on every own backend that is safe to try in a developer or CI build: our own model readers
# (FBX and every format that has one), our own HTTP client (Linux), our own okay::platform backend as the default (Linux),
# and OkayUI as the launcher / editor UI default. Each is still an ordinary option or run-time switch underneath and each
# keeps its third-party fallback. It changes only the build DEFAULTS; the run-time switches (OKAY_OWN_<FMT>=0, OKAY_OWN_HTTP=0,
# OKAY_PLATFORM_BACKEND=sdl2, OKAY_UI_BACKEND=imgui) still win. NOT turned on, ever, by this switch: our own cryptography
# (owner rule: needs an outside review first). OKAY_OWN_STACK_CRYPTO_TEST=ON (or that variable set to 1 in the environment at
# configure time) additionally turns on OKAY_OWN_CRYPTO, for TEST builds and the CI own-stack job only. Releases refuse both
# (scripts/check_release_own_stack.sh, called by scripts/build-windows-dist.sh and release.yml).
option(OKAY_OWN_STACK "Developer/CI dogfood build: own readers, own HTTP, own platform backend and OkayUI by default (never crypto; see docs/status/own_stack_rollout.md). NEVER for releases" OFF)
option(OKAY_OWN_STACK_CRYPTO_TEST "With OKAY_OWN_STACK: ALSO use our own UNREVIEWED crypto (OKAY_OWN_CRYPTO). Test builds and CI only; releases refuse it" OFF)
option(OKAY_LAUNCHER_ON_OKAYUI "Also build OkaySpaceOkayUI(.exe): the SAME launcher sources compiled against OkayUI's Dear ImGui compatibility layer (no Dear ImGui). Off by default; docs/ui/own_ui_toolkit.md" OFF)
option(OKAY_EDITOR_ON_OKAYUI   "Also build OkayEngineOkayUI(.exe): the SAME editor sources compiled against OkayUI's Dear ImGui compatibility layer (no Dear ImGui). Off by default; docs/ui/own_ui_toolkit.md" OFF)
if(NOT OKAY_OWN_STACK_CRYPTO_TEST AND "$ENV{OKAY_OWN_STACK_CRYPTO_TEST}" STREQUAL "1")
    set(OKAY_OWN_STACK_CRYPTO_TEST ON)
endif()
if(OKAY_OWN_STACK_CRYPTO_TEST AND NOT OKAY_OWN_STACK)
    message(FATAL_ERROR "OKAY_OWN_STACK_CRYPTO_TEST needs OKAY_OWN_STACK=ON (it is a test-build extra of the dogfood switch)")
endif()
if(OKAY_OWN_STACK)
    foreach(_f COLLADA 3DS BVH OFF SMD MD5 MS3D ASE BLEND 3MF AMF X3D DXF LWO MD2 MD3 IRRMESH B3D X FBX)
        set(OKAY_OWN_${_f} ON)
    endforeach()
    set(OKAY_OWN_HTTP ON)   # Linux only in effect (the engine adds the define only for non-Windows, non-Apple)
    if(OKAY_OWN_STACK_CRYPTO_TEST)
        set(OKAY_OWN_CRYPTO ON)
        message(WARNING "OkaySpace: OKAY_OWN_STACK_CRYPTO_TEST=ON uses our own UNREVIEWED crypto. Test build only: do not ship it")
    endif()
    message(STATUS "OkaySpace: OKAY_OWN_STACK=ON (dogfood build: own readers, HTTP, platform backend and OkayUI by default; crypto ${OKAY_OWN_CRYPTO})")
endif()

if(MSVC)
    add_compile_options(/W4 /permissive- /bigobj)
    # Windows headers must not define min/max macros (Tween.hpp uses std::numeric_limits<T>::max()).
    add_compile_definitions(NOMINMAX)
else()
    add_compile_options(-Wall -Wextra -Wpedantic)
    # No fused multiply-add contraction: on a compiler or CPU target with FMA, GCC fuses a*b+c differently in the scalar and
    # the SSE code of the software renderer (TriangleSpan.hpp), so the two paths stop being bit for bit equal. Plain IEEE
    # float arithmetic keeps every path (and every machine) giving the same pixels.
    add_compile_options(-ffp-contract=off)
endif()

# Sanitizer build (GCC/Clang), used by CI's sanitizer job. Off by default, e.g.
#   cmake -DOKAY_SANITIZE=address,undefined,float-cast-overflow ...
# Findings abort instead of printing and carrying on. GCC's "undefined" group
# leaves out float-cast-overflow (1e30 cast to an int), so name it explicitly.
set(OKAY_SANITIZE "" CACHE STRING "Build with -fsanitize=<list> (e.g. address,undefined); empty = off")
if(OKAY_SANITIZE AND NOT MSVC)
    add_compile_options(-fsanitize=${OKAY_SANITIZE} -fno-sanitize-recover=all -fno-omit-frame-pointer)
    add_link_options(-fsanitize=${OKAY_SANITIZE} -fno-sanitize-recover=all)
endif()

# Enable testing up front so subdirectories (editor, tests) can register tests.
if(OKAY_BUILD_TESTS)
    enable_testing()
endif()

# ---- Baked-in account server -----------------------------------------
# The default account server URL + publishable API key are compiled into every binary so a shipped build is online by
# default with no key file on disk. They live in ONE plain file, config/public-account.json (see config/README.md: the
# publishable key is public by design; Supabase row-level security is what protects the data). Order, first one wins:
#   1. cmake -DOKAY_DEFAULT_ACCOUNT_URL=... -DOKAY_DEFAULT_ACCOUNT_KEY=...
#   2. the environment variables of the same names (CI sets them only from the optional repository variables)
#   3. config/public-account.json
# If none gives a value (file removed) the build still works, with no online features until a server is set through
# OKAY_ACCOUNT_SERVER / OKAY_ACCOUNT_API_KEY or account_server.txt / account_apikey.txt. Values are never printed.
set(OKAY_DEFAULT_ACCOUNT_URL "" CACHE STRING "Default account server URL compiled into builds (empty = from config/public-account.json)")
set(OKAY_DEFAULT_ACCOUNT_KEY "" CACHE STRING "Default account API key compiled into builds (empty = from config/public-account.json)")
set(_okay_acct_file "${CMAKE_CURRENT_SOURCE_DIR}/config/public-account.json")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${_okay_acct_file}")
set(_okay_acct_url "")
set(_okay_acct_key "")
if(EXISTS "${_okay_acct_file}")
    file(READ "${_okay_acct_file}" _okay_acct_json)
    if(_okay_acct_json MATCHES "\"url\"[ \t\r\n]*:[ \t\r\n]*\"([^\"]*)\"")
        set(_okay_acct_url "${CMAKE_MATCH_1}")
    endif()
    if(_okay_acct_json MATCHES "\"key\"[ \t\r\n]*:[ \t\r\n]*\"([^\"]*)\"")
        set(_okay_acct_key "${CMAKE_MATCH_1}")
    endif()
endif()
set(_okay_acct_from "none")
if(OKAY_DEFAULT_ACCOUNT_URL OR OKAY_DEFAULT_ACCOUNT_KEY)
    set(_okay_acct_from "-D option")
elseif(NOT "$ENV{OKAY_DEFAULT_ACCOUNT_URL}" STREQUAL "" OR NOT "$ENV{OKAY_DEFAULT_ACCOUNT_KEY}" STREQUAL "")
    set(OKAY_DEFAULT_ACCOUNT_URL "$ENV{OKAY_DEFAULT_ACCOUNT_URL}")
    set(OKAY_DEFAULT_ACCOUNT_KEY "$ENV{OKAY_DEFAULT_ACCOUNT_KEY}")
    set(_okay_acct_from "environment")
elseif(_okay_acct_url OR _okay_acct_key)
    set(OKAY_DEFAULT_ACCOUNT_URL "${_okay_acct_url}")
    set(OKAY_DEFAULT_ACCOUNT_KEY "${_okay_acct_key}")
    set(_okay_acct_from "config/public-account.json")
endif()
if(OKAY_DEFAULT_ACCOUNT_URL)
    add_compile_definitions(OKAY_DEFAULT_ACCOUNT_URL="${OKAY_DEFAULT_ACCOUNT_URL}")
endif()
if(OKAY_DEFAULT_ACCOUNT_KEY)
    add_compile_definitions(OKAY_DEFAULT_ACCOUNT_KEY="${OKAY_DEFAULT_ACCOUNT_KEY}")
endif()
# Say what was set and where from (never a value).
if(OKAY_DEFAULT_ACCOUNT_URL)
    message(STATUS "Account server baked in: yes (from ${_okay_acct_from})")
else()
    message(STATUS "Account server baked in: NO (no default server; set OKAY_ACCOUNT_SERVER or account_server.txt at run time)")
endif()
if(OKAY_DEFAULT_ACCOUNT_URL AND NOT OKAY_DEFAULT_ACCOUNT_KEY)
    message(STATUS "Account API key baked in: NO")
endif()
# The updater reads releases ONLY from <account server>/storage/v1/object/public/releases/ (the programs never use GitHub,
# owner decision 2026-11-20). A release build (OKAY_REQUIRE_SODIUM=ON marks one) without that address would never update,
# so it stops here. scripts/check_no_github_runtime.py checks this guard is in place.
if(OKAY_REQUIRE_SODIUM AND NOT OKAY_DEFAULT_ACCOUNT_URL MATCHES "^https://[a-z0-9]+\\.supabase\\.co/?$")
    message(FATAL_ERROR "No update server for this release build: OKAY_DEFAULT_ACCOUNT_URL must be the https://<project>.supabase.co address "
                        "(config/public-account.json or the OKAY_DEFAULT_ACCOUNT_URL variable). Without it the programs could never update.")
endif()
# Hosted password-reset page (supabase/auth-pages/reset-password, docs/online/password-reset.md).
# Empty = not configured; the launcher then says so when it sends a reset email.
set(OKAY_DEFAULT_PASSWORD_RESET_URL "" CACHE STRING "URL of the hosted password-reset page, sent to Supabase as redirect_to")
if(OKAY_DEFAULT_PASSWORD_RESET_URL)
    add_compile_definitions(OKAY_DEFAULT_PASSWORD_RESET_URL="${OKAY_DEFAULT_PASSWORD_RESET_URL}")
endif()
# Default voice server for launcher calls ("host" or "host:port" of a running
# okayspace-relay; see docs/online/relay.md). Empty = players set one in Settings > Chat.
set(OKAY_DEFAULT_VOICE_RELAY "" CACHE STRING "Default voice relay (host[:port]) compiled into the launcher")
if(OKAY_DEFAULT_VOICE_RELAY)
    add_compile_definitions(OKAY_DEFAULT_VOICE_RELAY="${OKAY_DEFAULT_VOICE_RELAY}")
endif()
# Anti-cheat (opt-in, docs/online/anticheat.md): the ed25519 PUBLIC keys accepted on a
# build's Data/manifest.json, as hex, comma-separated (e.g. the release key, then
# the dev key). Public keys only; the private keys never go near CMake. Empty =
# none built in (a signature is checked but not its signer).
#  * OKAY_AC_TRUSTED_KEYS: the launcher's, which it applies to the official game.
#  * OKAY_AC_PLAYER_TRUSTED_KEYS: the player's. A player with keys refuses every
#    game not signed by them, so the general runtime (which runs anyone's
#    games) has none; set it only for a player that ships your own signed games.
set(OKAY_AC_TRUSTED_KEYS "" CACHE STRING "Anti-cheat: the launcher's trusted ed25519 public keys (hex, comma-separated)")
set(OKAY_AC_PLAYER_TRUSTED_KEYS "" CACHE STRING "Anti-cheat: the player's trusted ed25519 public keys (only for a player of your own signed games)")
include(${CMAKE_SOURCE_DIR}/cmake/OkayAntiCheatKeys.cmake)
okay_ac_check_trusted_keys("${OKAY_AC_TRUSTED_KEYS}")          # exactly 64 hex digits per key, or configure fails
okay_ac_check_trusted_keys("${OKAY_AC_PLAYER_TRUSTED_KEYS}")
# Signed releases (docs/release/updater.md): the ed25519 PUBLIC keys that must have signed
# the release index (versions.json.sig) before the launcher or editor will install
# anything from it: the current key, and during a rotation the next one too.
# Empty = none built in: an unsigned index is accepted (the transition for feeds
# nobody has signed yet; set it once the first signed publish is out).
set(OKAY_UPDATE_TRUSTED_KEYS "" CACHE STRING "Updater: ed25519 public keys (hex, comma-separated) that must sign versions.json (empty = unsigned feeds still accepted)")
okay_ac_check_trusted_keys("${OKAY_UPDATE_TRUSTED_KEYS}")      # same rule: exactly 64 hex digits per key, or configure fails
# The salt of the anti-cheat hardware hash (shared/AntiCheatHardware.hpp): give your
# deployment its own so its hashes can't be matched with another's. Not a secret.
set(OKAY_AC_HW_SALT "" CACHE STRING "Anti-cheat: hardware hash salt (empty = the built-in default)")
if(OKAY_AC_HW_SALT)
    string(FIND "${OKAY_AC_HW_SALT}" "\"" _okay_ac_q)
    string(FIND "${OKAY_AC_HW_SALT}" "\\" _okay_ac_b)
    if(NOT _okay_ac_q EQUAL -1 OR NOT _okay_ac_b EQUAL -1)
        message(FATAL_ERROR "OKAY_AC_HW_SALT can't contain quotes or backslashes")
    endif()
    add_compile_definitions(OKAY_AC_HW_SALT="${OKAY_AC_HW_SALT}")
endif()

# The Dear ImGui *docking-branch* commit the editor (and the editor-model test)
# builds against; see editor/CMakeLists.txt. Override with -DOKAY_IMGUI_TAG=<ref>.
set(OKAY_IMGUI_TAG "036bf939b6f8d74ad76bcf926b757c56e68c54ff"
    CACHE STRING "Dear ImGui git commit/branch to fetch (must be the docking branch)")

# ---- The engine library ----------------------------------------------
# Engine modules (docs/design/modular_gameplay_contracts.md): read engine/modules/*/module.okaymodule. Stage 0 only groups
# files and labels tests (ctest -L module:<name>); the build itself is unchanged.
include(${CMAKE_SOURCE_DIR}/cmake/OkayModules.cmake)
okay_modules_read()
add_subdirectory(engine)

# ---- OkayUI (SDL_Renderer immediate-mode UI toolkit) -----------------
# Self-gates on SDL2 (skips quietly if absent). Added before the editor and tests
# so both can link okay::ui. Only needed by SDL-based hosts and its own test.
# Skip OkayUI on Emscripten/WebGL: the web player links only okay::engine, and the
# toolkit includes a Direct3D 11 backend that can't compile to WASM.
if((OKAY_BUILD_EDITOR OR OKAY_BUILD_PLAYER OR OKAY_BUILD_LAUNCHER OR OKAY_BUILD_TESTS) AND NOT EMSCRIPTEN)
    add_subdirectory(okayui)
endif()

# ---- Sample game ------------------------------------------------------
if(OKAY_BUILD_SANDBOX)
    add_subdirectory(sandbox)
endif()

# ---- Self-updating launcher ------------------------------------------
if(OKAY_BUILD_LAUNCHER)
    add_subdirectory(launcher)
    if(OKAY_AC_TRUSTED_KEYS AND TARGET okayspace-launcher)
        target_compile_definitions(okayspace-launcher PRIVATE OKAY_AC_TRUSTED_KEYS="${OKAY_AC_TRUSTED_KEYS}")
    endif()
    if(OKAY_UPDATE_TRUSTED_KEYS AND TARGET okayspace-launcher)
        target_compile_definitions(okayspace-launcher PRIVATE OKAY_UPDATE_TRUSTED_KEYS="${OKAY_UPDATE_TRUSTED_KEYS}")
    endif()
endif()

# ---- Feature examples -------------------------------------------------
if(OKAY_BUILD_SANDBOX)
    add_subdirectory(examples)
endif()

# ---- Desktop GUI editor ----------------------------------------------
if(OKAY_BUILD_EDITOR)
    add_subdirectory(editor)
    if(OKAY_UPDATE_TRUSTED_KEYS AND TARGET okay-editor)
        target_compile_definitions(okay-editor PRIVATE OKAY_UPDATE_TRUSTED_KEYS="${OKAY_UPDATE_TRUSTED_KEYS}")
    endif()
endif()

# ---- Standalone game player ------------------------------------------
# The editor's "Build Game" ships this runtime, so build it alongside the editor.
if(OKAY_BUILD_PLAYER OR OKAY_BUILD_EDITOR)
    add_subdirectory(player)
    if(OKAY_AC_PLAYER_TRUSTED_KEYS AND TARGET okay-player)
        target_compile_definitions(okay-player PRIVATE OKAY_AC_TRUSTED_KEYS="${OKAY_AC_PLAYER_TRUSTED_KEYS}")
    endif()
endif()

# ---- Content delivery tool (docs/release/content_delivery.md) ----------------
# okay-content: make-manifest / verify / sync / status / rollback for the signed game content that an exported game can download.
# Tiny (crypto + our own HTTP layer only); it is what scripts/publish-content.sh calls and what the owner uses to test a content folder.
option(OKAY_BUILD_CONTENT_TOOL "Build the okay-content command-line tool" ON)
if(OKAY_BUILD_CONTENT_TOOL AND NOT EMSCRIPTEN)
    add_executable(okay-content tools/okay-content/main.cpp)
    target_compile_features(okay-content PRIVATE cxx_std_17)
    target_include_directories(okay-content PRIVATE ${CMAKE_SOURCE_DIR}/shared ${CMAKE_SOURCE_DIR}/engine/include)
    target_link_libraries(okay-content PRIVATE okay::crypto)
    if(WIN32)
        target_link_libraries(okay-content PRIVATE winhttp ws2_32)
    elseif(OKAY_OWN_HTTP AND NOT APPLE)
        target_compile_definitions(okay-content PRIVATE OKAY_OWN_HTTP=1)
        find_package(Threads REQUIRED)
        target_link_libraries(okay-content PRIVATE Threads::Threads ${CMAKE_DL_LIBS})
    endif()
    set_target_properties(okay-content PROPERTIES OUTPUT_NAME "okay-content")
endif()

# ---- okay: the engine's command line (tools/okay-cli) -------------------------
# new / open / play / run / build / test / info / doctor, and version control with the person's own git underneath
# (okay vc ...; scenes and prefabs merge and diff through SceneMerge / SceneDiff as git's drivers). Built by default.
# okay_cli_lib holds everything but main() so tests/test_okay_cli.cpp checks the same code.
option(OKAY_BUILD_CLI "Build the okay command-line program" ON)
if(OKAY_BUILD_CLI AND NOT EMSCRIPTEN)
    add_library(okay_cli_lib STATIC tools/okay-cli/Cli.cpp tools/okay-cli/Vc.cpp tools/okay-cli/Git.cpp tools/okay-cli/Process.cpp)
    target_include_directories(okay_cli_lib PUBLIC ${CMAKE_SOURCE_DIR}/tools/okay-cli
                               PRIVATE ${CMAKE_SOURCE_DIR}/editor ${CMAKE_SOURCE_DIR}/shared)
    target_link_libraries(okay_cli_lib PUBLIC okay::engine)
    target_compile_definitions(okay_cli_lib PRIVATE ${OKAY_VERSION_DEFINE})
    add_executable(okay_cli tools/okay-cli/main.cpp)
    target_link_libraries(okay_cli PRIVATE okay_cli_lib)
    if(WIN32)
        target_link_libraries(okay_cli PRIVATE shell32)
        okay_win_version(okay_cli "OkaySpace command line" "okay.exe")
    endif()
    set_target_properties(okay_cli PROPERTIES OUTPUT_NAME "okay")
endif()

# ---- Ship SDL2.dll beside the binaries ----
# Unless fully static (OKAY_STATIC_SDL), the Windows exes link SDL2 dynamically and
# need SDL2.dll next to them. Copy it into the output bin dir so a build is runnable
# in place and "Build Game" / packaging can grab it from there.
if(WIN32 AND NOT OKAY_STATIC_SDL AND DEFINED SDL2_DIR)
    # SDL2_DIR is .../<triple>/lib/cmake/SDL2; the DLL lives at .../<triple>/bin/SDL2.dll.
    set(_okay_sdl_dll "")
    foreach(_up "/../../../bin/SDL2.dll" "/../../bin/SDL2.dll")
        get_filename_component(_cand "${SDL2_DIR}${_up}" ABSOLUTE)
        if(EXISTS "${_cand}")
            set(_okay_sdl_dll "${_cand}")
            break()
        endif()
    endforeach()
    if(_okay_sdl_dll)
        add_custom_target(okay_copy_sdl2 ALL
            COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_BINARY_DIR}/bin"
            COMMAND ${CMAKE_COMMAND} -E copy_if_different "${_okay_sdl_dll}" "${CMAKE_BINARY_DIR}/bin/"
            COMMENT "Copying SDL2.dll next to the OkaySpace binaries")
    else()
        message(WARNING "SDL2.dll not found at ${_okay_sdl_dll}; ship it manually or use -DOKAY_STATIC_SDL=ON.")
    endif()
endif()

# ---- Developer tools (opt-in) ----------------------------------------
if(OKAY_BUILD_PLATFORM_SELFTEST AND NOT EMSCRIPTEN)
    add_subdirectory(tools/platform_selftest)
endif()
if(OKAY_BUILD_TOOLS)
    add_subdirectory(tools)
endif()

# ---- Reference relay (NAT traversal) ---------------------------------
# A tiny console UDP forwarder; no graphics deps, always built — except on
# Emscripten/WebGL, where raw UDP sockets don't exist in the browser sandbox.
if(NOT EMSCRIPTEN)
    add_subdirectory(relay)
endif()

# ---- okay-texture: command line texture tool (mips, atlas, compress), no graphics deps -----
if(NOT EMSCRIPTEN)
    add_subdirectory(tools/okaytexture)
endif()

# ---- Xbox One Developer Mode export (experimental, OFF by default) -----
if(OKAY_BUILD_XBOX_UWP)
    add_subdirectory(platform/xbox_uwp)
endif()

# ---- Tests ------------------------------------------------------------
if(OKAY_BUILD_TESTS)
    add_subdirectory(tests)
endif()
