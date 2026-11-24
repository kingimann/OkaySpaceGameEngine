// Survival module: the survival scene record (hunger, thirst, stamina, warmth).
// Written at its old place in the object so a save with every module on is byte-identical.
#include "okay/Modules/ModuleRegistry.hpp"
#include "okay/Scene/ComponentSerializer.hpp"
#include "okay/Components/SurvivalStats.hpp"
#include <cctype>

namespace okay {
namespace modules {

namespace {

void WriteSurvival(const GameObject& go, std::ostream& out) {
    for (auto* sv : go.GetComponents<SurvivalStats>()) {
        out << "  survival " << sv->maxHealth << " " << sv->armor << " " << sv->regenWhenFed
            << " " << sv->regenDelay << " " << sv->maxHunger << " " << sv->hungerDrain << " "
            << sv->starveDamage << " " << sv->maxThirst << " " << sv->thirstDrain << " "
            << sv->dehydrateDamage << " " << sv->maxStamina << " " << sv->staminaRegen << " "
            << sv->sprintCost << " " << sv->sprintDrainMult << " " << sv->maxOxygen << " "
            << sv->oxygenDrain << " " << sv->oxygenRefill << " " << sv->drownDamage << " "
            << sv->maxWarmth << " " << sv->coldDrain << " " << sv->warmRegen << " "
            << sv->freezeDamage << " " << (sv->publishPrefs ? 1 : 0) << " "
            << (sv->publishBars ? 1 : 0) << " " << (sv->sendMessages ? 1 : 0)
            << " " << sv->resistance << " " << (sv->godMode ? 1 : 0) << " " << sv->invincibleTime << "\n";
    }
}

void ReadSurvival(GameObject& go, std::istream& in) {
    auto* sv = go.AddComponent<SurvivalStats>();
    in >> sv->maxHealth >> sv->armor >> sv->regenWhenFed >> sv->regenDelay
       >> sv->maxHunger >> sv->hungerDrain >> sv->starveDamage
       >> sv->maxThirst >> sv->thirstDrain >> sv->dehydrateDamage
       >> sv->maxStamina >> sv->staminaRegen >> sv->sprintCost >> sv->sprintDrainMult
       >> sv->maxOxygen >> sv->oxygenDrain >> sv->oxygenRefill >> sv->drownDamage
       >> sv->maxWarmth >> sv->coldDrain >> sv->warmRegen >> sv->freezeDamage;
    int pp = 1, pb = 1, sm = 1; in >> pp >> pb >> sm;
    sv->publishPrefs = (pp != 0); sv->publishBars = (pb != 0); sv->sendMessages = (sm != 0);
    in >> std::ws;
    if (std::isdigit(in.peek()) || in.peek() == '.' || in.peek() == '-') {
        int gd = 0; in >> sv->resistance >> gd >> sv->invincibleTime; sv->godMode = (gd != 0);
    }
}

} // namespace

void Register_survival(ModuleContext&) {
    ComponentSerializerRegistry::Entry rec;
    rec.name = "survival";
    rec.write = &WriteSurvival;
    rec.read = &ReadSurvival;
    rec.inChain = true;
    ComponentSerializerRegistry::Register(std::move(rec));
}

} // namespace modules
} // namespace okay
