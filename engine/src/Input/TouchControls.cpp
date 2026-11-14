#include "okay/Components/TouchControls.hpp"
#include "okay/Components/UIElement.hpp"
#include "okay/Input/Gestures.hpp"
#include "okay/Input/InputActions.hpp"
#include "okay/Scene/GameObject.hpp"
#include "okay/Scene/Scene.hpp"
#include <algorithm>
#include <cmath>
#include <string>

namespace okay {
namespace {

struct Finger {
    long long id = 0;
    Vec2 position;
    bool began = false;
    bool ended = false;
};

float Clamp01(float v) { return v < 0.0f ? 0.0f : (v > 0.9f ? 0.9f : v); }

Vec2 ApplyDeadZone(Vec2 v, float dead) {
    dead = Clamp01(dead);
    const float len = std::sqrt(v.x * v.x + v.y * v.y);
    if (len <= dead || len < 1.0e-4f) return {};
    float s = (len - dead) / (1.0f - dead);
    if (s > 1.0f) s = 1.0f;
    return v * (s / len);
}

bool Usable(GameObject* go) {
    return go && go->active && !UIHidden(go);
}

void EnsureAction(const std::string& name, bool direction) {
    if (name.empty() || InputActions::Has(name)) return;
    InputActions::Define(name, {}, direction);
}

template <class T>
bool Zone(const T* c, const Vec2& p) {
    return TouchControlContains(c->anchor, c->position, c->size, p);
}

// Follow a finger the control already owns, or take a new one that just landed
// in its zone. Returns the point to read, or false when nothing is held.
template <class T>
bool Hold(T* c, const std::vector<Finger>& fingers, std::vector<long long>& used, bool mouseDown,
          bool mousePressed, const Vec2& mouse, bool& mouseTaken, Vec2& at) {
    if (c->hasFinger) {
        const Finger* f = nullptr;
        for (const Finger& e : fingers) if (e.id == c->fingerId) f = &e;
        if (f) {
            // Claim the lift too, so the finger leaving a stick is not also a tap.
            used.push_back(f->id);
            TouchClaims::Claim(f->id);
            if (!f->ended) { at = f->position; c->tracking = true; return true; }
        }
        c->Release();
    }
    if (c->mouseHeld) {
        if (!mouseDown || mouseTaken) { c->Release(); }
        else { mouseTaken = true; at = mouse; c->tracking = true; return true; }
    }
    for (const Finger& f : fingers) {
        if (!f.began || f.ended) continue;
        if (std::find(used.begin(), used.end(), f.id) != used.end()) continue;
        if (!Zone(c, f.position)) continue;
        c->hasFinger = true;
        c->fingerId = f.id;
        c->tracking = true;
        used.push_back(f.id);
        TouchClaims::Claim(f.id);
        at = f.position;
        return true;
    }
    if (mousePressed && !mouseTaken && !Input::PointerIsTouch() && Zone(c, mouse)) {
        c->mouseHeld = true;
        c->tracking = true;
        mouseTaken = true;
        at = mouse;
        return true;
    }
    c->tracking = false;
    return false;
}

Vec2 RectCenter(UIAnchor anchor, const Vec2& position, const Vec2& size) {
    return TouchControlOrigin(anchor, position, size) + size * 0.5f;
}

} // namespace

void TouchControls::ReleaseAll(Scene& scene) {
    for (const auto& up : scene.Objects()) {
        if (!up) continue;
        if (auto* j = up->GetComponent<VirtualJoystick>()) j->Release();
        if (auto* d = up->GetComponent<TouchDPad>()) d->Release();
        if (auto* b = up->GetComponent<TouchButton>()) b->Release();
    }
}

void TouchControls::Sample(Scene& scene) {
    InputActions::ClearContributions();
    TouchClaims::Clear();

    std::vector<Finger> fingers;
    for (int i = 0; i < Input::TouchCount(); ++i) {
        const Input::Touch& t = Input::GetTouch(i);
        Finger f;
        f.id = t.id;
        f.position = t.position;
        f.began = t.phase == Input::Touch::Phase::Began;
        f.ended = t.phase == Input::Touch::Phase::Ended;
        fingers.push_back(f);
    }
    const bool mouseDown = Input::GetMouseButton(0);
    const bool mousePressed = Input::GetMouseButtonDown(0);
    const Vec2 mouse = Input::MousePosition();
    // The first finger also becomes the mouse. Driving both would press twice.
    const bool allowMouse = !Input::PointerIsTouch();
    bool mouseTaken = false;
    std::vector<long long> used;

    auto hidden = [](GameObject* go, bool shows) { return !Usable(go) || !shows; };

    for (const auto& up : scene.Objects()) {
        auto* b = up->GetComponent<TouchButton>();
        if (!b) continue;
        if (hidden(up.get(), b->Shows())) { b->Release(); continue; }
        Vec2 at;
        Hold(b, fingers, used, mouseDown && allowMouse, mousePressed && allowMouse, mouse, mouseTaken, at);
        b->pressed = b->tracking;
        if (b->pressed && !b->action.empty()) {
            EnsureAction(b->action, false);
            InputActions::Contribute(b->action, 1.0f);
        }
    }
    for (const auto& up : scene.Objects()) {
        auto* d = up->GetComponent<TouchDPad>();
        if (!d) continue;
        if (hidden(up.get(), d->Shows())) { d->Release(); continue; }
        Vec2 at;
        if (!Hold(d, fingers, used, mouseDown && allowMouse, mousePressed && allowMouse, mouse, mouseTaken, at)) {
            d->value = {};
            continue;
        }
        const Vec2 c = RectCenter(d->anchor, d->position, d->size);
        const float hx = d->size.x * 0.5f > 1.0f ? d->size.x * 0.5f : 1.0f;
        const float hy = d->size.y * 0.5f > 1.0f ? d->size.y * 0.5f : 1.0f;
        float x = (at.x - c.x) / hx;
        float y = (c.y - at.y) / hy;
        const float dead = Clamp01(d->deadZone);
        if (std::fabs(x) < dead) x = 0.0f; else x = x > 0.0f ? 1.0f : -1.0f;
        if (std::fabs(y) < dead) y = 0.0f; else y = y > 0.0f ? 1.0f : -1.0f;
        if (!d->diagonals && x != 0.0f && y != 0.0f) {
            if (std::fabs(at.x - c.x) >= std::fabs(at.y - c.y)) y = 0.0f;
            else x = 0.0f;
        }
        d->value = {x, y};
        if ((x != 0.0f || y != 0.0f) && !d->action.empty()) {
            EnsureAction(d->action, true);
            InputActions::ContributeVector(d->action, d->value);
        }
    }
    for (const auto& up : scene.Objects()) {
        auto* j = up->GetComponent<VirtualJoystick>();
        if (!j) continue;
        if (hidden(up.get(), j->Shows())) { j->Release(); continue; }
        const bool was = j->tracking;
        Vec2 at;
        if (!Hold(j, fingers, used, mouseDown && allowMouse, mousePressed && allowMouse, mouse, mouseTaken, at)) {
            j->value = {};
            j->knob = {};
            continue;
        }
        if (j->floating && !was) j->floatCenter = at;
        const Vec2 center = j->floating ? j->floatCenter : RectCenter(j->anchor, j->position, j->size);
        const float rad = std::max(1.0f, std::min(j->size.x, j->size.y) * 0.5f);
        Vec2 delta{at.x - center.x, at.y - center.y};
        const float len = std::sqrt(delta.x * delta.x + delta.y * delta.y);
        if (len > rad) delta = delta * (rad / len);
        j->knob = delta;
        j->value = ApplyDeadZone({delta.x / rad, -delta.y / rad}, j->deadZone);
        if ((j->value.x != 0.0f || j->value.y != 0.0f) && !j->action.empty()) {
            EnsureAction(j->action, true);
            InputActions::ContributeVector(j->action, j->value);
        }
    }
}

} // namespace okay
