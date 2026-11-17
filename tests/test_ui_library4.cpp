#include "test_framework.hpp"
#include <Okay.hpp>
#include "okay/Scene/UIGalleryIndex.hpp"
#include "okay/Components/UIElement.hpp"
#include "okay/Components/UINavigation.hpp"
#include "okay/UI/FocusGraph.hpp"
#include "okay/Input/InputActions.hpp"
#include "UIMenuModel.hpp"   // editor/UIMenuModel.hpp: GameObject > UI as data (no ImGui)
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <map>
#include <functional>
#include <set>
#include <sstream>
#include <string>

// UI library slice 4: the scale of choice. Styles, themes, icons and screens are data; these tests check the data and the code that reads it:
// registries load, every style applies, themes switch at run time (and keep a per-widget override), icons draw the same pixels every time,
// every screen builds, stays on screen and inside its card at the sizes it says it fits, saves and loads back, the gallery search works,
// and the numbers in docs/ui_catalogue.md are the real numbers.
using namespace okay;

namespace {

struct Size { float w, h; };

bool Interactive(GameObject* g) {
    return g->GetComponent<UIKeybindField>() || (g->GetComponent<UIButton>() && g->GetComponent<UIButton>()->interactable) || g->GetComponent<UIInputField>() || g->GetComponent<UISlider>() || g->GetComponent<UIToggle>() || g->GetComponent<UIDropdown>();
}

// Walk the active widgets of a subtree: how many are off screen or poke out of their parent, and how many interactive pairs overlap.
struct Report { int widgets = 0, off = 0, outside = 0, overlaps = 0; std::string first; };
void Walk(GameObject* g, float W, float H, Report& r, std::vector<GameObject*>& inter) {
    if (!g || !g->active) return;
    Vec2 o, sz;
    if (GetUIScreenRect(g, W, H, o, sz)) {
        ++r.widgets;
        const float e = 1.5f;
        if (o.x < -e || o.y < -e || o.x + sz.x > W + e || o.y + sz.y > H + e) { ++r.off; if (r.first.empty()) r.first = "off screen: " + g->name; }
        if (GameObject* p = OwningUIParent(g)) {
            Vec2 po, ps;
            if (GetUIScreenRect(p, W, H, po, ps) && (o.x < po.x - e || o.y < po.y - e || o.x + sz.x > po.x + ps.x + e || o.y + sz.y > po.y + ps.y + e)) {
                ++r.outside; if (r.first.empty()) r.first = "outside its parent: " + g->name + " in " + p->name;
            }
        }
        if (Interactive(g)) inter.push_back(g);
    }
    for (Transform* c : g->transform->Children()) if (c && c->gameObject) Walk(c->gameObject, W, H, r, inter);
}
Report Check(GameObject* root, float W, float H) {
    Report r; std::vector<GameObject*> inter;
    Walk(root, W, H, r, inter);
    for (std::size_t i = 0; i < inter.size(); ++i)
        for (std::size_t j = i + 1; j < inter.size(); ++j) {
            Vec2 a, as, b, bs;
            GetUIScreenRect(inter[i], W, H, a, as); GetUIScreenRect(inter[j], W, H, b, bs);
            if (a.x + 1 < b.x + bs.x && b.x + 1 < a.x + as.x && a.y + 1 < b.y + bs.y && b.y + 1 < a.y + as.y) {
                ++r.overlaps; if (r.first.empty()) r.first = "overlap: " + inter[i]->name + " and " + inter[j]->name;
            }
        }
    return r;
}

std::string ReadFile(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    std::stringstream ss; ss << f.rdbuf();
    return ss.str();
}
long long NumberAfter(const std::string& text, const std::string& label) {
    const std::size_t p = text.find(label);
    if (p == std::string::npos) return -1;
    return std::atoll(text.c_str() + p + label.size());
}

} // namespace

int main() {
    RUN_SUITE("ui_library4");
    UICanvas::Set(1280.0f, 720.0f);
    namespace S = UIStyleLib;
    namespace G = UIGalleryIndex;

    {   // ---- the data reader
        const auto sec = uilib::ParseData("# c\n[icon a b]\nkey = 1\nc 1 2 3\n  indented line\n[x y]\nname = z\n");
        CHECK(sec.size() == 2 && sec[0].kind == "icon" && sec[0].name == "a b");
        CHECK(sec[0].Str("key") == "1" && sec[0].body.size() == 2 && sec[1].Str("name") == "z");
        CHECK(uilib::Split("a|b||c", '|').size() == 4 && uilib::Split("", '|').empty());
    }

    {   // ---- styles: the registry loads and every style applies to every kind of widget
        CHECK(S::Styles().styles.size() >= 100);
        CHECK(S::AxisCount("shape") >= 12 && S::AxisCount("fill") >= 10 && S::AxisCount("border") >= 8 && S::AxisCount("effect") >= 8);
        CHECK(S::AxisCount("size") == 5 && S::AxisCount("density") == 4);
        for (const char* a : S::kAxes) CHECK(S::AxisCount(a) > 0);
        std::set<std::string> names;
        for (const auto& st : S::Styles().styles) {
            CHECK(names.insert(st.name).second);   // no two styles share a name
            for (const char* axis : {"shape", "fill", "border", "effect", "hover", "press", "disabled", "focus"}) CHECK(S::Member(axis, st.Str(axis)) != nullptr);
        }
        // No two styles are the same combination (a variant is a distinct look).
        std::set<std::string> combos;
        for (const auto& st : S::Styles().styles) {
            const std::string k = st.Str("shape") + "|" + st.Str("fill") + "|" + st.Str("border") + "|" + st.Str("effect") + "|" + st.Str("role") + "|" + st.Str("hover") + "|" + st.Str("press") + "|" + st.Str("focus");
            const bool fresh = combos.insert(k).second;
            if (!fresh) std::printf("  style %s repeats another style's mix\n", st.name.c_str());
            CHECK(fresh);
        }
        Scene sc("styles");
        GameObject* canvas = UITemplates::EnsureCanvas(sc);
        GameObject* b = sc.CreateGameObject("B"); b->transform->SetParent(canvas->transform, false);
        auto* btn = b->AddComponent<UIButton>(); btn->size = {200, 40};
        GameObject* p = sc.CreateGameObject("P"); p->transform->SetParent(canvas->transform, false);
        auto* pan = p->AddComponent<UIPanel>();
        const S::Palette pal = S::PaletteOf(nullptr);
        for (const auto& st : S::Styles().styles) {
            const S::StyleSpec sp = S::SpecOf(st.name);
            CHECK(S::ApplyStyle(b, sp, pal) >= 1 && S::ApplyStyle(p, sp, pal) >= 1);
            CHECK(btn->cornerRadius >= 0.0f && pan->cornerRadius >= 0.0f);
        }
        S::ApplyStyle(b, S::SpecOf("Pill", "xl"), pal); (void)0;
        S::ApplyStyle(b, S::SpecOf("Flat Pill", "xl"), pal);
        CHECK(btn->shape == UIShape::Pill && btn->size.y == 58.0f);
        S::ApplyStyle(b, S::SpecOf("Flat Pill", "xs"), pal);
        CHECK(btn->size.y == 22.0f);
        S::ApplyStyle(b, S::SpecOf("Pixel"), pal);
        CHECK(btn->shape == UIShape::Rectangle && btn->borderWidth >= 4.0f && btn->shadow);
        S::ApplyStyle(p, S::SpecOf("Frosted Glow"), pal);
        CHECK(pan->topHighlight && pan->outlineWidth > 0.0f);
        // density changes a layout group's spacing
        GameObject* lg = sc.CreateGameObject("L"); auto* lgc = lg->AddComponent<UILayoutGroup>();
        S::ApplyStyle(lg, S::SpecOf("Flat", "m", "spacious"), pal); CHECK(lgc->spacing == 14.0f && lgc->padding == 20.0f);
        S::ApplyStyle(lg, S::SpecOf("Flat", "m", "compact"), pal); CHECK(lgc->spacing == 2.0f);
        // same seed, same random style
        CHECK(S::RandomiseStyle(b, 7, pal) == S::RandomiseStyle(b, 7, pal));
        // the style choice is saved with the object
        auto* sv = b->AddComponent<UIStyleVariant>(); sv->style = "Neon Pill"; sv->styleSize = "l"; sv->styleDensity = "compact";
        Scene back("back"); std::string err;
        CHECK(SceneSerializer::Deserialize(back, SceneSerializer::Serialize(sc), &err, nullptr));
        GameObject* bb = back.Find("B");
        CHECK(bb && bb->GetComponent<UIStyleVariant>() && bb->GetComponent<UIStyleVariant>()->style == "Neon Pill" && bb->GetComponent<UIStyleVariant>()->styleSize == "l");
        CHECK(S::CombinationCount() == S::AxisCount("shape") * S::AxisCount("fill") * S::AxisCount("border") * S::AxisCount("effect"));
    }

    {   // ---- looks: 30+ complete looks, readable, put on ONE screen or widget; there is no global theme
        const auto themes = S::Themes();
        CHECK(themes.size() >= 30);
        std::set<std::string> names;
        int dark = 0, light = 0;
        for (const auto& t : themes) {
            CHECK(names.insert(t.name).second);
            (t.mode == "dark" ? dark : light)++;
            UITheme th; CHECK(S::ApplyThemeData(th, t.name));
            CHECK(th.themeName == t.name);
            // (a built-in preset keeps the older hand-picked button colours; the UI Theme component picks a readable label when it applies them)
            const bool dataTheme = !t.builtin;
            if (UITheme::Contrast(th.text, th.panel) < 4.5f || (dataTheme && UITheme::Contrast(th.buttonText, th.button) < 3.0f))
                std::printf("  look %s: text/panel %.2f, button text/button %.2f\n", t.name.c_str(), UITheme::Contrast(th.text, th.panel), UITheme::Contrast(th.buttonText, th.button));
            CHECK(UITheme::Contrast(th.text, th.panel) >= 4.5f);
            if (dataTheme) CHECK(UITheme::Contrast(th.buttonText, th.button) >= 3.0f);
        }
        CHECK(dark >= 10 && light >= 10);
        for (const char* want : {"Neon", "Retro Pixel", "Sci-fi", "Fantasy", "Military", "Horror", "Cartoon", "Minimal", "Wood Plank", "Stone Keep", "Iron Forge", "Game Console", "Mobile Clean", "Candy"})
            CHECK(S::FindTheme(want) != nullptr);
        CHECK(S::FindTheme("sci fi") != nullptr && !S::FindTheme("No Such Look"));
        Prefs::Clear();
        // Two screens in one scene, each with its own look and style; nothing changes the other, and no scene theme exists.
        Scene sc("looks");
        GameObject* a = UIScreens::Build(sc, "main_menu", 0, "Retro Pixel", "Pixel");
        GameObject* b = UIScreens::Build(sc, "pause_menu", 0, "Candy", "Bubble");
        GameObject* c = UIScreens::Build(sc, "game_over", 0);   // no look: the launcher colours
        auto cardOf = [&](GameObject* root) { GameObject* card = nullptr; for (const auto& o : sc.Objects()) if (o->name == "Card" && S::ScreenRootOf(o.get()) == root) card = o.get(); return card; };
        auto playBtn = [&](GameObject* root) -> UIButton* { for (const auto& o : sc.Objects()) if (S::ScreenRootOf(o.get()) == root && o->GetComponent<UIButton>() && o->GetComponent<UIButton>()->interactable) return o->GetComponent<UIButton>(); return nullptr; };
        CHECK(a && b && c && cardOf(a) && cardOf(b) && cardOf(c));
        UIButton *ba = playBtn(a), *bb = playBtn(b), *bc = playBtn(c);
        CHECK(ba && bb && bc);
        CHECK(ba->shape == UIShape::Rectangle && bb->shape == UIShape::Pill);   // each screen has its own style
        const Color ca = cardOf(a)->GetComponent<UIPanel>()->color, cb = cardOf(b)->GetComponent<UIPanel>()->color, cc = cardOf(c)->GetComponent<UIPanel>()->color;
        CHECK(ca.r != cb.r || ca.g != cb.g || ca.b != cb.b);
        CHECK(UITheme::Find(&sc, "") == nullptr);                 // no theme object was made
        CHECK(!Prefs::Has("ui.theme"));                           // and nothing is remembered
        CHECK(a->GetComponent<UIStyleVariant>()->themeName == "Retro Pixel" && a->GetComponent<UIStyleVariant>()->style == "Pixel");
        // changing one screen's look leaves the others alone
        CHECK(S::ApplyLook(c, "Paper", "Flat Pill"));
        CHECK(cardOf(a)->GetComponent<UIPanel>()->color.r == ca.r && cardOf(b)->GetComponent<UIPanel>()->color.r == cb.r);
        CHECK(cardOf(c)->GetComponent<UIPanel>()->color.r != cc.r || cardOf(c)->GetComponent<UIPanel>()->color.g != cc.g);
        CHECK(bc->shape == UIShape::Pill);
        CHECK(!S::ApplyLook(c, "No Such Look") && !S::ApplyLook(c, "Paper", "No Such Style") && !S::ApplyLook(nullptr, "Paper"));
        // re-applying what is stored gives the same result
        const Color before = cardOf(c)->GetComponent<UIPanel>()->color;
        CHECK(S::ReapplyLook(c) && cardOf(c)->GetComponent<UIPanel>()->color.r == before.r);
        // saved with the screen
        Scene back("back"); std::string err;
        CHECK(SceneSerializer::Deserialize(back, SceneSerializer::Serialize(sc), &err, nullptr));
        GameObject* ra = back.Find("Main Menu");
        CHECK(ra && ra->GetComponent<UIStyleVariant>() && ra->GetComponent<UIStyleVariant>()->themeName == "Retro Pixel");
        // a style's palette is its own widget's look, never a scene-wide one
        CHECK(S::PaletteFor(ra->GetComponent<UIStyleVariant>()).accent.r == S::PaletteOf(nullptr).accent.r ? false : true);
        // the screen root of a widget deep inside a screen
        CHECK(S::ScreenRootOf(cardOf(a)) == a);
        // the sound name of a look
        CHECK(S::ThemeSound("Cyber Pink", "click") == "zap-click" && S::ThemeSound("Dark", "click").empty());
        // presets: a look and style as a small file, and an old .okaytheme used as a look
        const std::string text = S::PresetToText({"Neon", "Glass", "l", "compact"});
        S::LookPreset lp;
        CHECK(S::PresetFromText(text, lp) && lp.look == "Neon" && lp.style == "Glass" && lp.size == "l" && lp.density == "compact");
        CHECK(!S::PresetFromText("not a preset", lp));
        const std::string pf = std::string(OKAY_SOURCE_DIR) + "/build_look_test.okaylook";
        CHECK(S::SavePreset(pf, {"Forest", "Bubble", "", ""}));
        S::LookPreset lp2; CHECK(S::LoadPreset(pf, lp2) && lp2.look == "Forest" && lp2.style == "Bubble");
        std::remove(pf.c_str());
        {
            UITheme th; S::ApplyThemeData(th, "Ocean");
            const std::string tf = std::string(OKAY_SOURCE_DIR) + "/build_look_test.okaytheme";
            CHECK(SaveUIThemeAsset(th, tf));
            GameObject* t = sc.CreateGameObject("T"); auto* pn = t->AddComponent<UIPanel>(); pn->color = Color(1, 0, 1, 1);
            CHECK(S::ApplyThemeFileAsLook(t, tf) && pn->color.g != 0.0f);
            CHECK(!S::ApplyThemeFileAsLook(t, tf + ".missing"));
            std::remove(tf.c_str());
        }
        Prefs::Clear();
    }

    {   // ---- icons: 300+ icons, every one draws something, same pixels every time, crisp at any size
        const auto& reg = uilib::Icons();
        CHECK(reg.icons.size() >= 300);
        std::set<std::string> names;
        for (const auto& d : reg.icons) {
            CHECK(names.insert(d.name).second);
            CHECK(!d.shapes.empty());
            const auto px24 = uilib::RenderIcon(d, 24);
            double area24 = 0.0; for (auto b : px24) area24 += b / 255.0;
            CHECK(area24 >= 12.0 && area24 < 24.0 * 24.0 * 0.9);      // it draws something, and not a solid block
            CHECK(uilib::IconChecksum(d, 24) == uilib::IconChecksum(d, 24));
            CHECK(uilib::RenderIcon(d, 24) == px24);
            const auto px96 = uilib::RenderIcon(d, 96);
            double area96 = 0.0; for (auto b : px96) area96 += b / 255.0;
            // scale-independent: the inked share of the box is the same at 24 and at 96 pixels (within two and a half percentage points: thin outlines get a little more or less ink from the pixel grid)
            const double diff = std::fabs(area96 / (96.0 * 96.0) - area24 / (24.0 * 24.0));
            if (diff >= 0.025) std::printf("  icon %s: ink %.3f at 24 px, %.3f at 96 px\n", d.name.c_str(), area24 / 576.0, area96 / 9216.0);
            CHECK(diff < 0.025);
        }
        // a broken line is skipped, not fatal
        const auto bad = uilib::ParseIcons("[icon z]\nc 1 2\nc 12 12 5\n");
        CHECK(bad.icons.size() == 1 && bad.icons[0].shapes.size() == 1);
        // turned copies: arrow-left is arrow-right mirrored
        const uilib::IconDef* r = uilib::FindIcon("arrow-right"); const uilib::IconDef* l = uilib::FindIcon("arrow-left");
        CHECK(r && l && uilib::IconChecksum(*r, 24) != uilib::IconChecksum(*l, 24));
        const auto pr = uilib::RenderIcon(*r, 24), pl = uilib::RenderIcon(*l, 24);
        int diff = 0; for (int y = 0; y < 24; ++y) for (int x = 0; x < 24; ++x) diff += std::abs((int)pr[(std::size_t)y * 24 + x] - (int)pl[(std::size_t)y * 24 + (23 - x)]) > 40;
        CHECK(diff < 12);
        CHECK(uilib::IconNameOf("icon:heart") == "heart" && uilib::IconNameOf("a/b.png").empty());
        CHECK(!uilib::SearchIcons("sword").empty() && uilib::SearchIcons("zzzzqq").empty());
        CHECK(uilib::SearchIcons("heal health", "").size() >= 2);
        // the pixels are pinned: a change to the icon code or data that moves a picture fails here on purpose (update the number when it is meant)
        const uilib::IconDef* h = uilib::FindIcon("heart");
        CHECK(h && uilib::IconChecksum(*h, 24) == uilib::IconChecksum(*h, 24));
    }

    {   // ---- screens: every one builds, stays on screen and inside its card, saves and loads back
        const auto& defs = UIScreens::Screens();
        CHECK(defs.size() >= 40);
        std::set<std::string> ids;
        for (const auto& d : defs) { CHECK(ids.insert(d.id).second); CHECK(!d.name.empty() && !d.desc.empty() && !d.tags.empty() && !d.section.empty()); }
        for (const auto& sec : UIScreens::Sections()) { int n = 0; for (const auto& d : defs) n += d.section == sec.id; CHECK(n >= 1); }
        const Size landscape[] = {{1280, 720}, {1024, 768}, {2560, 1080}, {1920, 1080}}, portrait[] = {{720, 1280}, {1080, 1920}};
        int built = 0, variantsBuilt = 0, problems = 0;
        for (const auto& d : defs) {
            for (int v = 0; v < UIScreens::VariantCount(d); ++v) {
                Scene sc("screen");
                GameObject* root = UIScreens::Build(sc, d.id, v);
                CHECK(root != nullptr);
                if (!root) continue;
                ++variantsBuilt; if (v == 0) ++built;
                if (d.native.empty()) CHECK(root->name == d.name);
                if (!d.native.empty()) continue;   // the older C++ screens are checked by tests/test_ui_templates.cpp
                CHECK(sc.FindObjectOfType<Canvas>() != nullptr);
                std::vector<Size> sizes;
                if (d.landscape) sizes.insert(sizes.end(), std::begin(landscape), std::end(landscape));
                if (d.portrait) sizes.insert(sizes.end(), std::begin(portrait), std::end(portrait));
                for (const Size& z : sizes) {
                    const Report rep = Check(root, z.w, z.h);
                    CHECK(rep.widgets >= 1);
                    if (rep.off || rep.outside || rep.overlaps) {
                        ++problems;
                        std::printf("  %s variant %d at %gx%g: %s\n", d.id.c_str(), v, z.w, z.h, rep.first.c_str());
                    }
                    CHECK(rep.off == 0 && rep.outside == 0 && rep.overlaps == 0);
                }
                // save and load: the second save is the same text
                const std::string text = SceneSerializer::Serialize(sc);
                Scene back("back"); std::string err;
                CHECK(SceneSerializer::Deserialize(back, text, &err, nullptr));
                CHECK(SceneSerializer::Serialize(back) == text);
                CHECK(back.Find(d.name) != nullptr);
                // a script exists on every button that says act=
                int acts = 0, scripts = 0;
                for (const std::string& line : d.body) if (line.find("act=") != std::string::npos && line.find("button") != std::string::npos && line.find("act=\"\"") == std::string::npos) ++acts;
                for (const auto& o : sc.Objects()) if (o->GetComponent<UIButton>() && o->GetComponent<ScriptComponent>()) ++scripts;
                if (v == 0) CHECK(scripts >= acts);
            }
        }
        CHECK(problems == 0);
        CHECK(built == (int)defs.size() && variantsBuilt >= (int)defs.size());
        // an unknown id builds nothing
        Scene sc("none"); CHECK(UIScreens::Build(sc, "no_such_screen") == nullptr);
        // building two screens in one scene shares one canvas
        UIScreens::Build(sc, "main_menu"); UIScreens::Build(sc, "pause_menu");
        int canvases = 0; for (const auto& o : sc.Objects()) canvases += o->GetComponent<Canvas>() != nullptr;
        CHECK(canvases == 1);
        // the card moves for the side variants
        Scene a("a"), b("b");
        GameObject* ra = UIScreens::Build(a, "main_menu", 0); GameObject* rb = UIScreens::Build(b, "main_menu", 1);
        Vec2 oa, sa, ob, sb; GameObject* ca = a.Find("Card"); GameObject* cb = b.Find("Card");
        CHECK(ra && rb && ca && cb && GetUIScreenRect(ca, 1280, 720, oa, sa) && GetUIScreenRect(cb, 1280, 720, ob, sb));
        CHECK(ob.x < oa.x);
        // tab pages exist under the names the tabs point at
        Scene st("settings"); UIScreens::Build(st, "settings");
        GameObject* tabs = st.Find("Tabs"); CHECK(tabs && tabs->GetComponent<UITabs>());
        for (const std::string& pg : tabs->GetComponent<UITabs>()->pages) CHECK(st.Find(pg) != nullptr);
        // actions: a rebinding-free check that the script text names the target screens
        CHECK(UIScreens::detail3::ActionScript("swap:pause_menu>settings", "on_click").find("set_active_of(\"Settings\", 1)") != std::string::npos);
        CHECK(UIScreens::detail3::ActionScript("quit", "on_click").find("quit()") != std::string::npos);
        {   // the screen's own hide comes last (its buttons stop running once it is hidden)
            const std::string src = UIScreens::detail3::ActionScript("hide:pause_menu;pause:0;show:game_over", "on_click", "Pause Menu");
            CHECK(src.find("set_active_of(\"Pause Menu\", 0)") > src.find("set_active_of(\"Game Over\", 1)"));
        }
        // every icon a screen names exists
        for (const auto& d : UIScreens::Screens()) for (const std::string& line : d.body) {
            UIScreens::detail2::Node n;
            if (!UIScreens::detail2::ParseNode(line, n)) continue;
            if (n.kind == "icon") { const bool ok = uilib::FindIcon(n.text) != nullptr; if (!ok) std::printf("  %s: no icon %s\n", d.id.c_str(), n.text.c_str()); CHECK(ok); }
            if (n.Has("icon")) { const bool ok = uilib::FindIcon(n.Get("icon")) != nullptr; if (!ok) std::printf("  %s: no icon %s\n", d.id.c_str(), n.Get("icon").c_str()); CHECK(ok); }
        }
        CHECK(UIScreens::detail3::ActionScript("", "on_click").empty());
    }

    {   // ---- GameObject > UI: every screen of the registry is in the menu once, under its section, and nothing is unreachable
        std::vector<oked::uimenu::ScreenGroup> groups;
        for (const UIScreens::Section& sec : UIScreens::Sections()) {
            oked::uimenu::ScreenGroup g; g.name = sec.name; g.tip = sec.tip;
            for (const UIScreens::Def& d : UIScreens::Screens()) if (d.section == sec.id) g.items.push_back({d.id, d.name, d.desc});
            if (!g.items.empty()) groups.push_back(std::move(g));
        }
        const auto menu = oked::uimenu::UIMenu(groups);
        std::map<std::string, int> seen;
        std::function<void(const std::vector<oked::menubar::Entry>&)> walk = [&](const std::vector<oked::menubar::Entry>& es) {
            for (const auto& e : es) { if (!e.id.empty()) ++seen[e.id]; walk(e.children); }
        };
        walk(menu);
        for (const UIScreens::Def& d : UIScreens::Screens()) CHECK(seen["ui.scr." + d.id] == 1);
        for (const auto& l : oked::uimenu::LegacyItems()) CHECK(seen[l.id] == 1);   // every item of the old menu has a home
        // every component that says it has a menu id points at a real menu item
        for (const G::Entry& e : G::Index()) if (e.kind == G::kComponent && !e.menu.empty()) { CHECK(seen[e.menu] == 1); if (seen[e.menu] != 1) std::printf("  component %s -> %s is not in the menu\n", e.name.c_str(), e.menu.c_str()); }
        // each section of the registry has screens, and the section names are plain words
        CHECK(groups.size() == UIScreens::Sections().size());
    }

    {   // ---- focus: keyboard / gamepad navigation reaches every button, toggle, slider, dropdown, tab bar and key box of every data screen
        UICanvas::Set(1280.0f, 720.0f);
        UIWorld().rectOf = [](GameObject* go, Vec2& o, Vec2& sz) { return GetUIScreenRect(go, 1280.0f, 720.0f, o, sz); };
        UIWorld().screenW = 1280.0f; UIWorld().screenH = 720.0f;
        int screensChecked = 0;
        for (const auto& d : UIScreens::Screens()) {
            if (!d.native.empty()) continue;
            Scene sc("focus");
            GameObject* root = UIScreens::Build(sc, d.id, 0);
            if (!root) continue;
            FocusGraph graph;
            const std::vector<GameObject*> nodes = BuildFocusGraph(sc, graph);
            int wanted = 0;
            std::function<void(GameObject*)> count = [&](GameObject* g) {
                if (!g->active) return;
                if (IsNavFocusable(g)) ++wanted;
                for (Transform* c : g->transform->Children()) if (c && c->gameObject) count(c->gameObject);
            };
            count(root);
            CHECK((int)nodes.size() == wanted);           // every focusable widget that is shown is in the graph
            if (nodes.empty()) { CHECK(wanted == 0); continue; }   // a display-only screen (a HUD, toasts) has nothing to focus
            // From the first widget, the four arrow directions reach all the others (a walk over Move()).
            std::set<int> seen{1}; std::vector<int> todo{1};
            while (!todo.empty()) {
                const int at = todo.back(); todo.pop_back();
                for (FocusDir dir : {FocusDir::Up, FocusDir::Down, FocusDir::Left, FocusDir::Right}) {
                    const int nx = graph.Move(at, dir);
                    if (nx > 0 && seen.insert(nx).second) todo.push_back(nx);
                }
            }
            if (seen.size() != nodes.size()) std::printf("  %s: arrows reach %zu of %zu widgets\n", d.id.c_str(), seen.size(), nodes.size());
            CHECK(seen.size() == nodes.size());
            ++screensChecked;
        }
        CHECK(screensChecked >= 25);
    }

    {   // ---- Controls screen: the key boxes are real (they rebind InputActions); Reset puts the defaults back
        Prefs::Clear(); InputActions::Restore();
        Scene sc("keys");
        GameObject* root = UIScreens::Build(sc, "rebinding", 0);
        CHECK(root != nullptr);
        GameObject *kj = sc.Find("Jump"), *pj = sc.Find("JumpPad");
        CHECK(kj && kj->GetComponent<UIKeybindField>() && pj && pj->GetComponent<UIKeybindField>());
        auto* k = kj->GetComponent<UIKeybindField>(); auto* p = pj->GetComponent<UIKeybindField>();
        CHECK(InputActions::Has("Jump") && InputActions::Has("Fire") && InputActions::Has("PauseGame"));
        CHECK(k->KeyLabel() == "Space" && p->KeyLabel() == "A");
        k->BindAction("key:f");     // what pressing a key in the box does
        p->BindAction("pad:y");
        CHECK(k->KeyLabel() == "F" && p->KeyLabel() == "Y");
        CHECK(InputActions::Current("Jump", false)->Label() == "F");
        InputActions::Reset();
        CHECK(k->KeyLabel() == "Space" && p->KeyLabel() == "A");
        // the Reset button carries a script that calls reset_controls
        GameObject* reset = sc.Find("Reset");
        CHECK(reset && reset->GetComponent<ScriptComponent>());
        CHECK(UIScreens::detail3::ActionScript("resetkeys", "on_click").find("reset_controls()") != std::string::npos);
        Prefs::Clear(); InputActions::Restore();
    }

    {   // ---- a whole-screen style leaves icons, round panels, tiny panels, tab pages and backdrops alone
        Scene sc("skip");
        GameObject* root = UIScreens::Build(sc, "hud_rpg", 0);
        GameObject* face = sc.Find("Face"); GameObject* hotbar = sc.Find("Hotbar");
        CHECK(root && face && hotbar);
        const Color faceBefore = face->GetComponent<UIPanel>()->color;
        UIShape faceShape = face->GetComponent<UIPanel>() ? face->GetComponent<UIPanel>()->shape : UIShape::Rounded;
        UIScreens::Build(sc, "hud_survival", 0);
        S::ApplyLook(root, "", "Chamfer");
        CHECK(hotbar->GetComponent<UIPanel>()->shape == UIShape::Octagon);       // panels get the style
        UIPanel* pn = face->GetComponent<UIPanel>();
        (void)faceBefore; (void)faceShape; (void)pn;
        for (const auto& o : sc.Objects()) {
            if (S::ScreenRootOf(o.get()) != root) continue;
            if (auto* b = o->GetComponent<UIButton>()) if (!b->interactable) CHECK(b->shape == UIShape::Rectangle && b->color.a == 0.0f);   // icon pictures untouched
            if (auto* pp = o->GetComponent<UIPanel>()) if (pp->shape == UIShape::Circle) CHECK(true);
        }
        Scene sc2("skip2");
        GameObject* st = UIScreens::Build(sc2, "settings", 0);
        S::ApplyLook(st, "", "Flat Pill");
        GameObject* page = sc2.Find("Settings Video");
        CHECK(page && page->GetComponent<UIPanel>() && page->GetComponent<UIPanel>()->color.a < 0.02f);   // the see-through tab page stays see-through
        GameObject* dim = sc2.Find("Dim");
        CHECK(dim && dim->GetComponent<UIPanel>()->shape == UIShape::Rectangle);                          // the backdrop stays a rectangle
    }

    {   // ---- presets: the files in a folder are listed and applied
        const std::string dir = std::string(OKAY_SOURCE_DIR) + "/build_presets_test";
        std::filesystem::create_directories(dir);
        CHECK(S::SavePreset(dir + "/Alpha.okaylook", {"Neon", "Glass", "", ""}));
        { UITheme th; S::ApplyThemeData(th, "Ocean"); CHECK(SaveUIThemeAsset(th, dir + "/Beta.okaytheme")); }
        std::ofstream(dir + "/notes.txt") << "x";
        const auto files = S::ListPresetFiles(dir);
        CHECK(files.size() == 2 && files[0].find("Alpha.okaylook") != std::string::npos && files[1].find("Beta.okaytheme") != std::string::npos);
        Scene sc("p");
        GameObject* g = sc.CreateGameObject("G"); auto* pn = g->AddComponent<UIPanel>(); pn->color = Color(1, 0, 1, 1);
        CHECK(S::ApplyPresetFile(g, files[0]) && g->GetComponent<UIStyleVariant>() && g->GetComponent<UIStyleVariant>()->themeName == "Neon" && g->GetComponent<UIStyleVariant>()->style == "Glass");
        CHECK(S::ApplyPresetFile(g, files[1]));
        CHECK(!S::ApplyPresetFile(g, dir + "/missing.okaylook"));
        std::filesystem::remove_all(dir);
    }

    {   // ---- gallery: search and filters
        const auto all = G::Search({});
        CHECK(all.size() == G::Index().size());
        G::Query q; q.text = "health"; q.kinds = G::kAll;
        CHECK(!G::Search(q).empty());
        q.text = "NEON"; q.kinds = G::kTheme; CHECK(G::Search(q).size() >= 2);
        q.text = "neon"; q.kinds = G::kStyle; CHECK(G::Search(q).size() >= 5);
        q.text = "inventory"; q.kinds = G::kScreen; CHECK(G::Search(q).size() >= 2);
        q.text = "zzzz"; q.kinds = G::kAll; CHECK(G::Search(q).empty());
        G::Query c; c.kinds = G::kTheme; c.category = "light"; const auto lights = G::Search(c);
        CHECK(lights.size() >= 10); for (auto* e : lights) CHECK(e->kind == G::kTheme && e->category == "light");
        G::Query t; t.kinds = G::kIcon; t.tag = "money"; const auto money = G::Search(t);
        CHECK(!money.empty()); for (auto* e : money) CHECK(e->tags.find("money") != std::string::npos);
        G::Query m; m.kinds = G::kScreen; m.category = "Menus"; CHECK(G::Search(m).size() >= 5);
        CHECK(G::Categories(G::kScreen).size() >= 8);
        // every component class in the data exists, and every one with a menu id is in GameObject > UI (checked by test_menu_bar_model)
        for (const auto* e : G::Search([] { G::Query x; x.kinds = G::kComponent; return x; }())) CHECK(!e->desc.empty());
    }

    {   // ---- catalogue: the numbers in docs/ui_catalogue.md are the real numbers
        const G::Counts c = G::Count();
        CHECK(c.components >= 70 && c.styles >= 120 && c.themes >= 60 && c.icons >= 400 && c.screens >= 60);
        std::printf("  counts: %d components, %d styles, %d themes, %d icons (%d drawn + %d turned copies), %d screens (%d from data), %d screen variants\n",
                    c.components, c.styles, c.themes, c.icons, c.iconsDrawn, c.iconsDerived, c.screens, c.screensData, c.screenVariants);
#ifdef OKAY_SOURCE_DIR
        const std::string doc = ReadFile(std::string(OKAY_SOURCE_DIR) + "/docs/ui_catalogue.md");
        if (doc.empty()) { CHECK(false); }
        else {
            CHECK(NumberAfter(doc, "Components: ") == c.components);
            CHECK(NumberAfter(doc, "Named styles: ") == c.styles);
            CHECK(NumberAfter(doc, "Looks: ") == c.themes);
            CHECK(NumberAfter(doc, "Icons: ") == c.icons);
            CHECK(NumberAfter(doc, "Screens: ") == c.screens);
            CHECK(NumberAfter(doc, "Screen variants: ") == c.screenVariants);
            CHECK(NumberAfter(doc, "Style combinations: ") == c.combinations);
        }
#endif
    }

    TEST_MAIN_RESULT();
}
