#include "test_framework.hpp"
#include <Okay.hpp>

using namespace okay;

// World-space drag & drop (Draggable / DropZone). Camera ortho size 5 with a
// 800x600 canvas gives 60 px per world unit; world (0,0) maps to screen center.
int main() {
    RUN_SUITE("item_dragdrop");

    auto makeScene = [](Scene& s) {
        s.physicsEnabled = false;
        UICanvas::Set(800, 600);
        auto* camObj = s.CreateGameObject("Camera");
        auto* cam = camObj->AddComponent<Camera>();
        cam->orthographicSize = 5.0f;   // 60 px / unit
    };

    // --- Drag an item onto a drop zone; it reports the drop -------------
    {
        Scene s("dnd");
        makeScene(s);
        auto* item = s.CreateGameObject("Item");
        item->AddComponent<SpriteRenderer>()->size = {1, 1};
        item->AddComponent<Draggable>();
        auto* zone = s.CreateGameObject("Zone");
        zone->transform->localPosition = {2, 0, 0};   // screen x 520
        zone->AddComponent<SpriteRenderer>()->size = {1, 1};
        zone->AddComponent<DropZone>();
        s.Start();
        Prefs::Clear();

        Input::FeedMouse({400, 300}, 0);        s.Update(0.016f);
        Input::FeedMouse({400, 300}, 1u << 0);  s.Update(0.016f);   // grab item center
        Input::FeedMouse({520, 300}, 1u << 0);  s.Update(0.016f);   // drag onto zone
        CHECK(item->transform->Position().x > 1.0f);                // moved right
        Input::FeedMouse({520, 300}, 0);        s.Update(0.016f);   // release on zone
        CHECK(item->GetComponent<Draggable>()->LastDropTarget() == zone);
        CHECK(Prefs::GetString("drop_source", "") == "Item");
        CHECK(Prefs::GetString("drop_target", "") == "Zone");
        Prefs::Clear(); Input::FeedMouse({0, 0}, 0);
    }

    // --- Snap onto zone centers the item; tag filter rejects mismatches -
    {
        Scene s("snap");
        makeScene(s);
        auto* item = s.CreateGameObject("Gem");
        item->tag = "gem";
        item->AddComponent<SpriteRenderer>()->size = {1, 1};
        auto* d = item->AddComponent<Draggable>();
        d->snapToZone = true; d->returnToStart = true;
        auto* zone = s.CreateGameObject("Slot");
        zone->transform->localPosition = {2, 0, 0};
        zone->AddComponent<SpriteRenderer>()->size = {1, 1};
        zone->AddComponent<DropZone>()->acceptTag = "potion";   // won't accept a gem
        s.Start();
        Prefs::Clear();

        Input::FeedMouse({400, 300}, 0);        s.Update(0.016f);
        Input::FeedMouse({400, 300}, 1u << 0);  s.Update(0.016f);
        Input::FeedMouse({520, 300}, 1u << 0);  s.Update(0.016f);
        Input::FeedMouse({520, 300}, 0);        s.Update(0.016f);   // release: tag rejects
        CHECK(item->GetComponent<Draggable>()->LastDropTarget() == nullptr);
        CHECK_NEAR(item->transform->Position().x, 0.0f, 0.01f);     // returned to start

        // Now allow the tag and drop again — it should snap onto the slot.
        zone->GetComponent<DropZone>()->acceptTag = "gem";
        Input::FeedMouse({400, 300}, 0);        s.Update(0.016f);
        Input::FeedMouse({400, 300}, 1u << 0);  s.Update(0.016f);
        Input::FeedMouse({520, 300}, 1u << 0);  s.Update(0.016f);
        Input::FeedMouse({520, 300}, 0);        s.Update(0.016f);
        CHECK(item->GetComponent<Draggable>()->LastDropTarget() == zone);
        CHECK_NEAR(item->transform->Position().x, 2.0f, 0.01f);     // centered on slot
        Prefs::Clear(); Input::FeedMouse({0, 0}, 0);
    }

    TEST_MAIN_RESULT();
}
