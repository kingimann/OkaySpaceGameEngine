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
#include "okay/Components/TextRenderer.hpp"
#include "okay/Components/UIAnchor.hpp"
#include "okay/Components/ScriptComponent.hpp"

#include <sstream>
#include <vector>
#include <string>
#include <cctype>

namespace okay {
namespace {

// Trim trailing carriage return / spaces; return leading-space (indent) count.
int IndentOf(const std::string& line) {
    int n = 0;
    while (n < (int)line.size() && (line[n] == ' ' || line[n] == '\t')) ++n;
    return n;
}

Vec2 ParsePair(const std::string& v, float defY) {
    Vec2 out{0.0f, defY};
    std::size_t comma = v.find(',');
    if (comma == std::string::npos) { out.x = (float)std::atof(v.c_str()); return out; }
    out.x = (float)std::atof(v.substr(0, comma).c_str());
    out.y = (float)std::atof(v.substr(comma + 1).c_str());
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
            if (key == "onclick") {       // rest of line, verbatim
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

// Build the widget GameObject for one token (no parenting/positioning yet).
GameObject* Spawn(Scene& scene, const Token& t) {
    GameObject* go = scene.CreateGameObject(t.type.empty() ? "UIElement" : t.type);

    auto applyBox = [&](Vec2& pos, Vec2& size, UIAnchor& anchor, Color* color) {
        if (t.Has("pos"))    pos    = ParsePair(t.Get("pos"), pos.y);
        if (t.Has("size"))   size   = ParsePair(t.Get("size"), size.y);
        if (t.Has("anchor")) anchor = ParseAnchor(t.Get("anchor"));
        if (color && t.Has("color")) *color = ParseColor(t.Get("color"));
    };

    if (t.type == "panel") {
        auto* c = go->AddComponent<UIPanel>();
        applyBox(c->position, c->size, c->anchor, &c->color);
    } else if (t.type == "button") {
        auto* c = go->AddComponent<UIButton>();
        if (!t.label.empty()) c->label = t.label;
        applyBox(c->position, c->size, c->anchor, &c->color);
        if (t.Has("onclick")) {
            auto* sc = go->AddComponent<ScriptComponent>("okayscript");
            sc->LoadSource("function on_click() { " + t.Get("onclick") + " }\n");
        }
    } else if (t.type == "image") {
        auto* c = go->AddComponent<UIImage>();
        applyBox(c->position, c->size, c->anchor, &c->color);
        if (t.Has("texture")) c->texture = t.Get("texture");
    } else if (t.type == "slider") {
        auto* c = go->AddComponent<UISlider>();
        applyBox(c->position, c->size, c->anchor, nullptr);
        if (t.Has("value")) c->value = (float)std::atof(t.Get("value").c_str());
    } else if (t.type == "toggle") {
        auto* c = go->AddComponent<UIToggle>();
        if (!t.label.empty()) c->label = t.label;
        applyBox(c->position, c->size, c->anchor, nullptr);
        if (t.Has("on")) c->on = std::atoi(t.Get("on").c_str()) != 0;
    } else if (t.type == "progress") {
        auto* c = go->AddComponent<UIProgressBar>();
        applyBox(c->position, c->size, c->anchor, nullptr);
        if (t.Has("value")) c->value = (float)std::atof(t.Get("value").c_str());
    } else { // text (default)
        auto* c = go->AddComponent<TextRenderer>();
        c->screenSpace = true;
        if (!t.label.empty()) c->text = t.label;
        if (t.Has("pos"))    c->screenPos = ParsePair(t.Get("pos"), c->screenPos.y);
        if (t.Has("size"))   c->pixelSize = ParsePair(t.Get("size"), 0).x;
        if (t.Has("anchor")) c->anchor = ParseAnchor(t.Get("anchor"));
        if (t.Has("color"))  c->color = ParseColor(t.Get("color"));
    }
    return go;
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

    // Parse line by line, using indentation to nest widgets. The document's own
    // GameObject is the root parent; deeper indents parent under the prior line.
    std::stringstream ss(markup);
    std::string line;
    // Stack of (indent, transform-to-parent-under).
    std::vector<std::pair<int, Transform*>> stack;
    stack.push_back({-1, gameObject->transform});

    while (std::getline(ss, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        int indent = IndentOf(line);
        std::string body = line.substr(indent);
        if (body.empty() || body[0] == '#') continue;   // blank or comment

        Token t = Lex(body);
        if (t.type.empty()) continue;

        while (stack.size() > 1 && indent <= stack.back().first) stack.pop_back();
        Transform* parent = stack.back().second;

        GameObject* go = Spawn(scene, t);
        go->transform->SetParent(parent, /*worldPositionStays=*/false);
        m_generated.push_back(go);
        stack.push_back({indent, go->transform});
    }
}

} // namespace okay
