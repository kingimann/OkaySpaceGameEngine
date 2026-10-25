#pragma once
// What every editor source file includes: the engine, Dear ImGui, SDL, the
// standard library and the platform headers. Heavier headers that define data
// or code (fonts, the node editor, the theme) are included only by the files
// that use them.
#ifndef IMGUI_DEFINE_MATH_OPERATORS
#define IMGUI_DEFINE_MATH_OPERATORS
#endif
#include "EditorState.hpp"
#include "okay/Render/Lightmap.hpp"
#include "okay/Modeling/EditTransform.hpp"
#include "okay/Modeling/MeshAsset.hpp"
#include "okay/Modeling/PolyMesh.hpp"
#include "okay/Modeling/PolyOps.hpp"
#include "okay/Modeling/UVUnwrap.hpp"
#include "okay/Render/SkyStars.hpp"
#include "okay/Render/GLRenderer.hpp"
#include "okay/Render/Culling.hpp"
#include "okay/Render/D3D11Renderer.hpp"
#include "okay/Core/Profiler.hpp"
#include "okay/Scene/UITemplates.hpp"
#include "okay/Scene/UIStyles.hpp"
#include "okay/Scene/UILayoutTools.hpp"
#include "okay/Platform/PlayFab/PlayFab.hpp"
#include "okay/Scripting/OkayScriptVM.hpp"
#include "okay/Scripting/ScriptUI.hpp"
#include "okay/Scene/EnsureCamera.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <cctype>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <functional>
#include <future>
#include <iostream>
#include <numeric>
#include <set>
#include <sstream>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#ifndef SDL_MAIN_HANDLED
#define SDL_MAIN_HANDLED
#endif
#include <SDL.h>
#include "imgui.h"
#include "imgui_internal.h"

#if defined(_WIN32)
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  ifndef NOMINMAX
#    define NOMINMAX
#  endif
#  include <windows.h>
#else
#  include <unistd.h>
#endif

#ifndef OKAY_ENGINE_VERSION
#  define OKAY_ENGINE_VERSION "dev"
#endif

using namespace okay;
using namespace okay::editor;
