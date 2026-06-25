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
#include <cstring>
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

// ===========================================================================
//  HTML / CSS authoring mode
//  ---------------------------------------------------------------------------
//  As an alternative to the line-based OkayUI markup, a document may be written
//  as a small HTML subset with a <style> block of CSS. It's parsed into the same
//  Token model and built by the same Spawn()/widget backend, so it renders in the
//  editor, player, web and mobile builds identically — no browser embedded.
//  Tags map to widgets (div/panel, button, p/span/h1/label/text, a, img, input,
//  select, progress); CSS (tag, .class, #id, inline style="") maps to widget
//  properties; onclick="..." runs OkayScript, exactly like the markup `onclick=`.
//  (These helpers live in the same anonymous namespace as Spawn()/Token above.)
// ===========================================================================

struct HtmlNode {
    bool isText = false;
    std::string text;                                   // text-node content
    std::string tag;                                    // lowercased element name
    std::vector<std::pair<std::string, std::string>> attrs;
    std::vector<HtmlNode> kids;
    std::string attr(const std::string& k) const { for (auto& a : attrs) if (a.first == k) return a.second; return ""; }
    bool has(const std::string& k) const { for (auto& a : attrs) if (a.first == k) return true; return false; }
};

std::string Lower(std::string s) { for (char& c : s) c = (char)std::tolower((unsigned char)c); return s; }

// Decode the common HTML entities so e.g. onclick="f(&quot;x&quot;)" works inside a
// double-quoted attribute (and &amp; &lt; &gt; &#39; in text/labels).
std::string DecodeEntities(const std::string& s) {
    if (s.find('&') == std::string::npos) return s;
    struct E { const char* k; char v; };
    static const E ents[] = {{"&quot;", '"'}, {"&apos;", '\''}, {"&#39;", '\''},
                             {"&lt;", '<'}, {"&gt;", '>'}, {"&amp;", '&'}};
    std::string out; out.reserve(s.size());
    for (std::size_t i = 0; i < s.size();) {
        bool hit = false;
        if (s[i] == '&')
            for (const E& e : ents) {
                std::size_t n = std::strlen(e.k);
                if (s.compare(i, n, e.k) == 0) { out += e.v; i += n; hit = true; break; }
            }
        if (!hit) out += s[i++];
    }
    return out;
}
std::string TrimStr(const std::string& s) {
    std::size_t a = s.find_first_not_of(" \t\r\n");
    if (a == std::string::npos) return "";
    std::size_t b = s.find_last_not_of(" \t\r\n");
    return s.substr(a, b - a + 1);
}
bool HtmlVoid(const std::string& t) {
    static const char* v[] = {"img", "input", "br", "hr", "meta", "link"};
    for (auto* s : v) if (t == s) return true;
    return false;
}

// Parse the attributes inside a start tag (after the tag name, up to '>').
void ParseAttrs(const std::string& s, std::vector<std::pair<std::string, std::string>>& out) {
    std::size_t i = 0, n = s.size();
    auto ws = [&] { while (i < n && std::isspace((unsigned char)s[i])) ++i; };
    while (true) {
        ws();
        if (i >= n) break;
        std::string key;
        while (i < n && s[i] != '=' && !std::isspace((unsigned char)s[i]) && s[i] != '/') key += s[i++];
        if (key.empty()) { if (i < n) ++i; continue; }
        std::string val;
        ws();
        if (i < n && s[i] == '=') {
            ++i; ws();
            if (i < n && (s[i] == '"' || s[i] == '\'')) {
                char q = s[i++];
                while (i < n && s[i] != q) val += s[i++];
                if (i < n) ++i;
            } else {
                while (i < n && !std::isspace((unsigned char)s[i]) && s[i] != '/') val += s[i++];
            }
        }
        out.emplace_back(Lower(key), DecodeEntities(val));
    }
}

// Recursive-descent HTML parse. Stops when it sees `</until>` (consumed). Any
// <style>...</style> content is appended to `styleOut` and not treated as nodes.
void ParseNodes(const std::string& s, std::size_t& i, const std::string& until,
                std::vector<HtmlNode>& out, std::string& styleOut) {
    std::size_t n = s.size();
    while (i < n) {
        if (s[i] == '<') {
            if (s.compare(i, 4, "<!--") == 0) {            // comment
                std::size_t e = s.find("-->", i + 4);
                i = (e == std::string::npos) ? n : e + 3;
                continue;
            }
            if (i + 1 < n && s[i + 1] == '/') {            // closing tag </x>
                std::size_t e = s.find('>', i);
                std::string close = Lower(TrimStr(s.substr(i + 2, (e == std::string::npos ? n : e) - (i + 2))));
                i = (e == std::string::npos) ? n : e + 1;
                if (until.empty() || close == until) return;  // pop to parent
                continue;                                   // stray close: ignore
            }
            std::size_t e = s.find('>', i);
            if (e == std::string::npos) { i = n; break; }
            std::string inner = s.substr(i + 1, e - i - 1);
            bool selfClose = (!inner.empty() && inner.back() == '/');
            if (selfClose) inner.pop_back();
            i = e + 1;
            std::size_t sp = 0; while (sp < inner.size() && !std::isspace((unsigned char)inner[sp])) ++sp;
            HtmlNode node; node.tag = Lower(inner.substr(0, sp));
            if (sp < inner.size()) ParseAttrs(inner.substr(sp), node.attrs);
            if (node.tag == "style") {                      // capture raw CSS
                std::size_t e2 = s.find("</style", i);
                styleOut += s.substr(i, (e2 == std::string::npos ? n : e2) - i);
                std::size_t e3 = (e2 == std::string::npos) ? n : s.find('>', e2);
                i = (e3 == std::string::npos) ? n : e3 + 1;
                continue;
            }
            if (!selfClose && !HtmlVoid(node.tag))
                ParseNodes(s, i, node.tag, node.kids, styleOut);
            out.push_back(std::move(node));
        } else {                                            // text run
            std::size_t e = s.find('<', i);
            std::string txt = TrimStr(s.substr(i, (e == std::string::npos ? n : e) - i));
            if (!txt.empty()) { HtmlNode tn; tn.isText = true; tn.text = DecodeEntities(txt); out.push_back(std::move(tn)); }
            i = (e == std::string::npos) ? n : e;
        }
    }
}

// A CSS rule: one selector + its declarations.
struct CssRule { std::string sel; std::vector<std::pair<std::string, std::string>> decls; };

std::vector<CssRule> ParseCss(const std::string& css) {
    std::vector<CssRule> rules;
    std::size_t i = 0, n = css.size();
    while (i < n) {
        std::size_t open = css.find('{', i);
        if (open == std::string::npos) break;
        std::size_t close = css.find('}', open);
        if (close == std::string::npos) break;
        std::string sels = css.substr(i, open - i);
        std::string body = css.substr(open + 1, close - open - 1);
        std::vector<std::pair<std::string, std::string>> decls;
        std::stringstream ds(body); std::string d;
        while (std::getline(ds, d, ';')) {
            std::size_t c = d.find(':');
            if (c == std::string::npos) continue;
            decls.emplace_back(Lower(TrimStr(d.substr(0, c))), TrimStr(d.substr(c + 1)));
        }
        std::stringstream ss(sels); std::string one;          // selector list: a, .b, #c
        while (std::getline(ss, one, ',')) {
            std::string sel = TrimStr(one);
            if (!sel.empty()) rules.push_back({Lower(sel), decls});
        }
        i = close + 1;
    }
    return rules;
}

// CSS color (#rgb, #rrggbb, rgb()/rgba(), or a few names) -> "r,g,b[,a]".
std::string CssColor(const std::string& vin) {
    std::string v = TrimStr(vin);
    auto hex = [](char c) -> int {
        if (c >= '0' && c <= '9') return c - '0';
        c = (char)std::tolower((unsigned char)c);
        if (c >= 'a' && c <= 'f') return 10 + c - 'a';
        return 0;
    };
    if (!v.empty() && v[0] == '#') {
        std::string h = v.substr(1);
        int r = 0, g = 0, b = 0, a = 255;
        if (h.size() >= 6) { r = hex(h[0])*16+hex(h[1]); g = hex(h[2])*16+hex(h[3]); b = hex(h[4])*16+hex(h[5]); if (h.size() >= 8) a = hex(h[6])*16+hex(h[7]); }
        else if (h.size() >= 3) { r = hex(h[0])*17; g = hex(h[1])*17; b = hex(h[2])*17; }
        return std::to_string(r)+","+std::to_string(g)+","+std::to_string(b)+","+std::to_string(a);
    }
    if (v.rfind("rgb", 0) == 0) {
        std::size_t p = v.find('('); std::size_t q = v.find(')');
        if (p != std::string::npos && q != std::string::npos) {
            std::string inside = v.substr(p + 1, q - p - 1);
            for (char& c : inside) if (c == ' ') c = ' ';
            std::stringstream ss(inside); std::string t; std::string out; int k = 0;
            while (std::getline(ss, t, ',') && k < 4) { if (k) out += ","; out += std::to_string((int)std::atof(TrimStr(t).c_str()) ); ++k; }
            return out;
        }
    }
    struct N { const char* k; const char* v; };
    static const N named[] = {
        {"white","255,255,255,255"},{"black","0,0,0,255"},{"red","220,60,60,255"},
        {"green","60,200,90,255"},{"blue","70,120,220,255"},{"yellow","240,210,80,255"},
        {"gray","128,128,128,255"},{"grey","128,128,128,255"},{"orange","240,150,50,255"},
        {"transparent","0,0,0,0"}};
    for (auto& nm : named) if (v == nm.k) return nm.v;
    return "255,255,255,255";
}

// Strip a CSS unit ("12px", "50%" -> kept for %); returns the numeric/percent text.
std::string CssLen(const std::string& vin) {
    std::string v = TrimStr(vin);
    if (!v.empty() && v.back() == '%') return v;             // ParseComponent handles %
    std::string num;
    for (char c : v) { if ((c >= '0' && c <= '9') || c == '.' || c == '-') num += c; else break; }
    return num.empty() ? "0" : num;
}

const char* HtmlTagToType(const std::string& tag, const std::string& inputType) {
    if (tag == "div" || tag == "panel" || tag == "section" || tag == "header" ||
        tag == "footer" || tag == "nav" || tag == "ul" || tag == "form") return "panel";
    if (tag == "button" || tag == "a") return "button";
    if (tag == "img")      return "image";
    if (tag == "select")   return "dropdown";
    if (tag == "progress") return "progress";
    if (tag == "input") {
        if (inputType == "range")    return "slider";
        if (inputType == "checkbox") return "toggle";
        return "input";
    }
    return "text";   // p, span, h1..h6, label, text, li, default
}
bool HtmlContainer(const char* type) {
    return std::strcmp(type, "panel") == 0;
}

// Concatenate the direct text children of an element (its label).
std::string DirectText(const HtmlNode& el) {
    std::string s;
    for (const auto& k : el.kids) if (k.isText) { if (!s.empty()) s += " "; s += k.text; }
    return TrimStr(s);
}

// Apply a name=value CSS declaration onto a Token (mapping to OkayUI props).
void CssDeclToToken(const char* type, const std::string& prop, const std::string& val,
                    Token& t, std::string& w, std::string& h, std::string& left, std::string& top) {
    auto set = [&](const std::string& k, const std::string& v) {
        for (auto& p : t.props) if (p.first == k) { p.second = v; return; }
        t.props.emplace_back(k, v);
    };
    if (prop == "background" || prop == "background-color") set("color", CssColor(val));
    else if (prop == "color")            set(std::strcmp(type,"text")==0 ? "color" : "textcolor", CssColor(val));
    else if (prop == "width")            w = CssLen(val);
    else if (prop == "height")           h = CssLen(val);
    else if (prop == "left")             left = CssLen(val);
    else if (prop == "top")              top = CssLen(val);
    else if (prop == "border-radius")    set("corner", CssLen(val));
    else if (prop == "border-width" || prop == "border") set("border", CssLen(val));
    else if (prop == "border-color")     set("bordercolor", CssColor(val));
    else if (prop == "font-size")        set(std::strcmp(type,"button")==0 ? "font" : "size", CssLen(val));
    else if (prop == "text-align")       set("align", TrimStr(val));
}

void BuildHtmlNodes(Scene& scene, const std::vector<HtmlNode>& nodes, Transform* parentT,
                    const std::vector<CssRule>& css, std::vector<GameObject*>& out);

void BuildHtmlElement(Scene& scene, const HtmlNode& el, Transform* parentT,
                      const std::vector<CssRule>& css, std::vector<GameObject*>& out) {
    std::string inputType = Lower(el.attr("type"));
    const char* type = HtmlTagToType(el.tag, inputType);

    Token t; t.type = type;
    // Label / text content.
    std::string label = DirectText(el);
    if (!label.empty()) t.label = label;

    // Selectors: tag, then .class, then #id (ascending specificity), then inline.
    std::string id  = el.attr("id");
    std::string cls = el.attr("class");
    std::string w, h, left, top;
    auto applyDecls = [&](const std::vector<std::pair<std::string, std::string>>& decls) {
        for (auto& d : decls) CssDeclToToken(type, d.first, d.second, t, w, h, left, top);
    };
    auto classHas = [&](const std::string& name) {
        std::stringstream cs(cls); std::string c;
        while (cs >> c) if (c == name) return true;
        return false;
    };
    for (const CssRule& r : css) if (r.sel == el.tag || r.sel == "*") applyDecls(r.decls);
    for (const CssRule& r : css) if (!r.sel.empty() && r.sel[0] == '.' && classHas(r.sel.substr(1))) applyDecls(r.decls);
    for (const CssRule& r : css) if (!r.sel.empty() && r.sel[0] == '#' && r.sel.substr(1) == id) applyDecls(r.decls);
    if (el.has("style")) {                                  // inline style="" wins
        for (const CssRule& r : ParseCss("x{" + el.attr("style") + "}")) applyDecls(r.decls);
    }
    // Fold accumulated geometry into pos/size props.
    if (!w.empty() || !h.empty()) {
        std::string sz = (w.empty() ? "0" : w);
        if (!h.empty()) sz += "," + h;
        for (auto& p : t.props) if (p.first == "size") { p.second = sz; sz.clear(); break; }
        if (!sz.empty()) t.props.emplace_back("size", sz);
    }
    if (!left.empty() || !top.empty())
        t.props.emplace_back("pos", (left.empty() ? "0" : left) + "," + (top.empty() ? "0" : top));

    // HTML attributes -> widget props.
    if (!id.empty())            t.props.emplace_back("name", id);
    if (el.has("src"))          t.props.emplace_back("texture", el.attr("src"));
    if (el.has("placeholder"))  t.props.emplace_back("placeholder", el.attr("placeholder"));
    if (el.has("tooltip") || el.has("title")) t.props.emplace_back("tooltip", el.has("tooltip") ? el.attr("tooltip") : el.attr("title"));
    if (el.has("min"))          t.props.emplace_back("min", el.attr("min"));
    if (el.has("max"))          t.props.emplace_back("max", el.attr("max"));
    if (el.has("checked") && std::strcmp(type,"toggle")==0) t.props.emplace_back("on", "1");
    if (el.has("value")) {
        if (std::strcmp(type,"input")==0) t.label = el.attr("value");
        else t.props.emplace_back("value", el.attr("value"));
    }
    if (std::strcmp(type,"input")==0 && (inputType=="integer"||inputType=="decimal"||inputType=="password"))
        t.props.emplace_back("type", inputType);
    // <select><option>A</option>...</select> -> options=A|B|C
    if (std::strcmp(type,"dropdown")==0) {
        std::string opts;
        for (const auto& k : el.kids) if (!k.isText && k.tag == "option") { if (!opts.empty()) opts += "|"; opts += DirectText(k); }
        if (!opts.empty()) t.props.emplace_back("options", opts);
    }
    // Event handlers (verbatim OkayScript), same as markup onclick=.
    for (const char* ev : {"onclick", "onchange", "ontoggle", "onsubmit"})
        if (el.has(ev)) t.props.emplace_back(ev, el.attr(ev));

    GameObject* go = Spawn(scene, t, Vec2{0.0f, 0.0f});
    go->transform->SetParent(parentT, /*worldPositionStays=*/false);
    out.push_back(go);
    if (HtmlContainer(type)) BuildHtmlNodes(scene, el.kids, go->transform, css, out);
}

void BuildHtmlNodes(Scene& scene, const std::vector<HtmlNode>& nodes, Transform* parentT,
                    const std::vector<CssRule>& css, std::vector<GameObject*>& out) {
    for (const HtmlNode& nd : nodes) {
        if (nd.isText || nd.tag.empty()) continue;
        if (nd.tag == "option" || nd.tag == "head" || nd.tag == "title" || nd.tag == "meta") continue;
        if (nd.tag == "html" || nd.tag == "body") { BuildHtmlNodes(scene, nd.kids, parentT, css, out); continue; }
        BuildHtmlElement(scene, nd, parentT, css, out);
    }
}

// Heuristic: treat the document as HTML if its first non-space char is '<'.
bool LooksLikeHtml(const std::string& m) {
    for (char c : m) { if (std::isspace((unsigned char)c)) continue; return c == '<'; }
    return false;
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

    // HTML/CSS mode: if the document is written as HTML (first non-space char is
    // '<'), parse the tag tree + a <style> block of CSS and build the same widgets.
    if (LooksLikeHtml(markup)) {
        m_diagnostics.clear();
        std::string cssText;
        std::vector<HtmlNode> roots;
        std::size_t hi = 0;
        ParseNodes(markup, hi, /*until=*/"", roots, cssText);
        std::vector<CssRule> css = ParseCss(cssText);
        BuildHtmlNodes(scene, roots, gameObject->transform, css, m_generated);
        return;
    }

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
