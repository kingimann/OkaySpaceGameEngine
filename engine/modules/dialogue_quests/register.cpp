// Module dialogue_quests: the DialogueRunner adapter for the interaction contract.
#include "okay/Components/DialogueRunner.hpp"
#include "okay/Input/GameInput.hpp"
#include "okay/Modules/Interact.hpp"
#include "okay/Modules/ModuleRegistry.hpp"
#include "okay/Scene/GameObject.hpp"

namespace okay {
namespace {

class TalkInteractable : public IInteractable {
public:
    explicit TalkInteractable(DialogueRunner* d) : d_(d) {}
    GameObject* Object() const override { return d_->gameObject; }
    float Range() const override { return d_->interactRange; }
    bool CanInteract(const InteractContext&) const override {
        return d_->enabled && d_->startMode == 0 && d_->interactKey && !d_->IsActive() && d_->InRange();
    }
    std::string Prompt(const InteractContext&) const override {
        return d_->promptText.empty() ? UseKeyName(d_->interactKey) + " to talk" : d_->promptText;
    }
    bool Interact(const InteractContext&) override { return d_->StartDialogue(); }
    const char* Model() const override { return "DialogueRunner"; }
private:
    DialogueRunner* d_;
};

} // namespace

namespace modules {
void Register_dialogue_quests(ModuleContext&) {
    interact::AddFinder("DialogueRunner", [](GameObject* g, std::vector<std::unique_ptr<IInteractable>>& out) {
        for (DialogueRunner* d : g->GetComponents<DialogueRunner>()) out.emplace_back(new TalkInteractable(d));
    });
}
} // namespace modules
} // namespace okay
