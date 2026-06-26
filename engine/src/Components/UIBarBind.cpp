#include "okay/Components/UIBarBind.hpp"
#include "okay/Components/UIProgressBar.hpp"
#include "okay/Components/UIRadialProgress.hpp"
#include "okay/Components/ActionList.hpp"
#include "okay/Core/Prefs.hpp"
#include "okay/Scene/GameObject.hpp"
#include "okay/Math/Mathf.hpp"

namespace okay {

void UIBarBind::Apply() {
    if (!gameObject || var.empty()) return;
    // Read the value from a visual-scripting variable first, then Prefs.
    float value = 0.0f;
    auto& vars = ActionList::Vars();
    auto it = vars.find(var);
    if (it != vars.end()) value = it->second;
    else if (Prefs::Has(var)) value = Prefs::GetFloat(var, 0.0f);
    else return;   // nothing to show yet

    float span = max - min;
    float frac = Mathf::Clamp01(span != 0.0f ? (value - min) / span : 0.0f);
    if (auto* pb = gameObject->GetComponent<UIProgressBar>())   pb->SetValue(frac);
    if (auto* rp = gameObject->GetComponent<UIRadialProgress>()) rp->SetValue(frac);
}

} // namespace okay
