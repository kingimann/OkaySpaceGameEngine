#include "okay/Components/UIDocument.hpp"
#include "okay/Scene/GameObject.hpp"
#include "okay/Scene/Scene.hpp"
#include "okay/Scene/Transform.hpp"
#include "okay/Components/UIButton.hpp"
#include "okay/Components/UIPanel.hpp"
#include "okay/Components/UIImage.hpp"
#include "okay/Components/UISlider.hpp"
#include "okay/Components/UIToggle.hpp"
#include "okay/Components/UIProgressBar.hpp"
#include "okay/Components/UIInputField.hpp"
#include "okay/Components/UIDropdown.hpp"
#include "okay/Components/UITooltip.hpp"
#include "okay/Components/UIScrollView.hpp"
#include "okay/Components/UILayoutGroup.hpp"
#include "okay/Components/UITextBind.hpp"
#include "okay/Components/TextRenderer.hpp"
#include "okay/Components/UIAnchor.hpp"
#include "okay/Components/ScriptComponent.hpp"

#include <sstream>
#include <vector>
#include <string>
#include <cctype>
#include <algorithm>

namespace okay {
namespace {

// Trim trailing carriage return / spaces; return leading-space (indent) count.
int IndentOf(const std::string& line) {
    int n = 0;
    while (n < (int)line.size() && (line[n] == ' ' || line[n] == '\t')) ++n;
    return n;
}

// Resolve one pos/size component. A trailing '%' makes it a fraction of the
// given axis length (canvas width for x, height for y) — `size=50%,40` is half
// the canvas wide, 40px tall — so layouts adapt to the window.
float ParseComponent(const std::string& s, float axis) {
    if (!s.empty() && s.back() == '%')
        return (float)std::atof(s.c_str()) / 100.0f * axis;
    return (float)std::atof(s.c_str());
}

Vec2 ParsePair(const std::string& v, float defY) {
    Vec2 out{0.0f, defY};
    float cw = UICanvas::Width(), ch = UICanvas::Height();
    std::size_t comma = v.find(',');
    if (comma == std::string::npos) { out.x = ParseComponent(v, cw); return out; }
    out.x = ParseComponent(v.substr(0, comma), cw);
    out.y = ParseComponent(v.substr(comma + 1), ch);
    return out;
}

Color ParseColor(const std::string& v) {
    int c[4] = {255, 255, 255, 255};
    std::stringstream ss(v); std::string tok; int i = 0;
    while (i < 4 && std::getline(ss, tok, ',')) c[i++] = std::atoi(tok.c_str());
    return Color::FromBytes((std::uint8_t)c[0], (std::uint8_t)c[1],
                            (std::uint8_t)c[2], (std::uint8_t)c[3]);
}

UIAnchor ParseAnchor(const std::string& v) {
    if (v == "topcenter")    return UIAnchor::TopCenter;
    if (v == "topright")     return UIAnchor::TopRight;
    if (v == "middleleft")   return UIAnchor::MiddleLeft;
    if (v == "center")       return UIAnchor::Center;
    if (v == "middleright")  return UIAnchor::MiddleRight;
    if (v == "bottomleft")   return UIAnchor::BottomLeft;
    if (v == "bottomcenter") return UIAnchor::BottomCenter;
    if (v == "bottomright")  return UIAnchor::BottomRight;
    return UIAnchor::TopLeft;
}

// One parsed line: a widget type, an optional quoted label, and key=value props.
struct Token {
    std::string type;
    std::string label;
    std::vector<std::pair<std::string, std::string>> props;
    std::string Get(const std::string& k, const std::string& def = "") const {
        for (auto& p : props) if (p.first == k) return p.second;
        return def;
    }
    bool Has(const std::string& k) const {
        for (auto& p : props) if (p.first == k) return true;
        return false;
    }
};

// Tokenize a single trimmed markup line into type + label + key=value pairs.
// A bare value may be quoted; everything after `onclick=` is taken verbatim so
// script snippets can contain spaces and quotes.
Token Lex(const std::string& line) {
    Token t;
    std::size_t i = 0, n = line.size();
    auto skipws = [&] { while (i < n && std::isspace((unsigned char)line[i])) ++i; };

    skipws();
    while (i < n && !std::isspace((unsigned char)line[i])) t.type += line[i++];

    skipws();
    if (i < n && line[i] == '"') {        // optional quoted label
        ++i;
        while (i < n && line[i] != '"') t.label += line[i++];
        if (i < n) ++i;                   // closing quote
    }

    while (true) {
        skipws();
        if (i >= n) break;
        std::string key;
        while (i < n && line[i] != '=' && !std::isspace((unsigned char)line[i])) key += line[i++];
        if (key.empty()) break;
        std::string val;
        if (i < n && line[i] == '=') {
            ++i;
            // Event handlers (onclick/onchange/ontoggle/onsubmit) take the rest of
            // the line verbatim so script snippets keep their spaces/quotes — so a
            // callback must be the last key on its line.
            if (key.size() > 2 && key[0] == 'o' && key[1] == 'n') {
                val = line.substr(i);
                i = n;
            } else if (i < n && line[i] == '"') {
                ++i;
                while (i < n && line[i] != '"') val += line[i++];
                if (i < n) ++i;
            } else {
                while (i < n && !std::isspace((unsigned char)line[i])) val += line[i++];
            }
        }
        t.props.emplace_back(key, val);
    }
    return t;
}

bool  ParseBool(const std::string& v)  { return v == "1" || v == "true" || v == "yes" || v == "on"; }
float ParseF(const std::string& v, float def = 0.0f) { return v.empty() ? def : (float)std::atof(v.c_str()); }

// Split a `|`-separated list (used for dropdown options, which may contain
// spaces: options=Low|Medium|High).
std::vector<std::string> SplitPipes(const std::string& v) {
    std::vector<std::string> out; std::string cur;
    for (char ch : v) { if (ch == '|') { out.push_back(cur); cur.clear(); } else cur += ch; }
    if (!cur.empty() || !v.empty()) out.push_back(cur);
    return out;
}

// Wire a `<event>=<okayscript>` handler onto the GameObject's script component
// (creating it if needed) as `function <fn>() { ... }`.
void Handler(GameObject* go, const Token& t, const char* key, const char* fn) {
    if (!t.Has(key)) return;
    auto* sc = go->GetComponent<ScriptComponent>();
    if (!sc) sc = go->AddComponent<ScriptComponent>("okayscript");
    std::string src = sc->Source();
    src += "function " + std::string(fn) + "() { " + t.Get(key) + " }\n";
    sc->LoadSource(src);
}

// Build the widget GameObject for one token. `offset` shifts the widget's pixel
// position (used so a custom-widget instance moves as a whole).
GameObject* Spawn(Scene& scene, const Token& t, Vec2 offset) {
    GameObject* go = scene.CreateGameObject(t.type.empty() ? "UIElement" : t.type);

    auto applyBox = [&](Vec2& pos, Vec2& size, UIAnchor& anchor, Color* color) {
        if (t.Has("pos"))    pos    = ParsePair(t.Get("pos"), pos.y);
        if (t.Has("size"))   size   = ParsePair(t.Get("size"), size.y);
        if (t.Has("anchor")) anchor = ParseAnchor(t.Get("anchor"));
        if (color && t.Has("color")) *color = ParseColor(t.Get("color"));
        pos = pos + offset;
    };

    if (t.type == "panel") {
        auto* c = go->AddComponent<UIPanel>();
        applyBox(c->position, c->size, c->anchor, &c->color);
        if (t.Has("corner")) c->cornerRadius = ParseF(t.Get("corner"));
        if (t.Has("border")) c->borderWidth  = ParseF(t.Get("border"));
        if (t.Has("bordercolor")) c->borderColor = ParseColor(t.Get("bordercolor"));
        if (t.Has("gradient")) { c->useGradient = true; c->colorBottom = ParseColor(t.Get("gradient")); }
    } else if (t.type == "button") {
        auto* c = go->AddComponent<UIButton>();
        if (!t.label.empty()) c->label = t.label;
        applyBox(c->position, c->size, c->anchor, &c->color);
        if (t.Has("hover"))     c->hoverColor   = ParseColor(t.Get("hover"));
        if (t.Has("pressed"))   c->pressedColor = ParseColor(t.Get("pressed"));
        if (t.Has("textcolor")) c->textColor    = ParseColor(t.Get("textcolor"));
        if (t.Has("corner"))    c->cornerRadius = ParseF(t.Get("corner"));
        if (t.Has("font"))      c->fontScale    = ParseF(t.Get("font"), 2.0f);
        if (t.Has("border"))    c->borderWidth  = ParseF(t.Get("border"));
        if (t.Has("bordercolor")) c->borderColor = ParseColor(t.Get("bordercolor"));
        if (t.Has("icon"))     { c->icon = t.Get("icon"); if (!t.Has("iconsize")) c->iconSize = 24.0f; }
        if (t.Has("iconsize"))   c->iconSize = ParseF(t.Get("iconsize"));
        if (t.Has("bind")) { auto* bd = go->AddComponent<UITextBind>(); bd->format = t.Get("bind"); c->label = UITextBind::Resolve(bd->format); }
        Handler(go, t, "onclick", "on_click");
    } else if (t.type == "image") {
        auto* c = go->AddComponent<UIImage>();
        applyBox(c->position, c->size, c->anchor, &c->color);
        if (t.Has("texture"))  c->texture = t.Get("texture");
        if (t.Has("corner"))   c->cornerRadius = ParseF(t.Get("corner"));
        if (t.Has("nineslice")){ c->nineSlice = ParseBool(t.Get("nineslice")); if (t.Has("border")) c->border = ParseF(t.Get("border")); }
        if (t.Has("fill")) {     // fill=right|left|up|down
            std::string f = t.Get("fill");
            c->fillMode = f == "left" ? UIImage::FillMode::Left : f == "up" ? UIImage::FillMode::Up
                        : f == "down" ? UIImage::FillMode::Down : UIImage::FillMode::Right;
            if (t.Has("amount")) c->fillAmount = ParseF(t.Get("amount"), 1.0f);
        }
    } else if (t.type == "slider") {
        auto* c = go->AddComponent<UISlider>();
        applyBox(c->position, c->size, c->anchor, nullptr);
        if (t.Has("value")) c->value = ParseF(t.Get("value"), 0.5f);
        if (t.Has("min"))   c->minValue = ParseF(t.Get("min"));
        if (t.Has("max"))   c->maxValue = ParseF(t.Get("max"), 1.0f);
        if (t.Has("fill"))  c->fill = ParseColor(t.Get("fill"));
        if (t.Has("knob"))  c->knob = ParseColor(t.Get("knob"));
        if (t.Has("corner")) c->cornerRadius = ParseF(t.Get("corner"));
        if (t.Has("showvalue")) c->showValue = ParseBool(t.Get("showvalue"));
        Handler(go, t, "onchange", "on_change");
    } else if (t.type == "toggle") {
        auto* c = go->AddComponent<UIToggle>();
        if (!t.label.empty()) c->label = t.label;
        applyBox(c->position, c->size, c->anchor, nullptr);
        if (t.Has("on")) c->on = ParseBool(t.Get("on"));
        if (t.Has("corner")) c->cornerRadius = ParseF(t.Get("corner"));
        if (t.Has("check"))  c->checkColor = ParseColor(t.Get("check"));
        Handler(go, t, "ontoggle", "on_toggle");
    } else if (t.type == "progress") {
        auto* c = go->AddComponent<UIProgressBar>();
        applyBox(c->position, c->size, c->anchor, nullptr);
        if (t.Has("value")) c->value = ParseF(t.Get("value"), 1.0f);
        if (t.Has("fill"))  c->fill = ParseColor(t.Get("fill"));
        if (t.Has("corner")) c->cornerRadius = ParseF(t.Get("corner"));
        if (t.Has("percent")) c->showPercent = ParseBool(t.Get("percent"));
    } else if (t.type == "input") {
        auto* c = go->AddComponent<UIInputField>();
        applyBox(c->position, c->size, c->anchor, &c->color);
        if (!t.label.empty()) c->text = t.label;
        if (t.Has("placeholder")) c->placeholder = t.Get("placeholder");
        if (t.Has("max")) c->maxLength = std::atoi(t.Get("max").c_str());
        if (t.Has("type")) {   // content type: integer|decimal|password
            std::string ct = t.Get("type");
            c->contentType = ct == "integer" ? UIInputField::ContentType::Integer
                           : ct == "decimal" ? UIInputField::ContentType::Decimal
                           : ct == "password" ? UIInputField::ContentType::Password
                           : UIInputField::ContentType::Standard;
        }
        if (t.Has("bind")) { auto* bd = go->AddComponent<UITextBind>(); bd->format = t.Get("bind"); c->text = UITextBind::Resolve(bd->format); }
        Handler(go, t, "onsubmit", "on_submit");
    } else if (t.type == "dropdown") {
        auto* c = go->AddComponent<UIDropdown>();
        applyBox(c->position, c->size, c->anchor, &c->color);
        if (t.Has("options")) c->options = SplitPipes(t.Get("options"));
        if (t.Has("value")) c->value = std::atoi(t.Get("value").c_str());
        if (t.Has("placeholder")) c->placeholder = t.Get("placeholder");
        Handler(go, t, "onchange", "on_change");
    } else if (t.type == "scroll") {
        auto* c = go->AddComponent<UIScrollView>();
        applyBox(c->position, c->size, c->anchor, &c->background);
        if (t.Has("content")) c->contentHeight = ParseF(t.Get("content"));
        if (t.Has("bar"))     c->barColor = ParseColor(t.Get("bar"));
    } else if (t.type == "layout") {
        auto* c = go->AddComponent<UILayoutGroup>();
        if (t.Has("dir")) c->direction = (t.Get("dir") == "horizontal")
                              ? UILayoutGroup::Direction::Horizontal : UILayoutGroup::Direction::Vertical;
        if (t.Has("anchor"))  c->anchor  = ParseAnchor(t.Get("anchor"));
        if (t.Has("pos"))     c->origin  = ParsePair(t.Get("pos"), c->origin.y) + offset;
        if (t.Has("spacing")) c->spacing = ParseF(t.Get("spacing"));
        if (t.Has("padding")) c->padding = ParseF(t.Get("padding"));
    } else { // text (default)
        auto* c = go->AddComponent<TextRenderer>();
        c->screenSpace = true;
        if (!t.label.empty()) c->text = t.label;
        if (t.Has("pos"))    c->screenPos = ParsePair(t.Get("pos"), c->screenPos.y);
        if (t.Has("size"))   c->pixelSize = ParsePair(t.Get("size"), 0).x;
        if (t.Has("anchor")) c->anchor = ParseAnchor(t.Get("anchor"));
        if (t.Has("color"))  c->color = ParseColor(t.Get("color"));
        if (t.Has("align"))  { std::string a = t.Get("align"); c->align = a == "center" ? 1 : a == "right" ? 2 : 0; }
        if (t.Has("outline")){ c->outline = true; c->outlineColor = ParseColor(t.Get("outline")); }
        if (t.Has("shadow")) { c->shadow = true; c->shadowColor = ParseColor(t.Get("shadow")); }
        if (t.Has("bold"))   c->bold = ParseBool(t.Get("bold"));
        c->screenPos = c->screenPos + offset;
        // Data binding: `bind="Score: {score}"` keeps the label in sync with the
        // named Prefs values each frame (set them with prefs_set from script).
        if (t.Has("bind")) {
            auto* bind = go->AddComponent<UITextBind>();
            bind->format = t.Get("bind");
            if (t.label.empty()) c->text = UITextBind::Resolve(bind->format);
        }
    }

    // Any widget can carry a hover tooltip via `tooltip="..."`.
    if (t.Has("tooltip")) {
        auto* tip = go->AddComponent<UITooltip>();
        tip->text = t.Get("tooltip");
        if (t.Has("tipdelay")) tip->delay = ParseF(t.Get("tipdelay"), 0.5f);
    }
    // `name=` makes the widget addressable by the ui_* script API (ui_set_text,
    // ui_slider_value, …); `active=0` starts it hidden.
    if (t.Has("name") && !t.Get("name").empty()) go->name = t.Get("name");
    if (t.Has("active")) go->active = ParseBool(t.Get("active"));
    return go;
}

// One parsed markup line: its indentation depth, source line number, and token.
struct Line { int indent; int lineNo; Token tok; };

// The widget types the toolkit understands (besides `style`/`define`/instances).
bool KnownType(const std::string& t) {
    static const char* k[] = {"panel","text","button","image","slider","toggle",
        "progress","input","dropdown","scroll","layout"};
    for (auto* s : k) if (t == s) return true;
    return false;
}

// Every property key the parser reads (across all widget types). A key outside
// this set on a widget line is almost certainly a typo, so it's reported.
bool KnownKey(const std::string& k) {
    static const char* keys[] = {
        "pos","size","color","anchor","class","corner","tooltip","tipdelay","name","active",
        "border","bordercolor","gradient","hover","pressed","textcolor","font",
        "align","outline","shadow","bold","bind","value","min","max","fill","knob","knobsize",
        "showvalue","on","check","percent","texture","nineslice","amount","placeholder",
        "options","content","bar","dir","spacing","padding","type","icon","iconsize",
        "onclick","onchange","ontoggle","onsubmit"};
    for (auto* s : keys) if (k == s) return true;
    return false;
}

// The name of a style/define line is its first bare word (e.g. `style primary`
// or `define card`) — the first prop key that carries no value.
std::string DeclName(const Token& t) {
    if (!t.label.empty()) return t.label;
    for (auto& p : t.props) if (p.second.empty()) return p.first;
    return "";
}

// A reusable style (USS-like): a named bag of properties widgets pull in via
// `class=<name>`. A custom widget (`define`): a captured block of lines that an
// instance line expands, shifted to the instance's position.
using StyleMap  = std::vector<std::pair<std::string, std::vector<std::pair<std::string, std::string>>>>;
struct Define   { std::vector<Line> body; };
using DefineMap = std::vector<std::pair<std::string, Define>>;

const std::vector<std::pair<std::string, std::string>>*
FindStyle(const StyleMap& m, const std::string& name) {
    for (auto& p : m) if (p.first == name) return &p.second;
    return nullptr;
}
const Define* FindDefine(const DefineMap& m, const std::string& name) {
    for (auto& p : m) if (p.first == name) return &p.second;
    return nullptr;
}

// Merge a style's props under a widget's own props (the widget wins), so
// `class=primary color=red` overrides just the color of the `primary` style.
Token ApplyClass(const Token& t, const StyleMap& styles) {
    if (!t.Has("class")) return t;
    const auto* sp = FindStyle(styles, t.Get("class"));
    if (!sp) return t;
    Token out;
    out.type = t.type; out.label = t.label;
    out.props = *sp;                          // style defaults first
    for (auto& p : t.props) {                 // widget overrides
        if (p.first == "class") continue;
        bool replaced = false;
        for (auto& q : out.props) if (q.first == p.first) { q.second = p.second; replaced = true; break; }
        if (!replaced) out.props.push_back(p);
    }
    return out;
}

Vec2 TokenPos(const Token& t) {
    return t.Has("pos") ? ParsePair(t.Get("pos"), 0.0f) : Vec2{0.0f, 0.0f};
}

// Replace every `$name` occurrence in `s` with the matching instance argument
// (longest names first so `$titlebar` isn't clobbered by `$title`).
std::string SubstParams(std::string s, const std::vector<std::pair<std::string, std::string>>& params) {
    std::vector<const std::pair<std::string, std::string>*> ordered;
    for (auto& p : params) ordered.push_back(&p);
    std::sort(ordered.begin(), ordered.end(),
              [](auto* a, auto* b) { return a->first.size() > b->first.size(); });
    for (auto* p : ordered) {
        std::string needle = "$" + p->first;
        std::size_t pos = 0;
        while ((pos = s.find(needle, pos)) != std::string::npos) {
            s.replace(pos, needle.size(), p->second);
            pos += p->second.size();
        }
    }
    return s;
}

// A copy of a define's body with `$param` placeholders substituted in every
// token's label and property values, so custom widgets take arguments:
//   define card
//     panel ...
//     text "$title" pos=10,10
//   card title="Hello" pos=40,40
std::vector<Line> InstantiateBody(const std::vector<Line>& body,
                                  const std::vector<std::pair<std::string, std::string>>& params) {
    std::vector<Line> out = body;
    for (Line& ln : out) {
        ln.tok.label = SubstParams(ln.tok.label, params);
        for (auto& pr : ln.tok.props) pr.second = SubstParams(pr.second, params);
    }
    return out;
}

// Recursively build a forest of widget lines under `parentT`. `i` walks the
// shared line list; everything with indent > parentIndent is a child here.
// `offset` accumulates custom-widget instance positions.
void BuildForest(Scene& scene, const std::vector<Line>& lines, std::size_t& i,
                 int parentIndent, Transform* parentT, Vec2 offset,
                 const StyleMap& styles, const DefineMap& defines,
                 std::vector<GameObject*>& out) {
    while (i < lines.size() && lines[i].indent > parentIndent) {
        int myIndent = lines[i].indent;
        Token tok = lines[i].tok;
        ++i;

        if (const Define* def = FindDefine(defines, tok.type)) {
            // Instantiate a custom widget: a group holding a shifted copy of its
            // defined body. Any children on the instance line are ignored.
            GameObject* group = scene.CreateGameObject(tok.type);
            group->transform->SetParent(parentT, /*worldPositionStays=*/false);
            out.push_back(group);
            Vec2 instOffset = offset + TokenPos(tok);
            // Instance args = every prop except pos, substituted as $name in body.
            std::vector<std::pair<std::string, std::string>> params;
            for (auto& p : tok.props) if (p.first != "pos") params.push_back(p);
            std::vector<Line> body = InstantiateBody(def->body, params);
            std::size_t j = 0;
            BuildForest(scene, body, j, /*parentIndent*/ body.empty() ? -1 : body.front().indent - 1,
                        group->transform, instOffset, styles, defines, out);
            while (i < lines.size() && lines[i].indent > myIndent) ++i;
        } else {
            Token eff = ApplyClass(tok, styles);
            GameObject* go = Spawn(scene, eff, offset);
            go->transform->SetParent(parentT, /*worldPositionStays=*/false);
            out.push_back(go);
            BuildForest(scene, lines, i, myIndent, go->transform, offset, styles, defines, out);
            // A layout group lays its just-built children out by size + spacing.
            if (auto* lg = go->GetComponent<UILayoutGroup>()) lg->Arrange();
        }
    }
}

} // namespace

void UIDocument::ClearGenerated() {
    if (!gameObject || !gameObject->scene()) { m_generated.clear(); return; }
    Scene* s = gameObject->scene();
    for (GameObject* go : m_generated) if (go) s->Destroy(go);
    m_generated.clear();
}

void UIDocument::Rebuild() {
    if (!gameObject || !gameObject->scene()) return;
    Scene& scene = *gameObject->scene();
    ClearGenerated();

    // Pass 1: lex every line, pulling out top-level `style` rules and `define`
    // blocks (USS-like classes + reusable custom widgets) into lookup tables.
    StyleMap  styles;
    DefineMap defines;
    std::vector<Line> build;   // the widget lines left to build

    std::stringstream ss(markup);
    std::string line;
    std::vector<Line> all;
    int lineNo = 0;
    while (std::getline(ss, line)) {
        ++lineNo;
        if (!line.empty() && line.back() == '\r') line.pop_back();
        int indent = IndentOf(line);
        std::string body = line.substr(indent);
        if (body.empty() || body[0] == '#') continue;   // blank or comment
        Token t = Lex(body);
        if (t.type.empty()) continue;
        all.push_back({indent, lineNo, t});
    }

    for (std::size_t k = 0; k < all.size();) {
        const Line& ln = all[k];
        if (ln.tok.type == "style") {
            std::string nm = DeclName(ln.tok);
            std::vector<std::pair<std::string, std::string>> props;
            for (auto& p : ln.tok.props) if (p.first != nm) props.push_back(p);
            if (!nm.empty()) styles.push_back({nm, props});
            ++k;
        } else if (ln.tok.type == "define") {
            std::string nm = DeclName(ln.tok);
            Define def;
            std::size_t j = k + 1;
            while (j < all.size() && all[j].indent > ln.indent) { def.body.push_back(all[j]); ++j; }
            if (!nm.empty()) defines.push_back({nm, std::move(def)});
            k = j;
        } else {
            build.push_back(ln);
            ++k;
        }
    }

    // Validation: flag unknown widget types and unrecognized keys (typos) with
    // line numbers, so the editor can surface them. Declarations (style/define)
    // and custom-widget instances are exempt from the widget checks.
    m_diagnostics.clear();
    for (const Line& ln : all) {
        const std::string& ty = ln.tok.type;
        if (ty == "style" || ty == "define") continue;
        bool instance = FindDefine(defines, ty) != nullptr;
        if (!instance && !KnownType(ty)) {
            m_diagnostics.push_back("line " + std::to_string(ln.lineNo) +
                                    ": unknown widget '" + ty + "'");
            continue;   // keys are meaningless without a known type
        }
        if (instance) continue;   // instance lines only use pos
        for (const auto& p : ln.tok.props)
            if (!KnownKey(p.first))
                m_diagnostics.push_back("line " + std::to_string(ln.lineNo) +
                                        ": unknown key '" + p.first + "' on " + ty);
    }

    // Pass 2: build the remaining widgets as a nested forest.
    std::size_t i = 0;
    BuildForest(scene, build, i, /*parentIndent*/ -1, gameObject->transform,
                Vec2{0.0f, 0.0f}, styles, defines, m_generated);
}

} // namespace okay
