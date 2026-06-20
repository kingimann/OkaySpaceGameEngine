#include "okay/Components/CharacterBody.hpp"
#include "okay/Components/MeshRenderer.hpp"
#include "okay/Scene/GameObject.hpp"
#include <cmath>

namespace okay {

void CharacterBody::Apply() {
    if (!gameObject) return;
    auto* mr = gameObject->GetComponent<MeshRenderer>();
    if (!mr) mr = gameObject->AddComponent<MeshRenderer>();
    mr->mesh = Build();
    mr->color = color;
}

void CharacterBody::Update(float dt) {
    if (anim == 0) return;
    auto* mr = gameObject ? gameObject->GetComponent<MeshRenderer>() : nullptr;
    if (!mr) return;
    animTime += dt * (animSpeed <= 0.0f ? 1.0f : animSpeed);
    const float t = animTime;
    HumanoidParams pp = params;        // animate a copy; authored params untouched
    switch (anim) {
        case 1: {  // idle: gentle counter-sway of the arms (subtle breathing)
            pp.armSwing = 4.0f * std::sin(t * 1.6f);
            break;
        }
        case 2: {  // walk: legs swing fore/aft, arms counter them
            float s = std::sin(t * 4.0f) * 24.0f;
            pp.legSwing = s; pp.armSwing = -s;
            break;
        }
        case 3: {  // run: faster, larger swing
            float s = std::sin(t * 7.0f) * 40.0f;
            pp.legSwing = s; pp.armSwing = -s;
            break;
        }
        default: break;
    }
    mr->mesh = Build(pp);
}

} // namespace okay
