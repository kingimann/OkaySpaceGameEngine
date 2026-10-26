// Overlay widgets: UIModal (input blocking, stacking, draw order, Escape, gamepad
// focus scope, native Close action), UIContextMenu (right-click open, pick,
// separators, outside click, draw) and UIKeybindField (listen, bind, cancel, reset,
// Prefs persistence, focus hold), plus their scene round trip.
#include "test_framework.hpp"
#include "okay/Components/UINavigation.hpp"
#include "okay/Core/Prefs.hpp"
#include "okay/UI/UIRenderer.hpp"
#include <Okay.hpp>
#include <string>
#include <vector>

using namespace okay;
using namespace okay::ui2d;
using URect = okay::ui2d::Rect;

namespace {

// Records fills with the red channel, so a widget's fill can be told apart.
struct FillRecorder : Painter {
    struct Fill { URect r; std::uint8_t red, alpha; };
    std::vector<Fill> fills;
    std::uint8_t red = 0, alpha = 0;
    void SetDrawColor(std::uint8_t r, std::uint8_t, std::uint8_t, std::uint8_t a) override { red = r; alpha = a; }
    void FillRect(const URect* r) override { fills.push_back({*r, red, alpha}); }
    void DrawRect(const URect*) override {}
    void DrawLine(int, int, int, int) override {}
    void DrawPoint(int, int) override {}
    void SetClipRect(const URect*) override {}
    void GetClipRect(URect* r) override { *r = URect{0, 0, 0, 0}; }
    bool IsClipEnabled() override { return false; }
    void SetScale(float, float) override {}
    Tex LoadTexture(const std::string&) override { return nullptr; }
    Tex FontAtlas(TtfFont*) override { return nullptr; }
    void QueryTexture(Tex, int* w, int* h) override { *w = *h = 0; }
    void SetTextureColorMod(Tex, std::uint8_t, std::uint8_t, std::uint8_t) override {}
    void SetTextureAlphaMod(Tex, std::uint8_t) override {}
    void Copy(Tex, const URect*, const URect*) override {}
    void CopyF(Tex, const URect*, const FRect*) override {}
    void CopyEx(Tex, const URect*, const URect*, double, const Point*, int) override {}
    void CopyExF(Tex, const URect*, const FRect*, double, const FPoint*, int) override {}
    void Geometry(Tex, const Vertex*, int, const int*, int) override {}
    int IndexOfRed(std::uint8_t r) const {
        for (std::size_t i = 0; i < fills.size(); ++i) if (fills[i].red == r) return (int)i;
        return -1;
    }
};

GameObject* Child(Scene& s, GameObject* parent, const char* name) {
    GameObject* g = s.CreateGameObject(name);
    if (parent) g->transform->SetParent(parent->transform, false);
    return g;
}

UIPanel* Panel(Scene& s, GameObject* parent, const char* name, Vec2 pos, Vec2 size, int red) {
    auto* p = Child(s, parent, name)->AddComponent<UIPanel>();
    p->position = pos; p->size = size; p->borderWidth = 0.0f;
    p->color = Color::FromBytes((unsigned char)red, 0, 0, 255);
    return p;
}

void InstallHitTests() {   // what the player installs each frame (800x600 screen)
    UIWorld().rectOf = [](GameObject* go, Vec2& o, Vec2& sz) { return GetUIScreenRect(go, 800, 600, o, sz); };
    UIWorld().clipContains = [](GameObject* go, const Vec2& p) { return UIClipHitTest(go, p, 800, 600); };
    UIWorld().screenW = 800; UIWorld().screenH = 600;
}

// One game frame: layout, input edges, update.
void Frame(Scene& s, std::vector<char> keys = {}, Vec2 mouse = {0, 0}, unsigned buttons = 0) {
    UpdateUILayouts(s);
    Input::FeedKeys(keys);
    Input::FeedMouse(mouse, buttons);
    s.Update(0.016f);
}

} // namespace

int main() {
    RUN_SUITE("ui_overlays");
    InstallHitTests();

    // ---- Modal: blocks the rest, stacks, draws on top, closes on Escape ----
    {
        Scene s("modal");
        UIPanel* back = Panel(s, nullptr, "back", {0, 0}, {800, 600}, 10);
        GameObject* behind = Child(s, nullptr, "behind");
        behind->AddComponent<UIButton>()->position = {10, 10};
        UIPanel* dlg = Panel(s, nullptr, "dialog", {200, 150}, {400, 300}, 200);
        auto* md = dlg->gameObject->AddComponent<UIModal>();
        GameObject* inside = Child(s, dlg->gameObject, "ok");
        auto* okBtn = inside->AddComponent<UIButton>();
        okBtn->position = {20, 20};
        dlg->gameObject->active = false;
        s.Start();
        Frame(s);
        CHECK(!UIModal::Blocks(behind));                       // nothing open yet

        md->Open();
        Frame(s);
        CHECK(md->IsOpen() && UIModal::Top(&s) == md);
        CHECK(UIModal::Blocks(behind) && !UIModal::Blocks(inside) && !UIModal::Blocks(dlg->gameObject));
        CHECK(!UIClipHitTest(behind, {20, 20}, 800, 600));
        CHECK(UIClipHitTest(inside, {230, 175}, 800, 600));

        // Drawn above everything, its dim backdrop between the scene and the dialog.
        FillRecorder rec;
        RenderSceneUI(s, rec, 800, 600);
        const int iBack = rec.IndexOfRed(10), iDialog = rec.IndexOfRed(200);
        int iDim = -1;
        for (std::size_t i = 0; i < rec.fills.size(); ++i)
            if (rec.fills[i].red == 0 && rec.fills[i].alpha == 140 && rec.fills[i].r.w == 800 && rec.fills[i].r.h == 600)
                iDim = (int)i;
        CHECK(iBack >= 0 && iDim > iBack && iDialog > iDim);

        // Gamepad / keyboard focus stays inside the modal.
        Input::FeedKeys({}); Input::FeedKeys({});
        NavigateUI(s);
        CHECK(okBtn->IsFocused() && !behind->GetComponent<UIButton>()->IsFocused());

        // A second modal stacks on top and takes over.
        UIPanel* dlg2 = Panel(s, nullptr, "confirm", {300, 250}, {200, 100}, 220);
        auto* md2 = dlg2->gameObject->AddComponent<UIModal>();
        md2->Open();
        Frame(s);
        CHECK(UIModal::Top(&s) == md2 && UIModal::Blocks(inside));
        // Escape closes only the top one.
        Frame(s, {(char)27});
        CHECK(!md2->IsOpen() && md->IsOpen());
        Frame(s);
        CHECK(UIModal::Top(&s) == md && !UIModal::Blocks(inside));
        // A button can close its dialog with no script (native action).
        okBtn->clickTarget = "dialog"; okBtn->clickFunction = "Close";
        CHECK(InvokeNativeUIAction(dlg->gameObject, "Close", 0.0f));
        Frame(s);
        CHECK(!md->IsOpen() && !UIModal::Blocks(behind));
        (void)back;
    }

    // ---- Context menu: right-click opens, a click picks, outside closes ----
    {
        Scene s("context");
        UIPanel* target = Panel(s, nullptr, "target", {100, 100}, {200, 100}, 30);
        auto* cm = target->gameObject->AddComponent<UIContextMenu>();
        cm->items = {"Copy", "-", "Delete"};
        cm->itemHeight = 20.0f; cm->width = 120.0f;
        int picked = -1; std::string pickedText;
        cm->onSelect = [&](int i, const std::string& t) { picked = i; pickedText = t; };
        s.Start();
        Frame(s, {}, {50, 50}, 2);                 // right-click outside the widget: nothing
        CHECK(!cm->IsOpen());
        Frame(s, {}, {150, 120}, 0);
        Frame(s, {}, {150, 120}, 2);               // right-click on it
        CHECK(cm->IsOpen() && cm->Origin().x == 150 && cm->Origin().y == 120);
        CHECK(cm->ItemAt({160, 145}) == -1);       // the separator can't be picked
        CHECK(cm->ItemAt({160, 165}) == 2);
        // The menu takes the pointer from widgets beneath it.
        CHECK(!UIClipHitTest(target->gameObject, {160, 125}, 800, 600));
        // Drawn on top, at the cursor.
        FillRecorder rec;
        RenderSceneUI(s, rec, 800, 600);
        bool menuDrawn = false;
        for (const auto& f : rec.fills) if (f.r.x == 150 && f.r.y == 120 && f.r.w == 120 && f.r.h == 60) menuDrawn = true;
        CHECK(menuDrawn);
        Frame(s, {}, {160, 165}, 0);
        Frame(s, {}, {160, 165}, 0);               // (the opening frame passes)
        Frame(s, {}, {160, 165}, 1);               // left-click "Delete"
        CHECK(picked == 2 && pickedText == "Delete" && !cm->IsOpen());
        // A click elsewhere closes without picking; it is kept on screen at the edge.
        picked = -1;
        cm->OpenAt({790, 590}, 800, 600);
        CHECK(cm->Origin().x == 680 && cm->Origin().y == 540);
        Frame(s, {}, {10, 10}, 0);
        Frame(s, {}, {10, 10}, 1);
        CHECK(!cm->IsOpen() && picked == -1);
    }

    // ---- Keybind field: listen, bind, cancel, reset, persist ----
    {
        Prefs::Delete("bind_jump");
        Scene s("keybind");
        GameObject* g = Child(s, nullptr, "jump");
        auto* kb = g->AddComponent<UIKeybindField>();
        kb->position = {10, 10}; kb->size = {200, 30};
        kb->action = "Jump"; kb->key = ' '; kb->defaultKey = ' '; kb->prefKey = "bind_jump";
        s.Start();
        Frame(s, {}, {50, 20}, 0);
        Frame(s, {}, {50, 20}, 1);                 // click: listen
        CHECK(kb->IsListening());
        Frame(s, {}, {50, 20}, 0);                 // (the click frame passes)
        CHECK(NavigateUI(s) == nullptr);           // focus holds while it listens
        Frame(s, {'k'}, {50, 20}, 0);
        CHECK(!kb->IsListening() && kb->key == 'k' && kb->KeyLabel() == "K");
        CHECK(Prefs::GetInt("bind_jump", 0) == 'k');
        kb->Listen();
        Frame(s); Frame(s, {(char)27});            // Escape keeps the old key
        CHECK(!kb->IsListening() && kb->key == 'k');
        kb->Listen();
        Frame(s); Frame(s, {'\b'});                // Backspace restores the default
        CHECK(kb->key == ' ' && kb->KeyLabel() == "Space");
        // A new field loads the saved binding.
        Prefs::SetInt("bind_jump", 'q');
        Scene s2("keybind2");
        auto* kb2 = Child(s2, nullptr, "jump")->AddComponent<UIKeybindField>();
        kb2->prefKey = "bind_jump";
        s2.Start(); Frame(s2);
        CHECK(kb2->key == 'q');
        // Gamepad A / Enter starts listening when focused.
        Scene s3("keybind3");
        auto* kb3 = Child(s3, nullptr, "fire")->AddComponent<UIKeybindField>();
        s3.Start();
        Input::FeedKeys({}); Input::FeedKeys({'\r'});
        CHECK(NavigateUI(s3) == kb3->gameObject && kb3->IsListening());
        Prefs::Delete("bind_jump");
    }

    // ---- Round trip ----
    {
        Scene s("rt");
        GameObject* g = s.CreateGameObject("dlg");
        g->AddComponent<UIPanel>();
        auto* md = g->AddComponent<UIModal>();
        md->dimBackground = false; md->closeOnCancel = false; md->closeOnClickOutside = true;
        auto* cm = g->AddComponent<UIContextMenu>();
        cm->items = {"Rename", "-", "Remove \"it\""}; cm->anywhere = true; cm->width = 222;
        auto* kb = s.CreateGameObject("kb")->AddComponent<UIKeybindField>();
        kb->action = "Sprint fast"; kb->key = Input::KeyShift; kb->defaultKey = 'x'; kb->prefKey = "bind sprint";
        kb->anchor = UIAnchor::Center; kb->focusable = false;
        Scene b("rt2");
        CHECK(SceneSerializer::Deserialize(b, SceneSerializer::Serialize(s)));
        UIModal* md2 = nullptr; UIContextMenu* cm2 = nullptr; UIKeybindField* kb2 = nullptr;
        for (const auto& up : b.Objects()) {
            if (auto* x = up->GetComponent<UIModal>()) md2 = x;
            if (auto* x = up->GetComponent<UIContextMenu>()) cm2 = x;
            if (auto* x = up->GetComponent<UIKeybindField>()) kb2 = x;
        }
        CHECK(md2 && cm2 && kb2);
        if (md2) CHECK(!md2->dimBackground && !md2->closeOnCancel && md2->closeOnClickOutside);
        if (cm2) CHECK(cm2->items.size() == 3 && cm2->items[2] == "Remove \"it\"" && cm2->anywhere && cm2->width == 222);
        if (kb2) CHECK(kb2->action == "Sprint fast" && kb2->key == Input::KeyShift && kb2->defaultKey == 'x' &&
                       kb2->prefKey == "bind sprint" && kb2->anchor == UIAnchor::Center && !kb2->focusable);
    }

    TEST_MAIN_RESULT();
}
