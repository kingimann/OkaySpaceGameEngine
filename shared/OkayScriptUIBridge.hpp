#pragma once
// Implements okay::ScriptUIBridge with OkayUI, so game scripts' ui_* builtins draw
// real OkayUI widgets. The host installs one via okay::SetScriptUI() and brackets
// each frame with OkayUI::BeginFrame()/EndFrame() around the scene update.
#include "okay/Scripting/ScriptUI.hpp"
#include "okay/UI/OkayUI.hpp"

namespace okay {

struct OkayUIScriptBridge : ScriptUIBridge {
    void  Begin(const char* t, float x, float y, float w, float h) override { OkayUI::Begin(t ? t : "", x, y, w, h); }
    void  End() override { OkayUI::End(); }
    void  Text(const char* s) override { OkayUI::Text(s); }
    bool  Button(const char* l) override { return OkayUI::Button(l); }
    bool  Checkbox(const char* l, bool v) override { OkayUI::Checkbox(l, &v); return v; }
    bool  Switch(const char* l, bool v) override { OkayUI::ToggleSwitch(l, &v); return v; }
    float Slider(const char* l, float v, float lo, float hi) override { OkayUI::SliderFloat(l, &v, lo, hi); return v; }
    void  ProgressBar(float t) override { OkayUI::ProgressBar(t); }
    void  SameLine() override { OkayUI::SameLine(); }
    void  Separator() override { OkayUI::Separator(); }
};

} // namespace okay
