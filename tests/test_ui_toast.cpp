#include "test_framework.hpp"
#include <Okay.hpp>
#include "okay/Scene/UITemplates.hpp"
#include "okay/Scene/UILayoutTools.hpp"
#include "okay/Components/UIToast.hpp"
#include <algorithm>
#include <set>
#include <string>

using namespace okay;
using namespace okay::UITemplates;

// Toast stack. The bug these checks pin: UIToast::Push pushed the oldest card out with Scene::Destroy, which only
// QUEUES the removal until the end of the frame, then asked Count() again in the same loop. Count() still saw the
// card, so the loop picked the same card forever and the editor froze on the fifth toast of a four-card stack
// (the "UI Notify" template: click Info, Saved, Error, Confirm, then Cancel). A hang cannot be caught inside the
// test, so the ctest entry has a TIMEOUT; Push itself also has a hard cap on its loop.
namespace {

int LiveChildren(GameObject* host) {   // cards still attached (queued removals are gone after Update)
    int n = 0;
    for (Transform* c : host->transform->Children())
        if (c && c->gameObject && c->gameObject->GetComponent<UIToastItem>()) ++n;
    return n;
}

GameObject* MakeHost(Scene& s, int maxStack) {
    GameObject* g = s.CreateGameObject("Toasts");
    auto* t = g->AddComponent<UIToast>();
    t->maxStack = maxStack;
    t->duration = 6.0f;
    return g;
}

void Click(Scene& s, const char* name, bool& found) {
    GameObject* g = s.Find(name);
    found = g != nullptr;
    if (!g) return;
    Vec2 o, sz;
    if (!GetUIScreenRect(g, 1280.0f, 720.0f, o, sz)) { found = false; return; }
    const Vec2 c{o.x + sz.x * 0.5f, o.y + sz.y * 0.5f};
    Input::FeedMouse(c, 0);
    Input::FeedMouse(c, 1u << 0);
    s.Update(0.016f);
    Input::FeedMouse(c, 0);
    s.Update(0.016f);
}

} // namespace

int main() {
    RUN_SUITE("ui_toast");

    // --- Pushing more than the stack holds, all in one frame, does not hang and keeps the newest cards ---
    {
        Scene s("stack");
        GameObject* host = MakeHost(s, 4);
        auto* toast = host->GetComponent<UIToast>();
        s.Start();
        for (int i = 1; i <= 10; ++i) {
            GameObject* card = toast->Push("Title " + std::to_string(i), "body", i % 4);
            CHECK(card != nullptr);
            CHECK(toast->Count() == std::min(i, 4));    // never above the stack, even before the frame ends
        }
        // The four newest are the live ones (serials 7..10).
        std::set<int> serials;
        for (Transform* c : host->transform->Children()) {
            auto* item = c && c->gameObject ? c->gameObject->GetComponent<UIToastItem>() : nullptr;
            if (item && !item->leaving) serials.insert(item->serial);
        }
        CHECK(serials == (std::set<int>{7, 8, 9, 10}));
        s.Update(0.016f);                               // the queued removals are freed at the end of the frame
        CHECK(toast->Count() == 4);
        CHECK(LiveChildren(host) == 4);
        // The cards take distinct slots (nothing stacked on top of a leaving card).
        std::set<float> ys;
        for (Transform* c : host->transform->Children())
            if (auto* b = c->gameObject->GetComponent<UIButton>()) ys.insert(b->position.y);
        CHECK(ys.size() == 4);
    }

    // --- A stack of one, and a nonsense stack size ---
    {
        Scene s("one");
        GameObject* host = MakeHost(s, 1);
        auto* toast = host->GetComponent<UIToast>();
        s.Start();
        for (int i = 0; i < 5; ++i) toast->Push("t", "b", 0);
        CHECK(toast->Count() == 1);
        toast->SetMaxStack(0);                          // clamps to 1
        CHECK(toast->maxStack == 1);
        toast->Push("t", "b", 0);
        CHECK(toast->Count() == 1);
        s.Update(0.016f);
        CHECK(LiveChildren(host) == 1);
    }

    // --- Cards expire on their own and leave no children behind ---
    {
        Scene s("expire");
        GameObject* host = MakeHost(s, 4);
        auto* toast = host->GetComponent<UIToast>();
        toast->duration = 1.0f;
        s.Start();
        toast->Push("a", "b", 0);
        toast->Push("c", "d", 1);
        CHECK(toast->Count() == 2);
        for (int i = 0; i < 200; ++i) s.Update(0.016f);   // 3.2 s
        CHECK(toast->Count() == 0);
        CHECK(LiveChildren(host) == 0);
        toast->Push("again", "x", 2);                      // the host still works afterwards
        CHECK(toast->Count() == 1);
    }

    // --- The "UI Notify" template: the click sequence that froze the editor ---
    {
        UICanvas::Set(1280.0f, 720.0f);
        UIWorld().rectOf = [&](GameObject* go, Vec2& o, Vec2& sz) { return GetUIScreenRect(go, 1280.0f, 720.0f, o, sz); };
        Scene s("notify");
        GameObject* root = AddUINotify(s);
        CHECK(root != nullptr);
        s.Start();
        for (int i = 0; i < 30; ++i) s.Update(0.016f);
        UIToast* toast = s.FindObjectOfType<UIToast>();
        CHECK(toast != nullptr);
        int clicked = 0;
        for (const char* name : {"InfoButton", "SavedButton", "ErrorButton", "ConfirmButton", "CancelButton",
                                 "InfoButton", "SavedButton", "ErrorButton"}) {   // eight pushes into a stack of four
            bool found = false;
            Click(s, name, found);
            CHECK(found);
            if (found) ++clicked;
            for (int i = 0; i < 5; ++i) s.Update(0.016f);
        }
        CHECK(clicked == 8);
        if (toast) CHECK(toast->Count() == 4);
        for (int i = 0; i < 600; ++i) s.Update(0.016f);    // they all time out
        if (toast) CHECK(toast->Count() == 0);

        // It saves and loads again, and the loaded copy works too.
        const std::string text = SceneSerializer::Serialize(s);
        Scene loaded("loaded");
        std::string err;
        CHECK(SceneSerializer::Deserialize(loaded, text, &err));
        loaded.Start();
        UIToast* lt = loaded.FindObjectOfType<UIToast>();
        CHECK(lt != nullptr);
        if (lt) {
            for (int i = 0; i < 9; ++i) lt->Push("t", "b", 0);
            CHECK(lt->Count() == lt->maxStack);
        }
        UIWorld().rectOf = nullptr;
        UICanvas::Set(1280.0f, 720.0f);
    }

    TEST_MAIN_RESULT();
}
