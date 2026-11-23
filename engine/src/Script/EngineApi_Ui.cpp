// Typed script API, UI widget and UI library: the Bind<Topic> functions of the binding table (see EngineApiInternal.hpp).
#include "EngineApiInternal.hpp"

namespace okay::script::engine_api {

void Ctx::BindUiControls() {
    // ---- grok/ui-color ----
    {
        ApiType cp; cp.name = "UIColorPicker"; cp.isComponent = true;
        cp.doc = "A row of color swatches. The chosen swatch is the current color.";
        cp.find = [](GameObject* go) -> Component* { return go ? go->GetComponent<UIColorPicker>() : nullptr; };
        auto of = [](const Value& self, CallContext& c) -> UIColorPicker* {
            GameObject* g = Resolve(std::get<ObjRef>(self), c.loc);
            auto* p = g->GetComponent<UIColorPicker>();
            if (!p) throw RuntimeError("'" + g->name + "' has no Color Picker (add one in the Inspector)", c.loc);
            return p;
        };
        cp.methods.push_back(method("SetValue", {tInt}, {"index"}, tVoid,
            [of](const Value& s, std::vector<Value>& a, CallContext& c) { of(s, c)->SetValue(I(a[0])); return Value{}; },
            "Select a swatch by index. The chip shows that color."));
        ApiMethod val = method("Value", {}, {}, tInt,
            [of](const Value& s, std::vector<Value>&, CallContext& c) { return Value{(std::int32_t)of(s, c)->Value()}; },
            "The selected swatch, or -1.");
        val.pure = true;
        cp.methods.push_back(std::move(val));
        cp.methods.push_back(method("SetAdvanced", {tBool}, {"on"}, tVoid,
            [of](const Value& s, std::vector<Value>& a, CallContext& c) { of(s, c)->SetAdvanced(std::get<bool>(a[0])); return Value{}; },
            "Show or hide the hue / saturation square and the value and alpha sliders."));
        cp.methods.push_back(method("SetHSVA", {tFloat, tFloat, tFloat, tFloat}, {"hue", "saturation", "value", "alpha"}, tVoid,
            [of](const Value& s, std::vector<Value>& a, CallContext& c) { of(s, c)->SetHSVA(F(a[0]), F(a[1]), F(a[2]), F(a[3])); return Value{}; },
            "Make a colour from hue (0 to 360 degrees), saturation, value and alpha (each 0 to 1)."));
        ApiMethod hx = method("Hex", {}, {}, tString,
            [of](const Value& s, std::vector<Value>&, CallContext& c) { return Value{of(s, c)->Hex()}; },
            "The current colour as \"#RRGGBB\" (\"#RRGGBBAA\" when see-through).");
        hx.pure = true; cp.methods.push_back(std::move(hx));
        api.types.push_back(std::move(cp));
    }
    // ---- grok/ui-hud: crosshair recovery, radial options, slot grid ----
    {
        auto need = [](auto* comp, GameObject* g, CallContext& c, const char* what) {
            if (!comp) throw RuntimeError("'" + g->name + "' has no " + std::string(what) + " (add one in the Inspector)", c.loc);
            return comp;
        };
        {
            ApiType ch; ch.name = "Crosshair"; ch.isComponent = true;
            ch.doc = "An aim reticle. SpreadRecover pulls an extra gap back to 0; 0 keeps the authored spread.";
            ch.find = [](GameObject* go) -> Component* { return go ? go->GetComponent<Crosshair>() : nullptr; };
            auto of = [need](const Value& self, CallContext& c) -> Crosshair* {
                GameObject* g = Resolve(std::get<ObjRef>(self), c.loc);
                return need(g->GetComponent<Crosshair>(), g, c, "Crosshair");
            };
            ch.methods.push_back(method("SetSpread", {tFloat}, {"pixels"}, tVoid,
                [of](const Value& s, std::vector<Value>& a, CallContext& c) { of(s, c)->SetSpread(F(a[0])); return Value{}; },
                "Set the extra gap around the ticks, in pixels."));
            ch.methods.push_back(method("SetSpreadRecover", {tFloat}, {"pixelsPerSecond"}, tVoid,
                [of](const Value& s, std::vector<Value>& a, CallContext& c) { of(s, c)->SetSpreadRecover(F(a[0])); return Value{}; },
                "How fast the extra gap eases back to 0. 0 leaves the spread where it is."));
            ch.methods.push_back(method("KickSpread", {tFloat}, {"pixels"}, tVoid,
                [of](const Value& s, std::vector<Value>& a, CallContext& c) { of(s, c)->KickSpread(F(a[0])); return Value{}; },
                "Push the ticks apart. They ease back while spread recover is above 0."));
            ApiMethod spread = method("Spread", {}, {}, tFloat,
                [of](const Value& s, std::vector<Value>&, CallContext& c) { return Value{of(s, c)->Spread()}; },
                "The extra gap around the ticks, in pixels.");
            spread.pure = true;
            ch.methods.push_back(std::move(spread));
            api.types.push_back(std::move(ch));
        }
        {
            ApiType rp; rp.name = "UIRadialProgress"; rp.isComponent = true;
            rp.doc = "A ring or pie. Thickness 0 is a pie. Spin turns the arc into a loader.";
            rp.find = [](GameObject* go) -> Component* { return go ? go->GetComponent<UIRadialProgress>() : nullptr; };
            auto of = [need](const Value& self, CallContext& c) -> UIRadialProgress* {
                GameObject* g = Resolve(std::get<ObjRef>(self), c.loc);
                return need(g->GetComponent<UIRadialProgress>(), g, c, "Radial Progress");
            };
            auto flag = [](const Value& v) {
                if (auto* b = std::get_if<bool>(&v)) return *b;
                return F(v) != 0.0f;
            };
            rp.methods.push_back(method("SetValue", {tFloat}, {"value"}, tVoid,
                [of](const Value& s, std::vector<Value>& a, CallContext& c) { of(s, c)->SetValue(F(a[0])); return Value{}; },
                "Fill fraction from 0 to 1."));
            rp.methods.push_back(method("SetThickness", {tFloat}, {"pixels"}, tVoid,
                [of](const Value& s, std::vector<Value>& a, CallContext& c) { of(s, c)->SetThickness(F(a[0])); return Value{}; },
                "Ring width in pixels. 0 draws a pie."));
            rp.methods.push_back(method("SetStartAngle", {tFloat}, {"degrees"}, tVoid,
                [of](const Value& s, std::vector<Value>& a, CallContext& c) { of(s, c)->SetStartAngle(F(a[0])); return Value{}; },
                "Where the arc starts. 0 is 12 o'clock."));
            rp.methods.push_back(method("SetClockwise", {tBool}, {"on"}, tVoid,
                [of, flag](const Value& s, std::vector<Value>& a, CallContext& c) { of(s, c)->SetClockwise(flag(a[0])); return Value{}; },
                "Fill clockwise when on."));
            rp.methods.push_back(method("SetSpin", {tBool}, {"on"}, tVoid,
                [of, flag](const Value& s, std::vector<Value>& a, CallContext& c) { of(s, c)->SetSpin(flag(a[0])); return Value{}; },
                "Spin the arc as a loader."));
            rp.methods.push_back(method("SetShowPercent", {tBool}, {"on"}, tVoid,
                [of, flag](const Value& s, std::vector<Value>& a, CallContext& c) { of(s, c)->SetShowPercent(flag(a[0])); return Value{}; },
                "Draw the fill as a percent in the middle."));
            api.types.push_back(std::move(rp));
        }
        {
            ApiType sg; sg.name = "UISlotGrid"; sg.isComponent = true;
            sg.doc = "A grid of buttons. Selecting one fires on_change. It does not know about inventory.";
            sg.find = [](GameObject* go) -> Component* { return go ? go->GetComponent<UISlotGrid>() : nullptr; };
            auto of = [need](const Value& self, CallContext& c) -> UISlotGrid* {
                GameObject* g = Resolve(std::get<ObjRef>(self), c.loc);
                return need(g->GetComponent<UISlotGrid>(), g, c, "Slot Grid");
            };
            sg.methods.push_back(method("SetValue", {tInt}, {"index"}, tVoid,
                [of](const Value& s, std::vector<Value>& a, CallContext& c) { of(s, c)->SetValue(I(a[0])); return Value{}; },
                "Select a slot by index. -1 clears the selection."));
            sg.methods.push_back(method("SetColumns", {tInt}, {"columns"}, tVoid,
                [of](const Value& s, std::vector<Value>& a, CallContext& c) { of(s, c)->SetColumns(I(a[0])); of(s, c)->Rebuild(); return Value{}; },
                "How many slots across (1 to 16)."));
            sg.methods.push_back(method("SetRows", {tInt}, {"rows"}, tVoid,
                [of](const Value& s, std::vector<Value>& a, CallContext& c) { of(s, c)->SetRows(I(a[0])); of(s, c)->Rebuild(); return Value{}; },
                "How many slots down (1 to 16)."));
            ApiMethod val = method("Value", {}, {}, tInt,
                [of](const Value& s, std::vector<Value>&, CallContext& c) { return Value{(std::int32_t)of(s, c)->Value()}; },
                "The selected slot, or -1.");
            val.pure = true;
            sg.methods.push_back(std::move(val));
            api.types.push_back(std::move(sg));
        }
    }
    // ---- grok/ui-controls: visual-script nodes for the new widget options. ----
    {
        auto need = [](auto* c, const char* what, CallContext& ctx) {
            if (!c) throw RuntimeError(std::string("This object has no ") + what, ctx.loc);
            return c;
        };
        {
            ApiType dd; dd.name = "UIDropdown"; dd.isComponent = true;
            dd.doc = "A dropdown. Search, a row cap and per-option glyphs are optional.";
            dd.find = [](GameObject* go) -> Component* { return go ? go->GetComponent<UIDropdown>() : nullptr; };
            auto of = [need](const Value& self, CallContext& c) {
                return need(Resolve(std::get<ObjRef>(self), c.loc)->GetComponent<UIDropdown>(), "UI Dropdown", c);
            };
            dd.methods.push_back(method("SetSearchable", {tBool}, {"on"}, tVoid,
                [of](const Value& s, std::vector<Value>& a, CallContext& c) { of(s, c)->SetSearchable(std::get<bool>(a[0])); return Value{}; },
                "Show a search row that filters the options while the list is open."));
            dd.methods.push_back(method("SetMaxVisible", {tInt}, {"rows"}, tVoid,
                [of](const Value& s, std::vector<Value>& a, CallContext& c) { of(s, c)->SetMaxVisible(I(a[0])); return Value{}; },
                "How many matching options the open list shows. 0 shows every match."));
            dd.methods.push_back(method("SetOptionIcon", {tInt, tString}, {"index", "glyph"}, tVoid,
                [of](const Value& s, std::vector<Value>& a, CallContext& c) { of(s, c)->SetOptionIcon(I(a[0]), S(a[0 + 1])); return Value{}; },
                "A short glyph drawn in front of one option."));
            ApiMethod q = method("Searchable", {}, {}, tBool,
                [of](const Value& s, std::vector<Value>&, CallContext& c) { return Value{of(s, c)->Searchable()}; },
                "True when the open list has a search row.");
            q.pure = true; dd.methods.push_back(std::move(q));
            api.types.push_back(std::move(dd));
        }
        {
            ApiType sv; sv.name = "UIScrollView"; sv.isComponent = true;
            sv.doc = "A scroll view. Inertia, elastic edges and the bar style are optional.";
            sv.find = [](GameObject* go) -> Component* { return go ? go->GetComponent<UIScrollView>() : nullptr; };
            auto of = [need](const Value& self, CallContext& c) {
                return need(Resolve(std::get<ObjRef>(self), c.loc)->GetComponent<UIScrollView>(), "UI Scroll View", c);
            };
            sv.methods.push_back(method("SetInertia", {tFloat}, {"rate"}, tVoid,
                [of](const Value& s, std::vector<Value>& a, CallContext& c) { of(s, c)->SetInertia(F(a[0])); return Value{}; },
                "Wheel glide. 0 scrolls one notch immediately. Higher stops the glide sooner."));
            sv.methods.push_back(method("SetElastic", {tBool}, {"on"}, tVoid,
                [of](const Value& s, std::vector<Value>& a, CallContext& c) { of(s, c)->SetElastic(std::get<bool>(a[0])); return Value{}; },
                "Let the content pass the ends, then spring back."));
            sv.methods.push_back(method("SetBarStyle", {tInt}, {"style"}, tVoid,
                [of](const Value& s, std::vector<Value>& a, CallContext& c) { of(s, c)->SetBarStyle(I(a[0])); return Value{}; },
                "Scrollbar: 0 the usual bar, 1 thin, 2 hidden until the view is scrolling."));
            api.types.push_back(std::move(sv));
        }
        {
            ApiType tb; tb.name = "UITabs"; tb.isComponent = true;
            tb.doc = "A tab bar. Closable tabs fire on_tab_close. Vertical is a style.";
            tb.find = [](GameObject* go) -> Component* { return go ? go->GetComponent<UITabs>() : nullptr; };
            auto of = [need](const Value& self, CallContext& c) {
                return need(Resolve(std::get<ObjRef>(self), c.loc)->GetComponent<UITabs>(), "UI Tabs", c);
            };
            tb.methods.push_back(method("SetClosable", {tBool}, {"on"}, tVoid,
                [of](const Value& s, std::vector<Value>& a, CallContext& c) { of(s, c)->SetClosable(std::get<bool>(a[0])); return Value{}; },
                "Draw an X on each tab. Clicking it removes that tab."));
            tb.methods.push_back(method("CloseTab", {tInt}, {"index"}, tVoid,
                [of](const Value& s, std::vector<Value>& a, CallContext& c) { of(s, c)->CloseTab(I(a[0])); return Value{}; },
                "Remove one tab and fire on_tab_close."));
            ApiMethod n = method("Count", {}, {}, tInt,
                [of](const Value& s, std::vector<Value>&, CallContext& c) { return Value{(std::int32_t)of(s, c)->Count()}; },
                "How many tabs are left.");
            n.pure = true; tb.methods.push_back(std::move(n));
            api.types.push_back(std::move(tb));
        }
        {
            ApiType tt; tt.name = "UITooltip"; tt.isComponent = true;
            tt.doc = "A hover tooltip. It already waits and follows the cursor. Rich text and the offset are optional.";
            tt.find = [](GameObject* go) -> Component* { return go ? go->GetComponent<UITooltip>() : nullptr; };
            auto of = [need](const Value& self, CallContext& c) {
                return need(Resolve(std::get<ObjRef>(self), c.loc)->GetComponent<UITooltip>(), "UI Tooltip", c);
            };
            tt.methods.push_back(method("SetRichText", {tBool}, {"on"}, tVoid,
                [of](const Value& s, std::vector<Value>& a, CallContext& c) { of(s, c)->SetRichText(std::get<bool>(a[0])); return Value{}; },
                "Draw <b> and #rrggbb in the tooltip text."));
            tt.methods.push_back(method("SetOffset", {tFloat, tFloat}, {"x", "y"}, tVoid,
                [of](const Value& s, std::vector<Value>& a, CallContext& c) { of(s, c)->SetOffset(F(a[0]), F(a[1])); return Value{}; },
                "Pixels from the cursor to the box. 14, 14 is the usual spot."));
            tt.methods.push_back(method("SetDelay", {tFloat}, {"seconds"}, tVoid,
                [of](const Value& s, std::vector<Value>& a, CallContext& c) { of(s, c)->SetDelay(F(a[0])); return Value{}; },
                "How long the pointer must hover before the tooltip shows."));
            api.types.push_back(std::move(tt));
        }
        {
            ApiType inf; inf.name = "UIInputField"; inf.isComponent = true;
            inf.doc = "A text field. Password, a character limit and integer or decimal mode already exist. Multiline is optional.";
            inf.find = [](GameObject* go) -> Component* { return go ? go->GetComponent<UIInputField>() : nullptr; };
            auto of = [need](const Value& self, CallContext& c) {
                return need(Resolve(std::get<ObjRef>(self), c.loc)->GetComponent<UIInputField>(), "UI Input Field", c);
            };
            inf.methods.push_back(method("SetMultiline", {tBool}, {"on"}, tVoid,
                [of](const Value& s, std::vector<Value>& a, CallContext& c) { of(s, c)->SetMultiline(std::get<bool>(a[0])); return Value{}; },
                "Enter inserts a newline instead of submitting."));
            inf.methods.push_back(method("SetMaxLength", {tInt}, {"length"}, tVoid,
                [of](const Value& s, std::vector<Value>& a, CallContext& c) { of(s, c)->SetMaxLength(I(a[0])); return Value{}; },
                "The most characters the field accepts."));
            inf.methods.push_back(method("SetContentType", {tInt}, {"type"}, tVoid,
                [of](const Value& s, std::vector<Value>& a, CallContext& c) { of(s, c)->SetContentType(I(a[0])); return Value{}; },
                "0 standard, 1 integer, 2 decimal, 3 password."));
            api.types.push_back(std::move(inf));
        }
    }
    // ---- UIToast / UIModal (grok/ui-toast). Visual-script nodes come from these types. ----
    {
        auto asBool = [](const Value& v) {
            if (auto* b = std::get_if<bool>(&v)) return *b;
            return F(v) != 0.0f;
        };
        ApiType tt; tt.name = "UIToast"; tt.isComponent = true;
        tt.doc = "A stack of toast notifications. The object needs a UI Toast.";
        tt.find = [](GameObject* go) -> Component* { return go ? go->GetComponent<UIToast>() : nullptr; };
        auto toastOf = [](const Value& self, CallContext& c) -> UIToast* {
            GameObject* g = Resolve(std::get<ObjRef>(self), c.loc);
            auto* t = g->GetComponent<UIToast>();
            if (!t) throw RuntimeError("'" + g->name + "' has no UI Toast (add one in the Inspector)", c.loc);
            return t;
        };
        tt.methods.push_back(method("Push", {tString, tString, tInt}, {"title", "body", "kind"}, tVoid,
            [toastOf](const Value& s, std::vector<Value>& a, CallContext& c) {
                toastOf(s, c)->Push(S(a[0]), S(a[1]), I(a[2]));
                return Value{};
            }, "Show a toast. kind: 0 info, 1 success, 2 warning, 3 error."));
        tt.methods.push_back(method("SetDuration", {tFloat}, {"seconds"}, tVoid,
            [toastOf](const Value& s, std::vector<Value>& a, CallContext& c) {
                toastOf(s, c)->SetDuration(F(a[0]));
                return Value{};
            }, "How long a new toast stays up, in seconds."));
        tt.methods.push_back(method("SetCorner", {tInt}, {"corner"}, tVoid,
            [toastOf](const Value& s, std::vector<Value>& a, CallContext& c) {
                toastOf(s, c)->SetCorner(I(a[0]));
                return Value{};
            }, "Where the stack sits: 0 top-right, 1 top-left, 2 bottom-right, 3 bottom-left, 4 top-center."));
        tt.methods.push_back(method("SetMaxStack", {tInt}, {"count"}, tVoid,
            [toastOf](const Value& s, std::vector<Value>& a, CallContext& c) {
                toastOf(s, c)->SetMaxStack(I(a[0]));
                return Value{};
            }, "How many toasts stay on screen. The oldest one drops off past this."));
        ApiMethod count = method("Count", {}, {}, tInt,
            [toastOf](const Value& s, std::vector<Value>&, CallContext& c) {
                return Value{(std::int32_t)toastOf(s, c)->Count()};
            }, "How many toasts are on screen right now.");
        count.pure = true;
        tt.methods.push_back(std::move(count));
        api.types.push_back(std::move(tt));

        ApiType md; md.name = "UIModal"; md.isComponent = true;
        md.doc = "A modal dialog. Style Card restyles the Card, Title, Message, Confirm and Cancel inside it.";
        md.find = [](GameObject* go) -> Component* { return go ? go->GetComponent<UIModal>() : nullptr; };
        auto modalOf = [](const Value& self, CallContext& c) -> UIModal* {
            GameObject* g = Resolve(std::get<ObjRef>(self), c.loc);
            auto* m = g->GetComponent<UIModal>();
            if (!m) throw RuntimeError("'" + g->name + "' has no UI Modal (add one in the Inspector)", c.loc);
            return m;
        };
        md.methods.push_back(method("Open", {}, {}, tVoid,
            [modalOf](const Value& s, std::vector<Value>&, CallContext& c) { modalOf(s, c)->Open(); return Value{}; },
            "Show the dialog."));
        md.methods.push_back(method("Close", {}, {}, tVoid,
            [modalOf](const Value& s, std::vector<Value>&, CallContext& c) { modalOf(s, c)->Close(); return Value{}; },
            "Hide the dialog. Fires on_modal_close."));
        md.methods.push_back(method("SetStyleCard", {tBool}, {"on"}, tVoid,
            [modalOf, asBool](const Value& s, std::vector<Value>& a, CallContext& c) {
                modalOf(s, c)->SetStyleCard(asBool(a[0]));
                return Value{};
            }, "Turn the card styling on or off. Off leaves the dialog as it was authored."));
        md.methods.push_back(method("SetCardRadius", {tFloat}, {"radius"}, tVoid,
            [modalOf](const Value& s, std::vector<Value>& a, CallContext& c) {
                modalOf(s, c)->SetCardRadius(F(a[0]));
                return Value{};
            }, "Corner radius of the card named Card, in pixels."));
        md.methods.push_back(method("SetTitle", {tString}, {"text"}, tVoid,
            [modalOf](const Value& s, std::vector<Value>& a, CallContext& c) {
                modalOf(s, c)->SetTitle(S(a[0]));
                return Value{};
            }, "Title text. Empty leaves whatever the Title label already says."));
        md.methods.push_back(method("SetMessage", {tString}, {"text"}, tVoid,
            [modalOf](const Value& s, std::vector<Value>& a, CallContext& c) {
                modalOf(s, c)->SetMessage(S(a[0]));
                return Value{};
            }, "Body text. Empty leaves whatever the Message label already says."));
        md.methods.push_back(method("SetConfirmText", {tString}, {"text"}, tVoid,
            [modalOf](const Value& s, std::vector<Value>& a, CallContext& c) {
                modalOf(s, c)->SetConfirmText(S(a[0]));
                return Value{};
            }, "Confirm button label. Empty leaves the button's own label."));
        md.methods.push_back(method("SetCancelText", {tString}, {"text"}, tVoid,
            [modalOf](const Value& s, std::vector<Value>& a, CallContext& c) {
                modalOf(s, c)->SetCancelText(S(a[0]));
                return Value{};
            }, "Cancel button label. Empty leaves the button's own label."));
        ApiMethod open = method("IsOpen", {}, {}, tBool,
            [modalOf](const Value& s, std::vector<Value>&, CallContext& c) {
                return Value{modalOf(s, c)->IsOpen()};
            }, "True while the dialog is shown.");
        open.pure = true;
        md.methods.push_back(std::move(open));
        api.types.push_back(std::move(md));
    }
    // ---- UITheme (grok/ui-theme). Visual-script nodes come from this type. ----
    {
        ApiType th; th.name = "UITheme"; th.isComponent = true;
        th.doc = "A saved skin for panels, buttons, sliders and the rest of the UI kit. The object needs a UI Theme.";
        th.find = [](GameObject* go) -> Component* { return go ? go->GetComponent<UITheme>() : nullptr; };
        auto themeOf = [](const Value& self, CallContext& c) -> UITheme* {
            GameObject* g = Resolve(std::get<ObjRef>(self), c.loc);
            auto* t = g->GetComponent<UITheme>();
            if (!t) throw RuntimeError("'" + g->name + "' has no UI Theme (add one in the Inspector)", c.loc);
            return t;
        };
        th.methods.push_back(method("SetPreset", {tInt}, {"preset"}, tVoid,
            [themeOf](const Value& s, std::vector<Value>& a, CallContext& c) {
                UITheme* t = themeOf(s, c);
                t->ApplyPreset(I(a[0]));
                t->Apply();
                return Value{};
            }, "Switch to a built-in look and bake it: 0 custom, 1 dark, 2 light, 3 sci-fi, 4 fantasy."));
        th.methods.push_back(method("Apply", {}, {}, tVoid,
            [themeOf](const Value& s, std::vector<Value>&, CallContext& c) {
                themeOf(s, c)->Apply();
                return Value{};
            }, "Bake this skin onto this object and its children. Widgets keep their positions."));
        th.methods.push_back(method("ApplyTo", {tString}, {"objectName"}, tVoid,
            [themeOf](const Value& s, std::vector<Value>& a, CallContext& c) {
                UITheme* t = themeOf(s, c);
                GameObject* root = t->gameObject;
                if (t->gameObject && t->gameObject->scene())
                    if (GameObject* named = t->gameObject->scene()->Find(S(a[0]))) root = named;
                t->ApplyTo(root);
                return Value{};
            }, "Bake this skin onto the named object and its children."));
        api.types.push_back(std::move(th));
    }
    // ---- UI widgets (grok/ui-options). Visual-script nodes come from these types.
    // Value changes still raise on_change / OnValueChanged; clicks raise on_click / OnClick.
    {
        ApiType sl; sl.name = "UISlider"; sl.isComponent = true;
        sl.doc = "A draggable value. The object needs a UI Slider. on_change fires when the value moves.";
        sl.find = [](GameObject* go) -> Component* { return go ? go->GetComponent<UISlider>() : nullptr; };
        auto of = [](const Value& self, CallContext& c) -> UISlider* {
            GameObject* g = Resolve(std::get<ObjRef>(self), c.loc);
            auto* w = g->GetComponent<UISlider>();
            if (!w) throw RuntimeError("'" + g->name + "' has no UI Slider", c.loc);
            return w;
        };
        sl.methods.push_back(method("SetValue", {tFloat}, {"value"}, tVoid,
            [of](const Value& s, std::vector<Value>& a, CallContext& c) { of(s, c)->SetValue(F(a[0])); return Value{}; },
            "Set the value (snaps to the step). Fires OnValueChanged when it changes."));
        sl.methods.push_back(method("SetStep", {tFloat}, {"step"}, tVoid,
            [of](const Value& s, std::vector<Value>& a, CallContext& c) { of(s, c)->SetStep(F(a[0])); return Value{}; },
            "Snap spacing. 0 keeps the slider continuous."));
        sl.methods.push_back(method("SetFormat", {tString}, {"format"}, tVoid,
            [of](const Value& s, std::vector<Value>& a, CallContext& c) { of(s, c)->SetFormat(S(a[0])); return Value{}; },
            "Value label. \"{v}\" is the number. Empty keeps two decimals."));
        ApiMethod gv = method("GetValue", {}, {}, tFloat,
            [of](const Value& s, std::vector<Value>&, CallContext& c) { return Value{of(s, c)->value}; },
            "The current value.");
        gv.pure = true;
        sl.methods.push_back(std::move(gv));
        api.types.push_back(std::move(sl));
    }
    {
        ApiType pb; pb.name = "UIProgressBar"; pb.isComponent = true;
        pb.doc = "A fill bar (health, loading). The object needs a UI Progress Bar.";
        pb.find = [](GameObject* go) -> Component* { return go ? go->GetComponent<UIProgressBar>() : nullptr; };
        auto of = [](const Value& self, CallContext& c) -> UIProgressBar* {
            GameObject* g = Resolve(std::get<ObjRef>(self), c.loc);
            auto* w = g->GetComponent<UIProgressBar>();
            if (!w) throw RuntimeError("'" + g->name + "' has no UI Progress Bar", c.loc);
            return w;
        };
        pb.methods.push_back(method("SetValue", {tFloat}, {"value"}, tVoid,
            [of](const Value& s, std::vector<Value>& a, CallContext& c) { of(s, c)->SetValue(F(a[0])); return Value{}; },
            "Fill amount, 0 to 1. A damage trail eases down when Trail is set."));
        pb.methods.push_back(method("SetSegments", {tInt}, {"count"}, tVoid,
            [of](const Value& s, std::vector<Value>& a, CallContext& c) { of(s, c)->SetSegments(I(a[0])); return Value{}; },
            "Chunk count. 0 or 1 is one solid bar."));
        pb.methods.push_back(method("SetTrail", {tFloat}, {"seconds"}, tVoid,
            [of](const Value& s, std::vector<Value>& a, CallContext& c) { of(s, c)->SetTrail(F(a[0])); return Value{}; },
            "Seconds for the damage trail to catch a drop. 0 hides it."));
        pb.methods.push_back(method("SetTextFormat", {tString}, {"format"}, tVoid,
            [of](const Value& s, std::vector<Value>& a, CallContext& c) { of(s, c)->SetTextFormat(S(a[0])); return Value{}; },
            "Label on the bar. \"{n}\" is the percent, \"{v}\" is 0..1."));
        api.types.push_back(std::move(pb));
    }
    {
        ApiType bt; bt.name = "UIButton"; bt.isComponent = true;
        bt.doc = "A clickable button. The object needs a UI Button. on_click fires when it is pressed.";
        bt.find = [](GameObject* go) -> Component* { return go ? go->GetComponent<UIButton>() : nullptr; };
        auto of = [](const Value& self, CallContext& c) -> UIButton* {
            GameObject* g = Resolve(std::get<ObjRef>(self), c.loc);
            auto* w = g->GetComponent<UIButton>();
            if (!w) throw RuntimeError("'" + g->name + "' has no UI Button", c.loc);
            return w;
        };
        bt.methods.push_back(method("SetEase", {tInt, tFloat}, {"ease", "seconds"}, tVoid,
            [of](const Value& s, std::vector<Value>& a, CallContext& c) {
                of(s, c)->SetEase(I(a[0]), F(a[1])); return Value{};
            }, "Hover/press ease: 0 linear, 1 smooth, 2 ease-out, 3 ease-in, and how many seconds it takes."));
        bt.methods.push_back(method("SetInteractable", {tBool}, {"on"}, tVoid,
            [of](const Value& s, std::vector<Value>& a, CallContext& c) {
                of(s, c)->SetInteractable(std::get<bool>(a[0])); return Value{};
            }, "False greys the button out and ignores clicks."));
        bt.methods.push_back(method("Press", {}, {}, tVoid,
            [of](const Value& s, std::vector<Value>&, CallContext& c) { of(s, c)->Press(); return Value{}; },
            "Click the button. Fires OnClick."));
        bt.methods.push_back(method("Focus", {}, {}, tVoid,
            [of](const Value& s, std::vector<Value>&, CallContext& c) {
                if (!SetUIFocus(of(s, c)->gameObject))
                    throw RuntimeError("that button can't take focus (hidden, disabled, or not focusable)", c.loc);
                return Value{};
            },
            "Move keyboard and gamepad focus here, show the focus ring, and scroll it into view."));
        api.types.push_back(std::move(bt));
    }
}

void Ctx::BindUiApp() {
    // App widgets. Each method is a visual-script node. Value changes raise on_change.
    {
        ApiType w; w.name = "UISearchBar"; w.isComponent = true;
        w.doc = "A search field over a list. on_change fires when the text or the chosen row changes.";
        w.find = [](GameObject* go) -> Component* { return go ? go->GetComponent<UISearchBar>() : nullptr; };
        auto of = [](const Value& self, CallContext& c) -> UISearchBar* {
            GameObject* g = Resolve(std::get<ObjRef>(self), c.loc);
            auto* x = g->GetComponent<UISearchBar>();
            if (!x) throw RuntimeError("'" + g->name + "' has no UI Search Bar", c.loc);
            return x;
        };
        w.methods.push_back(method("SetQuery", {tString}, {"text"}, tVoid,
            [of](const Value& s, std::vector<Value>& a, CallContext& c) { of(s, c)->SetQuery(S(a[0])); return Value{}; },
            "Show only the items that contain this text. Fires OnValueChanged when it changes."));
        w.methods.push_back(method("Clear", {}, {}, tVoid,
            [of](const Value& s, std::vector<Value>&, CallContext& c) { of(s, c)->Clear(); return Value{}; },
            "Empty the search and clear the chosen row."));
        ApiMethod n = method("MatchCount", {}, {}, tInt,
            [of](const Value& s, std::vector<Value>&, CallContext& c) { return Value{(std::int32_t)of(s, c)->MatchCount()}; },
            "How many items match the search.");
        n.pure = true; w.methods.push_back(std::move(n));
        api.types.push_back(std::move(w));
    }
    {
        ApiType w; w.name = "UIDateTimePicker"; w.isComponent = true;
        w.doc = "A date and time. Invalid days are pulled back to a real calendar day. on_change fires when it changes.";
        w.find = [](GameObject* go) -> Component* { return go ? go->GetComponent<UIDateTimePicker>() : nullptr; };
        auto of = [](const Value& self, CallContext& c) -> UIDateTimePicker* {
            GameObject* g = Resolve(std::get<ObjRef>(self), c.loc);
            auto* x = g->GetComponent<UIDateTimePicker>();
            if (!x) throw RuntimeError("'" + g->name + "' has no UI Date and Time", c.loc);
            return x;
        };
        w.methods.push_back(method("SetDate", {tInt, tInt, tInt}, {"year", "month", "day"}, tVoid,
            [of](const Value& s, std::vector<Value>& a, CallContext& c) {
                of(s, c)->SetDate(I(a[0]), I(a[1]), I(a[2])); return Value{};
            }, "Set the date. 31 February becomes the last day of February."));
        w.methods.push_back(method("SetTime", {tInt, tInt}, {"hour", "minute"}, tVoid,
            [of](const Value& s, std::vector<Value>& a, CallContext& c) {
                of(s, c)->SetTime(I(a[0]), I(a[1])); return Value{};
            }, "Set the clock. Hours stay in 0 to 23 and minutes in 0 to 59."));
        ApiMethod n = method("Text", {}, {}, tString,
            [of](const Value& s, std::vector<Value>&, CallContext& c) { return Value{of(s, c)->Text()}; },
            "The date and time as text, such as 2026-02-28 14:05.");
        n.pure = true; w.methods.push_back(std::move(n));
        api.types.push_back(std::move(w));
    }
    {
        ApiType w; w.name = "UIFilePicker"; w.isComponent = true;
        w.doc = "A file list that can only open inside one folder. on_submit fires when a file is chosen.";
        w.find = [](GameObject* go) -> Component* { return go ? go->GetComponent<UIFilePicker>() : nullptr; };
        auto of = [](const Value& self, CallContext& c) -> UIFilePicker* {
            GameObject* g = Resolve(std::get<ObjRef>(self), c.loc);
            auto* x = g->GetComponent<UIFilePicker>();
            if (!x) throw RuntimeError("'" + g->name + "' has no UI File Picker", c.loc);
            return x;
        };
        w.methods.push_back(method("SetRoot", {tString}, {"folder"}, tVoid,
            [of](const Value& s, std::vector<Value>& a, CallContext& c) { of(s, c)->SetRoot(S(a[0])); return Value{}; },
            "The only folder this picker may open."));
        w.methods.push_back(method("Open", {tString}, {"name"}, tVoid,
            [of](const Value& s, std::vector<Value>& a, CallContext& c) { of(s, c)->Open(S(a[0])); return Value{}; },
            "Enter a folder or choose a file in the folder on screen. Paths that leave the folder do nothing."));
        ApiMethod n = method("Selected", {}, {}, tString,
            [of](const Value& s, std::vector<Value>&, CallContext& c) { return Value{of(s, c)->selected}; },
            "The chosen file, relative to the folder. Empty when none is chosen.");
        n.pure = true; w.methods.push_back(std::move(n));
        api.types.push_back(std::move(w));
    }
    {
        ApiType w; w.name = "UIAccordion"; w.isComponent = true;
        w.doc = "Stacked headings that open and close. on_change fires when one opens or closes.";
        w.find = [](GameObject* go) -> Component* { return go ? go->GetComponent<UIAccordion>() : nullptr; };
        auto of = [](const Value& self, CallContext& c) -> UIAccordion* {
            GameObject* g = Resolve(std::get<ObjRef>(self), c.loc);
            auto* x = g->GetComponent<UIAccordion>();
            if (!x) throw RuntimeError("'" + g->name + "' has no UI Accordion", c.loc);
            return x;
        };
        w.methods.push_back(method("Toggle", {tInt}, {"index"}, tVoid,
            [of](const Value& s, std::vector<Value>& a, CallContext& c) { of(s, c)->Toggle(I(a[0])); return Value{}; },
            "Open or close one section. The first section is 0."));
        w.methods.push_back(method("SetExpanded", {tInt, tBool}, {"index", "open"}, tVoid,
            [of](const Value& s, std::vector<Value>& a, CallContext& c) {
                of(s, c)->SetExpanded(I(a[0]), std::get<bool>(a[1])); return Value{};
            }, "Open or close one section."));
        ApiMethod n = method("IsExpanded", {tInt}, {"index"}, tBool,
            [of](const Value& s, std::vector<Value>& a, CallContext& c) { return Value{of(s, c)->IsExpanded(I(a[0]))}; },
            "True when that section is open.");
        n.pure = true; w.methods.push_back(std::move(n));
        api.types.push_back(std::move(w));
    }
    {
        ApiType w; w.name = "UIBreadcrumbs"; w.isComponent = true;
        w.doc = "A path such as Home / Library / Books. Choosing a crumb drops the ones after it.";
        w.find = [](GameObject* go) -> Component* { return go ? go->GetComponent<UIBreadcrumbs>() : nullptr; };
        auto of = [](const Value& self, CallContext& c) -> UIBreadcrumbs* {
            GameObject* g = Resolve(std::get<ObjRef>(self), c.loc);
            auto* x = g->GetComponent<UIBreadcrumbs>();
            if (!x) throw RuntimeError("'" + g->name + "' has no UI Breadcrumbs", c.loc);
            return x;
        };
        w.methods.push_back(method("SetPath", {tString}, {"path"}, tVoid,
            [of](const Value& s, std::vector<Value>& a, CallContext& c) { of(s, c)->SetPath(S(a[0])); return Value{}; },
            "Replace the path. Folders are separated by /."));
        w.methods.push_back(method("Select", {tInt}, {"index"}, tVoid,
            [of](const Value& s, std::vector<Value>& a, CallContext& c) { of(s, c)->Select(I(a[0])); return Value{}; },
            "Keep this crumb and drop the ones after it. The first crumb is 0."));
        ApiMethod n = method("Path", {}, {}, tString,
            [of](const Value& s, std::vector<Value>&, CallContext& c) { return Value{of(s, c)->Path()}; },
            "The path, folders separated by /.");
        n.pure = true; w.methods.push_back(std::move(n));
        api.types.push_back(std::move(w));
    }
    {
        ApiType w; w.name = "UIPagination"; w.isComponent = true;
        w.doc = "Page buttons. The page stays inside 1 to the page count. on_change fires when the page changes.";
        w.find = [](GameObject* go) -> Component* { return go ? go->GetComponent<UIPagination>() : nullptr; };
        auto of = [](const Value& self, CallContext& c) -> UIPagination* {
            GameObject* g = Resolve(std::get<ObjRef>(self), c.loc);
            auto* x = g->GetComponent<UIPagination>();
            if (!x) throw RuntimeError("'" + g->name + "' has no UI Pagination", c.loc);
            return x;
        };
        w.methods.push_back(method("SetPage", {tInt}, {"page"}, tVoid,
            [of](const Value& s, std::vector<Value>& a, CallContext& c) { of(s, c)->SetPage(I(a[0])); return Value{}; },
            "Show this page. Numbers outside 1 to the page count are pulled back in."));
        w.methods.push_back(method("Next", {}, {}, tVoid,
            [of](const Value& s, std::vector<Value>&, CallContext& c) { of(s, c)->Next(); return Value{}; },
            "Go to the next page. The last page stays put."));
        w.methods.push_back(method("Previous", {}, {}, tVoid,
            [of](const Value& s, std::vector<Value>&, CallContext& c) { of(s, c)->Previous(); return Value{}; },
            "Go to the previous page. The first page stays put."));
        w.methods.push_back(method("SetTotal", {tInt, tInt}, {"items", "perPage"}, tVoid,
            [of](const Value& s, std::vector<Value>& a, CallContext& c) { of(s, c)->SetTotal(I(a[0]), I(a[1])); return Value{}; },
            "Set how many items there are and how many fit on a page, then fix the page count."));
        ApiMethod n = method("Page", {}, {}, tInt,
            [of](const Value& s, std::vector<Value>&, CallContext& c) { return Value{(std::int32_t)of(s, c)->Page()}; },
            "The page on screen, starting at 1.");
        n.pure = true; w.methods.push_back(std::move(n));
        api.types.push_back(std::move(w));
    }
    {
        ApiType w; w.name = "UISkeleton"; w.isComponent = true;
        w.doc = "Grey bars shown while something is still loading.";
        w.find = [](GameObject* go) -> Component* { return go ? go->GetComponent<UISkeleton>() : nullptr; };
        auto of = [](const Value& self, CallContext& c) -> UISkeleton* {
            GameObject* g = Resolve(std::get<ObjRef>(self), c.loc);
            auto* x = g->GetComponent<UISkeleton>();
            if (!x) throw RuntimeError("'" + g->name + "' has no UI Skeleton", c.loc);
            return x;
        };
        w.methods.push_back(method("SetLoading", {tBool}, {"on"}, tVoid,
            [of](const Value& s, std::vector<Value>& a, CallContext& c) {
                of(s, c)->SetLoading(std::get<bool>(a[0])); return Value{};
            }, "True shows the loading bars. False clears them."));
        ApiMethod n = method("IsLoading", {}, {}, tBool,
            [of](const Value& s, std::vector<Value>&, CallContext& c) { return Value{of(s, c)->loading}; },
            "True while the loading bars are showing.");
        n.pure = true; w.methods.push_back(std::move(n));
        api.types.push_back(std::move(w));
    }
    {
        ApiType w; w.name = "UIRangeSlider"; w.isComponent = true;
        w.doc = "A track with two handles that pick a low and a high value. on_change fires when either moves.";
        w.find = [](GameObject* go) -> Component* { return go ? go->GetComponent<UIRangeSlider>() : nullptr; };
        auto of = [](const Value& self, CallContext& c) -> UIRangeSlider* {
            GameObject* g = Resolve(std::get<ObjRef>(self), c.loc);
            auto* x = g->GetComponent<UIRangeSlider>();
            if (!x) throw RuntimeError("'" + g->name + "' has no UI Range Slider", c.loc);
            return x;
        };
        w.methods.push_back(method("SetRange", {tFloat, tFloat}, {"low", "high"}, tVoid,
            [of](const Value& s, std::vector<Value>& a, CallContext& c) { of(s, c)->SetRange(F(a[0]), F(a[1])); return Value{}; },
            "Move both handles. The values are snapped to the step and held inside the track."));
        w.methods.push_back(method("SetLow", {tFloat}, {"value"}, tVoid,
            [of](const Value& s, std::vector<Value>& a, CallContext& c) { of(s, c)->SetLow(F(a[0])); return Value{}; },
            "Move the left handle. It stops at the right handle."));
        w.methods.push_back(method("SetHigh", {tFloat}, {"value"}, tVoid,
            [of](const Value& s, std::vector<Value>& a, CallContext& c) { of(s, c)->SetHigh(F(a[0])); return Value{}; },
            "Move the right handle. It stops at the left handle."));
        ApiMethod lo = method("Low", {}, {}, tFloat,
            [of](const Value& s, std::vector<Value>&, CallContext& c) { return Value{of(s, c)->low}; },
            "The value of the left handle.");
        lo.pure = true; w.methods.push_back(std::move(lo));
        ApiMethod hi = method("High", {}, {}, tFloat,
            [of](const Value& s, std::vector<Value>&, CallContext& c) { return Value{of(s, c)->high}; },
            "The value of the right handle.");
        hi.pure = true; w.methods.push_back(std::move(hi));
        api.types.push_back(std::move(w));
    }
    {
        ApiType w; w.name = "UIInputValidator"; w.isComponent = true;
        w.doc = "Rules for the UI Input Field on the same object. on_valid and on_invalid fire when the answer changes.";
        w.find = [](GameObject* go) -> Component* { return go ? go->GetComponent<UIInputValidator>() : nullptr; };
        auto of = [](const Value& self, CallContext& c) -> UIInputValidator* {
            GameObject* g = Resolve(std::get<ObjRef>(self), c.loc);
            auto* x = g->GetComponent<UIInputValidator>();
            if (!x) throw RuntimeError("'" + g->name + "' has no UI Input Validator", c.loc);
            return x;
        };
        w.methods.push_back(method("Check", {}, {}, tBool,
            [of](const Value& s, std::vector<Value>&, CallContext& c) { return Value{of(s, c)->Check()}; },
            "Check the field now and answer true when the text is acceptable. Use it before you accept a form."));
        ApiMethod v = method("IsValid", {}, {}, tBool,
            [of](const Value& s, std::vector<Value>&, CallContext& c) { return Value{of(s, c)->IsValid()}; },
            "True when the text passed at the last check.");
        v.pure = true; w.methods.push_back(std::move(v));
        ApiMethod e = method("Error", {}, {}, tString,
            [of](const Value& s, std::vector<Value>&, CallContext& c) { return Value{of(s, c)->Error()}; },
            "The sentence that says what is wrong, or an empty string.");
        e.pure = true; w.methods.push_back(std::move(e));
        api.types.push_back(std::move(w));
    }
    {
        ApiType w; w.name = "UIBottomSheet"; w.isComponent = true;
        w.doc = "A panel that slides up from the bottom. on_change fires when it opens or closes.";
        w.find = [](GameObject* go) -> Component* { return go ? go->GetComponent<UIBottomSheet>() : nullptr; };
        auto of = [](const Value& self, CallContext& c) -> UIBottomSheet* {
            GameObject* g = Resolve(std::get<ObjRef>(self), c.loc);
            auto* x = g->GetComponent<UIBottomSheet>();
            if (!x) throw RuntimeError("'" + g->name + "' has no UI Bottom Sheet", c.loc);
            return x;
        };
        w.methods.push_back(method("Open", {}, {}, tVoid,
            [of](const Value& s, std::vector<Value>&, CallContext& c) { of(s, c)->Open(); return Value{}; },
            "Slide the sheet open."));
        w.methods.push_back(method("Close", {}, {}, tVoid,
            [of](const Value& s, std::vector<Value>&, CallContext& c) { of(s, c)->Close(); return Value{}; },
            "Slide the sheet closed."));
        w.methods.push_back(method("Toggle", {}, {}, tVoid,
            [of](const Value& s, std::vector<Value>&, CallContext& c) { of(s, c)->Toggle(); return Value{}; },
            "Open it if it is closed, or close it if it is open."));
        ApiMethod n = method("IsOpen", {}, {}, tBool,
            [of](const Value& s, std::vector<Value>&, CallContext& c) { return Value{of(s, c)->IsOpen()}; },
            "True while the sheet is open.");
        n.pure = true; w.methods.push_back(std::move(n));
        api.types.push_back(std::move(w));
    }
}

void Ctx::BindUiLibrary() {
    // ---- UI library slice 2: game HUD parts and data glue ----
    {
        ApiType w; w.name = "UIRadialMenu"; w.isComponent = true;
        w.doc = "A wheel of choices. on_select fires when one is chosen; Chosen() is its index (0 is the top, going clockwise).";
        w.find = [](GameObject* go) -> Component* { return go ? go->GetComponent<UIRadialMenu>() : nullptr; };
        auto of = [](const Value& s, CallContext& c) { return UiComp<UIRadialMenu>(s, c, "UI Radial Menu"); };
        w.methods.push_back(method("Open", {}, {}, tVoid,
            [of](const Value& s, std::vector<Value>&, CallContext& c) { of(s, c)->Open(); return Value{}; }, "Show the wheel."));
        w.methods.push_back(method("Close", {}, {}, tVoid,
            [of](const Value& s, std::vector<Value>&, CallContext& c) { of(s, c)->Close(false); return Value{}; }, "Hide the wheel without choosing."));
        w.methods.push_back(method("Choose", {tInt}, {"index"}, tBool,
            [of](const Value& s, std::vector<Value>& a, CallContext& c) { return Value{of(s, c)->Choose(I(a[0]))}; },
            "Choose wedge number `index` as if the player picked it. False when there is no such wedge."));
        ApiMethod ch = method("Chosen", {}, {}, tInt,
            [of](const Value& s, std::vector<Value>&, CallContext& c) { return Value{(std::int32_t)of(s, c)->Chosen()}; },
            "The wedge chosen last (-1 for none yet).");
        ch.pure = true; w.methods.push_back(std::move(ch));
        ApiMethod op = method("IsOpen", {}, {}, tBool,
            [of](const Value& s, std::vector<Value>&, CallContext& c) { return Value{of(s, c)->IsOpen()}; }, "True while the wheel is showing.");
        op.pure = true; w.methods.push_back(std::move(op));
        api.types.push_back(std::move(w));
    }
    {
        ApiType w; w.name = "UIKillFeed"; w.isComponent = true;
        w.doc = "The lines that stack in a corner when something dies. Lines fade after their lifetime.";
        w.find = [](GameObject* go) -> Component* { return go ? go->GetComponent<UIKillFeed>() : nullptr; };
        auto of = [](const Value& s, CallContext& c) { return UiComp<UIKillFeed>(s, c, "UI Kill Feed"); };
        w.methods.push_back(method("Push", {tString, tString, tString}, {"killer", "victim", "weapon"}, tVoid,
            [of](const Value& s, std::vector<Value>& a, CallContext& c) { of(s, c)->Push(S(a[0]), S(a[1]), S(a[2])); return Value{}; },
            "Add a line. `weapon` may be an empty string."));
        w.methods.push_back(method("Clear", {}, {}, tVoid,
            [of](const Value& s, std::vector<Value>&, CallContext& c) { of(s, c)->Clear(); return Value{}; }, "Remove every line."));
        api.types.push_back(std::move(w));
    }
    {
        ApiType w; w.name = "UIAmmoCounter"; w.isComponent = true;
        w.doc = "The ammo number. It reads a Ranged Weapon or variables; SetAmmo types the numbers in by hand.";
        w.find = [](GameObject* go) -> Component* { return go ? go->GetComponent<UIAmmoCounter>() : nullptr; };
        auto of = [](const Value& s, CallContext& c) { return UiComp<UIAmmoCounter>(s, c, "UI Ammo Counter"); };
        w.methods.push_back(method("SetAmmo", {tInt, tInt}, {"current", "reserve"}, tVoid,
            [of](const Value& s, std::vector<Value>& a, CallContext& c) { auto* x = of(s, c); x->current = I(a[0]); x->reserve = I(a[1]); return Value{}; },
            "Show these numbers (used when no weapon or variable supplies them)."));
        ApiMethod t = method("Text", {}, {}, tString,
            [of](const Value& s, std::vector<Value>&, CallContext& c) { return Value{of(s, c)->Text()}; }, "The number as shown, like \"12 / 90\".");
        t.pure = true; w.methods.push_back(std::move(t));
        api.types.push_back(std::move(w));
    }
    {
        ApiType w; w.name = "UIInteractionPrompt"; w.isComponent = true;
        w.doc = "A key cap and a word that shows only when the player is near. on_interact fires when the key is pressed while it shows.";
        w.find = [](GameObject* go) -> Component* { return go ? go->GetComponent<UIInteractionPrompt>() : nullptr; };
        auto of = [](const Value& s, CallContext& c) { return UiComp<UIInteractionPrompt>(s, c, "UI Interaction Prompt"); };
        w.methods.push_back(method("SetAction", {tString}, {"text"}, tVoid,
            [of](const Value& s, std::vector<Value>& a, CallContext& c) { of(s, c)->action = S(a[0]); return Value{}; }, "Change the word next to the key."));
        ApiMethod v = method("IsShowing", {}, {}, tBool,
            [of](const Value& s, std::vector<Value>&, CallContext& c) { return Value{of(s, c)->ShouldShow()}; }, "True when the prompt should be on screen now.");
        v.pure = true; w.methods.push_back(std::move(v));
        api.types.push_back(std::move(w));
    }
    {
        ApiType w; w.name = "UICompass"; w.isComponent = true;
        w.doc = "A heading strip. North is +Z.";
        w.find = [](GameObject* go) -> Component* { return go ? go->GetComponent<UICompass>() : nullptr; };
        auto of = [](const Value& s, CallContext& c) { return UiComp<UICompass>(s, c, "UI Compass"); };
        ApiMethod h = method("Heading", {}, {}, tFloat,
            [of](const Value& s, std::vector<Value>&, CallContext& c) { return Value{of(s, c)->Heading()}; },
            "The heading now, in degrees clockwise from north.");
        h.pure = true; w.methods.push_back(std::move(h));
        api.types.push_back(std::move(w));
    }
    {
        ApiType w; w.name = "UIItemTooltipCard"; w.isComponent = true;
        w.doc = "The card for one item from the item database.";
        w.find = [](GameObject* go) -> Component* { return go ? go->GetComponent<UIItemTooltipCard>() : nullptr; };
        auto of = [](const Value& s, CallContext& c) { return UiComp<UIItemTooltipCard>(s, c, "UI Item Tooltip Card"); };
        w.methods.push_back(method("Show", {tString, tInt}, {"item", "count"}, tVoid,
            [of](const Value& s, std::vector<Value>& a, CallContext& c) { of(s, c)->Show(S(a[0]), I(a[1])); return Value{}; },
            "Show the card for this item id and stack size."));
        w.methods.push_back(method("Hide", {}, {}, tVoid,
            [of](const Value& s, std::vector<Value>&, CallContext& c) { of(s, c)->Hide(); return Value{}; }, "Hide the card."));
        api.types.push_back(std::move(w));
    }
    {
        ApiType w; w.name = "UIAbilityIcon"; w.isComponent = true;
        w.doc = "An ability slot with a cooldown ring. on_ability_use and on_ability_ready fire on the object's script.";
        w.find = [](GameObject* go) -> Component* { return go ? go->GetComponent<UIAbilityIcon>() : nullptr; };
        auto of = [](const Value& s, CallContext& c) { return UiComp<UIAbilityIcon>(s, c, "UI Ability Icon"); };
        w.methods.push_back(method("Use", {}, {}, tBool,
            [of](const Value& s, std::vector<Value>&, CallContext& c) { return Value{of(s, c)->Use()}; },
            "Spend a charge and start the cooldown. False when it is not ready."));
        w.methods.push_back(method("Reset", {}, {}, tVoid,
            [of](const Value& s, std::vector<Value>&, CallContext& c) { of(s, c)->Reset(); return Value{}; }, "Make the ability ready now."));
        ApiMethod r = method("IsReady", {}, {}, tBool,
            [of](const Value& s, std::vector<Value>&, CallContext& c) { return Value{of(s, c)->Ready()}; }, "True when a charge is ready.");
        r.pure = true; w.methods.push_back(std::move(r));
        ApiMethod l = method("SecondsLeft", {}, {}, tFloat,
            [of](const Value& s, std::vector<Value>&, CallContext& c) { return Value{of(s, c)->Remaining()}; }, "Seconds until the next charge.");
        l.pure = true; w.methods.push_back(std::move(l));
        api.types.push_back(std::move(w));
    }
    {
        ApiType w; w.name = "UIHotReload"; w.isComponent = true;
        w.doc = "Watches a UI markup file or .okaytheme and applies it when it changes.";
        w.find = [](GameObject* go) -> Component* { return go ? go->GetComponent<UIHotReload>() : nullptr; };
        auto of = [](const Value& s, CallContext& c) { return UiComp<UIHotReload>(s, c, "UI Hot Reload"); };
        w.methods.push_back(method("ReloadNow", {}, {}, tBool,
            [of](const Value& s, std::vector<Value>&, CallContext& c) { return Value{of(s, c)->ReloadNow()}; },
            "Apply the file now. False (and the reason in the log) when it could not be applied."));
        api.types.push_back(std::move(w));
    }
    // ---- UI library slice 3: app parts ----
    {
        ApiType w; w.name = "UIMenuBar"; w.isComponent = true;
        w.doc = "A bar of menus. on_select fires when an entry is chosen; ChosenPath() is \"File/Open\".";
        w.find = [](GameObject* go) -> Component* { return go ? go->GetComponent<UIMenuBar>() : nullptr; };
        auto of = [](const Value& s, CallContext& c) { return UiComp<UIMenuBar>(s, c, "UI Menu Bar"); };
        w.methods.push_back(method("Open", {tInt}, {"menu"}, tVoid,
            [of](const Value& s, std::vector<Value>& a, CallContext& c) { of(s, c)->Open(I(a[0])); return Value{}; }, "Drop menu number `menu` down (0 is the first)."));
        w.methods.push_back(method("Close", {}, {}, tVoid,
            [of](const Value& s, std::vector<Value>&, CallContext& c) { of(s, c)->Close(); return Value{}; }, "Close the open menu."));
        w.methods.push_back(method("Choose", {tInt, tInt}, {"menu", "entry"}, tBool,
            [of](const Value& s, std::vector<Value>& a, CallContext& c) { return Value{of(s, c)->Choose(I(a[0]), I(a[1]))}; },
            "Choose an entry as if it was clicked. False for a separator, a greyed-out entry or one that does not exist."));
        ApiMethod cp = method("ChosenPath", {}, {}, tString,
            [of](const Value& s, std::vector<Value>&, CallContext& c) { return Value{of(s, c)->ChosenPath()}; }, "The entry chosen last, like \"File/Open\" (empty before the first).");
        cp.pure = true; w.methods.push_back(std::move(cp));
        ApiMethod io = method("IsOpen", {}, {}, tBool,
            [of](const Value& s, std::vector<Value>&, CallContext& c) { return Value{of(s, c)->IsOpen()}; }, "True while a menu is dropped down.");
        io.pure = true; w.methods.push_back(std::move(io));
        api.types.push_back(std::move(w));
    }
    {
        ApiType w; w.name = "UIToolbar"; w.isComponent = true;
        w.doc = "A row of buttons, toggles and one-of-a-group choices. on_select fires when one is pressed.";
        w.find = [](GameObject* go) -> Component* { return go ? go->GetComponent<UIToolbar>() : nullptr; };
        auto of = [](const Value& s, CallContext& c) { return UiComp<UIToolbar>(s, c, "UI Toolbar"); };
        w.methods.push_back(method("Press", {tInt}, {"index"}, tBool,
            [of](const Value& s, std::vector<Value>& a, CallContext& c) { return Value{of(s, c)->Press(I(a[0]))}; }, "Press button number `index` (separators count). False when it cannot be pressed."));
        ApiMethod on = method("IsOn", {tInt}, {"index"}, tBool,
            [of](const Value& s, std::vector<Value>& a, CallContext& c) { return Value{of(s, c)->IsOn(I(a[0]))}; }, "True while the toggle or radio button is on.");
        on.pure = true; w.methods.push_back(std::move(on));
        w.methods.push_back(method("SetOn", {tInt, tBool}, {"index", "on"}, tVoid,
            [of](const Value& s, std::vector<Value>& a, CallContext& c) { of(s, c)->SetOn(I(a[0]), std::get<bool>(a[1])); return Value{}; }, "Turn a toggle or radio button on or off without pressing it."));
        api.types.push_back(std::move(w));
    }
    {
        ApiType w; w.name = "UITreeView"; w.isComponent = true;
        w.doc = "Nested rows that open and close. on_select fires when the selected row changes.";
        w.find = [](GameObject* go) -> Component* { return go ? go->GetComponent<UITreeView>() : nullptr; };
        auto of = [](const Value& s, CallContext& c) { return UiComp<UITreeView>(s, c, "UI Tree View"); };
        w.methods.push_back(method("Select", {tInt}, {"row"}, tBool,
            [of](const Value& s, std::vector<Value>& a, CallContext& c) { return Value{of(s, c)->Select(I(a[0]))}; }, "Select node number `row` (opens the rows above it). False when there is no such node."));
        ApiMethod sel = method("Selected", {}, {}, tInt,
            [of](const Value& s, std::vector<Value>&, CallContext& c) { return Value{(std::int32_t)of(s, c)->selected}; }, "The selected node (-1 for none).");
        sel.pure = true; w.methods.push_back(std::move(sel));
        ApiMethod sp = method("SelectedPath", {}, {}, tString,
            [of](const Value& s, std::vector<Value>&, CallContext& c) { auto* x = of(s, c); return Value{x->selected >= 0 ? x->Path(x->selected) : std::string()}; }, "The selected node's path, like \"World/Level 1\".");
        sp.pure = true; w.methods.push_back(std::move(sp));
        w.methods.push_back(method("Toggle", {tInt}, {"node"}, tVoid,
            [of](const Value& s, std::vector<Value>& a, CallContext& c) { of(s, c)->Toggle(I(a[0])); return Value{}; }, "Open a closed node, or close an open one."));
        w.methods.push_back(method("ExpandAll", {}, {}, tVoid,
            [of](const Value& s, std::vector<Value>&, CallContext& c) { of(s, c)->ExpandAll(); return Value{}; }, "Open every node."));
        w.methods.push_back(method("CollapseAll", {}, {}, tVoid,
            [of](const Value& s, std::vector<Value>&, CallContext& c) { of(s, c)->CollapseAll(); return Value{}; }, "Close every node."));
        w.methods.push_back(method("SetNodes", {tString}, {"nodes"}, tVoid,
            [of](const Value& s, std::vector<Value>& a, CallContext& c) { auto* x = of(s, c); x->nodes = S(a[0]); x->selected = -1; x->expanded.clear(); return Value{}; }, "Replace the tree (World|>Level 1|>>Boss ...)."));
        api.types.push_back(std::move(w));
    }
    {
        ApiType w; w.name = "UIDataTable"; w.isComponent = true;
        w.doc = "Columns and rows with sorting and a selected row. on_select and on_sort fire on the object's script.";
        w.find = [](GameObject* go) -> Component* { return go ? go->GetComponent<UIDataTable>() : nullptr; };
        auto of = [](const Value& s, CallContext& c) { return UiComp<UIDataTable>(s, c, "UI Data Table"); };
        w.methods.push_back(method("SortBy", {tInt, tBool}, {"column", "ascending"}, tVoid,
            [of](const Value& s, std::vector<Value>& a, CallContext& c) { of(s, c)->SortBy(I(a[0]), std::get<bool>(a[1])); return Value{}; }, "Sort by a column (0 is the first; -1 keeps the written order)."));
        w.methods.push_back(method("SetRows", {tString}, {"rows"}, tVoid,
            [of](const Value& s, std::vector<Value>& a, CallContext& c) { of(s, c)->SetRows(S(a[0])); return Value{}; }, "Replace every row (Ava|Mage|42;Bo|Rogue|17)."));
        w.methods.push_back(method("AddRow", {tString}, {"cells"}, tVoid,
            [of](const Value& s, std::vector<Value>& a, CallContext& c) { of(s, c)->AddRow(S(a[0])); return Value{}; }, "Add one row at the end (Ava|Mage|42)."));
        w.methods.push_back(method("Select", {tInt}, {"row"}, tBool,
            [of](const Value& s, std::vector<Value>& a, CallContext& c) { return Value{of(s, c)->Select(I(a[0]))}; }, "Select a row by its place in the written rows. False when there is no such row."));
        ApiMethod sel = method("Selected", {}, {}, tInt,
            [of](const Value& s, std::vector<Value>&, CallContext& c) { return Value{(std::int32_t)of(s, c)->selected}; }, "The selected row's place in the written rows (-1 for none).");
        sel.pure = true; w.methods.push_back(std::move(sel));
        ApiMethod rc = method("RowCount", {}, {}, tInt,
            [of](const Value& s, std::vector<Value>&, CallContext& c) { return Value{(std::int32_t)of(s, c)->RowCount()}; }, "How many rows there are.");
        rc.pure = true; w.methods.push_back(std::move(rc));
        ApiMethod ce = method("Cell", {tInt, tInt}, {"row", "column"}, tString,
            [of](const Value& s, std::vector<Value>& a, CallContext& c) {
                auto* x = of(s, c);
                const int r = I(a[0]);
                return Value{r >= 0 && r < x->RowCount() ? x->Cell(r, I(a[1])) : std::string()};
            }, "The text of a cell, counting rows in the order shown (after sorting).");
        ce.pure = true; w.methods.push_back(std::move(ce));
        api.types.push_back(std::move(w));
    }
    {
        ApiType w; w.name = "UIWizard"; w.isComponent = true;
        w.doc = "A sequence of steps. on_next, on_back, on_invalid and on_finish fire on the object's script.";
        w.find = [](GameObject* go) -> Component* { return go ? go->GetComponent<UIWizard>() : nullptr; };
        auto of = [](const Value& s, CallContext& c) { return UiComp<UIWizard>(s, c, "UI Wizard"); };
        w.methods.push_back(method("Next", {}, {}, tBool,
            [of](const Value& s, std::vector<Value>&, CallContext& c) { return Value{of(s, c)->Next()}; }, "Go to the next step (or finish on the last). False when the step is not valid yet."));
        w.methods.push_back(method("Back", {}, {}, tBool,
            [of](const Value& s, std::vector<Value>&, CallContext& c) { return Value{of(s, c)->Back()}; }, "Go back one step."));
        w.methods.push_back(method("GoTo", {tInt}, {"step"}, tBool,
            [of](const Value& s, std::vector<Value>& a, CallContext& c) { return Value{of(s, c)->GoTo(I(a[0]))}; }, "Jump to a step (earlier ones always; later ones only with Free Navigation)."));
        w.methods.push_back(method("Restart", {}, {}, tVoid,
            [of](const Value& s, std::vector<Value>&, CallContext& c) { of(s, c)->Restart(); return Value{}; }, "Go back to the first step."));
        ApiMethod st = method("Step", {}, {}, tInt,
            [of](const Value& s, std::vector<Value>&, CallContext& c) { return Value{(std::int32_t)of(s, c)->step}; }, "The current step (0 is the first).");
        st.pure = true; w.methods.push_back(std::move(st));
        ApiMethod fi = method("IsFinished", {}, {}, tBool,
            [of](const Value& s, std::vector<Value>&, CallContext& c) { return Value{of(s, c)->Finished()}; }, "True after Next on the last step.");
        fi.pure = true; w.methods.push_back(std::move(fi));
        api.types.push_back(std::move(w));
    }
    {
        ApiType w; w.name = "UIFormGroup"; w.isComponent = true;
        w.doc = "Labelled input fields that check themselves. on_submit and on_invalid fire on the object's script.";
        w.find = [](GameObject* go) -> Component* { return go ? go->GetComponent<UIFormGroup>() : nullptr; };
        auto of = [](const Value& s, CallContext& c) { return UiComp<UIFormGroup>(s, c, "UI Form Group"); };
        w.methods.push_back(method("Submit", {}, {}, tBool,
            [of](const Value& s, std::vector<Value>&, CallContext& c) { return Value{of(s, c)->Submit()}; }, "Check every field. When all are valid, write the answers to the variables and run on_submit; else show the problems."));
        w.methods.push_back(method("Reset", {}, {}, tVoid,
            [of](const Value& s, std::vector<Value>&, CallContext& c) { of(s, c)->Reset(); return Value{}; }, "Empty every field."));
        ApiMethod iv = method("IsValid", {}, {}, tBool,
            [of](const Value& s, std::vector<Value>&, CallContext& c) { return Value{of(s, c)->IsValid()}; }, "True when every field is acceptable now.");
        iv.pure = true; w.methods.push_back(std::move(iv));
        ApiMethod ec = method("ErrorCount", {}, {}, tInt,
            [of](const Value& s, std::vector<Value>&, CallContext& c) { return Value{(std::int32_t)of(s, c)->ErrorCount()}; }, "How many fields are not acceptable now.");
        ec.pure = true; w.methods.push_back(std::move(ec));
        ApiMethod gv = method("Value", {tString}, {"label"}, tString,
            [of](const Value& s, std::vector<Value>& a, CallContext& c) { auto* x = of(s, c); return Value{x->Value(x->IndexOfLabel(S(a[0])))}; }, "What is typed in the field with this label.");
        gv.pure = true; w.methods.push_back(std::move(gv));
        w.methods.push_back(method("SetValue", {tString, tString}, {"label", "text"}, tVoid,
            [of](const Value& s, std::vector<Value>& a, CallContext& c) { auto* x = of(s, c); x->SetValue(x->IndexOfLabel(S(a[0])), S(a[1])); return Value{}; }, "Type text into the field with this label."));
        api.types.push_back(std::move(w));
    }
}

} // namespace okay::script::engine_api
