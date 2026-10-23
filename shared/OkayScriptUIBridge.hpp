#pragma once
// Implements okay::ScriptUIBridge with OkayUI, so game scripts' ui_* builtins draw
// real OkayUI widgets. The host installs one via okay::SetScriptUI() and brackets
// each frame with OkayUI::BeginFrame()/EndFrame() around the scene update.
#include "okay/Scripting/ScriptUI.hpp"
#include "okay/UI/OkayUI.hpp"

#include <cstring>
#include <map>
#include <string>
#include <vector>

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
    void  Heading(const char* s) override { OkayUI::Heading(s); }
    void  Caption(const char* s, int r, int g, int b) override { OkayUI::Caption(s, (unsigned char)r, (unsigned char)g, (unsigned char)b); }
    void  TextDim(const char* s) override { OkayUI::TextDisabled(s); }
    void  TextWrapped(const char* s) override { OkayUI::TextWrapped(s); }
    bool  PrimaryButton(const char* l) override { return OkayUI::PrimaryButton(l); }
    void  Badge(const char* s, int r, int g, int b) override { OkayUI::Badge(s, (unsigned char)r, (unsigned char)g, (unsigned char)b); }
    void  Avatar(const char* n, float size) override { OkayUI::Avatar(n, size); }
    void  BeginCard(const char* id) override { OkayUI::BeginCard(id); }
    void  EndCard() override { OkayUI::EndCard(); }
    bool  ListItem(const char* t, const char* sub, const char* r, bool sel) override { return OkayUI::ListItem(t, sub, r, sel); }
    void  KeyValue(const char* k, const char* v) override { OkayUI::KeyValue(k, v); }
    void  Spinner() override { OkayUI::Spinner(); }
    void  Toast(const char* s, int kind, float secs) override { OkayUI::Toast(s, kind, secs); }
    int   Tabs(const char* labels, int current) override {
        std::vector<std::string> parts; std::string cur;
        for (const char* p = labels ? labels : ""; ; ++p) {
            if (!*p || *p == '|') { parts.push_back(cur); cur.clear(); if (!*p) break; } else cur += *p;
        }
        std::vector<const char*> ptrs; for (auto& x : parts) ptrs.push_back(x.c_str());
        int c = current;
        OkayUI::TabBar(ptrs.data(), (int)ptrs.size(), &c);
        return c;
    }
    const char* Input(const char* label, const char* text) override {
        // Each field keeps its buffer between frames; a script that sets new text replaces it.
        Field& f = m_fields[label ? label : ""];
        const std::string in = text ? text : "";
        if (in != f.lastOut) { std::strncpy(f.buf, in.c_str(), sizeof(f.buf) - 1); f.buf[sizeof(f.buf) - 1] = 0; }
        OkayUI::InputText(label ? label : "", f.buf, (int)sizeof(f.buf));
        f.lastOut = f.buf;
        return f.lastOut.c_str();
    }
    void  Spacing(float h) override { OkayUI::Spacing(h); }
    void  Style(const char* name) override { OkayUI::UseLauncherLook(!(name && std::strcmp(name, "classic") == 0)); }
private:
    struct Field { char buf[256] = ""; std::string lastOut; };
    std::map<std::string, Field> m_fields;
};

} // namespace okay
