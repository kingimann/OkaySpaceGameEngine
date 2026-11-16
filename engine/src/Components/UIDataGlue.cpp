// UI library slice 2, the data-driven glue (okay/Components/UIDataGlue.hpp).
#include "okay/Components/UIDataGlue.hpp"
#include "okay/Components/UIVars.hpp"
#include "okay/Components/UIWidgetUtil.hpp"
#include "okay/Components/UISlider.hpp"
#include "okay/Components/UIStepper.hpp"
#include "okay/Components/UIToggle.hpp"
#include "okay/Components/UIDropdown.hpp"
#include "okay/Components/UIInputField.hpp"
#include "okay/Components/UIRangeSlider.hpp"
#include "okay/Components/UIListView.hpp"
#include "okay/Components/UIDocument.hpp"
#include "okay/Components/UITheme.hpp"
#include "okay/Components/TextRenderer.hpp"
#include "okay/Components/Character.hpp"
#include "okay/Components/ActionList.hpp"
#include "okay/Components/ScriptComponent.hpp"
#include "okay/UI/UIRenderer.hpp"
#include "okay/Core/Prefs.hpp"
#include "okay/Input/Input.hpp"
#include "okay/VisualScript/VsVariables.hpp"
#include "okay/Scene/GameObject.hpp"
#include "okay/Scene/Scene.hpp"
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace okay {

// ---- UIBind ---------------------------------------------------------------------------------------------------------

bool UIBind::HasWidget() const {
    if (!gameObject) return false;
    return gameObject->GetComponent<UISlider>() || gameObject->GetComponent<UIStepper>() || gameObject->GetComponent<UIToggle>()
        || gameObject->GetComponent<UIDropdown>() || gameObject->GetComponent<UIRangeSlider>() || gameObject->GetComponent<UIInputField>();
}

template <class Get, class Set>
void UIBind::SyncNumber(Channel& c, const std::string& name, Get get, Set set) {
    if (name.empty()) return;
    const float w = get();
    float v = 0.0f;
    const bool hasVar = uivars::ReadNumber(gameObject, name, v);
    if (!c.init) {
        c.init = true;
        if (hasVar && mode != Mode::ToVariable) { set(v); c.variable = v; c.widget = get(); }
        else if (mode != Mode::FromVariable) { uivars::WriteNumber(gameObject, name, w); c.widget = w; c.variable = w; }
        else { c.widget = w; c.variable = v; }
        return;
    }
    if (mode != Mode::FromVariable && w != c.widget) {          // the player moved the widget
        uivars::WriteNumber(gameObject, name, w);
        c.widget = w; c.variable = w;
        return;
    }
    if (mode != Mode::ToVariable && hasVar && v != c.variable) { // something else changed the variable
        set(v);
        c.variable = v;
        c.widget = get();
    }
}

void UIBind::SyncText() {
    auto* in = gameObject->GetComponent<UIInputField>();
    if (!in || var.empty()) return;
    const uivars::Value v = uivars::Read(gameObject, var);
    if (!m_textInit) {
        m_textInit = true;
        if (v.found && mode != Mode::ToVariable) in->text = v.text;
        else if (mode != Mode::FromVariable && !in->text.empty()) {   // an empty box does not create a variable
            char* end = nullptr; const float f = std::strtof(in->text.c_str(), &end);
            if (!in->text.empty() && end && *end == '\0') uivars::WriteNumber(gameObject, var, f); else uivars::WriteText(gameObject, var, in->text);
        }
        m_widgetText = in->text; m_varText = v.found ? v.text : in->text;
        return;
    }
    if (mode != Mode::FromVariable && in->text != m_widgetText) {   // the player typed
        char* end = nullptr; const float f = std::strtof(in->text.c_str(), &end);
        if (!in->text.empty() && end && *end == '\0') uivars::WriteNumber(gameObject, var, f); else uivars::WriteText(gameObject, var, in->text);
        m_widgetText = in->text; m_varText = in->text;
        return;
    }
    if (mode != Mode::ToVariable && v.found && v.text != m_varText) {
        in->text = v.text;
        m_varText = v.text; m_widgetText = in->text;
    }
}

void UIBind::Sync() {
    if (!gameObject || var.empty()) return;
    if (auto* s = gameObject->GetComponent<UISlider>()) {
        SyncNumber(m_ch[0], var, [s] { return s->value; }, [s](float v) { s->SetValue(v); });
    } else if (auto* st = gameObject->GetComponent<UIStepper>()) {
        SyncNumber(m_ch[0], var, [st] { return st->value; }, [st](float v) { st->SetValue(v); });
    } else if (auto* t = gameObject->GetComponent<UIToggle>()) {
        SyncNumber(m_ch[0], var, [t] { return t->on ? 1.0f : 0.0f; }, [t](float v) { if ((v != 0.0f) != t->on) t->Toggle(); });
    } else if (auto* d = gameObject->GetComponent<UIDropdown>()) {
        SyncNumber(m_ch[0], var, [d] { return (float)d->value; }, [d](float v) { d->SetValue((int)std::lround(v)); });
        // The picked option's words too, in "<var>_text" (a UI Item Tooltip Card or UI Text Bind can use them).
        const std::string label = d->HasSelection() ? d->options[(std::size_t)d->value] : std::string();
        if (!m_textInit || label != m_widgetText) { uivars::WriteText(gameObject, var + "_text", label); m_widgetText = label; m_textInit = true; }
    } else if (auto* r = gameObject->GetComponent<UIRangeSlider>()) {
        SyncNumber(m_ch[0], var, [r] { return r->low; }, [r](float v) { r->SetLow(v); });
        SyncNumber(m_ch[1], varHigh, [r] { return r->high; }, [r](float v) { r->SetHigh(v); });
    } else if (gameObject->GetComponent<UIInputField>()) {
        SyncText();
    }
}

// ---- UIVisibleIf ----------------------------------------------------------------------------------------------------

GameObject* UIVisibleIf::Resolved() const {
    if (!gameObject) return nullptr;
    if (target.empty()) return gameObject;
    Scene* s = gameObject->scene();
    return s ? s->Find(target) : nullptr;
}

bool UIVisibleIf::ShouldShow() const {
    const bool ok = uivars::Evaluate(gameObject, condition, nullptr);
    return invert ? !ok : ok;
}

bool UIVisibleIf::Tick() {
    GameObject* t = Resolved();
    if (!t) return false;
    const bool show = ShouldShow();
    if (t->active == show) { if (show) m_hid = false; return false; }
    t->active = show;
    m_hid = !show;
    return true;
}

void UIVisibleIf::TickHidden(Scene& scene) {
    for (const auto& up : scene.Objects()) {
        if (up->active) continue;
        if (auto* v = up->GetComponent<UIVisibleIf>())
            if (v->m_hid && v->target.empty()) v->Tick();   // only a self-hiding object can no longer update itself
    }
}

// ---- UIListBind -----------------------------------------------------------------------------------------------------

namespace {

void AddField(std::map<std::string, std::string>& row, const std::string& k, const vs::VsValue& v) { row[k] = v.AsString(); }

std::vector<std::map<std::string, std::string>> ReadRows(const GameObject* go, const std::string& var) {
    std::vector<std::map<std::string, std::string>> rows;
    auto fromArray = [&](const vs::VsValue::ArrayRef& a) {
        if (!a) return;
        for (const vs::VsValue& e : *a) {
            std::map<std::string, std::string> row;
            if (e.IsMap()) { if (auto m = e.AsMap()) for (const auto& kv : *m) AddField(row, kv.first, kv.second); }
            row["item"] = e.IsMap() ? (row.count("name") ? row["name"] : e.AsString()) : e.AsString();
            rows.push_back(std::move(row));
        }
    };
    if (const Scene* s = go ? go->scene() : nullptr) {
        auto& sv = vs::SceneVariables(s);
        auto it = sv.find(var);
        if (it != sv.end() && it->second.IsArray()) { fromArray(it->second.AsArray()); return rows; }
    }
    {
        auto& gv = vs::GlobalVariables();
        auto it = gv.find(var);
        if (it != gv.end() && it->second.IsArray()) { fromArray(it->second.AsArray()); return rows; }
    }
    auto& arrays = ActionList::Arrays();
    auto it = arrays.find(var);
    if (it != arrays.end())
        for (float f : it->second) { std::map<std::string, std::string> row; row["item"] = uivars::FormatNumber(f); rows.push_back(std::move(row)); }
    return rows;
}

void TextsOf(GameObject* root, std::vector<TextRenderer*>& out) {
    if (!root) return;
    root->ForEachDescendant([&](GameObject& g) {
        if (auto* tr = g.GetComponent<TextRenderer>()) if (tr->screenSpace) out.push_back(tr);
    }, true, true);
}

} // namespace

const std::map<std::string, std::string>& UIListBind::Row(int i) const {
    static const std::map<std::string, std::string> none;
    return (i >= 0 && i < (int)m_rows.size()) ? m_rows[(std::size_t)i] : none;
}

void UIListBind::FillRow(GameObject* row, int index) {
    auto* lv = gameObject ? gameObject->GetComponent<UIListView>() : nullptr;
    if (!lv || !row) return;
    GameObject* tpl = lv->Template();
    std::vector<TextRenderer*> rowTexts, tplTexts;
    TextsOf(row, rowTexts);
    TextsOf(tpl, tplTexts);
    const std::map<std::string, std::string> fields = Row(index);
    for (std::size_t k = 0; k < rowTexts.size(); ++k) {
        const std::string tplText = k < tplTexts.size() ? tplTexts[k]->text : rowTexts[k]->text;
        const bool hole = tplText.find('{') != std::string::npos && !(k == 0 && tplText == "{item}");   // a lone {item} in the first text means "use the format"
        if (k == 0 && !hole) rowTexts[k]->text = uivars::FillTemplate(format.empty() ? "{item}" : format, fields);
        else if (hole) rowTexts[k]->text = uivars::FillTemplate(tplText, fields);
    }
}

void UIListBind::Hook() {
    auto* lv = gameObject ? gameObject->GetComponent<UIListView>() : nullptr;
    if (!lv) return;
    lv->onBind = [this](GameObject* row, int index) { FillRow(row, index); };
    m_hooked = true;
}

void UIListBind::Start() { Hook(); Sync(); }

bool UIListBind::Sync() {
    auto* lv = gameObject ? gameObject->GetComponent<UIListView>() : nullptr;
    if (!lv || var.empty()) return false;
    if (!m_hooked || !lv->onBind) Hook();
    auto rows = ReadRows(gameObject, var);
    if (rows.empty() && !emptyText.empty()) { std::map<std::string, std::string> r; r["item"] = emptyText; rows.push_back(std::move(r)); }
    for (std::size_t i = 0; i < rows.size(); ++i) { rows[i]["index"] = std::to_string(i + 1); rows[i]["n"] = std::to_string(rows.size()); }
    std::string sig = format + '\n';
    for (const auto& r : rows) { for (const auto& kv : r) { sig += kv.first; sig += '='; sig += kv.second; sig += ';'; } sig += '\n'; }
    if (sig == m_signature && (int)rows.size() == lv->Count()) return false;
    m_signature = std::move(sig);
    m_rows = std::move(rows);
    lv->items.clear();            // the rows are filled by onBind, so the list's own item text must not overwrite them
    lv->itemCount = (int)m_rows.size();
    lv->Refresh();
    return true;
}

// ---- UIHotReload ----------------------------------------------------------------------------------------------------

std::string UIHotReload::Resolved() const {
    namespace fs = std::filesystem;
    std::error_code ec;
    const fs::path p(file);
    if (file.empty() || p.is_absolute() || Character::AssetBaseDir().empty() || fs::exists(p, ec)) return file;
    return (fs::path(Character::AssetBaseDir()) / p).string();
}

GameObject* UIHotReload::Target() const {
    if (!gameObject) return nullptr;
    if (target.empty()) return gameObject;
    Scene* s = gameObject->scene();
    return s ? s->Find(target) : nullptr;
}

long long UIHotReload::Stamp() const {
    namespace fs = std::filesystem;
    std::error_code ec;
    const std::string path = Resolved();
    if (path.empty() || !fs::exists(path, ec)) return -1;
    const auto t = fs::last_write_time(path, ec);
    if (ec) return -1;
    const auto sz = fs::file_size(path, ec);
    return (long long)t.time_since_epoch().count() * 31 + (ec ? 0 : (long long)sz);
}

bool UIHotReload::ReloadNow() {
    m_error.clear();
    GameObject* t = Target();
    if (!t) { m_error = "the target object was not found"; return false; }
    const std::string path = Resolved();
    if (path.empty()) { m_error = "no file is named"; return false; }
    m_stamp = Stamp();
    const bool isTheme = path.size() >= 10 && path.compare(path.size() - 10, 10, ".okaytheme") == 0;
    if (isTheme) {
        auto* th = t->GetComponent<UITheme>();
        if (!th) { m_error = "the target has no UI Theme for the .okaytheme file"; return false; }
        std::string err;
        if (!LoadUIThemeAsset(*th, path, &err)) { m_error = err.empty() ? "could not read the theme" : err; return false; }
        th->Apply();
        ++m_reloads;
        return true;
    }
    auto* doc = t->GetComponent<UIDocument>();
    if (!doc) { m_error = "the target has no UI Document for the markup file"; return false; }
    std::ifstream f(path, std::ios::binary);
    if (!f) { m_error = "could not open " + path; return false; }
    std::stringstream ss; ss << f.rdbuf();
    doc->markup = ss.str();
    doc->Rebuild();
    ++m_reloads;
    return true;
}

bool UIHotReload::Poll() {
    const long long now = Stamp();
    if (now < 0 || now == m_stamp) return false;
    return ReloadNow();
}

void UIHotReload::Start() {
    m_stamp = Stamp();
    if (loadAtStart && m_stamp >= 0) ReloadNow();
}

void UIHotReload::Update(float dt) {
    m_wait -= dt;
    if (m_wait > 0.0f) return;
    m_wait = interval < 0.05f ? 0.05f : interval;
    Poll();
}

// ---- UIRichLinks ----------------------------------------------------------------------------------------------------

std::string UIRichLinks::LinkAt(float x, float y) const {
    for (const ui2d::RichLinkRect& r : ui2d::RichLinksOf(gameObject))
        if (x >= r.x && y >= r.y && x <= r.x + r.w && y <= r.y + r.h) return r.target;
    return "";
}

bool UIRichLinks::Click(float x, float y) {
    const std::string t = LinkAt(x, y);
    if (t.empty()) return false;
    m_last = t;
    if (storeInPrefs) Prefs::SetString("ui_link", t);
    if (fireEvent) FireUIEvent(gameObject, "on_link");
    return true;
}

void UIRichLinks::Update(float) {
    if (!enabled || !Input::GetMouseButtonDown(0)) return;
    const Vec2 m = Input::MousePosition();
    Click(m.x, m.y);
}

} // namespace okay
