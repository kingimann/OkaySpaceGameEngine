// Headless tests for in-game build mode (BuildMode), logic links (LogicSource /
// LogicTarget) and creations (BuildCreation): placement snapping, edit
// permissions, undo / redo, signal propagation, scripting hooks and save / load.
// Everything is driven by rays and direct calls, so no window or camera is needed.
#include "test_framework.hpp"
#include <Okay.hpp>
#include "okay/Components/VisualScriptComponent.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

using namespace okay;

namespace {

const char* kBlock  = "test_bm_block.okayprefab";
const char* kWide   = "test_bm_wide.okayprefab";
const char* kButton = "test_bm_button.okayprefab";
const char* kDoor   = "test_bm_door.okayprefab";
const char* kLamp   = "test_bm_lamp.okayprefab";
const char* kPlate  = "test_bm_plate.okayprefab";
const char* kTimer  = "test_bm_timer.okayprefab";

// Save a one-object prefab made by `fill`.
template <class F>
void MakePrefab(const char* path, const char* name, const Vec3& scale, F fill) {
    Scene mk("mk");
    GameObject* g = mk.CreateGameObject(name);
    g->transform->localScale = scale;
    g->AddComponent<MeshRenderer>()->mesh = Mesh::Cube();
    g->AddComponent<BoxCollider3D>();
    fill(g);
    SceneSerializer::SaveObjectToFile(*g, path);
}

void MakePrefabs() {
    MakePrefab(kBlock, "Block", {1, 1, 1}, [](GameObject*) {});
    MakePrefab(kWide, "Wide", {2, 1, 1}, [](GameObject* g) { g->AddComponent<Buildable>(); });
    MakePrefab(kButton, "Button", {0.5f, 0.5f, 0.5f}, [](GameObject* g) {
        g->AddComponent<LogicSource>()->kind = LogicSource::Kind::Button;
    });
    MakePrefab(kDoor, "Door", {1, 2, 0.25f}, [](GameObject* g) {
        auto* t = g->AddComponent<LogicTarget>();
        t->kind = LogicTarget::Kind::Door; t->openOffset = {0, 2, 0}; t->moveTime = 0.5f;
    });
    MakePrefab(kLamp, "Lamp", {0.5f, 0.5f, 0.5f}, [](GameObject* g) {
        g->AddComponent<Light>()->intensity = 0.3f;
        auto* t = g->AddComponent<LogicTarget>();
        t->kind = LogicTarget::Kind::Light; t->lightIntensity = 2.0f;
    });
    MakePrefab(kPlate, "Plate", {1, 0.1f, 1}, [](GameObject* g) {
        auto* s = g->AddComponent<LogicSource>();
        s->kind = LogicSource::Kind::PressurePlate;
        s->zoneSize = {0.9f, 6.0f, 0.9f};    // local units: the plate is 0.1 tall, so 6 = 0.6 world
        s->zoneOffset = {0.0f, 3.5f, 0.0f};
    });
    MakePrefab(kTimer, "Timer", {0.5f, 0.5f, 0.5f}, [](GameObject* g) {
        auto* s = g->AddComponent<LogicSource>();
        s->kind = LogicSource::Kind::Timer; s->interval = 1.0f; s->onTime = 0.5f;
    });
}
void RemovePrefabs() {
    for (const char* p : {kBlock, kWide, kButton, kDoor, kLamp, kPlate, kTimer}) std::remove(p);
}

// A flat static ground (top at y = 0) and a builder with the test palette.
BuildMode* World(Scene& s) {
    GameObject* ground = s.CreateGameObject("Ground");
    ground->transform->localPosition = {0, -0.5f, 0};
    ground->transform->localScale = {60, 1, 60};
    ground->AddComponent<BoxCollider3D>();
    ground->isStatic = true;
    auto* bm = s.CreateGameObject("Builder")->AddComponent<BuildMode>();
    bm->maxReach = 30.0f;
    bm->AddToPalette("Block", kBlock);    // 0
    bm->AddToPalette("Wide", kWide);      // 1
    bm->AddToPalette("Button", kButton);  // 2
    bm->AddToPalette("Door", kDoor);      // 3
    bm->AddToPalette("Lamp", kLamp);      // 4
    bm->AddToPalette("Plate", kPlate);    // 5
    bm->AddToPalette("Timer", kTimer);    // 6
    return bm;
}

const Vec3 kDown{0, -1, 0};
GameObject* PlaceDown(BuildMode* bm, Scene& s, int slot, float x, float z) {
    bm->SelectSlot(slot);
    return bm->PlaceAt(s, {x, 8.0f, z}, kDown);
}
void Run(Scene& s, int frames, float dt = 0.05f) { for (int i = 0; i < frames; ++i) s.Update(dt); }
int IdOf(GameObject* g) { auto* b = g ? g->GetComponent<Buildable>() : nullptr; return b ? b->buildId : 0; }
bool Near(const Vec3& a, const Vec3& b, float e = 1e-3f) { return Vec3::Distance(a, b) <= e; }

} // namespace

int main() {
    RUN_SUITE("build_mode");
    MakePrefabs();

    // ---- Snapping: flush on the surface, edges on the grid ----------------------
    {
        const Vec3 mn{-0.5f, -0.5f, -0.5f}, mx{0.5f, 0.5f, 0.5f};
        Vec3 p = BuildMode::SnapToSurface({0.3f, 0.0f, 0.2f}, {0, 1, 0}, mn, mx, 1.0f);
        CHECK(Near(p, {0.5f, 0.5f, 0.5f}));                       // sits on the floor, in a grid cell
        p = BuildMode::SnapToSurface({0.3f, 0.0f, 0.2f}, {0, 1, 0}, mn, mx, 0.0f);
        CHECK(Near(p, {0.3f, 0.5f, 0.2f}));                       // grid off: just flush
        p = BuildMode::SnapToSurface({1.0f, 0.7f, 0.4f}, {1, 0, 0}, mn, mx, 1.0f);
        CHECK(Near(p, {1.5f, 0.5f, 0.5f}));                       // against a side face
        p = BuildMode::SnapToSurface({0.2f, 3.0f, 0.2f}, {0, -1, 0}, mn, mx, 1.0f);
        CHECK_NEAR(p.y, 2.5f, 1e-4f);                             // hangs under a ceiling
        p = BuildMode::SnapToSurface({0.3f, 0.0f, 0.0f}, {0, 1, 0}, mn, mx, 1.0f, {0.25f, 0, 0});
        CHECK_NEAR(p.x, 0.75f, 1e-4f);                            // grid lines through the anchor
        // A 2-wide piece keeps its edges on grid lines too.
        p = BuildMode::SnapToSurface({0.3f, 0.0f, 0.0f}, {0, 1, 0}, {-1, -0.5f, -0.5f}, {1, 0.5f, 0.5f}, 1.0f);
        CHECK_NEAR(p.x, 0.0f, 1e-4f);
    }

    // ---- Placement through BuildMode: ghost position, stacking, rotation, validity ----
    {
        Scene s("place");
        BuildMode* bm = World(s);
        bm->SelectSlot(0);
        BuildPlacement pl = bm->ComputePlacement(s, {0.3f, 8.0f, 0.2f}, kDown);
        CHECK(pl.hit && pl.valid);
        CHECK(Near(pl.position, {0.5f, 0.5f, 0.5f}));
        GameObject* a = bm->PlaceAt(s, {0.3f, 8.0f, 0.2f}, kDown);
        CHECK(a != nullptr);
        CHECK(a && Near(a->transform->Position(), {0.5f, 0.5f, 0.5f}));
        auto* ab = a ? a->GetComponent<Buildable>() : nullptr;
        CHECK(ab && ab->placed && ab->buildId > 0 && ab->prefab == kBlock);   // added because the prefab had none
        // Aiming at the top of it stacks a second block on it.
        GameObject* b = bm->PlaceAt(s, {0.6f, 8.0f, 0.4f}, kDown);
        CHECK(b && Near(b->transform->Position(), {0.5f, 1.5f, 0.5f}));
        CHECK(IdOf(b) != IdOf(a));
        // Aiming at its side puts the next one beside it, lined up with it.
        GameObject* c = bm->PlaceAt(s, {4.0f, 0.7f, 0.45f}, {-1, 0, 0});
        CHECK(c && Near(c->transform->Position(), {1.5f, 0.5f, 0.5f}));
        CHECK(bm->PlacedCount() == 3);

        // Rotating a 2x1x1 piece by 90 degrees swaps its footprint on the grid.
        bm->SelectSlot(1);
        bm->SetYaw(90.0f);
        GameObject* w = bm->PlaceAt(s, {5.3f, 8.0f, 5.2f}, kDown);
        CHECK(w && Near(w->transform->Position(), {5.5f, 0.5f, 5.0f}));
        CHECK(w && Quat::Angle(w->transform->Rotation(), Quat::Euler(0, 90, 0)) < 0.5f);
        bm->SetYaw(0.0f);

        // A level pillar in the way: red, and nothing is placed.
        GameObject* pillar = s.CreateGameObject("Pillar");
        pillar->transform->localPosition = {3.0f, 1.0f, 0.5f};
        pillar->transform->localScale = {0.8f, 2.0f, 1.0f};
        pillar->AddComponent<BoxCollider3D>();
        bm->SelectSlot(0);
        pl = bm->ComputePlacement(s, {2.3f, 8.0f, 0.5f}, kDown);
        CHECK(pl.hit && !pl.valid && pl.reason == "Blocked");
        CHECK(bm->PlaceAt(s, {2.3f, 8.0f, 0.5f}, kDown) == nullptr);
        // Aiming at the sky: nothing to build on, unless mid-air building is allowed.
        pl = bm->ComputePlacement(s, {0, 2, 20}, {0, 1, 0});
        CHECK(!pl.hit && !pl.valid);
        bm->allowAirPlacement = true;
        pl = bm->ComputePlacement(s, {0, 2, 20}, {0, 1, 0}, 5.0f);
        CHECK(pl.valid);
        bm->allowAirPlacement = false;
        // Surfaces on layers players may not build on.
        bm->buildLayers = 1u << 5;
        pl = bm->ComputePlacement(s, {-6.0f, 8.0f, -6.0f}, kDown);
        CHECK(!pl.valid && pl.reason == "Can't build here");
        bm->buildLayers = Physics3D::kAllLayers;
        // The object cap.
        bm->maxObjects = bm->PlacedCount();
        pl = bm->ComputePlacement(s, {-6.0f, 8.0f, -6.0f}, kDown);
        CHECK(!pl.valid && pl.reason.rfind("Limit reached", 0) == 0);
        CHECK(bm->PlaceItem(0, {-6, 0.5f, -6}, Quat::Identity) == nullptr);
        bm->maxObjects = 0;
        CHECK(bm->PlaceItem(0, {-6, 0.5f, -6}, Quat::Identity) != nullptr);
        // A missing prefab can't be placed.
        bm->AddToPalette("Missing", "no_such_file.okayprefab");
        bm->SelectSlot((int)bm->palette.size() - 1);
        CHECK(bm->PlaceAt(s, {-8.0f, 8.0f, -8.0f}, kDown) == nullptr);
    }

    // ---- Permissions: only placed / buildable objects can be edited -------------
    {
        Scene s("perm");
        BuildMode* bm = World(s);
        GameObject* wall = s.CreateGameObject("LevelWall");
        wall->transform->localPosition = {0, 0.5f, 0};
        wall->AddComponent<BoxCollider3D>();
        GameObject* prop = s.CreateGameObject("LevelCrate");      // a level prop marked buildable
        prop->transform->localPosition = {4, 0.5f, 0};
        prop->AddComponent<BoxCollider3D>();
        prop->AddComponent<Buildable>();
        GameObject* locked = s.CreateGameObject("LockedProp");
        locked->transform->localPosition = {8, 0.5f, 0};
        locked->AddComponent<BoxCollider3D>();
        locked->AddComponent<Buildable>()->editable = false;

        CHECK(!bm->CanEdit(wall));
        CHECK(bm->PickAt(s, {0, 8, 0}, kDown) == nullptr);
        CHECK(!bm->RemoveAt(s, {0, 8, 0}, kDown));
        CHECK(!bm->BeginMove(wall));
        CHECK(bm->Duplicate(wall) == nullptr);
        CHECK(!bm->SetPose(wall, {1, 1, 1}, Quat::Identity));
        Run(s, 1);
        CHECK(!wall->IsDestroyed() && wall->active);              // the level is untouched

        CHECK(!bm->CanEdit(locked));
        CHECK(!bm->RemoveAt(s, {8, 8, 0}, kDown));
        CHECK(bm->CanEdit(prop));
        CHECK(bm->PickAt(s, {4, 8, 0}, kDown) == prop);
        GameObject* door = PlaceDown(bm, s, 3, -4.0f, 4.0f);
        CHECK(door && bm->CanEdit(door));
        // Links need editable ends with the right components.
        GameObject* button = PlaceDown(bm, s, 2, -8.0f, 4.0f);
        CHECK(!bm->Link(wall, door));
        CHECK(!bm->Link(button, wall));
        CHECK(!bm->Link(door, button));                          // a door isn't a source
        CHECK(bm->Link(button, door));
        CHECK(!bm->Link(button, door));                          // already linked
        CHECK(bm->RemoveAt(s, {4, 8, 0}, kDown));                // a marked level prop can go
        Run(s, 1);
        CHECK(prop->IsDestroyed());
    }

    // ---- Undo / redo of every kind of edit ---------------------------------------
    {
        Scene s("undo");
        BuildMode* bm = World(s);
        GameObject* a = PlaceDown(bm, s, 0, 0.3f, 0.3f);
        const int aid = IdOf(a);
        CHECK(bm->PlacedCount() == 1 && bm->CanUndo());
        CHECK(bm->Undo());                                        // un-place
        CHECK(bm->PlacedCount() == 0);
        Run(s, 1);
        CHECK(a->IsDestroyed());
        CHECK(bm->Redo());                                        // place again, same id, same spot
        CHECK(bm->PlacedCount() == 1);
        Buildable* back = Buildable::Find(s, aid);
        CHECK(back != nullptr);
        a = back ? back->gameObject : nullptr;
        CHECK(a && Near(a->transform->Position(), {0.5f, 0.5f, 0.5f}));

        // Move (pose) undo / redo.
        CHECK(bm->SetPose(a, {3.5f, 0.5f, 0.5f}, Quat::Euler(0, 90, 0)));
        CHECK(bm->Undo());
        CHECK(Near(a->transform->Position(), {0.5f, 0.5f, 0.5f}));
        CHECK(bm->Redo());
        CHECK(Near(a->transform->Position(), {3.5f, 0.5f, 0.5f}));

        // Carry-move: pick it up, aim elsewhere, drop; one undo puts it back.
        CHECK(bm->BeginMove(a));
        CHECK(bm->Carried() == a);
        bm->UpdateCarry(s, {-4.3f, 8.0f, -4.3f}, kDown);
        CHECK(bm->DropCarried());
        CHECK(Near(a->transform->Position(), {-4.5f, 0.5f, -4.5f}));
        CHECK(bm->Undo());
        CHECK(Near(a->transform->Position(), {3.5f, 0.5f, 0.5f}));
        // Cancelling a carry restores the pose and records nothing.
        CHECK(bm->BeginMove(a));
        bm->UpdateCarry(s, {-8.3f, 8.0f, 8.3f}, kDown);
        bm->CancelCarry();
        CHECK(Near(a->transform->Position(), {3.5f, 0.5f, 0.5f}));

        // Duplicate: the copy gets a new id and is placed when dropped.
        GameObject* copy = bm->Duplicate(a);
        CHECK(copy != nullptr && IdOf(copy) != aid);
        bm->UpdateCarry(s, {8.3f, 8.0f, 8.3f}, kDown);
        CHECK(bm->DropCarried());
        CHECK(bm->PlacedCount() == 2);
        CHECK(bm->Undo());
        CHECK(bm->PlacedCount() == 1);
        // A cancelled copy vanishes.
        GameObject* copy2 = bm->Duplicate(a);
        CHECK(copy2 != nullptr);
        bm->CancelCarry();
        CHECK(bm->PlacedCount() == 1);

        // Delete, and undo brings it back with its links.
        GameObject* button = PlaceDown(bm, s, 2, -10.0f, 0.0f);
        GameObject* door = PlaceDown(bm, s, 3, -10.0f, 4.0f);
        const int bid = IdOf(button), did = IdOf(door);
        CHECK(bm->Link(button, door));
        CHECK(bm->Remove(button));
        CHECK(Buildable::Find(s, bid) == nullptr);
        CHECK(bm->Undo());
        Buildable* rb = Buildable::Find(s, bid);
        CHECK(rb != nullptr);
        auto* rls = rb ? rb->gameObject->GetComponent<LogicSource>() : nullptr;
        CHECK(rls && rls->HasLink(did));
        // Link undo / redo.
        CHECK(bm->Undo());                                        // the next undo takes the link back off
        rb = Buildable::Find(s, bid);
        rls = rb ? rb->gameObject->GetComponent<LogicSource>() : nullptr;
        CHECK(rls && !rls->HasLink(did));
        CHECK(bm->Redo());
        CHECK(rls && rls->HasLink(did));
        // A new edit clears the redo history.
        CHECK(bm->Undo());
        CHECK(bm->CanRedo());
        PlaceDown(bm, s, 0, 12.3f, 12.3f);
        CHECK(!bm->CanRedo());
    }

    // ---- Logic links: buttons open doors, plates, timers, lights, spawners, events ----
    {
        Scene s("logic");
        BuildMode* bm = World(s);
        GameObject* button = PlaceDown(bm, s, 2, 0.0f, 0.0f);
        GameObject* door = PlaceDown(bm, s, 3, 4.0f, 0.0f);
        CHECK(button && door);
        CHECK(bm->Link(button, door));
        auto* src = button->GetComponent<LogicSource>();
        auto* dt = door->GetComponent<LogicTarget>();
        Run(s, 2);
        const Vec3 closed = door->transform->Position();
        CHECK(!dt->State() && dt->Amount() == 0.0f);
        src->Press();                                             // toggle button: on
        Run(s, 20);
        CHECK(src->Output() && dt->Powered() && dt->State());
        CHECK_NEAR(dt->Amount(), 1.0f, 1e-4f);
        CHECK(Near(door->transform->Position(), closed + Vec3{0, 2, 0}));
        src->Press();                                             // off: the door closes
        Run(s, 20);
        CHECK(!dt->State());
        CHECK(Near(door->transform->Position(), closed));

        // Momentary button: on for holdTime, then the door closes by itself.
        src->toggle = false; src->holdTime = 0.2f;
        src->Press();
        Run(s, 2);
        CHECK(dt->State());
        Run(s, 25);
        CHECK(!src->Output() && !dt->State());
        CHECK(Near(door->transform->Position(), closed));

        // Toggle-mode target: each new signal flips it.
        dt->mode = LogicTarget::Mode::Toggle;
        src->Press(); Run(s, 8);
        CHECK(dt->State());
        Run(s, 20);                                               // the button times out; the door stays open
        CHECK(dt->State());
        src->Press(); Run(s, 8);
        CHECK(!dt->State());
        dt->mode = LogicTarget::Mode::Follow;
        Run(s, 20);

        // Two sources on one target: on while either is on (OR).
        GameObject* button2 = PlaceDown(bm, s, 2, 0.0f, -4.0f);
        CHECK(bm->Link(button2, door));
        auto* src2 = button2->GetComponent<LogicSource>();
        src2->SetOutput(true);
        Run(s, 2);
        CHECK(dt->Powered());
        src2->SetOutput(false);
        Run(s, 2);
        CHECK(!dt->Powered());

        // Pressure plate: something that moves on it holds the door open.
        GameObject* plate = PlaceDown(bm, s, 5, -6.0f, 0.0f);
        CHECK(plate != nullptr);
        CHECK(bm->Link(plate, door));
        auto* ps = plate->GetComponent<LogicSource>();
        Run(s, 2);
        CHECK(ps->Occupants() == 0 && !dt->Powered());
        GameObject* crate = s.CreateGameObject("Crate");          // e.g. the player
        crate->transform->localPosition = plate->transform->Position() + Vec3{0, 0.4f, 0};
        crate->transform->localScale = {0.5f, 0.5f, 0.5f};
        crate->AddComponent<BoxCollider3D>();
        Run(s, 2);
        CHECK(ps->Occupants() == 1 && ps->Output() && dt->Powered());
        crate->isStatic = true;                                   // static level geometry doesn't count
        Run(s, 2);
        CHECK(!ps->Output());
        crate->isStatic = false;
        crate->transform->localPosition = {20, 0.5f, 20};         // walked off
        Run(s, 2);
        CHECK(!ps->Output() && !dt->Powered());
        // A placed block on the plate doesn't hold it down (it isn't a physics prop).
        GameObject* onPlate = bm->PlaceItem(0, plate->transform->Position() + Vec3{0, 0.55f, 0}, Quat::Identity);
        CHECK(onPlate != nullptr);
        Run(s, 2);
        CHECK(!ps->Output());
        // An activator tag narrows it to tagged things.
        ps->activatorTag = "Player";
        crate->transform->localPosition = plate->transform->Position() + Vec3{0, 0.4f, 0};
        Run(s, 2);
        CHECK(!ps->Output());
        crate->tag = "Player";
        Run(s, 2);
        CHECK(ps->Output());
        crate->transform->localPosition = {20, 0.5f, 20};
        Run(s, 2);

        // Timer: on for onTime out of every interval.
        GameObject* timer = PlaceDown(bm, s, 6, 8.0f, -8.0f);
        GameObject* lamp = PlaceDown(bm, s, 4, 12.0f, -8.0f);
        CHECK(timer && lamp);
        CHECK(bm->Link(timer, lamp));
        auto* ts = timer->GetComponent<LogicSource>();
        auto* lt = lamp->GetComponent<LogicTarget>();
        auto* light = lamp->GetComponent<Light>();
        Run(s, 2, 0.1f);                                          // 0.2 s: still off
        CHECK(!ts->Output() && !lt->State());
        CHECK_NEAR(light->intensity, 0.0f, 1e-4f);
        Run(s, 4, 0.1f);                                          // 0.6 s: on
        CHECK(ts->Output() && lt->State());
        CHECK_NEAR(light->intensity, 2.0f, 1e-4f);
        CHECK(lamp->GetComponent<MeshRenderer>()->emissive.r > 0.5f);
        Run(s, 5, 0.1f);                                          // 1.1 s: off again
        CHECK(!ts->Output() && !lt->State());
        // Inverted light: on while unpowered.
        lt->invert = true;
        bm->Unlink(timer, lamp);
        Run(s, 2);
        CHECK(lt->State());

        // Spawner target: one spawn per new signal, capped by maxAlive; spawns aren't placed.
        GameObject* sp = PlaceDown(bm, s, 0, -12.0f, -12.0f);
        auto* spt = sp->AddComponent<LogicTarget>();
        spt->kind = LogicTarget::Kind::Spawner; spt->spawnPrefab = kBlock; spt->maxAlive = 2;
        CHECK(bm->Link(button2, sp));
        const int placedBefore = bm->PlacedCount();
        Run(s, 1);                                                // let the new target settle (off)
        const std::size_t objsBefore = s.Objects().size();
        for (int i = 0; i < 3; ++i) { src2->SetOutput(true); Run(s, 2); src2->SetOutput(false); Run(s, 2); }
        CHECK(s.Objects().size() == objsBefore + 2);
        CHECK(bm->PlacedCount() == placedBefore);

        // Event target: sends a custom event and On Logic Signal to graphs and scripts.
        GameObject* ev = PlaceDown(bm, s, 0, -16.0f, -12.0f);
        auto* evt = ev->AddComponent<LogicTarget>();
        evt->kind = LogicTarget::Kind::Event; evt->eventName = "GateOpen";
        bm->Unlink(button2, door);                                // only the event target reacts now
        bm->Unlink(button2, sp);
        CHECK(bm->Link(button2, ev));
        GameObject* listener = s.CreateGameObject("Listener");
        auto* vc = listener->AddComponent<VisualScriptComponent>();
        std::string err;
        CHECK(vc->LoadFromText(
            "node 1 OnEvent GateOpen\nentry OnEvent:GateOpen 1\nnode 2 AddVar hits\ndefault 2 0 1\nexec 1 0 2\n"
            "node 3 OnLogicSignal\nentry OnLogicSignal 3\nnode 4 SetVar sig\nnode 5 EventName\nexec 3 0 4\ndata 4 0 5 0\n"
            "node 6 SetVar sigOn\nnode 7 EventValue\nexec 4 0 6\ndata 6 0 7 0\n", &err));
        auto* sc = listener->AddComponent<ScriptComponent>("okayscript");
        CHECK(sc->LoadSource(
            "var got = \"\";\nvar gotOn = false;\nvar custom = 0;\n"
            "function on_logic_signal(name) { got = name; gotOn = signal_on; }\n"
            "function OnEvent_GateOpen(value) { custom = custom + 1; }\n"));
        Run(s, 1);
        src2->SetOutput(true); Run(s, 2);
        CHECK(evt->State());
        CHECK_NEAR(vc->GetVariable("hits").AsFloat(), 1.0f, 1e-4f);
        CHECK(vc->GetVariable("sig").AsString() == "GateOpen");
        CHECK(vc->GetVariable("sigOn").AsBool());
        CHECK(sc->VM()->GetGlobal("got").AsString() == "GateOpen");
        CHECK(sc->VM()->GetGlobal("gotOn").AsBool());
        CHECK_NEAR(sc->VM()->GetGlobal("custom").AsFloat(), 1.0f, 1e-4f);
        src2->SetOutput(false); Run(s, 2);
        CHECK(!sc->VM()->GetGlobal("gotOn").AsBool());            // off is a signal too

        // Deleting a source cuts its signal; the door closes.
        src->toggle = true;
        src->Press(); Run(s, 20);
        CHECK(dt->State());
        CHECK(bm->Remove(button));
        Run(s, 20);
        CHECK(!dt->Powered() && !dt->State());
    }

    // ---- Build-mode events reach scripts and graphs; script builtins --------------
    {
        Scene s("hooks");
        BuildMode* bm = World(s);
        int placedCb = 0, removedCb = 0;
        bm->onPlaced = [&](GameObject*) { ++placedCb; };
        bm->onRemoved = [&](GameObject*) { ++removedCb; };
        GameObject* host = s.CreateGameObject("GameScript");
        auto* sc = host->AddComponent<ScriptComponent>("okayscript");
        CHECK(sc->LoadSource(
            "var placed = 0;\nvar removed = 0;\nvar last = \"\";\nvar slot = -1;\nvar count = 0;\n"
            "function start() {\n"
            "  build_enter();\n"
            "  build_palette_clear();\n"
            "  build_palette_add(\"Block\", \"" + std::string(kBlock) + "\", \"\");\n"
            "  slot = build_palette_add(\"Door\", \"" + std::string(kDoor) + "\", \"\");\n"
            "}\n"
            "function on_object_placed(name) { placed = placed + 1; last = name; count = build_count(); }\n"
            "function on_object_removed(name) { removed = removed + 1; }\n"));
        auto* vc = host->AddComponent<VisualScriptComponent>();
        std::string err;
        CHECK(vc->LoadFromText(
            "node 1 OnObjectPlaced\nentry OnObjectPlaced 1\nnode 2 AddVar n\ndefault 2 0 1\nexec 1 0 2\n"
            "node 3 SetVar who\nnode 4 OtherName\nexec 2 0 3\ndata 3 0 4 0\n"
            "node 5 OnObjectRemoved\nentry OnObjectRemoved 5\nnode 6 AddVar gone\ndefault 6 0 1\nexec 5 0 6\n", &err));
        s.Start();
        Run(s, 1);
        CHECK(bm->Active());
        CHECK(bm->palette.size() == 2);
        CHECK_NEAR(sc->VM()->GetGlobal("slot").AsFloat(), 1.0f, 1e-4f);
        GameObject* d = PlaceDown(bm, s, 1, 0.0f, 0.0f);
        CHECK(d && d->name == "Door");
        CHECK_NEAR(sc->VM()->GetGlobal("placed").AsFloat(), 1.0f, 1e-4f);
        CHECK(sc->VM()->GetGlobal("last").AsString() == "Door");
        CHECK_NEAR(sc->VM()->GetGlobal("count").AsFloat(), 1.0f, 1e-4f);
        CHECK_NEAR(vc->GetVariable("n").AsFloat(), 1.0f, 1e-4f);
        CHECK(vc->GetVariable("who").AsString() == "Door");
        CHECK(bm->Remove(d));
        CHECK_NEAR(sc->VM()->GetGlobal("removed").AsFloat(), 1.0f, 1e-4f);
        CHECK_NEAR(vc->GetVariable("gone").AsFloat(), 1.0f, 1e-4f);
        CHECK(bm->Undo());                                        // undo counts as placing again
        CHECK_NEAR(sc->VM()->GetGlobal("placed").AsFloat(), 2.0f, 1e-4f);
        CHECK(placedCb == 2 && removedCb == 1);

        // Visual-script build nodes.
        GameObject* g2 = s.CreateGameObject("Graph2");
        auto* vc2 = g2->AddComponent<VisualScriptComponent>();
        CHECK(vc2->LoadFromText(
            "node 1 OnKey k\nentry OnKey:k 1\nnode 2 ExitBuildMode\nexec 1 0 2\n"
            "node 3 SetVar active\nnode 4 InBuildMode\nexec 2 0 3\ndata 3 0 4 0\n"
            "node 5 SetBuildPalette \"Lamp=" + std::string(kLamp) + "|lamp.png, Block=" + kBlock + "\"\nexec 3 0 5\n", &err));
        Run(s, 1);
        Input::FeedKeys({'k'}); s.Update(0.016f); Input::FeedKeys({}); s.Update(0.016f);
        CHECK(!bm->Active());
        CHECK(!vc2->GetVariable("active").AsBool());
        CHECK(bm->palette.size() == 2 && bm->palette[0].name == "Lamp" && bm->palette[0].icon == "lamp.png");
        CHECK(bm->palette.size() == 2 && bm->palette[1].prefab == kBlock);
    }

    // ---- Save / load creations ------------------------------------------------------
    {
        const std::string path = "test_bm_creation.okaycreation";
        Vec3 doorClosed, blockPos;
        int doorId = 0;
        {
            Scene s("save");
            BuildMode* bm = World(s);
            GameObject* block = PlaceDown(bm, s, 0, 0.3f, 0.3f);
            GameObject* button = PlaceDown(bm, s, 2, 4.0f, 0.0f);
            GameObject* door = PlaceDown(bm, s, 3, 8.0f, 0.0f);
            bm->SelectSlot(1); bm->SetYaw(90.0f);
            GameObject* wide = bm->PlaceAt(s, {12.3f, 8.0f, 12.3f}, kDown);
            CHECK(block && button && door && wide);
            CHECK(bm->Link(button, door));
            Run(s, 2);
            doorClosed = door->transform->Position();
            blockPos = block->transform->Position();
            doorId = IdOf(door);
            button->GetComponent<LogicSource>()->Press();
            Run(s, 20);                                           // saved with the door open...
            CHECK(!Near(door->transform->Position(), doorClosed));
            // A level object (not placed) is not part of the creation.
            s.CreateGameObject("LevelProp")->AddComponent<Buildable>();
            std::string err;
            CHECK(BuildCreation::Save(s, path, &err));
            CHECK(Near(door->transform->Position(), doorClosed + Vec3{0, 2, 0}));   // saving didn't move it
        }
        std::string text;
        { std::ifstream f(path); std::stringstream ss; ss << f.rdbuf(); text = ss.str(); }
        CHECK(text.rfind("okaycreation 1\n", 0) == 0);
        CHECK(text.find("objects 4\n") != std::string::npos);
        CHECK(text.find("LevelProp") == std::string::npos);
        CHECK(text.find(BuildMode::kRuntimeTag) == std::string::npos);
        {
            Scene s("load");
            BuildMode* bm = World(s);
            std::string err;
            std::vector<GameObject*> added;
            CHECK(BuildCreation::Load(s, path, true, &err, &added));
            CHECK(added.size() == 4);
            CHECK(bm->PlacedCount() == 4);
            GameObject* door = nullptr, *button = nullptr, *block = nullptr, *wide = nullptr;
            for (GameObject* g : added) {
                if (g->name == "Door") door = g;
                if (g->name == "Button") button = g;
                if (g->name == "Block") block = g;
                if (g->name == "Wide") wide = g;
            }
            CHECK(door && button && block && wide);
            CHECK(block && Near(block->transform->Position(), blockPos));
            CHECK(door && Near(door->transform->Position(), doorClosed));   // ...but loads shut
            CHECK(IdOf(door) == doorId);
            CHECK(wide && Quat::Angle(wide->transform->Rotation(), Quat::Euler(0, 90, 0)) < 0.5f);
            auto* ls = button ? button->GetComponent<LogicSource>() : nullptr;
            CHECK(ls && ls->HasLink(doorId));
            CHECK(block && block->GetComponent<Buildable>()->prefab == kBlock);
            // The wiring works after loading.
            Run(s, 2);
            ls->Press();
            Run(s, 20);
            CHECK(Near(door->transform->Position(), doorClosed + Vec3{0, 2, 0}));
            // Loading again replaces rather than doubles; edits after a load start a fresh history.
            CHECK(BuildCreation::Load(s, path, true, &err));
            CHECK(bm->PlacedCount() == 4);
            CHECK(bm->LoadCreation(path));
            CHECK(bm->PlacedCount() == 4 && !bm->CanUndo());
            // Clear removes only what players placed.
            bm->ClearCreation();
            CHECK(bm->PlacedCount() == 0);
            CHECK(s.Find("Ground") != nullptr);
        }
        {   // Ids that clash with the level are renumbered, and links follow.
            Scene s("clash");
            World(s);
            GameObject* lvl = s.CreateGameObject("LevelThing");
            lvl->AddComponent<Buildable>()->buildId = doorId;
            std::vector<GameObject*> added;
            CHECK(BuildCreation::Load(s, path, true, nullptr, &added));
            GameObject* door = nullptr, *button = nullptr;
            for (GameObject* g : added) { if (g->name == "Door") door = g; if (g->name == "Button") button = g; }
            CHECK(door && button && IdOf(door) != doorId);
            CHECK(button && button->GetComponent<LogicSource>()->HasLink(IdOf(door)));
            CHECK(!button->GetComponent<LogicSource>()->HasLink(doorId));
            CHECK(lvl->GetComponent<Buildable>()->buildId == doorId);   // the level keeps its id
        }
        {   // Versioning: newer files are refused, unknown lines skipped, bad files load nothing.
            Scene s("ver");
            World(s);
            std::string err;
            CHECK(!BuildCreation::Deserialize(s, "okaycreation 99\nobjects 0\n", true, &err));
            CHECK(err.find("newer") != std::string::npos);
            CHECK(!BuildCreation::Deserialize(s, "okayscene 1\n", true, &err));
            std::string future = text;
            future.insert(future.find('\n') + 1, "author \"someone\"\nthumbnail 64 64\n");
            CHECK(BuildCreation::Deserialize(s, future, true, &err));
            CHECK(BuildCreation::PlacedRoots(s).size() == 4);
            std::string broken = "okaycreation 1\nobject\nokayscene 1\ngameobject 0 \"X\"\n  nosuchfield 1\nend\nendobject\n";
            CHECK(!BuildCreation::Deserialize(s, broken, true, &err));
            CHECK(BuildCreation::PlacedRoots(s).size() == 4);     // untouched
            CHECK(!BuildCreation::Deserialize(s, "okaycreation 1\nobject\nokayscene 1\n", true, &err));
        }
        std::remove(path.c_str());

        {   // Through BuildMode: names go to <save folder>/<creationFolder>/<name>.okaycreation.
            namespace fs = std::filesystem;
            const std::string dir = "test_bm_saves/";
            Save::SetBaseDir(dir);
            CHECK(BuildMode::CreationPath("castle") == dir + "creations/castle.okaycreation");
            CHECK(BuildMode::CreationPath("maps/castle.txt") == dir + "maps/castle.txt");
            Scene s("named");
            BuildMode* bm = World(s);
            PlaceDown(bm, s, 0, 0.3f, 0.3f);
            CHECK(bm->SaveCreation("castle"));
            CHECK(fs::exists(dir + "creations/castle.okaycreation"));
            bm->ClearCreation();
            CHECK(bm->PlacedCount() == 0);
            CHECK(bm->LoadCreation("castle"));
            CHECK(bm->PlacedCount() == 1);
            CHECK(!bm->LoadCreation("nope"));
            Save::SetBaseDir("");
            std::error_code ec; fs::remove_all(dir, ec);
        }
    }

    // ---- Scene round trip of the components; runtime objects aren't saved ---------
    {
        Scene s("ser");
        GameObject* p = s.CreateGameObject("Player");
        auto* bm = p->AddComponent<BuildMode>();
        bm->AddToPalette("Wall Piece", "Assets/wall.okayprefab", "Assets/wall.png");
        bm->AddToPalette("Door", "Assets/door.okayprefab");
        bm->gridSize = 0.5f; bm->rotationStep = 45.0f; bm->maxReach = 12.0f; bm->maxObjects = 99;
        bm->buildLayers = 0x80000001u; bm->startActive = true; bm->showHotbar = false;
        bm->creationFolder = "worlds"; bm->bindings.toggle = 'v'; bm->bindings.padPlace = 3; bm->bindings.redo = 0;
        GameObject* b = s.CreateGameObject("Btn");
        auto* bb = b->AddComponent<Buildable>(); bb->buildId = 7; bb->placed = true; bb->prefab = "Assets/btn.okayprefab";
        auto* ls = b->AddComponent<LogicSource>();
        ls->kind = LogicSource::Kind::Timer; ls->interval = 3.0f; ls->links = {8, 12}; ls->activatorTag = "Hero";
        ls->pressKey = 'f'; ls->zoneSize = {2, 3, 4};
        GameObject* t = s.CreateGameObject("Tgt");
        auto* lt = t->AddComponent<LogicTarget>();
        lt->kind = LogicTarget::Kind::Event; lt->mode = LogicTarget::Mode::Toggle; lt->invert = true;
        lt->eventName = "Boom"; lt->openOffset = {1, 2, 3}; lt->openYaw = 90.0f; lt->spawnPrefab = "x.okayprefab";
        GameObject* ghost = s.CreateGameObject("Ghost");
        ghost->tag = BuildMode::kRuntimeTag;
        s.CreateGameObject("GhostChild")->transform->SetParent(ghost->transform, false);
        const std::string text = SceneSerializer::Serialize(s);
        CHECK(text.find("Ghost") == std::string::npos);
        Scene r("r");
        std::string err;
        CHECK(SceneSerializer::Deserialize(r, text, &err));
        auto* rbm = r.Find("Player") ? r.Find("Player")->GetComponent<BuildMode>() : nullptr;
        CHECK(rbm != nullptr);
        if (rbm) {
            CHECK(rbm->palette.size() == 2 && rbm->palette[0].name == "Wall Piece" && rbm->palette[0].icon == "Assets/wall.png");
            CHECK(rbm->palette[1].prefab == "Assets/door.okayprefab");
            CHECK_NEAR(rbm->gridSize, 0.5f, 1e-5f); CHECK_NEAR(rbm->rotationStep, 45.0f, 1e-5f);
            CHECK(rbm->maxObjects == 99 && rbm->buildLayers == 0x80000001u && rbm->startActive && !rbm->showHotbar);
            CHECK(rbm->creationFolder == "worlds" && rbm->bindings.toggle == 'v' && rbm->bindings.padPlace == 3);
            CHECK(rbm->bindings.redo == 0 && rbm->bindings.undo == 'u' && rbm->bindings.removeKey == 'x');
        }
        auto* rb = r.Find("Btn") ? r.Find("Btn")->GetComponent<Buildable>() : nullptr;
        CHECK(rb && rb->buildId == 7 && rb->placed && rb->prefab == "Assets/btn.okayprefab");
        auto* rls = r.Find("Btn") ? r.Find("Btn")->GetComponent<LogicSource>() : nullptr;
        CHECK(rls && rls->kind == LogicSource::Kind::Timer && rls->links.size() == 2 && rls->links[1] == 12);
        CHECK(rls && rls->activatorTag == "Hero" && rls->pressKey == 'f' && std::fabs(rls->zoneSize.z - 4.0f) < 1e-5f);
        auto* rlt = r.Find("Tgt") ? r.Find("Tgt")->GetComponent<LogicTarget>() : nullptr;
        CHECK(rlt && rlt->kind == LogicTarget::Kind::Event && rlt->mode == LogicTarget::Mode::Toggle && rlt->invert);
        CHECK(rlt && rlt->eventName == "Boom" && std::fabs(rlt->openOffset.z - 3.0f) < 1e-5f && rlt->spawnPrefab == "x.okayprefab");
        // Assets a built game needs: palette prefabs + icons, spawner prefabs.
        auto assets = SceneSerializer::CollectAssetPaths(s);
        auto has = [&](const char* a) { return std::find(assets.begin(), assets.end(), a) != assets.end(); };
        CHECK(has("Assets/wall.okayprefab") && has("Assets/wall.png") && has("x.okayprefab"));

        // A scene from before build mode still loads exactly as it did.
        Scene old("old");
        CHECK(SceneSerializer::Deserialize(old,
            "okayscene 1\nname \"Old\"\ngameobject 0 \"Thing\"\n  active 1\n  parent -1\n"
            "  transform 1 2 3 0 0 0 1 1 1\nend\n", &err));
        CHECK(old.Find("Thing") && !old.Find("Thing")->GetComponent<Buildable>());
    }

    // ---- The hotbar, ghost and wires are runtime objects that come and go -------
    {
        Scene s("hud");
        BuildMode* bm = World(s);
        GameObject* camGo = s.CreateGameObject("Cam");
        camGo->AddComponent<Camera>();
        camGo->transform->localPosition = {0.3f, 6.0f, 0.3f};
        camGo->transform->localRotation = Quat::Euler(-90.0f, 0.0f, 0.0f);   // look straight down
        s.Start();
        s.mainCamera = camGo->GetComponent<Camera>();
        auto countRuntime = [&]() {
            int n = 0;
            for (const auto& up : s.Objects()) if (up && up->active && up->tag == BuildMode::kRuntimeTag) ++n;
            return n;
        };
        Run(s, 1);
        CHECK(countRuntime() == 0);                               // nothing until build mode is on
        bm->SetActive(true);
        Run(s, 2);
        CHECK(countRuntime() > 0);                                // hotbar + ghost
        CHECK(bm->LastPlacement().hit);
        GameObject* ghost = s.Find("BuildGhost");
        CHECK(ghost && ghost->active);
        CHECK(ghost && Near(ghost->transform->Position(), bm->LastPlacement().position));
        // Left click places at the ghost.
        Input::FeedMouse({0, 0}, 1u); s.Update(0.016f); Input::FeedMouse({0, 0}, 0u); s.Update(0.016f);
        CHECK(bm->PlacedCount() == 1);
        // The key bindings: B toggles build mode off and everything hides.
        Input::FeedKeys({'b'}); s.Update(0.016f); Input::FeedKeys({}); s.Update(0.016f);
        CHECK(!bm->Active());
        CHECK(countRuntime() == 0);
        const std::string text = SceneSerializer::Serialize(s);
        CHECK(text.find("BuildGhost") == std::string::npos && text.find("BuildModeHUD") == std::string::npos);
    }

    RemovePrefabs();
    TEST_MAIN_RESULT();
}
