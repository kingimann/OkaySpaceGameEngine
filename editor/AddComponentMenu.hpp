#pragma once
// The Add Component browser's window (ImGui). The logic is in AddComponentModel.*; this file draws it: a wide popup with a
// big search box, plain-word categories on the left and component cards on the right, and runs the keys.
//
// How the host (main.cpp, DrawInspector) uses it, once per frame while the popup is open:
//   if (session.Begin(go->name.c_str())) {
//       session.BeginCat("Rendering");  if (session.Item(!go->GetComponent<Foo>(), "Foo (what it does)")) { ...add it... }
//       ...                              // every category, every frame: Item() only RECORDS the component; it returns true on
//       session.End(go);                 // the frame after you pick one, so the add runs in your code, not in here
//   }
// Written from the owner's request (2026-11-19); no code from another project.
#include "AddComponentModel.hpp"
#include "AddComponentRegistry.hpp"

#include <functional>
#include <string>
#include <vector>

namespace okay { class GameObject; struct ComponentTypeInfo; }

namespace oked {
namespace addcomp {

class Session {
public:
    /// Ask for the popup to open on the next Begin() (the "+ Add Component" button, Hierarchy > Add Component...).
    void RequestOpen() { openRequested_ = true; }
    /// Opens / draws the popup frame. True while it is open: feed it every component, then call End().
    bool Begin(const char* targetName, okay::GameObject* go);
    void BeginCat(const char* rawCategory) { cat_ = rawCategory; }
    /// Records one component. `absent` = it can be added. True once: the frame after the user chose it.
    bool Item(bool absent, const char* label);
    /// After the hand-written rows: one row for every registered component type they did not list (no per-type edit needed).
    /// `add` runs the frame after the pick, like a row's own code; AddRegistered does the plain registry add.
    void FeedRegistry(okay::GameObject& go, const std::function<void(const okay::ComponentTypeInfo&)>& add);
    /// Draws the browser and handles the keys. Closes the popup when something was chosen (the host's add has run by then).
    void End(okay::GameObject* go);
    /// The one-line "Added ..." text after an add; empty when there is none. Read once.
    std::string TakeNotice();
    /// A short line under the button, for a few seconds after an add.
    void DrawNotice() const;

    /// The row widget as a function, for helpers in other files (SplineAddComponentItems, ...).
    std::function<bool(bool, const char*)> ItemFn() { return [this](bool a, const char* l) { return Item(a, l); }; }

    // for tests / tools
    State& state() { return state_; }
    Prefs& prefs() { return prefs_; }

private:
    void LoadOnce();
    void SaveNow();
    void DrawBody(float bodyH, float scale);

    bool openRequested_ = false, prefsLoaded_ = false, refocus_ = false, scrollToCursor_ = false;
    std::string cat_, target_;
    std::vector<Entry> entries_;
    State state_;
    Prefs prefs_;
    View view_;
    char buf_[64] = {0};
    std::string pendingKey_;      // chosen last frame: Item() reports it now
    bool activated_ = false;
    std::string activatedName_, activatedLabel_;
    std::vector<const void*> before_;   // the object's components before the add (to say what else came with it)
    std::string notice_;
    double noticeAt_ = -100.0;
    std::string noticeShown_;
};

}  // namespace addcomp
}  // namespace oked
