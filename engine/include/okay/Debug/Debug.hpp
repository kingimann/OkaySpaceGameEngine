#pragma once
// Umbrella include for the profiling and debugging data layers (docs/debug_tools.md).
// MemoryHooks.hpp is left out on purpose: it is opt-in and replaces the global
// operator new/delete when its macro is defined, so include it explicitly.
#include "okay/Debug/FrameHistory.hpp"
#include "okay/Debug/GpuTimer.hpp"
#include "okay/Debug/RenderStats.hpp"
#include "okay/Debug/MemoryTracker.hpp"
#include "okay/Debug/DrawCallRecorder.hpp"
#include "okay/Debug/ConsoleModel.hpp"
#include "okay/Debug/DebugStatsOverlay.hpp"
