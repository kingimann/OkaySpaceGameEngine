#include "okay/Components/UIBarBind.hpp"
#include "okay/Components/UIProgressBar.hpp"
#include "okay/Components/UIRadialProgress.hpp"
#include "okay/Components/ActionList.hpp"
#include "okay/Components/VisualScriptComponent.hpp"
#include "okay/VisualScript/VsVariables.hpp"
#include "okay/Core/Prefs.hpp"
#include "okay/Scene/GameObject.hpp"
#include "okay/Math/Mathf.hpp"

namespace okay {

void UIBarBind::Apply() {
    if (!gameObject || var.empty()) return;
    // A visual-script variable on this object, then a scene variable, then an
    // Actions variable, then a saved Prefs value.
    float value = 0.0f;
    bool found = false;
    if (auto* vs = gameObject->GetComponent<VisualScriptComponent>()) {
        auto it = vs->Variables().find(var);
        if (it != vs->Variables().end() && it->second.IsNumber()) { value = it->second.AsFloat(); found = true; }
    }
    if (!found && gameObject->scene()) {
        auto& sceneVars = vs::SceneVariables(gameObject->scene());
        auto it = sceneVars.find(var);
        if (it != sceneVars.end() && it->second.IsNumber()) { value = it->second.AsFloat(); found = true; }
    }
    if (!found) {
        auto& vars = ActionList::Vars();
        auto it = vars.find(var);
        if (it != vars.end()) { value = it->second; found = true; }
        else if (Prefs::Has(var)) { value = Prefs::GetFloat(var, 0.0f); found = true; }
    }
    if (!found) return;

    float span = max - min;
    float frac = Mathf::Clamp01(span != 0.0f ? (value - min) / span : 0.0f);
    if (auto* pb = gameObject->GetComponent<UIProgressBar>())   pb->SetValue(frac);
    if (auto* rp = gameObject->GetComponent<UIRadialProgress>()) rp->SetValue(frac);
}

} // namespace okay
