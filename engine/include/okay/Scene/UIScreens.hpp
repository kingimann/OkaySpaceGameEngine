#pragma once
// Ready-made UI screens as DATA: assets/ui/ui_screens.okaydata describes each screen in a short layout language and ONE builder here
// turns it into real objects (panels, text, buttons, sliders, tabs...) under the scene's Canvas. Adding a screen means adding a few lines
// of data, not a new function. Written for this engine from its own UI helpers (UITemplates::detail); no other project's code or art.
//
// The layout language (one widget per line, two spaces of indent = a child of the line above):
//   <kind> <Name> ["text"] key=value ...
//   kinds: group panel dim text caption button badge avatar divider bar slider toggle dropdown input tabs icon rep
//   common keys: anchor=center|topleft|topcenter|topright|midleft|midright|bottomleft|bottomcenter|bottomright|stretch|stretchh|stretchv
//                pos=x,y  size=w,h  fill=<role>  color=<role>  v=0,1 (only for those variants)  off=1 (starts hidden)
//   <role>: panel card field accent text dim success danger warning backdrop none, or #RRGGBB / #RRGGBBAA
//   text: px=16 align=l|c|r bold=1 wrap=1 up=1     button: btn=primary|secondary|danger|ghost act=<actions> icon=<icon name>
//   bar: value=0..1 fill=<role>   slider: min= max= value= step= act=volume   toggle: on=1 style=switch|check|radio
//   dropdown: options=A|B|C value=0   input: hint="..." password=1   tabs: tabs=A|B pages=Local1|Local2 style=underline|pills|vertical
//   rep n=8 cols=4 dx=70 dy=70: its child lines are repeated (name gets 1..n, {i} in text becomes the number)
//   act: ";"-separated: show:<screen> hide:<screen> swap:<a>><b> toggle:<screen> quit scene:<path> pause:0|1 theme:<name> tab:<tabs>:<i> volume
//        (a <screen> is a screen id from the data; other names are used as they are). They become a small script on the widget.
#include "okay/Scene/UITemplates.hpp"
#include "okay/Scene/UIGallery.hpp"
#include "okay/Scene/UIStyleLib.hpp"
#include "okay/UI/UILibData.hpp"
#include "okay/Components/UITheme.hpp"
#include "okay/Components/UIDropdown.hpp"
#include "okay/Components/ScriptComponent.hpp"
#include <algorithm>
#include <functional>
#include <map>
#include <string>
#include <vector>

namespace okay { namespace UIScreens {

namespace slate = UITemplates::slate;

struct Section { std::string id, name, tip; };
struct Def {
    std::string id, name, section, desc;
    std::vector<std::string> tags;
    std::vector<std::string> variants;   // presentation variants of a card screen; empty = one look
    std::string native;                  // a screen that is still built by an older C++ function (UITemplates); no body
    bool landscape = true, portrait = true;   // the sizes it is checked to fit (tests/test_ui_screens.cpp)
    bool startsHidden = false;           // popups: built hidden, shown by an action
    std::string replaces;                // the older prebuilt this one replaced, when any
    std::vector<std::string> body;
};

namespace detail2 {
using namespace UITemplates::detail;

inline Color Role(const std::string& r, Color def) {
    if (r.empty()) return def;
    if (r == "panel") return slate::Panel();
    if (r == "card") return slate::Card();
    if (r == "field") return slate::Field();
    if (r == "accent") return slate::Accent();
    if (r == "text") return slate::Text();
    if (r == "dim") return slate::Dim();
    if (r == "success") return slate::Success();
    if (r == "danger") return slate::Danger();
    if (r == "warning") return slate::Warning();
    if (r == "backdrop") return slate::Backdrop();
    if (r == "none") return Color(0, 0, 0, 0);
    if (r[0] == '#' && (r.size() == 7 || r.size() == 9)) {
        auto hx = [&](std::size_t i) { return (int)std::strtol(r.substr(i, 2).c_str(), nullptr, 16); };
        return Color::FromBytes(hx(1), hx(3), hx(5), r.size() == 9 ? hx(7) : 255);
    }
    return def;
}
inline UIAnchor Anchor(const std::string& a, UIAnchor def) {
    static const std::pair<const char*, UIAnchor> t[] = {
        {"topleft", UIAnchor::TopLeft}, {"topcenter", UIAnchor::TopCenter}, {"topright", UIAnchor::TopRight},
        {"midleft", UIAnchor::MiddleLeft}, {"center", UIAnchor::Center}, {"midright", UIAnchor::MiddleRight},
        {"bottomleft", UIAnchor::BottomLeft}, {"bottomcenter", UIAnchor::BottomCenter}, {"bottomright", UIAnchor::BottomRight},
        {"stretch", UIAnchor::StretchFull}, {"stretchh", UIAnchor::StretchHorizontal}, {"stretchv", UIAnchor::StretchVertical}};
    for (const auto& p : t) if (a == p.first) return p.second;
    return def;
}
inline Vec2 Pair(const std::string& s, Vec2 def) {
    const std::size_t c = s.find(',');
    if (c == std::string::npos) return def;
    return {std::strtof(s.substr(0, c).c_str(), nullptr), std::strtof(s.substr(c + 1).c_str(), nullptr)};
}

struct Node {
    int depth = 0;
    std::string kind, name, text;
    bool hasText = false;
    std::map<std::string, std::string> a;
    std::string Get(const char* k, const std::string& def = std::string()) const { auto it = a.find(k); return it == a.end() ? def : it->second; }
    float Num(const char* k, float def) const { auto it = a.find(k); return it == a.end() ? def : std::strtof(it->second.c_str(), nullptr); }
    bool Has(const char* k) const { return a.count(k) != 0; }
};

/// Split one body line into a Node. Quoted pieces keep their spaces: `"Hello there"` or `key="a b"`.
inline bool ParseNode(const std::string& raw, Node& n) {
    std::size_t sp = 0;
    while (sp < raw.size() && raw[sp] == ' ') ++sp;
    n.depth = (int)(sp / 2);
    std::vector<std::string> tok; std::vector<bool> quoted;
    std::size_t i = sp;
    while (i < raw.size()) {
        while (i < raw.size() && raw[i] == ' ') ++i;
        if (i >= raw.size()) break;
        std::string t; bool q = false;
        while (i < raw.size() && raw[i] != ' ') {
            if (raw[i] == '"') {
                q = true; ++i;
                while (i < raw.size() && raw[i] != '"') t.push_back(raw[i++]);
                if (i < raw.size()) ++i;
            } else t.push_back(raw[i++]);
        }
        tok.push_back(t); quoted.push_back(q);
    }
    if (tok.empty()) return false;
    n.kind = tok[0];
    const bool rep = n.kind == "rep";   // "rep n=8 cols=4": no name, only settings
    if (!rep) { if (tok.size() < 2) return false; n.name = tok[1]; }
    for (std::size_t k = rep ? 1 : 2; k < tok.size(); ++k) {
        const std::size_t eq = tok[k].find('=');
        if (eq != std::string::npos && eq > 0 && !(quoted[k] && tok[k].find('=') == std::string::npos)) n.a[tok[k].substr(0, eq)] = tok[k].substr(eq + 1);
        else if (!n.hasText) { n.text = tok[k]; n.hasText = true; }
    }
    if (!n.hasText && n.a.count("text")) { n.text = n.a["text"]; n.hasText = true; }
    return true;
}

inline std::string Replace(std::string s, const std::string& from, const std::string& to) {
    for (std::size_t p = 0; (p = s.find(from, p)) != std::string::npos; p += to.size()) s.replace(p, from.size(), to);
    return s;
}

/// Expand every `rep` node: its deeper lines are copied n times, each moved by the column/row step.
inline std::vector<Node> ExpandReps(const std::vector<Node>& in) {
    std::vector<Node> out;
    for (std::size_t k = 0; k < in.size(); ++k) {
        const Node& n = in[k];
        if (n.kind != "rep") { out.push_back(n); continue; }
        std::vector<Node> kids;
        std::size_t e = k + 1;
        while (e < in.size() && in[e].depth > n.depth) kids.push_back(in[e++]);
        kids = ExpandReps(kids);
        const int count = (int)n.Num("n", 1), cols = std::max(1, (int)n.Num("cols", (float)count));
        const float dx = n.Num("dx", 0), dy = n.Num("dy", 0);
        for (int i = 0; i < count; ++i) {
            for (Node c : kids) {
                c.depth -= 1;
                if (c.depth == n.depth) {
                    const Vec2 p = Pair(c.Get("pos"), {0, 0});
                    char buf[48]; std::snprintf(buf, sizeof buf, "%g,%g", p.x + (float)(i % cols) * dx, p.y + (float)(i / cols) * dy);
                    c.a["pos"] = buf;
                }
                c.name += std::to_string(i + 1);
                c.text = Replace(c.text, "{i}", std::to_string(i + 1));
                for (auto& kv : c.a) kv.second = Replace(kv.second, "{i}", std::to_string(i + 1));
                out.push_back(c);
            }
        }
        k = e - 1;
    }
    return out;
}

} // namespace detail2

// ---- the registry (parsed once from the embedded data) ----
struct Registry { std::vector<Section> sections; std::vector<Def> screens; };

inline Registry ParseRegistry(const std::string& text) {
    Registry r;
    for (const uilib::DataSection& s : uilib::ParseData(text)) {
        if (s.kind == "section") {
            r.sections.push_back({s.name, s.Str("name", s.name), s.Str("tip")});
        } else if (s.kind == "screen") {
            Def d;
            d.id = s.name; d.name = s.Str("name", s.name); d.section = s.Str("section"); d.desc = s.Str("desc");
            d.tags = uilib::Split(s.Str("tags"), ',');
            d.variants = uilib::Split(s.Str("variants"), '|');
            d.native = s.Str("native"); d.replaces = s.Str("replaces");
            d.landscape = s.Num("landscape", 1) != 0; d.portrait = s.Num("portrait", 1) != 0;
            d.startsHidden = s.Num("hidden", 0) != 0;
            d.body = s.body;
            r.screens.push_back(std::move(d));
        }
    }
    return r;
}
inline const Registry& Reg() {
    static const Registry r = ParseRegistry(uilib::EmbeddedData("ui_screens"));
    return r;
}
inline const std::vector<Def>& Screens() { return Reg().screens; }
inline const std::vector<Section>& Sections() { return Reg().sections; }
inline const Def* Find(const std::string& id) {
    for (const Def& d : Screens()) if (d.id == id) return &d;
    return nullptr;
}
inline int VariantCount(const Def& d) { return d.variants.empty() ? 1 : (int)d.variants.size(); }

/// Older screens still built by a C++ function (UITemplates / UIGallery), by the name used in the data's `native=`.
inline const std::vector<std::pair<std::string, GameObject* (*)(Scene&)>>& NativeBuilders() {
    using namespace UITemplates;
    static const std::vector<std::pair<std::string, GameObject* (*)(Scene&)>> v = {
        {"SaveSlots", &AddSaveSlots}, {"LoadingScreen", &AddLoadingScreen}, {"LoadingMinimal", &AddLoadingMinimal},
        {"LoadingCard", &AddLoadingCard}, {"LoadingSplash", &AddLoadingSplash}, {"LoadingCinematic", &AddLoadingCinematic},
        {"Lobby", &AddLobby}, {"Scoreboard", &AddScoreboard}, {"Leaderboard", &AddLeaderboard}, {"Matchmaking", &AddMatchmaking},
        {"HUD", &AddHUD}, {"HealthBar", &AddHealthBar}, {"DialogBox", &AddDialogBox}, {"Notification", &AddNotificationScreen},
        {"Showcase", &AddUIShowcase}, {"UIColor", &AddUIColor}, {"UIHUD", &AddUIHUD}, {"UIControls", &AddUIControls},
        {"UINotify", &AddUINotify}, {"UIOptions", &AddUIOptions},
        {"Inventory", &UIGallery::AddInventoryScreen}, {"Dialogue", &UIGallery::AddDialogueScreen},
    };
    return v;
}

namespace detail3 {
using namespace detail2;

/// The root object name of a screen id (the data's `name`), or the text itself when it is not a screen id.
inline std::string RootName(const std::string& idOrName) {
    if (const Def* d = Find(idOrName)) return d->name;
    return idOrName;
}

/// One action list -> OkayScript source for `function <event>()`. A statement that hides the screen the button belongs to (`selfRoot`) is
/// written last: once the screen is hidden its buttons are inactive and a script on one stops running, so the rest would be lost.
inline std::string ActionScript(const std::string& acts, const char* event, const std::string& selfRoot = std::string()) {
    std::string body, last;
    auto emit = [&](const std::string& target, int on) {
        const std::string line = "  set_active_of(\"" + target + "\", " + std::to_string(on) + ");\n";
        if (!on && !selfRoot.empty() && target == selfRoot) last += line; else body += line;
    };
    for (const std::string& a : uilib::Split(acts, ';')) {
        const std::size_t c = a.find(':');
        const std::string verb = c == std::string::npos ? a : a.substr(0, c);
        const std::string arg = c == std::string::npos ? std::string() : a.substr(c + 1);
        if (verb == "show") emit(RootName(arg), 1);
        else if (verb == "hide") emit(RootName(arg), 0);
        else if (verb == "swap") {
            const std::size_t g = arg.find('>');
            if (g != std::string::npos) { emit(RootName(arg.substr(g + 1)), 1); emit(RootName(arg.substr(0, g)), 0); }
        }
        else if (verb == "toggle") body += "  if (is_active(\"" + RootName(arg) + "\")) { set_active_of(\"" + RootName(arg) + "\", 0); } else { set_active_of(\"" + RootName(arg) + "\", 1); }\n";
        else if (verb == "quit") body += "  quit();\n";
        else if (verb == "scene") body += "  load_scene(\"" + arg + "\");\n";
        else if (verb == "pause") body += std::string("  pause_game(") + (arg == "0" ? "0" : "1") + ");\n";
        else if (verb == "theme") body += "  ui_set_theme(\"" + arg + "\");\n";
        else if (verb == "tab") {
            const std::size_t k = arg.find(':');
            if (k != std::string::npos) body += "  ui_set_tab(\"" + arg.substr(0, k) + "\", " + arg.substr(k + 1) + ");\n";
        }
        else if (verb == "volume") body += "  set_volume(slider_value());\n";
    }
    body += last;
    if (body.empty()) return std::string();
    return std::string("function ") + event + "() {\n" + body + "}\n";
}

inline void Script(GameObject* g, const std::string& acts, const char* event, const std::string& selfRoot = std::string()) {
    const std::string src = ActionScript(acts, event, selfRoot);
    if (src.empty()) return;
    g->AddComponent<ScriptComponent>("okayscript")->LoadSource(src);
}

/// A vector icon that stays visible on top of panels. A plain Image is drawn under every panel (so it can be a background), which would hide an
/// icon put on a card; a button is drawn above them, so the icon is a button that cannot be clicked, with no label and no fill.
inline GameObject* AddIconPicture(Scene& s, GameObject* parent, const char* name, const std::string& icon, UIAnchor an, Vec2 pos, float side, Color tint) {
    GameObject* g = Child(s, parent, name);
    auto* b = g->AddComponent<UIButton>();
    b->label.clear(); b->icon = "icon:" + icon; b->iconSize = side;
    b->anchor = an; b->position = {pos.x - 8.0f, pos.y}; b->size = {side + 16.0f, side};
    const Color none(0, 0, 0, 0);
    b->color = b->hoverColor = b->pressedColor = b->disabledColor = none;
    b->textColor = b->hoverTextColor = tint; b->borderWidth = 0.0f; b->shadow = false; b->hoverScale = 1.0f; b->pressOffset = 0.0f;
    b->shape = UIShape::Rectangle; b->cornerRadius = 0.0f; b->interactable = false; b->focusable = false;
    return g;
}

inline GameObject* BuildNode(Scene& s, GameObject* parent, const Node& n, const std::string& rootName, int variant) {
    using UITemplates::ButtonKind;
    const UIAnchor an = Anchor(n.Get("anchor"), UIAnchor::TopLeft);
    const Vec2 pos = Pair(n.Get("pos"), {0, 0}), size = Pair(n.Get("size"), {100, 40});
    GameObject* g = nullptr;
    const std::string& k = n.kind;
    if (k == "group") {
        g = Group(s, parent, n.name.c_str());
    } else if (k == "panel") {
        const float cr = n.Num("cr", 12.0f);
        g = Panel(s, parent, n.name.c_str(), an, pos, size, Role(n.Get("fill"), slate::Panel()), cr, n.Get("lift") == "1");
        auto* p = g->GetComponent<UIPanel>();
        if (n.Has("border")) p->borderWidth = n.Num("border", 0.0f);
        if (n.Get("shape") == "circle") p->shape = UIShape::Circle;
        else if (n.Get("shape") == "pill") p->shape = UIShape::Pill;
        if (n.Get("block") == "1") p->blocksPointer = true;
    } else if (k == "dim") {
        g = Dimmer(s, parent, n.Num("a", 0.55f));
        g->name = n.name;
        g->GetComponent<UIPanel>()->blocksPointer = true;
    } else if (k == "text" || k == "caption") {
        if (k == "caption") g = Caption(s, parent, n.name.c_str(), n.text, an, pos, size.x, Role(n.Get("color"), slate::Accent()));
        else {
            const int al = n.Get("align") == "c" ? 1 : n.Get("align") == "r" ? 2 : 0;
            g = Text(s, parent, n.name.c_str(), n.text, an, pos, size, n.Num("px", 16.0f), Role(n.Get("color"), slate::Text()), al, n.Get("bold") == "1");
            auto* tr = g->GetComponent<TextRenderer>();
            if (n.Get("wrap") == "1") { tr->wrap = true; tr->vcenter = false; }
            if (n.Get("up") == "1") { tr->uppercase = true; tr->letterSpacing = 1.0f; }
        }
    } else if (k == "button") {
        const std::string b = n.Get("btn", "primary");
        const ButtonKind bk = b == "secondary" ? ButtonKind::Secondary : b == "danger" ? ButtonKind::Danger : b == "ghost" ? ButtonKind::Ghost : ButtonKind::Primary;
        g = Button(s, parent, n.name.c_str(), n.text, an, pos, size, bk);
        auto* bt = g->GetComponent<UIButton>();
        if (n.Has("icon")) { bt->icon = "icon:" + n.Get("icon"); bt->iconSize = std::min(size.y - 12.0f, 24.0f); }
        if (n.Get("shape") == "pill") bt->shape = UIShape::Pill;
        if (n.Get("shape") == "circle") bt->shape = UIShape::Circle;
        if (n.Has("px")) bt->fontScale = n.Num("px", 16.0f) / 8.0f;
        Script(g, n.Get("act"), "on_click", rootName);
    } else if (k == "badge") {
        g = Badge(s, parent, n.name.c_str(), n.text, an, pos, Role(n.Get("color"), slate::Accent()));
    } else if (k == "avatar") {
        g = Avatar(s, parent, n.name.c_str(), n.text, an, pos, n.Num("d", 36.0f));
    } else if (k == "divider") {
        g = Divider(s, parent, an, pos, size.x);
        g->name = n.name;
    } else if (k == "bar") {
        g = Child(s, parent, n.name.c_str());
        auto* pb = g->AddComponent<UIProgressBar>();
        pb->anchor = an; pb->position = pos; pb->size = size; pb->value = n.Num("value", 0.5f);
        pb->background = slate::Field(); pb->fill = Role(n.Get("fill"), slate::Accent());
        pb->cornerRadius = std::min(size.y * 0.5f, 6.0f);
        if (n.Get("pct") == "1") pb->showPercent = true;
        if (n.Has("segments")) pb->segments = (int)n.Num("segments", 0);
    } else if (k == "slider") {
        g = Child(s, parent, n.name.c_str());
        auto* sl = g->AddComponent<UISlider>();
        sl->anchor = an; sl->position = pos; sl->size = size;
        sl->minValue = n.Num("min", 0); sl->maxValue = n.Num("max", 1); sl->value = n.Num("value", 0.5f); sl->step = n.Num("step", 0);
        sl->fill = slate::Accent(); sl->background = slate::Field();
        Script(g, n.Get("act"), "on_change", rootName);
    } else if (k == "toggle") {
        g = Child(s, parent, n.name.c_str());
        auto* tg = g->AddComponent<UIToggle>();
        tg->anchor = an; tg->position = pos; tg->size = n.Has("size") ? size : Vec2{44, 24};
        const std::string st = n.Get("style", "switch");
        tg->style = st == "check" ? UIToggle::Style::Checkbox : st == "radio" ? UIToggle::Style::Radio : UIToggle::Style::Switch;
        tg->label = n.text; tg->on = n.Get("on") == "1";
        if (st == "radio") tg->radioGroup = n.Get("group", rootName);
        Script(g, n.Get("act"), "on_toggle", rootName);
    } else if (k == "dropdown") {
        g = Child(s, parent, n.name.c_str());
        auto* dd = g->AddComponent<UIDropdown>();
        dd->anchor = an; dd->position = pos; dd->size = n.Has("size") ? size : Vec2{200, 32};
        dd->options = uilib::Split(n.Get("options"), '|'); dd->value = (int)n.Num("value", 0);
    } else if (k == "input") {
        g = Child(s, parent, n.name.c_str());
        auto* in = g->AddComponent<UIInputField>();
        in->anchor = an; in->position = pos; in->size = size; in->placeholder = n.Get("hint", "");
        in->color = slate::Field(); in->focusColor = slate::FieldHover(); in->borderWidth = 1.0f;
        if (n.Get("password") == "1") in->contentType = UIInputField::ContentType::Password;
    } else if (k == "tabs") {
        g = Child(s, parent, n.name.c_str());
        auto* tb = g->AddComponent<UITabs>();
        tb->anchor = an; tb->position = pos; tb->size = size;
        tb->tabs = uilib::Split(n.Get("tabs"), '|');
        for (const std::string& p : uilib::Split(n.Get("pages"), '|')) tb->pages.push_back(p.empty() ? p : rootName + " " + p);
        const std::string st = n.Get("style", "underline");
        tb->style = st == "pills" ? UITabs::Style::Pills : st == "vertical" ? UITabs::Style::Vertical : st == "segmented" ? UITabs::Style::Segmented : UITabs::Style::Underline;
        tb->shape = UIShape::Rounded;
        tb->sizing = n.Get("fit") == "1" ? UITabs::Sizing::FitText : UITabs::Sizing::Equal;
        tb->selected = slate::Accent(); tb->background = slate::Card();
    } else if (k == "icon") {
        g = AddIconPicture(s, parent, n.name.c_str(), n.text, an, pos, n.Has("size") ? size.x : 32.0f, Role(n.Get("color"), slate::Text()));
    }
    (void)variant;
    if (g && n.Get("page") == "1") g->name = rootName + " " + n.name;   // a tab page: UITabs finds it by this name
    if (g && n.Get("off") == "1") g->active = false;
    return g;
}

} // namespace detail3

/// Build screen `id` (variant 0 is the look as written; the others move the card: Left side, Right side, Dimmed backdrop).
/// Returns the root, or null for an unknown id. A UITheme, when given, restyles the result.
inline GameObject* Build(Scene& scene, const std::string& id, int variant = 0, UITheme* theme = nullptr) {
    using namespace detail3;
    const Def* d = Find(id);
    if (!d) return nullptr;
    if (!d->native.empty()) {
        for (const auto& nb : NativeBuilders())
            if (nb.first == d->native) {
                GameObject* r = nb.second(scene);
                if (r && theme) theme->ApplyTo(r);
                return r;
            }
        return nullptr;
    }
    GameObject* canvas = UITemplates::EnsureCanvas(scene);   // a Canvas (and an Event System) is made when the scene has none
    std::vector<Node> nodes;
    for (const std::string& line : d->body) {
        Node n;
        if (!ParseNode(line, n)) continue;
        nodes.push_back(n);
    }
    nodes = ExpandReps(nodes);
    std::vector<GameObject*> stack;
    GameObject* root = nullptr;
    const int vcount = VariantCount(*d);
    variant = std::max(0, std::min(variant, vcount - 1));
    for (std::size_t k = 0; k < nodes.size(); ++k) {
        Node n = nodes[k];
        if (n.Has("v")) {
            bool in = false;
            for (const std::string& x : uilib::Split(n.Get("v"), ',')) if (std::atoi(x.c_str()) == variant) in = true;
            if (!in) {   // skip this node and everything under it
                while (k + 1 < nodes.size() && nodes[k + 1].depth > n.depth) ++k;
                continue;
            }
        }
        GameObject* parent = n.depth == 0 ? canvas : (n.depth - 1 < (int)stack.size() ? stack[(std::size_t)n.depth - 1] : canvas);
        if (n.Get("card") == "1" && variant > 0 && variant < 3) {
            // Left / Right side: the card moves to the edge of the screen.
            n.a["anchor"] = variant == 1 ? "midleft" : "midright";
            n.a["pos"] = variant == 1 ? "32,0" : "-32,0";
        }
        GameObject* g = BuildNode(scene, parent, n, d->name, variant);
        if (!g) continue;
        if (n.depth == 0 && !root) {
            root = g;
            root->name = d->name;
            if (d->startsHidden) root->active = false;
        }
        stack.resize((std::size_t)n.depth + 1);
        stack[(std::size_t)n.depth] = g;
    }
    if (root && theme) theme->ApplyTo(root);
    return root;
}

/// "Starter Pack": a main menu, settings, game HUD, pause menu and game over screen in one scene, already joined by working buttons
/// (Play opens the HUD, Pause opens the pause menu, Give up opens Game Over, Try again goes back to the game...). `theme` is a library
/// theme name ("" keeps the scene's). Returns the Main Menu root. Only the Main Menu is visible at the start.
inline GameObject* BuildStarterPack(Scene& scene, const std::string& theme = std::string()) {
    GameObject* first = nullptr;
    for (const char* id : {"main_menu", "settings", "hud_shooter", "pause_menu", "game_over"}) {
        GameObject* g = Build(scene, id, 0);
        if (!first) first = g;
        else if (g) g->active = false;
    }
    if (!theme.empty()) UIStyleLib::SetTheme(scene, theme, false);
    return first;
}

} } // namespace okay::UIScreens
