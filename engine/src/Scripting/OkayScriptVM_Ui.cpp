#include "OkayScriptVMInternal.hpp"

namespace okay {

void OkayScriptVM::Impl::RegisterScriptUiBuiltins(BuiltinCtx& c) {
        auto& b = c.b;
        auto tf = c.tf;
        // ---- Immediate-mode UI (forwarded to the host's ScriptUIBridge) ----
        // A game draws its UI each frame; the host brackets the frame and renders it.
        //   ui_begin("Title", x, y, w, h) ... widgets ... ui_end()
        //   hp = ui_slider("HP", hp, 0, 100);  if (ui_button("Reset")) hp = 100
        b["ui_begin"] = [](std::vector<Value>& a) {
            if (auto* u = GetScriptUI())
                u->Begin(a.size() > 0 ? a[0].AsString().c_str() : "",
                         a.size() > 1 ? a[1].AsFloat() : 20.0f, a.size() > 2 ? a[2].AsFloat() : 20.0f,
                         a.size() > 3 ? a[3].AsFloat() : 260.0f, a.size() > 4 ? a[4].AsFloat() : 320.0f);
            return Value{};
        };
        b["ui_end"]       = [](std::vector<Value>&)    { if (auto* u = GetScriptUI()) u->End(); return Value{}; };
        b["ui_text"]      = [](std::vector<Value>& a)  { if (auto* u = GetScriptUI()) u->Text(a.empty() ? "" : a[0].AsString().c_str()); return Value{}; };
        b["ui_sameline"]  = [](std::vector<Value>&)    { if (auto* u = GetScriptUI()) u->SameLine(); return Value{}; };
        b["ui_separator"] = [](std::vector<Value>&)    { if (auto* u = GetScriptUI()) u->Separator(); return Value{}; };
        b["ui_progress"]  = [](std::vector<Value>& a)  { if (auto* u = GetScriptUI()) u->ProgressBar(a.empty() ? 0.0f : a[0].AsFloat()); return Value{}; };
        b["ui_button"]    = [](std::vector<Value>& a)  { auto* u = GetScriptUI(); return Value{u && u->Button(a.empty() ? "" : a[0].AsString().c_str()) ? 1.0f : 0.0f}; };
        b["ui_checkbox"]  = [](std::vector<Value>& a)  {
            auto* u = GetScriptUI(); bool cur = a.size() > 1 && a[1].AsFloat() != 0.0f;
            bool nv = u ? u->Checkbox(a.empty() ? "" : a[0].AsString().c_str(), cur) : cur;
            return Value{nv ? 1.0f : 0.0f};
        };
        b["ui_switch"]    = [](std::vector<Value>& a)  {   // sliding on/off switch
            auto* u = GetScriptUI(); bool cur = a.size() > 1 && a[1].AsFloat() != 0.0f;
            bool nv = u ? u->Switch(a.empty() ? "" : a[0].AsString().c_str(), cur) : cur;
            return Value{nv ? 1.0f : 0.0f};
        };
        // Launcher-style building blocks: ui_heading("Lobby"), ui_caption("NOW PLAYING"[, r, g, b]),
        // ui_text_dim, ui_text_wrapped, ui_primary_button, ui_badge("3/4"[, r, g, b]),
        // ui_avatar("Maya"[, size]), ui_card_begin("id") ... ui_card_end(),
        // ui_list_item(title[, subtitle[, right[, selected]]]) -> clicked, ui_key_value(k, v),
        // ui_spinner(), ui_toast(text[, kind 0/1/2[, seconds]]),
        // ui_tabs("A|B|C", current) -> index, ui_input(label, text) -> text, ui_spacing(h),
        // ui_style("launcher" | "classic").
        auto uiS = [](std::vector<Value>& a, std::size_t i) { return a.size() > i ? a[i].AsString() : std::string(); };
        auto uiN = [](std::vector<Value>& a, std::size_t i, float d) { return a.size() > i ? a[i].AsFloat() : d; };
        b["ui_heading"]   = [uiS](std::vector<Value>& a) { if (auto* u = GetScriptUI()) u->Heading(uiS(a, 0).c_str()); return Value{}; };
        b["ui_caption"]   = [uiS, uiN](std::vector<Value>& a) {
            if (auto* u = GetScriptUI()) u->Caption(uiS(a, 0).c_str(), SafeInt(uiN(a, 1, 115)), SafeInt(uiN(a, 2, 209)), SafeInt(uiN(a, 3, 143)));
            return Value{};
        };
        b["ui_text_dim"]     = [uiS](std::vector<Value>& a) { if (auto* u = GetScriptUI()) u->TextDim(uiS(a, 0).c_str()); return Value{}; };
        b["ui_text_wrapped"] = [uiS](std::vector<Value>& a) { if (auto* u = GetScriptUI()) u->TextWrapped(uiS(a, 0).c_str()); return Value{}; };
        b["ui_primary_button"] = [uiS](std::vector<Value>& a) { auto* u = GetScriptUI(); return Value{u && u->PrimaryButton(uiS(a, 0).c_str()) ? 1.0f : 0.0f}; };
        b["ui_badge"]     = [uiS, uiN](std::vector<Value>& a) {
            if (auto* u = GetScriptUI()) u->Badge(uiS(a, 0).c_str(), SafeInt(uiN(a, 1, 0)), SafeInt(uiN(a, 2, 0)), SafeInt(uiN(a, 3, 0)));
            return Value{};
        };
        b["ui_avatar"]    = [uiS, uiN](std::vector<Value>& a) { if (auto* u = GetScriptUI()) u->Avatar(uiS(a, 0).c_str(), uiN(a, 1, 0.0f)); return Value{}; };
        b["ui_card_begin"] = [uiS](std::vector<Value>& a) { if (auto* u = GetScriptUI()) u->BeginCard(a.empty() ? "card" : uiS(a, 0).c_str()); return Value{}; };
        b["ui_card_end"]  = [](std::vector<Value>&) { if (auto* u = GetScriptUI()) u->EndCard(); return Value{}; };
        b["ui_list_item"] = [uiS, uiN](std::vector<Value>& a) {
            auto* u = GetScriptUI();
            const std::string t = uiS(a, 0), sub = uiS(a, 1), r = uiS(a, 2);
            return Value{u && u->ListItem(t.c_str(), sub.empty() ? nullptr : sub.c_str(), r.empty() ? nullptr : r.c_str(), uiN(a, 3, 0.0f) != 0.0f) ? 1.0f : 0.0f};
        };
        b["ui_key_value"] = [uiS](std::vector<Value>& a) { if (auto* u = GetScriptUI()) u->KeyValue(uiS(a, 0).c_str(), uiS(a, 1).c_str()); return Value{}; };
        b["ui_spinner"]   = [](std::vector<Value>&) { if (auto* u = GetScriptUI()) u->Spinner(); return Value{}; };
        b["ui_toast"]     = [uiS, uiN](std::vector<Value>& a) { if (auto* u = GetScriptUI()) u->Toast(uiS(a, 0).c_str(), SafeInt(uiN(a, 1, 0)), uiN(a, 2, 3.0f)); return Value{}; };
        b["ui_tabs"]      = [uiS, uiN](std::vector<Value>& a) {
            auto* u = GetScriptUI(); const int cur = SafeInt(uiN(a, 1, 0));
            return Value{(float)(u ? u->Tabs(uiS(a, 0).c_str(), cur) : cur)};
        };
        b["ui_input"]     = [uiS](std::vector<Value>& a) {
            auto* u = GetScriptUI(); const std::string cur = uiS(a, 1);
            return Value{std::string(u ? u->Input(uiS(a, 0).c_str(), cur.c_str()) : cur.c_str())};
        };
        b["ui_spacing"]   = [uiN](std::vector<Value>& a) { if (auto* u = GetScriptUI()) u->Spacing(uiN(a, 0, 8.0f)); return Value{}; };
        b["ui_style"]     = [uiS](std::vector<Value>& a) { if (auto* u = GetScriptUI()) u->Style(uiS(a, 0).c_str()); return Value{}; };
        b["ui_slider"]    = [](std::vector<Value>& a)  {
            auto* u = GetScriptUI();
            float v = a.size() > 1 ? a[1].AsFloat() : 0.0f;
            float lo = a.size() > 2 ? a[2].AsFloat() : 0.0f, hi = a.size() > 3 ? a[3].AsFloat() : 1.0f;
            return Value{u ? u->Slider(a.empty() ? "" : a[0].AsString().c_str(), v, lo, hi) : v};
        };
        b["move"] = [tf](std::vector<Value>& a) {
            if (Transform* t = tf())
                t->Translate({a.size() > 0 ? a[0].AsFloat() : 0.0f,
                              a.size() > 1 ? a[1].AsFloat() : 0.0f, 0.0f});
            return Value{};
        };
        b["set_pos"] = [tf](std::vector<Value>& a) {
            if (Transform* t = tf())
                t->localPosition = {a.size() > 0 ? a[0].AsFloat() : 0.0f,
                                    a.size() > 1 ? a[1].AsFloat() : 0.0f, 0.0f};
            return Value{};
        };
        b["rotate"] = [tf](std::vector<Value>& a) {
            if (Transform* t = tf()) t->Rotate({0, 0, a.empty() ? 0.0f : a[0].AsFloat()});
            return Value{};
        };
}

void OkayScriptVM::Impl::RegisterUiByNameBuiltins(BuiltinCtx& c) {
        auto& b = c.b;
        auto sceneOf = c.sceneOf;
        // ---- UI by name: read/drive any widget from script -----------------
        // All take the GameObject name as the first argument so one script can
        // wire up a whole menu. Missing objects/components are no-ops / defaults.
        b["ui_set_text"] = [sceneOf](std::vector<Value>& a) {
            if (a.size() >= 2) if (Scene* s = sceneOf()) if (GameObject* g = ResolveObject(s, a[0])) {
                if (auto* tr = g->GetComponent<TextRenderer>()) tr->text = a[1].AsString();
                else if (auto* bt = g->GetComponent<UIButton>()) bt->label = a[1].AsString();
                else if (auto* in = g->GetComponent<UIInputField>()) in->text = a[1].AsString();
            }
            return Value{};
        };
        // ui_set_focus("Play"): keyboard/gamepad focus, the ring, and scroll into view. 1 if it took focus.
        b["ui_set_focus"] = [sceneOf](std::vector<Value>& a) -> Value {
            if (a.empty()) return Value{0.0f};
            Scene* s = sceneOf();
            if (!s) return Value{0.0f};
            GameObject* g = ResolveObject(s, a[0]);
            return Value{g && SetUIFocus(g) ? 1.0f : 0.0f};
        };
        b["ui_get_text"] = [sceneOf](std::vector<Value>& a) -> Value {
            if (!a.empty()) if (Scene* s = sceneOf()) if (GameObject* g = ResolveObject(s, a[0])) {
                if (auto* tr = g->GetComponent<TextRenderer>()) return Value{tr->text};
                if (auto* in = g->GetComponent<UIInputField>()) return Value{in->text};
                if (auto* bt = g->GetComponent<UIButton>()) return Value{bt->label};
            }
            return Value{std::string{}};
        };
        // The game clipboard (the same text a focused input field copies and pastes).
        // Control characters are removed and the text is capped. See Clipboard.hpp.
        b["get_clipboard_text"] = [](std::vector<Value>&) -> Value { return Value{GetClipboardText()}; };
        b["set_clipboard_text"] = [](std::vector<Value>& a) -> Value {
            const std::string t = a.empty() ? std::string() : a[0].AsString();
            return Value{SetClipboardText(t) ? 1.0f : 0.0f};
        };
        b["ui_clicked"] = [sceneOf](std::vector<Value>& a) -> Value {
            if (!a.empty()) if (Scene* s = sceneOf()) if (GameObject* g = ResolveObject(s, a[0]))
                if (auto* bt = g->GetComponent<UIButton>()) return Value{bt->WasClicked()};
            return Value{false};
        };
        b["ui_set_interactable"] = [sceneOf](std::vector<Value>& a) {
            if (a.size() >= 2) if (Scene* s = sceneOf()) if (GameObject* g = ResolveObject(s, a[0]))
                if (auto* bt = g->GetComponent<UIButton>()) bt->interactable = a[1].AsBool();
            return Value{};
        };
        b["ui_slider_value"] = [sceneOf](std::vector<Value>& a) -> Value {
            if (!a.empty()) if (Scene* s = sceneOf()) if (GameObject* g = ResolveObject(s, a[0]))
                if (auto* sl = g->GetComponent<UISlider>()) return Value{sl->value};
            return Value{0.0f};
        };
        b["ui_set_slider"] = [sceneOf](std::vector<Value>& a) {
            if (a.size() >= 2) if (Scene* s = sceneOf()) if (GameObject* g = ResolveObject(s, a[0]))
                if (auto* sl = g->GetComponent<UISlider>()) sl->value = a[1].AsFloat();
            return Value{};
        };
        b["ui_toggle_value"] = [sceneOf](std::vector<Value>& a) -> Value {
            if (!a.empty()) if (Scene* s = sceneOf()) if (GameObject* g = ResolveObject(s, a[0]))
                if (auto* tg = g->GetComponent<UIToggle>()) return Value{tg->on};
            return Value{false};
        };
        b["ui_set_toggle"] = [sceneOf](std::vector<Value>& a) {
            if (a.size() >= 2) if (Scene* s = sceneOf()) if (GameObject* g = ResolveObject(s, a[0]))
                if (auto* tg = g->GetComponent<UIToggle>()) tg->on = a[1].AsBool();
            return Value{};
        };
        b["ui_dropdown_value"] = [sceneOf](std::vector<Value>& a) -> Value {
            if (!a.empty()) if (Scene* s = sceneOf()) if (GameObject* g = ResolveObject(s, a[0]))
                if (auto* dd = g->GetComponent<UIDropdown>()) return Value{(float)dd->value};
            return Value{0.0f};
        };
        b["ui_dropdown_text"] = [sceneOf](std::vector<Value>& a) -> Value {
            if (!a.empty()) if (Scene* s = sceneOf()) if (GameObject* g = ResolveObject(s, a[0]))
                if (auto* dd = g->GetComponent<UIDropdown>()) return Value{dd->Selected()};
            return Value{std::string{}};
        };
        b["ui_set_dropdown"] = [sceneOf](std::vector<Value>& a) {
            if (a.size() >= 2) if (Scene* s = sceneOf()) if (GameObject* g = ResolveObject(s, a[0]))
                if (auto* dd = g->GetComponent<UIDropdown>()) dd->SetValue(SafeInt(a[1].AsFloat()));
            return Value{};
        };
        // Tab bars: read / select the tab (fires on_change), badge a tab (0 none,
        // -1 dot, N number) and enable / disable a tab. Tab indices are 0-based.
        b["ui_tab_value"] = [sceneOf](std::vector<Value>& a) -> Value {
            if (!a.empty()) if (Scene* s = sceneOf()) if (GameObject* g = ResolveObject(s, a[0]))
                if (auto* tb = g->GetComponent<UITabs>()) return Value{(float)tb->value};
            return Value{0.0f};
        };
        b["ui_tab_text"] = [sceneOf](std::vector<Value>& a) -> Value {
            if (!a.empty()) if (Scene* s = sceneOf()) if (GameObject* g = ResolveObject(s, a[0]))
                if (auto* tb = g->GetComponent<UITabs>()) return Value{tb->SelectedLabel()};
            return Value{std::string{}};
        };
        b["ui_set_tab"] = [sceneOf](std::vector<Value>& a) {
            if (a.size() >= 2) if (Scene* s = sceneOf()) if (GameObject* g = ResolveObject(s, a[0]))
                if (auto* tb = g->GetComponent<UITabs>()) tb->Select(SafeInt(a[1].AsFloat()));
            return Value{};
        };
        b["ui_set_tab_badge"] = [sceneOf](std::vector<Value>& a) {
            if (a.size() >= 3) if (Scene* s = sceneOf()) if (GameObject* g = ResolveObject(s, a[0]))
                if (auto* tb = g->GetComponent<UITabs>()) tb->SetBadge(SafeInt(a[1].AsFloat()), SafeInt(a[2].AsFloat()));
            return Value{};
        };
        b["ui_set_tab_enabled"] = [sceneOf](std::vector<Value>& a) {
            if (a.size() >= 3) if (Scene* s = sceneOf()) if (GameObject* g = ResolveObject(s, a[0]))
                if (auto* tb = g->GetComponent<UITabs>()) tb->SetTabEnabled(SafeInt(a[1].AsFloat()), a[2].AsBool());
            return Value{};
        };
        b["ui_set_progress"] = [sceneOf](std::vector<Value>& a) {
            if (a.size() >= 2) if (Scene* s = sceneOf()) if (GameObject* g = ResolveObject(s, a[0]))
                if (auto* pb = g->GetComponent<UIProgressBar>()) pb->value = a[1].AsFloat();
            return Value{};
        };
        // App widgets. A missing object is a no-op so a graph can call them safely.
        b["ui_search_query"] = [sceneOf](std::vector<Value>& a) -> Value {
            if (!a.empty()) if (Scene* s = sceneOf()) if (GameObject* g = ResolveObject(s, a[0]))
                if (auto* w = g->GetComponent<UISearchBar>()) return Value{w->query};
            return Value{std::string{}};
        };
        b["ui_set_search"] = [sceneOf](std::vector<Value>& a) {
            if (a.size() >= 2) if (Scene* s = sceneOf()) if (GameObject* g = ResolveObject(s, a[0]))
                if (auto* w = g->GetComponent<UISearchBar>()) w->SetQuery(a[1].AsString());
            return Value{};
        };
        b["ui_search_count"] = [sceneOf](std::vector<Value>& a) -> Value {
            if (!a.empty()) if (Scene* s = sceneOf()) if (GameObject* g = ResolveObject(s, a[0]))
                if (auto* w = g->GetComponent<UISearchBar>()) return Value{(float)w->MatchCount()};
            return Value{0.0f};
        };
        b["ui_set_date"] = [sceneOf](std::vector<Value>& a) {
            if (a.size() >= 4) if (Scene* s = sceneOf()) if (GameObject* g = ResolveObject(s, a[0]))
                if (auto* w = g->GetComponent<UIDateTimePicker>())
                    w->SetDate(SafeInt(a[1].AsFloat()), SafeInt(a[2].AsFloat()), SafeInt(a[3].AsFloat()));
            return Value{};
        };
        b["ui_set_time"] = [sceneOf](std::vector<Value>& a) {
            if (a.size() >= 3) if (Scene* s = sceneOf()) if (GameObject* g = ResolveObject(s, a[0]))
                if (auto* w = g->GetComponent<UIDateTimePicker>())
                    w->SetTime(SafeInt(a[1].AsFloat()), SafeInt(a[2].AsFloat()));
            return Value{};
        };
        b["ui_date_text"] = [sceneOf](std::vector<Value>& a) -> Value {
            if (!a.empty()) if (Scene* s = sceneOf()) if (GameObject* g = ResolveObject(s, a[0]))
                if (auto* w = g->GetComponent<UIDateTimePicker>()) return Value{w->Text()};
            return Value{std::string{}};
        };
        b["ui_file_set_root"] = [sceneOf](std::vector<Value>& a) {
            if (a.size() >= 2) if (Scene* s = sceneOf()) if (GameObject* g = ResolveObject(s, a[0]))
                if (auto* w = g->GetComponent<UIFilePicker>()) w->SetRoot(a[1].AsString());
            return Value{};
        };
        b["ui_file_open"] = [sceneOf](std::vector<Value>& a) {
            if (a.size() >= 2) if (Scene* s = sceneOf()) if (GameObject* g = ResolveObject(s, a[0]))
                if (auto* w = g->GetComponent<UIFilePicker>()) w->Open(a[1].AsString());
            return Value{};
        };
        b["ui_file_selected"] = [sceneOf](std::vector<Value>& a) -> Value {
            if (!a.empty()) if (Scene* s = sceneOf()) if (GameObject* g = ResolveObject(s, a[0]))
                if (auto* w = g->GetComponent<UIFilePicker>()) return Value{w->selected};
            return Value{std::string{}};
        };
        b["ui_accordion_toggle"] = [sceneOf](std::vector<Value>& a) {
            if (a.size() >= 2) if (Scene* s = sceneOf()) if (GameObject* g = ResolveObject(s, a[0]))
                if (auto* w = g->GetComponent<UIAccordion>()) w->Toggle(SafeInt(a[1].AsFloat()));
            return Value{};
        };
        b["ui_accordion_open"] = [sceneOf](std::vector<Value>& a) -> Value {
            if (a.size() >= 2) if (Scene* s = sceneOf()) if (GameObject* g = ResolveObject(s, a[0]))
                if (auto* w = g->GetComponent<UIAccordion>()) return Value{w->IsExpanded(SafeInt(a[1].AsFloat()))};
            return Value{false};
        };
        b["ui_breadcrumb_select"] = [sceneOf](std::vector<Value>& a) {
            if (a.size() >= 2) if (Scene* s = sceneOf()) if (GameObject* g = ResolveObject(s, a[0]))
                if (auto* w = g->GetComponent<UIBreadcrumbs>()) w->Select(SafeInt(a[1].AsFloat()));
            return Value{};
        };
        b["ui_breadcrumb_path"] = [sceneOf](std::vector<Value>& a) -> Value {
            if (!a.empty()) if (Scene* s = sceneOf()) if (GameObject* g = ResolveObject(s, a[0]))
                if (auto* w = g->GetComponent<UIBreadcrumbs>()) return Value{w->Path()};
            return Value{std::string{}};
        };
        b["ui_page"] = [sceneOf](std::vector<Value>& a) -> Value {
            if (!a.empty()) if (Scene* s = sceneOf()) if (GameObject* g = ResolveObject(s, a[0]))
                if (auto* w = g->GetComponent<UIPagination>()) return Value{(float)w->Page()};
            return Value{0.0f};
        };
        b["ui_page_count"] = [sceneOf](std::vector<Value>& a) -> Value {
            if (!a.empty()) if (Scene* s = sceneOf()) if (GameObject* g = ResolveObject(s, a[0]))
                if (auto* w = g->GetComponent<UIPagination>()) return Value{(float)w->PageCount()};
            return Value{0.0f};
        };
        b["ui_set_page"] = [sceneOf](std::vector<Value>& a) {
            if (a.size() >= 2) if (Scene* s = sceneOf()) if (GameObject* g = ResolveObject(s, a[0]))
                if (auto* w = g->GetComponent<UIPagination>()) w->SetPage(SafeInt(a[1].AsFloat()));
            return Value{};
        };
        b["ui_set_loading"] = [sceneOf](std::vector<Value>& a) {
            if (a.size() >= 2) if (Scene* s = sceneOf()) if (GameObject* g = ResolveObject(s, a[0]))
                if (auto* w = g->GetComponent<UISkeleton>()) w->SetLoading(a[1].AsBool());
            return Value{};
        };
        b["ui_sheet_open"] = [sceneOf](std::vector<Value>& a) {
            if (!a.empty()) if (Scene* s = sceneOf()) if (GameObject* g = ResolveObject(s, a[0]))
                if (auto* w = g->GetComponent<UIBottomSheet>()) w->Open();
            return Value{};
        };
        b["ui_sheet_close"] = [sceneOf](std::vector<Value>& a) {
            if (!a.empty()) if (Scene* s = sceneOf()) if (GameObject* g = ResolveObject(s, a[0]))
                if (auto* w = g->GetComponent<UIBottomSheet>()) w->Close();
            return Value{};
        };
        b["ui_sheet_open_state"] = [sceneOf](std::vector<Value>& a) -> Value {
            if (!a.empty()) if (Scene* s = sceneOf()) if (GameObject* g = ResolveObject(s, a[0]))
                if (auto* w = g->GetComponent<UIBottomSheet>()) return Value{w->IsOpen()};
            return Value{false};
        };
        // Rotation (degrees, clockwise on screen) and scale (width, height) of a UI frame.
        // One scale number sets both axes. A widget with no frame gets one when you set these.
        auto uiFrame = [sceneOf](const std::string& name, bool create) -> UIFrame* {
            Scene* s = sceneOf();
            GameObject* g = s ? s->Find(name) : nullptr;
            if (!g) return nullptr;
            if (auto* f = g->GetComponent<UIFrame>()) return f;
            return create ? ConvertToUIFrame(g) : nullptr;
        };
        b["ui_get_rotation"] = [uiFrame](std::vector<Value>& a) -> Value {
            if (a.empty()) return Value{0.0f};
            UIFrame* f = uiFrame(a[0].AsString(), false);
            return Value{f ? f->rotation : 0.0f};
        };
        b["ui_set_rotation"] = [uiFrame](std::vector<Value>& a) {
            if (a.size() >= 2) if (UIFrame* f = uiFrame(a[0].AsString(), true)) f->rotation = a[1].AsFloat();
            return Value{};
        };
        b["ui_get_scale"] = [uiFrame](std::vector<Value>& a) -> Value {
            Vec2 s{1.0f, 1.0f};
            if (!a.empty()) if (UIFrame* f = uiFrame(a[0].AsString(), false)) s = f->scale;
            return Value{Vec3{s.x, s.y, 0.0f}};
        };
        b["ui_set_scale"] = [uiFrame](std::vector<Value>& a) {
            if (a.size() >= 2) if (UIFrame* f = uiFrame(a[0].AsString(), true)) {
                if (a.size() >= 3) f->scale = {a[1].AsFloat(), a[2].AsFloat()};
                else { const float u = a[1].AsFloat(); f->scale = {u, u}; }
            }
            return Value{};
        };
}

void OkayScriptVM::Impl::RegisterUiRuntimeBuiltins(BuiltinCtx& c) {
        auto& b = c.b;
        auto sceneOf = c.sceneOf;
        // ---- Runtime UI: roles, radio groups, floating numbers, dialogue boxes (by object name) ----
        // ui_set_variant("Quit", "danger"): give a widget a role (primary, secondary, danger, ghost,
        // outline, pill, default) and style it from the scene's theme now.
        b["ui_set_variant"] = [sceneOf](std::vector<Value>& a) {
            if (a.size() < 2) return Value{0.0f};
            Scene* s = sceneOf();
            GameObject* g = s ? ResolveObject(s, a[0]) : nullptr;
            if (!g) return Value{0.0f};
            int v = a[1].IsNumber() ? SafeInt(a[1].AsFloat()) : UITheme::VariantFromName(a[1].AsString());
            if (v < 0 || v >= UITheme::kVariantCount) return Value{0.0f};
            auto* sv = g->GetComponent<UIStyleVariant>();
            if (!sv) sv = g->AddComponent<UIStyleVariant>();
            sv->variant = v;
            if (a.size() >= 3) sv->themeName = a[2].AsString();
            if (UITheme* th = UITheme::Find(s, sv->themeName)) th->ApplyTo(g);
            return Value{1.0f};
        };
        // ui_theme_preset("Neon") restyles the scene with a built-in look (first theme in the scene, or the one named).
        b["ui_theme_preset"] = [sceneOf](std::vector<Value>& a) {
            if (a.empty()) return Value{0.0f};
            Scene* s = sceneOf();
            if (!s) return Value{0.0f};
            int p = a[0].IsNumber() ? SafeInt(a[0].AsFloat()) : UITheme::PresetFromName(a[0].AsString());
            if (p < 0 || p >= UITheme::kPresetCount) return Value{0.0f};
            UITheme* th = UITheme::Find(s, a.size() >= 2 ? a[1].AsString() : std::string());
            if (!th) return Value{0.0f};
            th->ApplyPreset(p);
            th->Apply();
            UIGallery::StyleGalleryExtras(*s, th);   // the UI Gallery's slots, insets and Glass blobs follow too
            return Value{1.0f};
        };
        // ui_set_look("Pause Menu", "Neon"): put one of the 54 looks (and optional style, size, density) on one screen or widget and everything under
        // it. There is no scene-wide theme. Returns 1, or 0 for an unknown name or look.
        b["ui_set_look"] = [sceneOf](std::vector<Value>& a) {
            Scene* s = sceneOf();
            GameObject* g = (s && a.size() >= 2) ? s->Find(a[0].AsString()) : nullptr;
            if (!g) return Value{0.0f};
            return Value{UIStyleLib::ApplyLook(g, a[1].AsString(), a.size() >= 3 ? a[2].AsString() : std::string(), a.size() >= 4 ? a[3].AsString() : std::string(),
                                               a.size() >= 5 ? a[4].AsString() : std::string()) ? 1.0f : 0.0f};
        };
        // The global theme was removed (looks are per screen and per widget). An old script that calls ui_set_theme keeps running: it logs a line
        // and does nothing.
        b["ui_set_theme"] = [](std::vector<Value>&) {
            static bool told = false;
            if (!told) { told = true; Log::Warning("[ui] ui_set_theme is gone: looks are per screen or widget now. Use ui_set_look(\"Screen name\", \"Look name\")."); }
            return Value{0.0f};
        };
        // ui_look_sound("Neon", "click") is the sound name a look uses for hover, click or open ("" when it names none).
        b["ui_look_sound"] = [](std::vector<Value>& a) -> Value {
            if (a.size() < 2) return Value{std::string()};
            return Value{UIStyleLib::ThemeSound(a[0].AsString(), a[1].AsString())};
        };
        // ui_set_style("Play", "Neon Pill") gives a widget a named look from the style library now; optional 3rd and 4th arguments are the
        // size (xs..xl) and the density (compact..spacious).
        b["ui_set_style"] = [sceneOf](std::vector<Value>& a) {
            Scene* s = sceneOf();
            GameObject* g = (s && a.size() >= 2) ? s->Find(a[0].AsString()) : nullptr;
            if (!g || !UIStyleLib::FindStyle(a[1].AsString())) return Value{0.0f};
            auto* sv = g->GetComponent<UIStyleVariant>();
            if (!sv) sv = g->AddComponent<UIStyleVariant>();
            sv->style = a[1].AsString();
            if (a.size() >= 3) sv->styleSize = a[2].AsString();
            if (a.size() >= 4) sv->styleDensity = a[3].AsString();
            UIStyleLib::ApplyStyle(g, UIStyleLib::SpecOf(sv->style, sv->styleSize.empty() ? "m" : sv->styleSize, sv->styleDensity.empty() ? "cosy" : sv->styleDensity), UIStyleLib::PaletteFor(sv));
            return Value{1.0f};
        };
        // ui_set_rich("Label", true): read [color=...] [b] [i] [size=N] tags in that text (screen or world text).
        b["ui_set_rich"] = [sceneOf](std::vector<Value>& a) {
            if (a.size() >= 2) if (Scene* s = sceneOf()) if (GameObject* g = ResolveObject(s, a[0]))
                if (auto* tr = g->GetComponent<TextRenderer>()) tr->richText = a[1].AsBool();
            return Value{};
        };
        // ui_load_theme("Assets/UI/Forest.okaytheme") loads a saved theme into the scene's theme and restyles the UI.
        b["ui_load_theme"] = [sceneOf](std::vector<Value>& a) {
            if (a.empty()) return Value{0.0f};
            Scene* s = sceneOf();
            UITheme* th = s ? UITheme::Find(s, a.size() >= 2 ? a[1].AsString() : std::string()) : nullptr;
            if (!th) return Value{0.0f};
            if (!LoadUIThemeAsset(*th, a[0].AsString())) return Value{0.0f};
            th->Apply();
            return Value{1.0f};
        };
        // Radio buttons: ui_radio_value("Quality") is the index of the picked one in its group (-1 none);
        // ui_set_radio("High") picks one by object name.
        b["ui_radio_value"] = [sceneOf](std::vector<Value>& a) -> Value {
            Scene* s = sceneOf();
            if (a.empty() || !s) return Value{-1.0f};
            const std::string group = a[0].AsString();
            int idx = 0;
            for (auto* t : s->FindObjectsOfType<UIToggle>()) {
                if (t->style != UIToggle::Style::Radio || t->radioGroup != group) continue;
                if (t->on) return Value{(float)idx};
                ++idx;
            }
            return Value{-1.0f};
        };
        b["ui_set_radio"] = [sceneOf](std::vector<Value>& a) {
            if (!a.empty()) if (Scene* s = sceneOf()) if (GameObject* g = ResolveObject(s, a[0]))
                if (auto* tg = g->GetComponent<UIToggle>()) tg->Select();
            return Value{};
        };
        // Floating numbers: ui_damage_number("-25", x, y [, "crit"|"heal"|"miss"|"info"]) on the first
        // UI Damage Numbers in the scene.
        b["ui_damage_number"] = [sceneOf](std::vector<Value>& a) {
            if (a.size() < 3) return Value{};
            Scene* s = sceneOf();
            auto* dn = s ? s->FindObjectOfType<UIDamageNumbers>() : nullptr;
            if (dn) dn->Spawn(a[0].AsString(), a.size() >= 4 ? UIDamageNumbers::KindFromName(a[3].AsString()) : UIDamageNumbers::Kind::Normal,
                              Vec2{a[1].AsFloat(), a[2].AsFloat()});
            return Value{};
        };
        // Dialogue box: ui_dialogue_show("Mara", "Hello", "Yes", "No") shows a line with up to 4 choices;
        // ui_dialogue_choice() is the last picked choice (-1 none); ui_dialogue_open() tells if it is up.
        b["ui_dialogue_show"] = [sceneOf](std::vector<Value>& a) {
            if (a.size() < 2) return Value{};
            Scene* s = sceneOf();
            auto* db = s ? s->FindObjectOfType<UIDialogueBox>() : nullptr;
            if (!db) return Value{};
            std::vector<std::string> ch;
            for (std::size_t i = 2; i < a.size() && ch.size() < 4; ++i) ch.push_back(a[i].AsString());
            db->Show(a[0].AsString(), a[1].AsString(), ch);
            return Value{};
        };
        b["ui_dialogue_choice"] = [sceneOf](std::vector<Value>&) -> Value {
            Scene* s = sceneOf();
            auto* db = s ? s->FindObjectOfType<UIDialogueBox>() : nullptr;
            return Value{db ? (float)db->LastChoice() : -1.0f};
        };
        b["ui_dialogue_open"] = [sceneOf](std::vector<Value>&) -> Value {
            Scene* s = sceneOf();
            auto* db = s ? s->FindObjectOfType<UIDialogueBox>() : nullptr;
            return Value{db ? db->IsOpen() : false};
        };
        // grok/ui-color — pick a swatch on a named color picker (fires on_change).
        b["set_swatch"] = [sceneOf](std::vector<Value>& a) {
            if (a.size() >= 2) if (Scene* s = sceneOf()) if (GameObject* g = ResolveObject(s, a[0]))
                if (auto* cp = g->GetComponent<UIColorPicker>()) cp->SetValue(SafeInt(a[1].AsFloat()));
            return Value{};
        };
        b["swatch_value"] = [sceneOf](std::vector<Value>& a) -> Value {
            if (!a.empty()) if (Scene* s = sceneOf()) if (GameObject* g = ResolveObject(s, a[0]))
                if (auto* cp = g->GetComponent<UIColorPicker>()) return Value{(float)cp->Value()};
            return Value{-1.0f};
        };
        // grok/ui-hud — crosshair recovery, radial options, and the slot grid, by object name.
        b["kick_spread"] = [sceneOf](std::vector<Value>& a) {
            if (a.size() >= 2) if (Scene* s = sceneOf()) if (GameObject* g = ResolveObject(s, a[0]))
                if (auto* cr = g->GetComponent<Crosshair>()) cr->KickSpread(a[1].AsFloat());
            return Value{};
        };
        b["set_spread"] = [sceneOf](std::vector<Value>& a) {
            if (a.size() >= 2) if (Scene* s = sceneOf()) if (GameObject* g = ResolveObject(s, a[0]))
                if (auto* cr = g->GetComponent<Crosshair>()) cr->SetSpread(a[1].AsFloat());
            return Value{};
        };
        b["set_spread_recover"] = [sceneOf](std::vector<Value>& a) {
            if (a.size() >= 2) if (Scene* s = sceneOf()) if (GameObject* g = ResolveObject(s, a[0]))
                if (auto* cr = g->GetComponent<Crosshair>()) cr->SetSpreadRecover(a[1].AsFloat());
            return Value{};
        };
        b["set_radial"] = [sceneOf](std::vector<Value>& a) {
            if (a.size() >= 2) if (Scene* s = sceneOf()) if (GameObject* g = ResolveObject(s, a[0]))
                if (auto* rp = g->GetComponent<UIRadialProgress>()) rp->SetValue(a[1].AsFloat());
            return Value{};
        };
        b["set_radial_thickness"] = [sceneOf](std::vector<Value>& a) {
            if (a.size() >= 2) if (Scene* s = sceneOf()) if (GameObject* g = ResolveObject(s, a[0]))
                if (auto* rp = g->GetComponent<UIRadialProgress>()) rp->SetThickness(a[1].AsFloat());
            return Value{};
        };
        b["set_radial_start"] = [sceneOf](std::vector<Value>& a) {
            if (a.size() >= 2) if (Scene* s = sceneOf()) if (GameObject* g = ResolveObject(s, a[0]))
                if (auto* rp = g->GetComponent<UIRadialProgress>()) rp->SetStartAngle(a[1].AsFloat());
            return Value{};
        };
        b["set_radial_spin"] = [sceneOf](std::vector<Value>& a) {
            if (a.size() >= 2) if (Scene* s = sceneOf()) if (GameObject* g = ResolveObject(s, a[0]))
                if (auto* rp = g->GetComponent<UIRadialProgress>()) rp->SetSpin(a[1].AsBool());
            return Value{};
        };
        b["set_radial_clockwise"] = [sceneOf](std::vector<Value>& a) {
            if (a.size() >= 2) if (Scene* s = sceneOf()) if (GameObject* g = ResolveObject(s, a[0]))
                if (auto* rp = g->GetComponent<UIRadialProgress>()) rp->SetClockwise(a[1].AsBool());
            return Value{};
        };
        b["set_radial_percent"] = [sceneOf](std::vector<Value>& a) {
            if (a.size() >= 2) if (Scene* s = sceneOf()) if (GameObject* g = ResolveObject(s, a[0]))
                if (auto* rp = g->GetComponent<UIRadialProgress>()) rp->SetShowPercent(a[1].AsBool());
            return Value{};
        };
        b["set_slot"] = [sceneOf](std::vector<Value>& a) {
            if (a.size() >= 2) if (Scene* s = sceneOf()) if (GameObject* g = ResolveObject(s, a[0]))
                if (auto* sg = g->GetComponent<UISlotGrid>()) sg->SetValue(SafeInt(a[1].AsFloat()));
            return Value{};
        };
        b["slot_value"] = [sceneOf](std::vector<Value>& a) -> Value {
            if (!a.empty()) if (Scene* s = sceneOf()) if (GameObject* g = ResolveObject(s, a[0]))
                if (auto* sg = g->GetComponent<UISlotGrid>()) return Value{(float)sg->Value()};
            return Value{-1.0f};
        };
        // grok/ui-controls: the options added on top of dropdown, scroll, tabs, tooltip and text fields.
        b["set_dropdown_search"] = [sceneOf](std::vector<Value>& a) {
            if (a.size() >= 2) if (Scene* s = sceneOf()) if (GameObject* g = ResolveObject(s, a[0]))
                if (auto* dd = g->GetComponent<UIDropdown>()) dd->SetSearchable(a[1].AsBool());
            return Value{};
        };
        b["set_dropdown_max"] = [sceneOf](std::vector<Value>& a) {
            if (a.size() >= 2) if (Scene* s = sceneOf()) if (GameObject* g = ResolveObject(s, a[0]))
                if (auto* dd = g->GetComponent<UIDropdown>()) dd->SetMaxVisible(SafeInt(a[1].AsFloat()));
            return Value{};
        };
        b["set_dropdown_icon"] = [sceneOf](std::vector<Value>& a) {
            if (a.size() >= 3) if (Scene* s = sceneOf()) if (GameObject* g = ResolveObject(s, a[0]))
                if (auto* dd = g->GetComponent<UIDropdown>()) dd->SetOptionIcon(SafeInt(a[1].AsFloat()), a[2].AsString());
            return Value{};
        };
        b["set_scroll_inertia"] = [sceneOf](std::vector<Value>& a) {
            if (a.size() >= 2) if (Scene* s = sceneOf()) if (GameObject* g = ResolveObject(s, a[0]))
                if (auto* sv = g->GetComponent<UIScrollView>()) sv->SetInertia(a[1].AsFloat());
            return Value{};
        };
        b["set_scroll_elastic"] = [sceneOf](std::vector<Value>& a) {
            if (a.size() >= 2) if (Scene* s = sceneOf()) if (GameObject* g = ResolveObject(s, a[0]))
                if (auto* sv = g->GetComponent<UIScrollView>()) sv->SetElastic(a[1].AsBool());
            return Value{};
        };
        b["set_scroll_bar"] = [sceneOf](std::vector<Value>& a) {
            if (a.size() >= 2) if (Scene* s = sceneOf()) if (GameObject* g = ResolveObject(s, a[0]))
                if (auto* sv = g->GetComponent<UIScrollView>()) sv->SetBarStyle(SafeInt(a[1].AsFloat()));
            return Value{};
        };
        b["set_tabs_closable"] = [sceneOf](std::vector<Value>& a) {
            if (a.size() >= 2) if (Scene* s = sceneOf()) if (GameObject* g = ResolveObject(s, a[0]))
                if (auto* tb = g->GetComponent<UITabs>()) tb->SetClosable(a[1].AsBool());
            return Value{};
        };
        b["close_tab"] = [sceneOf](std::vector<Value>& a) {
            if (a.size() >= 2) if (Scene* s = sceneOf()) if (GameObject* g = ResolveObject(s, a[0]))
                if (auto* tb = g->GetComponent<UITabs>()) tb->CloseTab(SafeInt(a[1].AsFloat()));
            return Value{};
        };
        b["set_tooltip_rich"] = [sceneOf](std::vector<Value>& a) {
            if (a.size() >= 2) if (Scene* s = sceneOf()) if (GameObject* g = ResolveObject(s, a[0]))
                if (auto* tt = g->GetComponent<UITooltip>()) tt->SetRichText(a[1].AsBool());
            return Value{};
        };
        b["set_tooltip_offset"] = [sceneOf](std::vector<Value>& a) {
            if (a.size() >= 3) if (Scene* s = sceneOf()) if (GameObject* g = ResolveObject(s, a[0]))
                if (auto* tt = g->GetComponent<UITooltip>()) tt->SetOffset(a[1].AsFloat(), a[2].AsFloat());
            return Value{};
        };
        b["set_tooltip_delay"] = [sceneOf](std::vector<Value>& a) {
            if (a.size() >= 2) if (Scene* s = sceneOf()) if (GameObject* g = ResolveObject(s, a[0]))
                if (auto* tt = g->GetComponent<UITooltip>()) tt->SetDelay(a[1].AsFloat());
            return Value{};
        };
        b["set_input_multiline"] = [sceneOf](std::vector<Value>& a) {
            if (a.size() >= 2) if (Scene* s = sceneOf()) if (GameObject* g = ResolveObject(s, a[0]))
                if (auto* in = g->GetComponent<UIInputField>()) in->SetMultiline(a[1].AsBool());
            return Value{};
        };
        b["set_input_limit"] = [sceneOf](std::vector<Value>& a) {
            if (a.size() >= 2) if (Scene* s = sceneOf()) if (GameObject* g = ResolveObject(s, a[0]))
                if (auto* in = g->GetComponent<UIInputField>()) in->SetMaxLength(SafeInt(a[1].AsFloat()));
            return Value{};
        };
        b["set_input_type"] = [sceneOf](std::vector<Value>& a) {
            if (a.size() >= 2) if (Scene* s = sceneOf()) if (GameObject* g = ResolveObject(s, a[0]))
                if (auto* in = g->GetComponent<UIInputField>()) in->SetContentType(SafeInt(a[1].AsFloat()));
            return Value{};
        };
}

void OkayScriptVM::Impl::RegisterUiControlBuiltins(BuiltinCtx& c) {
        auto& b = c.b;
        auto go = c.go;
        auto sceneOf = c.sceneOf;
        // grok/ui-toast. toast_push (not ui_toast, which is the immediate-mode popup).
        auto firstToast = [sceneOf]() -> UIToast* {
            Scene* s = sceneOf();
            return s ? s->FindObjectOfType<UIToast>() : nullptr;
        };
        auto firstModal = [sceneOf]() -> UIModal* {
            Scene* s = sceneOf();
            return s ? s->FindObjectOfType<UIModal>() : nullptr;
        };
        b["toast_push"] = [firstToast](std::vector<Value>& a) {
            if (UIToast* t = firstToast())
                t->Push(a.size() > 0 ? a[0].AsString() : std::string{},
                        a.size() > 1 ? a[1].AsString() : std::string{},
                        a.size() > 2 ? SafeInt(a[2].AsFloat()) : 0);
            return Value{};
        };
        b["set_toast_duration"] = [firstToast](std::vector<Value>& a) {
            if (UIToast* t = firstToast()) t->SetDuration(a.empty() ? 4.0f : a[0].AsFloat());
            return Value{};
        };
        b["set_toast_corner"] = [firstToast](std::vector<Value>& a) {
            if (UIToast* t = firstToast()) t->SetCorner(a.empty() ? 0 : SafeInt(a[0].AsFloat()));
            return Value{};
        };
        b["set_toast_max"] = [firstToast](std::vector<Value>& a) {
            if (UIToast* t = firstToast()) t->SetMaxStack(a.empty() ? 4 : SafeInt(a[0].AsFloat()));
            return Value{};
        };
        b["toast_count"] = [firstToast](std::vector<Value>&) -> Value {
            return Value{firstToast() ? (float)firstToast()->Count() : 0.0f};
        };
        b["modal_open"] = [firstModal](std::vector<Value>&) {
            if (UIModal* m = firstModal()) m->Open();
            return Value{};
        };
        b["modal_close"] = [firstModal](std::vector<Value>&) {
            if (UIModal* m = firstModal()) m->Close();
            return Value{};
        };
        b["set_modal_style"] = [firstModal](std::vector<Value>& a) {
            if (UIModal* m = firstModal()) m->SetStyleCard(a.empty() ? true : a[0].AsBool());
            return Value{};
        };
        b["set_modal_title"] = [firstModal](std::vector<Value>& a) {
            if (UIModal* m = firstModal()) m->SetTitle(a.empty() ? std::string{} : a[0].AsString());
            return Value{};
        };
        b["set_modal_message"] = [firstModal](std::vector<Value>& a) {
            if (UIModal* m = firstModal()) m->SetMessage(a.empty() ? std::string{} : a[0].AsString());
            return Value{};
        };
        b["set_modal_confirm"] = [firstModal](std::vector<Value>& a) {
            if (UIModal* m = firstModal()) m->SetConfirmText(a.empty() ? std::string{} : a[0].AsString());
            return Value{};
        };
        b["set_modal_cancel"] = [firstModal](std::vector<Value>& a) {
            if (UIModal* m = firstModal()) m->SetCancelText(a.empty() ? std::string{} : a[0].AsString());
            return Value{};
        };
        b["ui_set_fill"] = [sceneOf](std::vector<Value>& a) {
            if (a.size() >= 2) if (Scene* s = sceneOf()) if (GameObject* g = ResolveObject(s, a[0]))
                if (auto* im = g->GetComponent<UIImage>()) im->fillAmount = a[1].AsFloat();
            return Value{};
        };
        // Pointer / hover over UI. uiHit returns the topmost widget under the mouse.
        auto uiHit = [sceneOf]() -> GameObject* {
            Scene* s = sceneOf(); if (!s) return nullptr;
            Vec2 m = Input::MousePosition();
            GameObject* hit = nullptr;
            for (const auto& up : s->Objects())
                if (up->active && UIScreenContains(up.get(), m, UICanvas::Width(), UICanvas::Height()))
                    hit = up.get();
            return hit;
        };
        b["ui_pointer_over"] = [uiHit](std::vector<Value>&) -> Value { return Value{uiHit() != nullptr}; };
        b["ui_hovered"] = [uiHit](std::vector<Value>&) -> Value {
            GameObject* h = uiHit(); return Value{h ? h->name : std::string{}};
        };
        b["ui_is_hovered"] = [uiHit](std::vector<Value>& a) -> Value {
            GameObject* h = uiHit(); return Value{h && !a.empty() && h->name == a[0].AsString()};
        };
        // The last drag-and-drop result (set by UIDraggable via Prefs).
        b["ui_drop_source"] = [](std::vector<Value>&) -> Value { return Value{Prefs::GetString("ui_drop_source", "")}; };
        b["ui_drop_target"] = [](std::vector<Value>&) -> Value { return Value{Prefs::GetString("ui_drop_target", "")}; };
        b["drop_source"] = [](std::vector<Value>&) -> Value { return Value{Prefs::GetString("drop_source", "")}; };
        b["drop_target"] = [](std::vector<Value>&) -> Value { return Value{Prefs::GetString("drop_target", "")}; };
        b["set_texture"] = [go](std::vector<Value>& a) {
            if (GameObject* g = go())
                if (auto* sr = g->GetComponent<SpriteRenderer>())
                    sr->texture = a.empty() ? std::string{} : a[0].AsString();
            return Value{};
        };
        b["flip_x"] = [go](std::vector<Value>& a) {
            if (GameObject* g = go())
                if (auto* sr = g->GetComponent<SpriteRenderer>())
                    sr->flipX = a.empty() ? true : a[0].AsBool();
            return Value{};
        };
        b["flip_y"] = [go](std::vector<Value>& a) {
            if (GameObject* g = go())
                if (auto* sr = g->GetComponent<SpriteRenderer>())
                    sr->flipY = a.empty() ? true : a[0].AsBool();
            return Value{};
        };
        b["play_sound"] = [go](std::vector<Value>& a) {
            // play_sound() restarts the source; play_sound(volume) plays its clip
            // as a one-shot instead, so quick repeats overlap.
            if (GameObject* g = go())
                if (auto* au = g->GetComponent<AudioSource>()) {
                    if (a.empty()) au->Play();
                    else au->PlayOneShot(a[0].AsFloat());
                }
            return Value{};
        };
        b["set_progress"] = [go](std::vector<Value>& a) {
            if (GameObject* g = go())
                if (auto* pb = g->GetComponent<UIProgressBar>())
                    pb->SetValue(a.empty() ? 0.0f : a[0].AsFloat());
            return Value{};
        };
        // UI slider/toggle on this object (settings menus react via on_change/on_toggle).
        b["slider_value"] = [go](std::vector<Value>&) -> Value {
            if (GameObject* g = go())
                if (auto* sl = g->GetComponent<UISlider>()) return Value{sl->value};
            return Value{0.0f};
        };
        b["set_slider"] = [go](std::vector<Value>& a) {
            if (GameObject* g = go())
                if (auto* sl = g->GetComponent<UISlider>())
                    sl->SetValue(a.empty() ? 0.0f : a[0].AsFloat());
            return Value{};
        };
        // grok/ui-options: the main new slider / bar / button controls. Events stay
        // on_change (OnValueChanged), on_click (OnClick) and on_submit (OnSubmit).
        b["set_slider_step"] = [go](std::vector<Value>& a) {
            if (GameObject* g = go())
                if (auto* sl = g->GetComponent<UISlider>())
                    sl->step = a.empty() ? 0.0f : a[0].AsFloat();
            return Value{};
        };
        b["set_slider_format"] = [go](std::vector<Value>& a) {
            if (GameObject* g = go())
                if (auto* sl = g->GetComponent<UISlider>())
                    sl->valueFormat = a.empty() ? std::string{} : a[0].AsString();
            return Value{};
        };
        b["ui_set_slider_step"] = [sceneOf](std::vector<Value>& a) {
            if (a.size() >= 2) if (Scene* s = sceneOf()) if (GameObject* g = ResolveObject(s, a[0]))
                if (auto* sl = g->GetComponent<UISlider>()) sl->step = a[1].AsFloat();
            return Value{};
        };
        b["ui_set_slider_format"] = [sceneOf](std::vector<Value>& a) {
            if (a.size() >= 2) if (Scene* s = sceneOf()) if (GameObject* g = ResolveObject(s, a[0]))
                if (auto* sl = g->GetComponent<UISlider>()) sl->valueFormat = a[1].AsString();
            return Value{};
        };
        b["set_bar_segments"] = [go](std::vector<Value>& a) {
            if (GameObject* g = go())
                if (auto* pb = g->GetComponent<UIProgressBar>())
                    pb->segments = a.empty() ? 0 : SafeInt(a[0].AsFloat());
            return Value{};
        };
        b["set_bar_trail"] = [go](std::vector<Value>& a) {
            if (GameObject* g = go())
                if (auto* pb = g->GetComponent<UIProgressBar>())
                    pb->trailDelay = a.empty() ? 0.0f : a[0].AsFloat();
            return Value{};
        };
        b["set_bar_format"] = [go](std::vector<Value>& a) {
            if (GameObject* g = go())
                if (auto* pb = g->GetComponent<UIProgressBar>())
                    pb->textFormat = a.empty() ? std::string{} : a[0].AsString();
            return Value{};
        };
        b["ui_set_bar_segments"] = [sceneOf](std::vector<Value>& a) {
            if (a.size() >= 2) if (Scene* s = sceneOf()) if (GameObject* g = ResolveObject(s, a[0]))
                if (auto* pb = g->GetComponent<UIProgressBar>()) pb->segments = SafeInt(a[1].AsFloat());
            return Value{};
        };
        b["ui_set_bar_trail"] = [sceneOf](std::vector<Value>& a) {
            if (a.size() >= 2) if (Scene* s = sceneOf()) if (GameObject* g = ResolveObject(s, a[0]))
                if (auto* pb = g->GetComponent<UIProgressBar>()) pb->trailDelay = a[1].AsFloat();
            return Value{};
        };
        b["ui_set_bar_format"] = [sceneOf](std::vector<Value>& a) {
            if (a.size() >= 2) if (Scene* s = sceneOf()) if (GameObject* g = ResolveObject(s, a[0]))
                if (auto* pb = g->GetComponent<UIProgressBar>()) pb->textFormat = a[1].AsString();
            return Value{};
        };
        b["set_button_ease"] = [go](std::vector<Value>& a) {
            if (GameObject* g = go())
                if (auto* bt = g->GetComponent<UIButton>()) {
                    bt->transitionEase = a.empty() ? 0 : SafeInt(a[0].AsFloat());
                    if (a.size() >= 2) bt->transitionDuration = a[1].AsFloat();
                }
            return Value{};
        };
        b["ui_set_button_ease"] = [sceneOf](std::vector<Value>& a) {
            if (a.size() >= 2) if (Scene* s = sceneOf()) if (GameObject* g = ResolveObject(s, a[0]))
                if (auto* bt = g->GetComponent<UIButton>()) {
                    bt->transitionEase = SafeInt(a[1].AsFloat());
                    if (a.size() >= 3) bt->transitionDuration = a[2].AsFloat();
                }
            return Value{};
        };
        b["toggle_on"] = [go](std::vector<Value>&) -> Value {
            if (GameObject* g = go())
                if (auto* tg = g->GetComponent<UIToggle>()) return Value{tg->on};
            return Value{false};
        };
        b["set_toggle"] = [go](std::vector<Value>& a) {
            if (GameObject* g = go())
                if (auto* tg = g->GetComponent<UIToggle>()) tg->on = !a.empty() && a[0].AsBool();
            return Value{};
        };
        // UI tabs on this object (react to switches via on_change).
        b["tab_value"] = [go](std::vector<Value>&) -> Value {
            if (GameObject* g = go())
                if (auto* tb = g->GetComponent<UITabs>()) return Value{(float)tb->value};
            return Value{0.0f};
        };
        b["set_tab"] = [go](std::vector<Value>& a) {
            if (GameObject* g = go())
                if (auto* tb = g->GetComponent<UITabs>()) tb->Select(a.empty() ? 0 : SafeInt(a[0].AsFloat()));
            return Value{};
        };
        b["set_tab_badge"] = [go](std::vector<Value>& a) {
            if (a.size() >= 2) if (GameObject* g = go())
                if (auto* tb = g->GetComponent<UITabs>()) tb->SetBadge(SafeInt(a[0].AsFloat()), SafeInt(a[1].AsFloat()));
            return Value{};
        };
        b["set_tab_enabled"] = [go](std::vector<Value>& a) {
            if (a.size() >= 2) if (GameObject* g = go())
                if (auto* tb = g->GetComponent<UITabs>()) tb->SetTabEnabled(SafeInt(a[0].AsFloat()), a[1].AsBool());
            return Value{};
        };
}

} // namespace okay
