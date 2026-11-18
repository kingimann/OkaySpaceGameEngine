#pragma once
// Port probe stand-in for imgui.h: lets a Dear ImGui panel compile (syntax check only) against OkayUI, so what is missing shows as
// compiler errors. Put this directory first on the include path (scripts/okayui_port_probe.sh does). Not used by any real build.
#include "okay/UI/OkayUIImGuiNames.hpp"
#ifndef IMGUI_IMPL_API
#define IMGUI_IMPL_API
#endif
// Types of the Dear ImGui draw output, declared only (the backend calls that take them are host-specific: see backends/).
struct ImDrawData; struct ImTextureData;
