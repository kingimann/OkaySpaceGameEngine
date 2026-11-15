// UIToast keeps at most maxStack cards: pushing one more while the stack is full removes the oldest. Scene::Destroy only
// queues (a card leaves at the end of the frame) and Push used to wait for the card to be gone, which it never was, so the
// call never returned: a script that pushed a fifth toast within a few seconds froze the editor and the game. (Found by
// the inventory sample, whose messages are toasts.) Checked outside an update, and from a script's update.
#include "test_framework.hpp"
#include <Okay.hpp>
#include "okay/Components/ScriptComponent.hpp"
#include "okay/Components/TextRenderer.hpp"
#include "okay/Components/UIToast.hpp"
#include <set>
#include <string>

using namespace okay;

namespace {
std::set<std::string> Bodies(Scene& s) {
    std::set<std::string> out;
    for (const auto& up : s.Objects())
        if (up && up->name == "ToastBody")
            if (auto* tr = up->GetComponent<TextRenderer>()) out.insert(tr->text);
    return out;
}
} // namespace

int main() {
    RUN_SUITE("ui_toast_stack");

    {   // pushes outside an update
        Scene s("toasts");
        s.physicsEnabled = false;
        auto* t = s.CreateGameObject("Toasts")->AddComponent<UIToast>();
        t->maxStack = 3;
        t->duration = 100.0f;
        s.Start();
        for (int i = 1; i <= 8; ++i) t->Push("T" + std::to_string(i), "body " + std::to_string(i), 0);
        CHECK(t->Count() == 3);                         // the cards still queued to go are not counted
        s.Update(0.016f);                               // the queued cards are freed at the end of the frame
        CHECK(t->Count() == 3);
        const std::set<std::string> want = {"body 6", "body 7", "body 8"};
        CHECK(Bodies(s) == want);                       // the newest three stay
    }

    {   // pushes from a script, inside the scene's update (where the toasts of a game really come from)
        Scene s("script");
        s.physicsEnabled = false;
        GameObject* host = s.CreateGameObject("Toasts");
        auto* t = host->AddComponent<UIToast>();
        t->maxStack = 2;
        t->duration = 100.0f;
        auto* sc = host->AddComponent<ScriptComponent>("okayscript");
        CHECK(sc->LoadSource("var n = 0;\nfunction update(dt) { n = n + 1; toast_push(\"T\", \"n \" + n, 0); toast_push(\"U\", \"again \" + n, 0); }\n"));
        s.Start();
        for (int i = 0; i < 12; ++i) s.Update(0.016f);
        CHECK(t->Count() == 2);
        const std::set<std::string> want = {"n 12", "again 12"};
        CHECK(Bodies(s) == want);
    }
    TEST_MAIN_RESULT();
}
