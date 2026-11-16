// Every project template is PROVEN to work, by running it the way a person does.
//
// For each title in okayproj::Templates() (the list the launcher's Templates page and the editor's New Project form show):
//
//   create     the launcher's flow: PlanProject, EditorState::CreateProject into a temp folder with the editor's own
//              settings writer, the scene file lands in Assets/<name>.okayscene, the project reopens through the
//              version gate with no question
//   camera     exactly one active Main Camera, and it is the one the Game view renders through once Play runs
//   extras     the template's own objects: a light in 3D scenes, the player (or car) with its controller
//   roundtrip  serialise, deserialise, serialise gives the same text; nothing is skipped as an unknown record
//   problems   the Problems panel's own model (CollectProjectDiagnostics) reports no error
//   play120    Play runs 120 fixed frames: no error line in the Console, no exception; Stop puts the scene back
//              exactly as it was, and the project does not read as "changed"
//   interact   the family's own check with simulated keys and mouse (the same key mapping the editor feeds Play):
//              the player or car moves, looks around, jumps and lands, shoots, gets in and out of the car, collects
//              coins, the snake and the ball move, the Start button does something that exists, an inventory item
//              moves. It runs twice: in the scene the template just built ("fresh") and in the project reopened from
//              disk ("reopened"), because a template whose wiring is lost on save shows only in the second.
//
// A `void New...()` in EditorState.hpp that is not in the table is a scene nobody can start. It must be listed in
// MakeNotOffered() with a reason (CheckNotOffered fails otherwise); those scenes run through the same seven columns.
//
// The table of checks prints at the end (run the binary directly to see it, with the numbers it measured). A template
// with no row in MakeSpecs() fails: that is how a new template is forced to bring its own test (docs/templates_check.md).
//
// What is NOT covered here: a real window, the launcher's UI, Windows, the editor's frame loop (these tests call
// EditorState::Tick the way the editor's Play does), the project settings the editor shell applies (gravity, physics
// Hz), and pixels (nothing is rendered).
#include "test_framework.hpp"
#include "test_sandbox.hpp"
#include "play_smoke.hpp"
#include "EditorState.hpp"
#include "TemplateTable.hpp"
#include "ProjectUpgrade.hpp"
#include "ProjectState.hpp"
#include "../shared/ProjectCreate.hpp"
#include "../shared/ProjectVersion.hpp"
#include <Okay.hpp>
#include "okay/Debug/ProjectDiagnostics.hpp"
#include "okay/Scene/EnsureCamera.hpp"
#include "okay/Testing/ObjectPath.hpp"

#if defined(OKAY_PLATFORM_HAVE_SDL2) && __has_include(<SDL.h>)
#include "EditorPlayInput.hpp"   // SDL scancodes -> the keys scripts read: the same mapping the editor's Play uses
#define OKAY_TW_SDL 1
#endif

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <map>
#include <memory>
#include <set>
#include <sstream>
#include <string>
#include <vector>

using namespace okay;
using okay::editor::EditorState;
namespace fs = std::filesystem;

#ifndef OKAY_TEST_ENGINE_VERSION
#define OKAY_TEST_ENGINE_VERSION "2.0.0"
#endif

namespace {

constexpr float DT = 1.0f / 60.0f;
const char* const kRunning = OKAY_TEST_ENGINE_VERSION;   // the version "the editor" says it is

// ---- the results table --------------------------------------------------------------------------------------------

enum Col { kCreate, kCamera, kExtras, kRound, kProblems, kPlay, kInteract, kColCount };
const char* const kColName[kColCount] = {"create", "camera", "extras", "roundtrip", "problems", "play120", "interact"};

// A check that fails because of a bug deep in an engine system (not in the template's data or its builder) is listed
// here, with the reason, so it is visible and the rest of the suite stays green. `needle` is a piece of the failure
// message: only a failure that contains it is excused, so a different failure in the same cell still fails the test.
// A listed failure that does NOT happen fails the test ("remove it"), so the list cannot rot. Keep it tiny.
struct KnownFailure { const char* title; Col col; const char* needle; const char* why; };
const std::vector<KnownFailure> kKnownFailures = {
    // {"Some Template", kInteract, "piece of the message", "what is broken, in the engine system that owns it, and who has the bug"},
    {"Sandbox", kInteract, "never came to rest",
     "Physics3D's voxel terrain contact (engine/src/Physics/Physics3D.cpp, section 5) pushes a body out along the surface normal and "
     "removes the speed into it, but has no friction and never sets Rigidbody3D::groundNormal, so a player standing still on a gentle "
     "voxel slope creeps downhill at about 0.2 m/s and FirstPersonController's slope hold never sees the slope. Handed to Claude 2 (physics)."},
    {"Sandbox", kInteract, "did not come to rest before the jump", "same cause as the line above: the player creeps down the slope while idle."},
};

struct Note { std::string text; bool known = false; };
struct Cell {
    bool ran = false;
    std::vector<Note> failures;
};
struct Row {
    std::string title;
    Cell cell[kColCount];
    std::vector<std::string> consoleWarnings;   // Console warnings seen while Play ran (information, not a failure)
    std::vector<std::string> problemWarnings;   // Problems-panel warnings (information, not a failure)
    std::vector<std::string> facts;             // what the interaction measured
};

void Expect(Row& row, Col c, bool ok, const std::string& what) {
    row.cell[c].ran = true;
    ++okaytest::g_checks;
    if (!ok) row.cell[c].failures.push_back({what, false});
}

using okaytest::LogCatch;       // what the Console says while a template runs (tests/play_smoke.hpp)
using okaytest::ScreenOf;       // where a world point is on the screen
using okaytest::SomethingVisible;

// ---- simulated input ----------------------------------------------------------------------------------------------

// Hold `keys` for one frame. With SDL's headers the keys go through the editor's own mapping (scancodes -> letters),
// so a break in that mapping shows here; without them the letters are fed directly.
void FeedHeld(const std::vector<char>& keys) {
#ifdef OKAY_TW_SDL
    Uint8 ks[SDL_NUM_SCANCODES] = {};
    for (char c : keys) {
        if (c >= 'a' && c <= 'z') ks[SDL_SCANCODE_A + (c - 'a')] = 1;
        else if (c == ' ') ks[SDL_SCANCODE_SPACE] = 1;
        else if (c == Input::KeyShift) ks[SDL_SCANCODE_LSHIFT] = 1;
        else if (c == Input::KeyCtrl) ks[SDL_SCANCODE_LCTRL] = 1;
        else if (c == Input::KeyUp) ks[SDL_SCANCODE_UP] = 1;
        else if (c == Input::KeyDown) ks[SDL_SCANCODE_DOWN] = 1;
        else if (c == Input::KeyLeft) ks[SDL_SCANCODE_LEFT] = 1;
        else if (c == Input::KeyRight) ks[SDL_SCANCODE_RIGHT] = 1;
    }
    std::vector<char> down;
    okay::editor::CollectEditorPlayKeys(down, ks, SDL_NUM_SCANCODES);
    Input::FeedKeys(down);
#else
    Input::FeedKeys(keys);
#endif
}

// ---- one Play session ---------------------------------------------------------------------------------------------

struct Session {
    EditorState& ed;
    std::vector<char> keys;
    Vec2 mouse{640.0f, 360.0f};
    unsigned buttons = 0;
    int frames = 0;
    std::string threw;   // what an exception said, if one escaped a frame

    explicit Session(EditorState& e) : ed(e) {}

    void Begin() {
        Input::FeedKeys({});
        Input::FeedMouse(mouse, 0);   // park the pointer first: the first frame must not see a mouse-look jump
        Input::FeedMouse(mouse, 0);
        ed.Play();
    }
    void Frame() {
        if (!threw.empty()) return;
        try {
            FeedHeld(keys);
            Input::FeedMouse(mouse, buttons);
            ed.Tick(DT);
            ++frames;
        } catch (const std::exception& e) { threw = e.what(); }
        catch (...) { threw = "unknown exception"; }
    }
    void Frames(int n) { for (int i = 0; i < n; ++i) Frame(); }
    void Hold(std::vector<char> k, int n) { keys = std::move(k); Frames(n); keys.clear(); }
    void Press(char k) { keys.clear(); Frame(); keys = {k}; Frame(); keys.clear(); Frame(); }
    void End() { Input::FeedKeys({}); Input::FeedMouse(mouse, 0); ed.Stop(); }
    Scene& scene() { return ed.scene(); }
    GameObject* Obj(const char* name) { return ed.scene().Find(name); }
    Vec3 Pos(const char* name) { GameObject* g = Obj(name); return g && g->transform ? g->transform->Position() : Vec3{0, 0, 0}; }
};

std::string Num(float v, int digits = 2) { char b[32]; std::snprintf(b, sizeof(b), "%.*f", digits, (double)v); return b; }

float Dist2D(const Vec3& a, const Vec3& b, bool xz) {   // the ground distance: x/z in 3D, x/y in 2D
    const float dx = a.x - b.x, dz = xz ? a.z - b.z : a.y - b.y;
    return std::sqrt(dx * dx + dz * dz);
}

// Step with no keys until the body stops moving (it falls onto the floor first). Returns the frames it took, or -1 when
// it never came to rest within `maxFrames`.
int SettleBody(Session& s, const char* name, int maxFrames = 420) {
    Vec3 last = s.Pos(name);
    int still = 0;
    for (int i = 0; i < maxFrames; ++i) {
        s.Frame();
        const Vec3 p = s.Pos(name);
        const float d = std::fabs(p.x - last.x) + std::fabs(p.y - last.y) + std::fabs(p.z - last.z);
        still = d < 0.0015f ? still + 1 : 0;
        last = p;
        if (still >= 20) return i + 1;
    }
    return -1;
}

bool HasComponentNamed(GameObject* g, const char* type) {
    if (!g) return false;
    for (const auto& c : g->Components()) if (c && testing::ComponentTypeName(*c) == type) return true;
    return false;
}

// Objects that are saved to the file: not runtime-only, and not a Character's generated "Rig" (the serializer's own rule).
std::size_t PersistentObjects(const Scene& s) {
    std::size_t n = 0;
    for (const auto& up : s.Objects()) {
        bool skip = false;
        for (const GameObject* g = up.get(); g && !skip; ) {
            if (g->transient) skip = true;
            const Transform* par = g->transform ? g->transform->Parent() : nullptr;
            if (g->name == "Rig" && par && par->gameObject && par->gameObject->GetComponent<Character>()) skip = true;
            g = par ? par->gameObject : nullptr;
        }
        if (!skip) ++n;
    }
    return n;
}

// ---- the interaction checks ---------------------------------------------------------------------------------------

struct Ctx {
    Row& row;
    Session& s;
    std::string world;   // "fresh" (just built) or "reopened" (loaded from the saved file)
    void Expect(bool ok, const std::string& what) { ::Expect(row, kInteract, ok, "[" + world + "] " + what); }
    void Fact(const std::string& text) { if (world == "fresh") row.facts.push_back(text); }
};

struct MoverSpec {
    const char* body;          // the object that moves
    std::vector<char> keys;    // held to move it
    bool xz;                   // 3D (x/z plane) or 2D (x/y)
    bool jumps;                // Space jumps: it rises, then gravity brings it back
    float minMove;             // metres (or units) it must cover in one second
};

// Distance covered per second over `frames` frames, taken from two positions.
float Speed(const Vec3& a, const Vec3& b, bool xz, int frames) { return Dist2D(a, b, xz) * 60.0f / (float)frames; }

// Hold the move keys for a second: it moved; let go: it slows down; jump: it rises and lands again.
void DriveMover(Ctx& c, const MoverSpec& m) {
    Session& s = c.s;
    c.Expect(s.Obj(m.body) != nullptr, std::string("no object called ") + m.body);
    if (!s.Obj(m.body)) return;
    const int settled = SettleBody(s, m.body);
    c.Expect(settled > 0, std::string(m.body) + " never came to rest (it keeps falling or sliding)");
    const Vec3 rest = s.Pos(m.body);
    c.Expect(rest.y > -50.0f, std::string(m.body) + " fell out of the world, y=" + Num(rest.y));

    s.keys = m.keys;
    s.Frames(55);
    const Vec3 near = s.Pos(m.body);
    s.Frames(5);
    s.keys.clear();
    const Vec3 moved = s.Pos(m.body);
    const float d = Dist2D(moved, rest, m.xz);
    const float goingAt = Speed(moved, near, m.xz, 5);
    c.Fact(std::string(m.body) + " moved " + Num(d) + " in 1 s (" + Num(goingAt, 1) + "/s at the end)");
    c.Expect(d > m.minMove, std::string(m.body) + " held the move keys for 1 s and moved only " + Num(d) + " (needs more than " + Num(m.minMove) + ")");

    // Keys up: it must slow down (a car coasts, a character stops), never keep accelerating.
    const Vec3 r0 = s.Pos(m.body);
    s.Frames(5);
    const float atRelease = Speed(s.Pos(m.body), r0, m.xz, 5);
    s.Frames(100);
    const Vec3 r1 = s.Pos(m.body);
    s.Frames(5);
    const float later = Speed(s.Pos(m.body), r1, m.xz, 5);
    c.Expect(later < 0.8f * atRelease + 0.05f, std::string(m.body) + " does not slow down after the keys are let go (" + Num(atRelease, 1) + "/s then, " + Num(later, 1) + "/s 2 s later)");

    if (m.jumps) {
        s.Frames(120);   // a car or a character that was still coasting comes to a stop first
        const int again = SettleBody(s, m.body);
        c.Expect(again > 0, std::string(m.body) + " did not come to rest before the jump");
        const float y0 = s.Pos(m.body).y;
        s.Press(' ');
        float top = y0;
        for (int i = 0; i < 90; ++i) { s.Frame(); top = std::max(top, s.Pos(m.body).y); }
        c.Fact(std::string("jump rose ") + Num(top - y0));
        c.Expect(top - y0 > 0.3f, std::string("Space did not make ") + m.body + " jump: it rose " + Num(top - y0));
        s.Frames(150);
        const float y1 = s.Pos(m.body).y;
        c.Expect(y1 < y0 + 0.2f && y1 > y0 - 1.5f,
                 std::string("gravity did not bring ") + m.body + " back to the ground after the jump: " + Num(y0) + " -> " + Num(y1));
    }
}

// The yaw of whatever turns when the mouse moves (the first or third person controller, the shooter's orbit).
bool LookAngle(GameObject* player, float& yaw) {
    if (!player) return false;
    if (auto* pc = PlayerController::Of(player)) {
        if (pc->CurrentView() == 0 && pc->first()) { yaw = pc->first()->yaw; return true; }
        if (pc->third()) { yaw = pc->third()->yaw; return true; }
    }
    if (auto* t = player->GetComponent<ThirdPersonShooterController>()) { yaw = t->yaw; return true; }
    return false;
}

// Mouse-look: move the pointer sideways and the view turns.
void CheckMouseLook(Ctx& c, const char* playerName) {
    Session& s = c.s;
    float before = 0, after = 0;
    GameObject* player = s.Obj(playerName);
    const bool can = LookAngle(player, before);
    c.Expect(can, "nothing reads the mouse to look around");
    if (!can) return;
    for (int i = 1; i <= 10; ++i) { s.mouse = {640.0f + 12.0f * (float)i, 360.0f}; s.Frame(); }
    s.Frames(5);
    LookAngle(player, after);
    c.Fact("mouse-look turned " + Num(std::fabs(after - before), 1) + " degrees");
    c.Expect(std::fabs(after - before) > 3.0f, "moving the mouse 120 pixels did not turn the view (" + Num(before, 1) + " -> " + Num(after, 1) + ")");
    s.mouse = {640.0f, 360.0f}; s.Frames(3);
}

float TotalHealth(Scene& sc, const char* name) {
    float h = 0;
    for (const auto& up : sc.Objects()) if (up->name == name) if (auto* hp = up->GetComponent<HealthStat>()) h += hp->health;
    return h;
}

// Shooting: hold the left mouse button and the targets in front lose health.
void CheckShooting(Ctx& c, const char* targetName) {
    Session& s = c.s;
    s.Frames(30);
    const float before = TotalHealth(s.scene(), targetName);
    c.Expect(before > 0.0f, std::string("the ") + targetName + " objects have no health, so nothing can be shot");
    s.buttons = 1; s.Frames(90); s.buttons = 0; s.Frames(5);
    const float after = TotalHealth(s.scene(), targetName);
    c.Fact(std::string("shooting took ") + Num(before - after, 0) + " health off the " + targetName + " objects");
    c.Expect(after < before - 0.5f, std::string("holding the left mouse button for 1.5 s did not hurt any ") + targetName + " (health " + Num(before, 0) + " -> " + Num(after, 0) + ")");
}

void DriveStatic(Ctx& c) {   // nothing to steer: it must simply sit there and run
    c.s.Frames(60);
    c.Expect(c.s.threw.empty(), "an exception while the scene ran: " + c.s.threw);
}

void DriveHumanoidWiring(Ctx& c) {   // the one-call humanoid setup: foot IK and root motion wired to the rig
    Session& s = c.s;
    s.Frames(3);
    GameObject* p = s.Obj("Player");
    auto* fik = p ? p->GetComponent<FootIK>() : nullptr;
    auto* rm = p ? p->GetComponent<RootMotion>() : nullptr;
    c.Expect(fik && rm, "the Player has no FootIK or no RootMotion");
    if (!fik || !rm) return;
    c.Expect(fik->leftHip && fik->leftKnee && fik->leftFoot && fik->rightHip && fik->rightKnee && fik->rightFoot && fik->pelvis,
             "foot IK is not wired to the rig's hips, knees, feet and pelvis");
    c.Expect(rm->rootNode != nullptr, "root motion is not wired to the rig's hips (root node is empty)");
}

void DriveInventory(Ctx& c) {
    Session& s = c.s;
    DriveMover(c, {"Player", {'d'}, false, false, 1.0f});   // it is a top-down 2D player too
    GameObject* player = s.Obj("Player");
    auto* bag = player ? player->GetComponent<Inventory>() : nullptr;
    c.Expect(bag != nullptr, "the Player has no Inventory");
    GameObject* hud = s.Obj("InventoryUI");
    auto* ui = hud ? hud->GetComponent<InventoryUI>() : nullptr;
    c.Expect(ui != nullptr, "there is no InventoryUI (the item grid)");
    if (!bag || !ui) return;
    c.Expect(ui->Inv() == bag, "the item grid is not wired to the Player's bag (inventoryTarget)");
    c.Expect(bag->Count("Wood") == 8 && bag->Count("Stone") == 4 && bag->Count("Torch") == 2, "the sample bag does not hold the sample items");
    c.Expect(bag->capacity >= 36, "the bag has fewer slots than the grid (9 hotbar + 27 backpack)");
    const std::string first = bag->slots.empty() ? "" : bag->slots[0].item;
    ui->MoveSlot(0, 5);   // what dropping a dragged stack on another slot does
    c.Expect((int)bag->slots.size() > 5 && bag->slots[5].item == first && Inventory::Empty(bag->slots[0]), "dragging the first stack to slot 5 did not move it");
    ui->MoveSlot(5, 0);
    c.Expect(!bag->slots.empty() && bag->slots[0].item == first, "dragging it back did not work");
    GameObject* chest = s.Obj("Chest");
    c.Expect(chest && chest->GetComponent<ChestInventory>() && chest->GetComponent<Inventory>() && chest->GetComponent<Inventory>()->Count("Apple") == 5, "the chest is missing or empty");
    GameObject* bench = s.Obj("Workbench");
    auto* cs = bench ? bench->GetComponent<CraftingStation>() : nullptr;
    c.Expect(cs && !cs->recipes.empty(), "the workbench has no recipes");
}

void DriveMultiplayer(Ctx& c) {   // no network is opened: the host / join controls must exist and the scene must run offline
    Session& s = c.s;
    GameObject* player = s.Obj("Player");
    auto* sc = player ? player->GetComponent<ScriptComponent>() : nullptr;
    c.Expect(sc != nullptr, "the Player has no script");
    if (sc) {
        const std::string& src = sc->Source();
        c.Expect(src.find("net_host") != std::string::npos && src.find("net_join") != std::string::npos, "the Player's script does not host and join");
    }
    GameObject* help = s.Obj("Help");
    auto* ht = help ? help->GetComponent<TextRenderer>() : nullptr;
    c.Expect(ht && ht->text.find("host") != std::string::npos && ht->text.find("join") != std::string::npos, "the help line does not say how to host and join");
    GameObject* status = s.Obj("NetStatus");
    auto* st = status ? status->GetComponent<TextRenderer>() : nullptr;
    c.Expect(st != nullptr, "there is no NetStatus line");
    s.Frames(30);
    c.Expect(st && st->text == "Net: offline", "the status line does not say offline before anything is hosted: '" + (st ? st->text : std::string()) + "'");
    const Vec3 a = s.Pos("Player");
    s.Hold({'d'}, 30);   // the script's own WASD movement works offline too
    c.Expect(Dist2D(s.Pos("Player"), a, false) > 0.5f, "WASD does not move the Player offline");
}

void DriveCoinCollector(Ctx& c) {
    Session& s = c.s;
    auto coins = [&] { int n = 0; for (const auto& up : s.scene().Objects()) if (up->name == "Coin") ++n; return n; };
    c.Expect(coins() == 3, "the level should start with 3 coins, has " + std::to_string(coins()));
    s.Frames(2);   // the player's start() runs on the first frame and sets the score to 0
    c.Expect(Prefs::GetInt("score") == 0, "the score does not start at 0");
    s.Hold({'w'}, 20);          // up to the middle coin
    const int afterUp = Prefs::GetInt("score");
    c.Expect(afterUp >= 1 && coins() <= 2, "walking onto a coin did not score or remove it (score " + std::to_string(afterUp) + ", coins left " + std::to_string(coins()) + ")");
    s.Hold({'a'}, 30);          // along to the left coin
    s.Hold({'d'}, 60);          // and across to the right one
    c.Fact("collected " + std::to_string(Prefs::GetInt("score")) + " of 3 coins");
    c.Expect(Prefs::GetInt("score") == 3 && coins() == 0, "after walking over all three coins the score is " + std::to_string(Prefs::GetInt("score")) + " with " + std::to_string(coins()) + " left");
    GameObject* hud = s.Obj("HUD");
    auto* tr = hud ? hud->GetComponent<TextRenderer>() : nullptr;
    s.Frames(2);
    c.Expect(tr && tr->text == "Score: " + std::to_string(Prefs::GetInt("score")), "the HUD does not show the score: '" + (tr ? tr->text : std::string()) + "'");
}

std::set<std::pair<int, int>> TileCells(Tilemap* tm, int id) {
    std::set<std::pair<int, int>> cells;
    for (int y = 0; y < tm->Height(); ++y)
        for (int x = 0; x < tm->Width(); ++x)
            if (tm->GetTile(x, y) == id) cells.insert({x, y});
    return cells;
}

void DriveSnake(Ctx& c) {
    Session& s = c.s;
    GameObject* board = s.Obj("Board");
    auto* tm = board ? board->GetComponent<Tilemap>() : nullptr;
    c.Expect(tm != nullptr, "there is no Board with a Tilemap");
    if (!tm) return;
    s.Frames(2);
    c.Expect(tm->Width() == 16 && tm->Height() == 16, "the board is not 16 x 16");
    const auto before = TileCells(tm, 1);
    c.Expect(before.size() >= 2, "the snake is not drawn");
    c.Expect(TileCells(tm, 2).size() == 1, "there is not exactly one piece of food");
    s.Frames(30);   // a step every 0.14 s: it moves right on its own
    c.Expect(TileCells(tm, 1) != before, "the snake did not move by itself");
    s.Hold({'w'}, 12);   // turn up: the snake now has cells on more than one row
    std::set<int> ys;
    for (const auto& cell : TileCells(tm, 1)) ys.insert(cell.second);
    c.Expect(ys.size() >= 2, "pressing W did not turn the snake up");
    s.Frames(240);
    c.Expect(!TileCells(tm, 1).empty(), "the snake disappeared (the game did not restart after a crash)");
}

void DriveBrickBreaker(Ctx& c) {
    Session& s = c.s;
    auto bricks = [&] { int n = 0; for (const auto& up : s.scene().Objects()) if (up->name.rfind("Brick", 0) == 0) ++n; return n; };
    c.Expect(bricks() == 18, "the wall should start with 18 bricks, has " + std::to_string(bricks()));
    s.Frames(2);
    const Vec3 b0 = s.Pos("Ball");
    s.Frames(30);
    c.Expect(Dist2D(s.Pos("Ball"), b0, false) > 1.0f, "the ball does not move");
    const float p0 = s.Pos("Paddle").x;
    s.Hold({'d'}, 30);
    c.Expect(s.Pos("Paddle").x > p0 + 1.0f, "D does not move the paddle right");
    s.Hold({'a'}, 60);
    c.Expect(s.Pos("Paddle").x < p0 - 1.0f, "A does not move the paddle left");
    s.Frames(420);   // the ball flies about: some brick must get hit
    c.Fact(std::to_string(18 - bricks()) + " bricks hit in 8 s");
    c.Expect(bricks() < 18 && Prefs::GetInt("score") >= 1, "after 8 seconds no brick was hit (bricks left " + std::to_string(bricks()) + ", score " + std::to_string(Prefs::GetInt("score")) + ")");
    GameObject* hud = s.Obj("HUD");
    auto* tr = hud ? hud->GetComponent<TextRenderer>() : nullptr;
    c.Expect(tr && tr->text.find("Score: ") != std::string::npos, "the HUD does not show a score");
}

// A point inside a button on screen (found with the button's own hit test, so no layout maths is repeated here).
bool FindPointInside(UIButton* b, Vec2& out) {
    for (float y = 4; y < 716; y += 4)
        for (float x = 4; x < 1276; x += 4)
            if (b->Contains({x, y})) { out = {x, y}; return true; }
    return false;
}

// One click on a button: the pointer arrives, the left button goes down for a frame, then up.
void ClickButton(Session& s, UIButton* b, bool& found) {
    Vec2 where;
    found = FindPointInside(b, where);
    if (!found) return;
    s.mouse = where; s.buttons = 0; s.Frames(2);
    s.buttons = 1; s.Frame();
    s.buttons = 0; s.Frames(2);
}

void DriveMainMenu(Ctx& c) {
    Session& s = c.s;
    GameObject* start = s.Obj("StartButton");
    auto* btn = start ? start->GetComponent<UIButton>() : nullptr;
    c.Expect(btn != nullptr, "there is no Start button");
    GameObject* quit = s.Obj("QuitButton");
    auto* qsc = quit ? quit->GetComponent<ScriptComponent>() : nullptr;
    c.Expect(quit && quit->GetComponent<UIButton>() && qsc && qsc->Source().find("quit()") != std::string::npos, "the Quit button does not quit");
    if (!btn) return;
    auto* sc = start->GetComponent<ScriptComponent>();
    c.Expect(sc && sc->Source().find("on_click") != std::string::npos, "the Start button has no on_click handler");
    s.Frames(3);
    Scene& scene = s.scene();
    GameObject* panel = scene.Find("Panel");
    c.Expect(panel && panel->ActiveInHierarchy(), "the menu panel is not showing when Play starts");
    bool found = false;
    ClickButton(s, btn, found);
    c.Expect(found, "the Start button is nowhere on a 1280 x 720 screen");
    if (!found) return;
    // Start must lead somewhere that exists: a scene that loads, or the game part of this scene. A scene that is not
    // there ("could not load scene") is the bug this check is for.
    c.Expect(scene.LastLoadError().empty(), "clicking Start tried to load a scene that does not exist: " + scene.LastLoadError());
    GameObject* game = scene.Find("Game");
    const bool menuGone = scene.Find("StartButton") == nullptr;                       // another scene took its place
    const bool switched = panel && !panel->ActiveInHierarchy() && game && game->ActiveInHierarchy();   // or the game part showed
    c.Expect(menuGone || switched, "clicking Start did not start anything (the menu is still the only thing there)");
    // ... and there is a way back to the menu.
    if (switched) {
        GameObject* back = scene.Find("BackButton");
        auto* bb = back ? back->GetComponent<UIButton>() : nullptr;
        c.Expect(bb != nullptr, "the game part has no way back to the menu");
        if (bb) {
            bool f2 = false;
            ClickButton(s, bb, f2);
            c.Expect(f2 && panel->ActiveInHierarchy() && !game->ActiveInHierarchy(), "the Back button did not bring the menu back");
        }
    }
}

void DriveClickToMove(Ctx& c) {
    Session& s = c.s;
    SettleBody(s, "Player");
    c.Expect(s.scene().mainCamera != nullptr, "no main camera to click through");
    const Vec3 start = s.Pos("Player");
    Vec2 px;   // a spot on the ground 6 m to the player's right: where is it on the screen?
    const bool onScreen = ScreenOf(s.scene(), {start.x + 6.0f, 0.0f, start.z}, px);
    c.Expect(onScreen, "the click target is not on the camera's screen");
    if (!onScreen) return;
    s.mouse = px; s.buttons = 0; s.Frames(2);
    s.buttons = 1; s.Frame();
    s.buttons = 0; s.Frames(150);
    const Vec3 now = s.Pos("Player");
    c.Fact("click-to-move covered " + Num(Dist2D(now, start, true)) + " m of the 6 m asked");
    c.Expect(Dist2D(now, start, true) > 3.0f && now.x > start.x + 3.0f, "clicking the ground 6 m away moved the Player only " + Num(Dist2D(now, start, true)) + " m");
}

void DriveCarSeats(Ctx& c) {
    Session& s = c.s;
    SettleBody(s, "Player");
    GameObject* car = s.Obj("Car");
    auto* seat = car ? car->GetComponent<VehicleSeat>() : nullptr;
    auto* veh = car ? car->GetComponent<VehicleController>() : nullptr;
    c.Expect(seat && veh, "the Car has no seat or no VehicleController");
    if (!seat || !veh) return;
    GameObject* player = s.Obj("Player");
    c.Expect(player != nullptr, "there is no Player");
    if (!player) return;
    // Nobody inside: the keys do not drive the car.
    const Vec3 parked = s.Pos("Car");
    s.Hold({'w'}, 20);
    c.Expect(Dist2D(s.Pos("Car"), parked, true) < 0.1f, "the car moved with nobody driving it");
    SettleBody(s, "Player");
    // Walk up to the car, then press E.
    s.Hold({'d'}, 12);
    s.Frames(30);
    s.Press('e');
    c.Expect(seat->occupied && veh->Driver() == player, "pressing E next to the car did not seat the Player in the driver's seat");
    if (!(seat->occupied && veh->Driver() == player)) return;
    const Vec3 before = s.Pos("Car");
    s.Hold({'w'}, 60);
    const Vec3 after = s.Pos("Car");
    c.Fact("driven " + Num(Dist2D(after, before, true)) + " m in 1 s");
    c.Expect(Dist2D(after, before, true) > 3.0f, "holding W did not drive the car (moved " + Num(Dist2D(after, before, true)) + " m)");
    c.Expect(Dist2D(s.Pos("Player"), after, true) < 1.5f, "the driver did not ride with the car");
    s.Frames(90);   // let go: it slows down
    s.Press('e');
    c.Expect(!seat->occupied && veh->Driver() == nullptr, "pressing E did not get the Player out");
    c.Expect(Dist2D(s.Pos("Player"), s.Pos("Car"), true) < 4.0f && s.Pos("Player").y > -1.0f, "the Player did not step out beside the car");
}

// Top-down 2D: the two walls must stop the player.
void CheckWalls(Ctx& c) {
    Session& s = c.s;
    s.Hold({'d'}, 150);
    const float x = s.Pos("Player").x;
    c.Fact("walking right for 2.5 s ended at x=" + Num(x));
    c.Expect(x < 4.0f, "the player walked straight through the wall at x=4 (it ended at x=" + Num(x) + "): the walls have no collider");
}

// 2.5D: W hops to the layer behind, S back to the front.
void CheckLayerHop(Ctx& c) {
    Session& s = c.s;
    SettleBody(s, "Player");
    const float z0 = s.Pos("Player").z;
    s.Press('w');
    s.Frames(60);
    const float z1 = s.Pos("Player").z;
    s.Press('s');
    s.Frames(60);
    const float z2 = s.Pos("Player").z;
    c.Fact("W hopped the player " + Num(std::fabs(z1 - z0)) + " m to the back layer");
    c.Expect(std::fabs(z1 - z0) > 1.0f, "pressing W did not hop to the layer behind (z " + Num(z0) + " -> " + Num(z1) + ")");
    c.Expect(std::fabs(z2 - z0) < 0.3f, "pressing S did not hop back to the front layer (z " + Num(z0) + " -> " + Num(z2) + ")");
}

// Sandbox: look at the ground and the left mouse button digs, the right one fills.
void CheckVoxelDig(Ctx& c) {
    Session& s = c.s;
    GameObject* t = s.Obj("Terrain");
    auto* vox = t ? t->GetComponent<VoxelTerrain>() : nullptr;
    c.Expect(vox != nullptr, "there is no voxel Terrain");
    if (!vox) return;
    s.mouse = {640.0f, 360.0f}; s.Frames(3);
    for (int i = 1; i <= 15; ++i) { s.mouse = {640.0f, 360.0f + 12.0f * (float)i}; s.Frame(); }   // look down at the ground
    const std::string before = vox->EncodeDensity();
    s.buttons = 1; s.Frames(40); s.buttons = 0; s.Frames(3);
    const std::string dug = vox->EncodeDensity();
    c.Expect(dug != before, "holding the left mouse button did not dig the terrain");
    s.buttons = 2; s.Frames(40); s.buttons = 0; s.Frames(3);
    c.Expect(vox->EncodeDensity() != dug, "holding the right mouse button did not fill the terrain");
    s.mouse = {640.0f, 360.0f}; s.Frames(3);
}

// The same for the heightmap terrain (TerrainDigger): the left button lowers the ground under the crosshair, the right raises it.
void CheckTerrainDig(Ctx& c) {
    Session& s = c.s;
    GameObject* t = s.Obj("Terrain");
    auto* terr = t ? t->GetComponent<Terrain>() : nullptr;
    c.Expect(terr != nullptr, "there is no heightmap Terrain");
    if (!terr) return;
    auto sum = [&] { double v = 0; for (float h : terr->heights) v += h; return v; };
    s.mouse = {640.0f, 360.0f}; s.Frames(3);
    for (int i = 1; i <= 15; ++i) { s.mouse = {640.0f, 360.0f + 12.0f * (float)i}; s.Frame(); }   // look down at the ground
    const double before = sum();
    s.buttons = 1; s.Frames(40); s.buttons = 0; s.Frames(3);
    const double dug = sum();
    c.Fact("digging lowered the terrain by " + Num((float)(before - dug), 1) + " (summed heights)");
    c.Expect(dug < before - 0.01, "holding the left mouse button did not lower the terrain");
    s.buttons = 2; s.Frames(40); s.buttons = 0; s.Frames(3);
    c.Expect(sum() > dug + 0.01, "holding the right mouse button did not raise the terrain");
    s.mouse = {640.0f, 360.0f}; s.Frames(3);
}

// Top-down shooter: the body turns to the mouse, Space dashes, the left mouse fires and the magazine drains.
void CheckTopDownShooter(Ctx& c) {
    Session& s = c.s;
    GameObject* player = s.Obj("Player");
    auto* tds = player ? player->GetComponent<TopDownShooterController>() : nullptr;
    c.Expect(tds != nullptr, "the Player has no TopDownShooterController");
    if (!tds) return;
    SettleBody(s, "Player");
    // Aim: the pointer on the right of the screen, then on the left: the body turns to face it.
    auto facing = [&] { const Vec3 f = player->transform->Forward(); return std::atan2(f.x, f.z) * 57.29578f; };
    s.mouse = {1150.0f, 360.0f}; s.Frames(40);
    const float toRight = facing();
    s.mouse = {130.0f, 360.0f}; s.Frames(40);
    const float toLeft = facing();
    float turned = std::fabs(toRight - toLeft);
    if (turned > 180.0f) turned = 360.0f - turned;
    s.mouse = {640.0f, 360.0f}; s.Frames(10);
    c.Fact("aim swung the body " + Num(turned, 0) + " degrees between the two sides of the screen");
    c.Expect(turned > 60.0f, "the body does not turn to the mouse (it turned " + Num(turned, 0) + " degrees between the left and right of the screen)");
    SettleBody(s, "Player");
    const Vec3 p0 = s.Pos("Player");
    s.Hold({'a', ' '}, 12);
    const float dashed = Dist2D(s.Pos("Player"), p0, true);
    SettleBody(s, "Player");
    const Vec3 p1 = s.Pos("Player");
    s.Hold({'a'}, 12);
    const float walked = Dist2D(s.Pos("Player"), p1, true);
    c.Fact("dash covered " + Num(dashed) + " m in 0.2 s, walking " + Num(walked) + " m");
    c.Expect(dashed > walked + 0.5f, "Space does not dash (dash " + Num(dashed) + " m, walking " + Num(walked) + " m in the same time)");
    SettleBody(s, "Player");
    const int ammo0 = tds->ammo, shots0 = tds->ShotsFired();
    s.buttons = 1; s.Frames(45); s.buttons = 0; s.Frames(3);
    c.Fact("fired " + std::to_string(tds->ShotsFired() - shots0) + " shots in 0.75 s");
    c.Expect(tds->ShotsFired() > shots0, "holding the left mouse button fired nothing");
    c.Expect(tds->magazineSize <= 0 || tds->ammo < ammo0, "firing did not use any ammo (" + std::to_string(ammo0) + " -> " + std::to_string(tds->ammo) + ")");
}

// ---- the table: one row per template (keep in the order of okayproj::Templates()) ---------------------------------

using Drive = std::function<void(Ctx&)>;

struct Spec {
    const char* title;
    bool light;              // a Light is expected (3D scenes)
    bool ortho;              // the main camera is orthographic (2D scenes)
    const char* hero;        // "the player": the object the controller sits on ("" = none)
    const char* controller;  // component type name the hero must carry ("" = none)
    Drive drive;
};

std::vector<Spec> MakeSpecs() {
    auto mover = [](MoverSpec m, std::vector<Drive> then = {}) -> Drive {
        return [m, then](Ctx& c) { DriveMover(c, m); for (const Drive& d : then) d(c); };
    };
    auto look = [](const char* who) -> Drive { return [who](Ctx& c) { CheckMouseLook(c, who); }; };
    auto fixed = [](void (*f)(Ctx&)) -> Drive { return [f](Ctx& c) { f(c); }; };
    const Drive shoot = [](Ctx& c) { CheckShooting(c, "Target"); };
    return {
        {"3D Scene",                   true,  false, "", "", fixed(&DriveStatic)},
        {"2D Scene",                   false, true,  "", "", fixed(&DriveStatic)},
        {"Empty",                      false, false, "", "", fixed(&DriveStatic)},
        {"First Person",               true,  false, "Player", "PlayerController", mover({"Player", {'w'}, true, true, 1.0f}, {look("Player")})},
        {"Third Person",               true,  false, "Player", "PlayerController", mover({"Player", {'w'}, true, true, 1.0f}, {look("Player")})},
        {"Third Person Shooter",       true,  false, "Player", "ThirdPersonShooterController", mover({"Player", {'w'}, true, true, 1.0f}, {look("Player"), shoot})},
        {"Platformer",                 false, true,  "Player", "CharacterController2D", mover({"Player", {'d'}, false, true, 1.0f})},
        {"Top-Down",                   false, true,  "Player", "TopDownController2D", mover({"Player", {'d'}, false, false, 1.0f}, {fixed(&CheckWalls)})},
        {"Top-Down Shooter",           true,  false, "Player", "TopDownShooterController", mover({"Player", {'w'}, true, false, 1.0f}, {fixed(&CheckTopDownShooter)})},
        {"2.5D Platformer",            true,  false, "Player", "Platformer25DController", mover({"Player", {'d'}, true, true, 1.0f}, {fixed(&CheckLayerHop)})},
        {"Point & Click",              true,  false, "Player", "ClickToMoveController", fixed(&DriveClickToMove)},
        {"Vehicle (3D)",               true,  false, "Car",    "VehicleController", mover({"Car", {'w'}, true, false, 1.0f})},
        {"Vehicle (get in and drive)", true,  false, "Car",    "VehicleController", fixed(&DriveCarSeats)},
        {"Vehicle (2D)",               false, true,  "Car",    "VehicleController2D", mover({"Car", {'w'}, false, false, 1.0f})},
        {"Sandbox",                    true,  false, "Player", "PlayerController", mover({"Player", {'w'}, true, true, 1.0f}, {look("Player"), fixed(&CheckVoxelDig)})},
        {"Multiplayer",                false, true,  "Player", "ScriptComponent", fixed(&DriveMultiplayer)},
        {"Coin Collector",             false, true,  "Player", "ScriptComponent", fixed(&DriveCoinCollector)},
        {"Snake",                      false, true,  "Board",  "Tilemap", fixed(&DriveSnake)},
        {"Brick Breaker",              false, true,  "Paddle", "ScriptComponent", fixed(&DriveBrickBreaker)},
        {"Main Menu",                  false, true,  "StartButton", "UIButton", fixed(&DriveMainMenu)},
        {"Inventory",                  false, true,  "Player", "Inventory", fixed(&DriveInventory)},
        {"Humanoid",                   true,  false, "Player", "PlayerController", mover({"Player", {'w'}, true, true, 1.0f}, {fixed(&DriveHumanoidWiring), look("Player")})},
    };
}

// Scenes EditorState can build that no menu and no launcher entry reaches. Each is `void New...()` in EditorState.hpp and is
// not in the table (editor/TemplateTable.hpp), so a person cannot start it: it would rot unseen. They are listed here, with
// the reason, and run through the same checks as the templates, so the report can say they still work. CheckNotOffered()
// fails when a builder is in neither the table nor this list, and when an entry here is stale (offered by now, or gone).
// The way out of this list is to offer the scene (a row in okayproj::Templates(), in the table and in MakeSpecs()) or to
// delete the builder.
struct NotOffered {
    const char* method;               // EditorState::<method>, and the name this scene goes by in the table below
    oked::TemplateBuilder build;
    const char* why;
    Spec spec;                        // spec.title is `method`
};

std::vector<NotOffered> MakeNotOffered() {
    auto mover = [](MoverSpec m, std::vector<Drive> then = {}) -> Drive {
        return [m, then](Ctx& c) { DriveMover(c, m); for (const Drive& d : then) d(c); };
    };
    auto look = [](const char* who) -> Drive { return [who](Ctx& c) { CheckMouseLook(c, who); }; };
    auto fixed = [](void (*f)(Ctx&)) -> Drive { return [f](Ctx& c) { f(c); }; };
    return {
        {"NewPlatformer3D", &EditorState::NewPlatformer3D,
         "a 3D platformer (Templates::Platformer3D). The launcher lists the 2D and 2.5D platformers only; nothing calls this builder. "
         "The engine builder has its own tests (test_templates, test_scene_cameras).",
         {"NewPlatformer3D", true, false, "Player", "CharacterController3D", mover({"Player", {'d'}, true, true, 1.0f})}},
        {"NewTerrainSandbox", &EditorState::NewTerrainSandbox,
         "the heightmap terrain sandbox (Templates::TerrainSandbox), replaced in the launcher by the voxel Sandbox; nothing calls "
         "this builder. The engine builder has its own tests (test_terrain, test_scene_cameras).",
         {"NewTerrainSandbox", true, false, "Player", "PlayerController", mover({"Player", {'w'}, true, true, 1.0f}, {look("Player"), fixed(&CheckTerrainDig)})}},
    };
}

// ---- one template, start to finish --------------------------------------------------------------------------------

// What the editor writes into a new project before the scene opens (editor/main.cpp, DrawNewProjectPopup): the settings
// file stamped with the version, and the build settings.
auto MakeWriteFiles(const std::string& folder) {
    return [folder](const std::string& rt, std::string& err) {
        oked::ProjectSettings settings;
        const std::string generated = oked::WriteProjectSettings(settings, "");
        const std::string text = okayver::ComposeProjectFile(generated, "", okayver::NormalizeVersion(kRunning), okayup::IsKnownProjectKey);
        if (!okayproj::WriteTextAtomic(oked::ProjectSettingsFile(rt), text, &err)) return false;
        oked::BuildProfile live;
        live.productName = folder;
        live.outputDir = "build/" + folder;
        return okayproj::WriteTextAtomic(oked::BuildLiveFile(rt), oked::WriteBuildLive(live, ""), &err);
    };
}

std::string ReadAll(const fs::path& p) { std::string s; okayproj::ReadText(p.string(), s); return s; }

struct GateHook {   // the editor's version gate under EditorState::Load, for the length of one template
    okayup::ProjectGate gate;
    explicit GateHook(const std::string& running) : gate([&] { okayup::GateEnv e; e.running = running; return e; }()) {
        EditorState::beforeLoad = [this](const std::string& p, std::string* why) { return gate.MayLoad(p, why); };
    }
    ~GateHook() { EditorState::beforeLoad = nullptr; }
};

// Run `spec.drive` in a fresh Play session of `ed`, and count what the Console said meanwhile as failures.
void RunDriven(Row& row, EditorState& ed, const Spec& spec, const char* world, LogCatch& logs) {
    const std::size_t errorsBefore = logs.errors.size();
    Session s(ed);
    s.Begin();
    Ctx ctx{row, s, world};
    try { spec.drive(ctx); }
    catch (const std::exception& e) { ctx.Expect(false, std::string("exception: ") + e.what()); }
    ctx.Expect(s.threw.empty(), "an exception escaped a frame: " + s.threw);
    s.End();
    for (std::size_t i = errorsBefore; i < logs.errors.size(); ++i) ctx.Expect(false, "Console error while it was driven: " + logs.errors[i]);
}

// `build` is what the editor runs for the title (oked::FindBuilder), or, for a scene nothing offers, the method itself.
// `spec` is the title's row in MakeSpecs() (null when it has none, which fails).
void RunTemplate(const std::string& title, oked::TemplateBuilder build, const Spec* spec, const fs::path& location, Row& row) {
    row.title = title;
    Expect(row, kInteract, spec != nullptr, "no row in MakeSpecs() for this template: add one (docs/templates_check.md)");
    LogCatch logs;
    GateHook hook(kRunning);

    // ---- create: the launcher's flow ---------------------------------------------------------------------------
    const okayproj::Plan plan = okayproj::PlanProject(location.string(), title);   // the wizard names the project after the template
    Expect(row, kCreate, plan.ok(), "the title can't be used as a project name: " + plan.Problem());
    Expect(row, kCreate, build != nullptr, "the editor has no builder for this title");
    if (!plan.ok() || !build) return;

    EditorState ed;
    EditorState::NewProjectResult made;
    try {
        made = ed.CreateProject(plan.root.string(), plan.folder, build, MakeWriteFiles(plan.folder));
    } catch (const std::exception& e) { made.error = std::string("exception: ") + e.what(); }
    Expect(row, kCreate, made.ok, "CreateProject failed: " + made.error);
    if (!made.ok) return;
    const fs::path root = made.root, scenePath = made.scene;
    std::error_code ec;
    Expect(row, kCreate, scenePath == root / "Assets" / (plan.folder + ".okayscene") && fs::is_regular_file(scenePath, ec) && fs::file_size(scenePath, ec) > 100,
           "the scene file is not at Assets/<name>.okayscene: " + scenePath.string());
    for (const std::string& f : okayproj::StandardFolders())
        Expect(row, kCreate, fs::is_directory(root / "Assets" / f, ec), "the standard folder Assets/" + f + " is missing");
    Expect(row, kCreate, fs::is_regular_file(okayproj::SettingsPath(root.string()), ec), "project.okayproj was not written");
    Expect(row, kCreate, !ed.HasUnsavedChanges() && !ed.dirty, "a brand new project already has unsaved changes");
    Expect(row, kCreate, ed.projectDir() == root.string() && ed.path() == scenePath.string(), "the editor did not open the new project");
    // The version gate: stamped with this version, so no question. (A project nobody stamped would ask, which is the
    // control in main() that shows the gate is really looking.)
    Expect(row, kCreate, okayver::Decide(okayver::ReadProjectVersion(root.string()), kRunning) == okayver::Decision::Same,
           "the new project's version stamp is '" + okayver::ReadProjectVersion(root.string()) + "', not " + kRunning);
    {
        okayup::GateEnv env; env.running = kRunning;
        okayup::ProjectGate gate(env);
        int go = 0, no = 0;
        gate.Open(scenePath.string(), [&] { ++go; }, [&] { ++no; });
        Expect(row, kCreate, go == 1 && no == 0 && !gate.Busy(), "opening the new project asks a version question");
    }
    // Reopen it as a person would: a second editor loads the file.
    std::unique_ptr<EditorState> again(new EditorState);
    {
        std::string err;
        const bool ok = again->Load(scenePath.string(), &err);
        Expect(row, kCreate, ok, "the saved project does not reopen: " + err);
        if (ok) {
            Expect(row, kCreate, PersistentObjects(again->scene()) == PersistentObjects(ed.scene()),
                   "reopened with " + std::to_string(PersistentObjects(again->scene())) + " saved objects, made with " + std::to_string(PersistentObjects(ed.scene())));
            Expect(row, kCreate, !again->HasUnsavedChanges(), "a project just reopened already has unsaved changes");
            Expect(row, kCreate, again->projectDir() == root.string(), "reopening did not find the project folder");
        } else again.reset();
    }

    // ---- camera + extras -------------------------------------------------------------------------------------
    Scene& scene = ed.scene();
    {
        int mains = 0, usable = 0;
        for (const auto& up : scene.Objects())
            for (Camera* cam : up->GetComponents<Camera>())
                if (CameraUsable(cam)) { ++usable; if (cam->main) ++mains; }
        Expect(row, kCamera, mains == 1, "the scene has " + std::to_string(mains) + " active Main Cameras (needs exactly 1; " + std::to_string(usable) + " cameras in all)");
        for (const auto& up : scene.Objects())
            for (Camera* cam : up->GetComponents<Camera>())
                if (cam->main && spec)
                    Expect(row, kCamera, (cam->projection == Camera::Projection::Orthographic) == spec->ortho,
                           spec->ortho ? "a 2D template needs an orthographic camera" : "a 3D template needs a perspective camera");
    }
    if (spec) {
        bool light = false;
        for (const auto& up : scene.Objects()) if (up->GetComponent<Light>()) light = true;
        Expect(row, kExtras, light == spec->light, spec->light ? "a 3D template with no light" : "a 2D template that has a light");
        if (spec->hero[0]) {
            GameObject* hero = scene.Find(spec->hero);
            Expect(row, kExtras, hero != nullptr, std::string("no object called ") + spec->hero);
            if (spec->controller[0]) Expect(row, kExtras, HasComponentNamed(hero, spec->controller), std::string(spec->hero) + " has no " + spec->controller);
        }
    }

    // ---- roundtrip -------------------------------------------------------------------------------------------
    {
        const std::string t1 = SceneSerializer::Serialize(scene);
        Expect(row, kRound, ReadAll(scenePath) == t1, "the file on disk is not what the scene serialises to");
        Scene s2("round");
        std::string err; std::vector<std::string> warn;
        const bool ok = SceneSerializer::Deserialize(s2, t1, &err, &warn);
        Expect(row, kRound, ok, "the scene text does not load back: " + err);
        if (ok) {
            const std::string t2 = SceneSerializer::Serialize(s2);
            std::size_t at = 0;
            while (at < t1.size() && at < t2.size() && t1[at] == t2[at]) ++at;
            Expect(row, kRound, t1 == t2, "serialise, load, serialise does not give the same text: they differ at byte " + std::to_string(at) +
                   " (" + std::to_string(t1.size()) + " vs " + std::to_string(t2.size()) + " bytes), near '" + t1.substr(at > 30 ? at - 30 : 0, 70) + "'");
            Expect(row, kRound, warn.empty(), "loading skipped records it does not know: " + (warn.empty() ? std::string() : warn[0]));
            std::size_t unknown = s2.unknownRecords.size();
            for (const auto& up : s2.Objects()) unknown += up->unknownRecords.size();
            Expect(row, kRound, unknown == 0, std::to_string(unknown) + " unknown records were kept");
            Scene s3("round3");
            if (SceneSerializer::Deserialize(s3, t2)) Expect(row, kRound, SceneSerializer::Serialize(s3) == t2, "a second round trip changes the text again");
        }
    }

    // ---- problems: the Problems panel's own model ------------------------------------------------------------
    {
        ProjectDiagOptions opt;
        opt.assetRoot = ed.projectDir();
        const ProjectDiagResult r = CollectProjectDiagnostics(scene, opt);
        for (const Diagnostic& d : r.items) {
            const std::string line = "[" + d.code + "] " + d.message + (d.source.objectPath.empty() ? "" : " (" + d.source.objectPath + ")");
            if (d.severity == DiagSeverity::Error) Expect(row, kProblems, false, "Problems error " + line);
            else if (d.severity == DiagSeverity::Warning) row.problemWarnings.push_back(line);
        }
        Expect(row, kProblems, true, "");
    }

    // ---- play120: the first Play, 120 frames, then Stop -----------------------------------------------------
    const std::string textBefore = SceneSerializer::Serialize(scene);
    const std::size_t errorsBefore = logs.errors.size();
    {
        Session s(ed);
        s.Begin();
        Expect(row, kPlay, ed.isPlaying(), "Play did not start: " + ed.PlayBlockedReason());
        s.Frames(1);
        std::string seen;
        // The Empty template is empty on purpose ("nothing at all: build it your way"): a camera and nothing to look at.
        if (title != "Empty") Expect(row, kCamera, SomethingVisible(ed.scene(), &seen), "when Play starts the camera shows nothing: " + seen);
        int drawn = 0, inView = 0;
        okaytest::DrawnInView(ed.scene(), drawn, inView);
        Expect(row, kCamera, drawn == 0 || inView > 0, "the template has " + std::to_string(drawn) + " drawn objects and none is in front of the camera");
        s.Frames(119);
        Expect(row, kPlay, s.threw.empty(), "an exception escaped a frame: " + s.threw);
        Expect(row, kPlay, s.frames == 120 || !s.threw.empty(), "fewer than 120 frames ran");
        Camera* cam = ed.scene().mainCamera;
        Expect(row, kCamera, cam && cam->main && CameraUsable(cam), "while Play runs there is no usable main camera to render through");
        s.End();
        Expect(row, kPlay, !ed.isPlaying(), "Stop did not stop");
        const std::string textAfter = SceneSerializer::Serialize(ed.scene());
        std::size_t at = 0;
        while (at < textBefore.size() && at < textAfter.size() && textBefore[at] == textAfter[at]) ++at;
        Expect(row, kPlay, textAfter == textBefore, "Stop did not put the scene back as it was: the text differs at byte " + std::to_string(at) +
               " (" + std::to_string(textBefore.size()) + " vs " + std::to_string(textAfter.size()) + " bytes)");
        Expect(row, kPlay, !ed.HasUnsavedChanges(), "after Play and Stop the project reads as changed (the editor would ask to save)");
    }
    for (std::size_t i = errorsBefore; i < logs.errors.size(); ++i) Expect(row, kPlay, false, "Console error during Play: " + logs.errors[i]);
    row.consoleWarnings = logs.warnings;

    // ---- interact: Play again, driven by keys and the mouse; once in the scene as built, once as reopened ------
    if (spec) {
        RunDriven(row, ed, *spec, "fresh", logs);
        if (again) RunDriven(row, *again, *spec, "reopened", logs);
    }
}

// ---- the launcher's own list of quick templates --------------------------------------------------------------------

// launcher/main.cpp has a second hand-written list ({"label", "blurb", "<template title>"}) behind the Templates page cards.
// A title in it that okayproj::Templates() does not have would silently open the wizard on the wrong template.
void CheckLauncherQuickList() {
    const std::string text = ReadAll(fs::path(OKAY_SOURCE_DIR) / "launcher" / "main.cpp");
    const std::size_t at = text.find("struct Template { const char* name; const char* desc; const char* tmpl; };");
    CHECK(at != std::string::npos);   // if the launcher's list moved, update this test (it must not skip silently)
    if (at == std::string::npos) return;
    const std::size_t begin = text.find("const Template templates[] = {", at);
    const std::size_t end = text.find("};", begin);
    CHECK(begin != std::string::npos && end != std::string::npos);
    if (begin == std::string::npos || end == std::string::npos) return;
    std::istringstream in(text.substr(begin, end - begin));
    std::string line;
    int rows = 0;
    while (std::getline(in, line)) {
        if (line.find('{') == std::string::npos || line.find("const Template") != std::string::npos) continue;
        // the last quoted string on the line is the template title
        const std::size_t q2 = line.rfind('"');
        if (q2 == std::string::npos || q2 == 0) continue;
        const std::size_t q1 = line.rfind('"', q2 - 1);
        if (q1 == std::string::npos) continue;
        const std::string tmpl = line.substr(q1 + 1, q2 - q1 - 1);
        ++rows;
        if (!okayproj::FindTemplate(tmpl)) std::fprintf(stderr, "  the launcher's quick list names '%s', which is not a template\n", tmpl.c_str());
        CHECK(okayproj::FindTemplate(tmpl) != nullptr);
        CHECK(oked::FindBuilder(tmpl) != nullptr);
    }
    CHECK(rows >= 5);
}

// ---- the table of titles -------------------------------------------------------------------------------------------

void CheckTable() {
    const auto& launcher = okayproj::Templates();
    const auto& table = oked::TemplateTable();
    std::set<std::string> seen;
    for (const okayproj::TemplateInfo& t : launcher) {
        const std::string title = t.title ? t.title : "";
        CHECK(!title.empty());
        CHECK(seen.insert(title).second);   // no title twice
        CHECK(oked::FindBuilder(title) != nullptr);   // every launcher entry has a builder
        if (!oked::FindBuilder(title)) std::fprintf(stderr, "  the launcher lists '%s' but the editor has no builder for it\n", title.c_str());
        CHECK(t.blurb && t.blurb[0]);
        CHECK(t.group >= 1 && t.group < (int)okayproj::TemplateGroups().size());
        const char *a, *b, *c;
        okayproj::TemplateFeatureLines(t.title, a, b, c);   // the card's three lines are real, not blank
        CHECK(a[0] && b[0] && c[0]);
        CHECK(okayproj::CheckName(title).ok());   // the wizard names the project after the template: it must be a legal name
    }
    std::set<std::string> inTable;
    for (const oked::TemplateEntry& e : table) {
        const std::string title = e.title ? e.title : "";
        CHECK(!title.empty());
        CHECK(inTable.insert(title).second);   // no title twice in the table
        CHECK(e.build != nullptr);
        CHECK(okayproj::FindTemplate(title) != nullptr);   // no builder without a launcher entry
        if (!okayproj::FindTemplate(title)) std::fprintf(stderr, "  the editor has a builder for '%s' that the launcher never lists\n", title.c_str());
    }
    CHECK(table.size() == launcher.size());
    // Different titles never share a builder (two rows pointing at one function would be a copy-paste slip).
    for (std::size_t i = 0; i < table.size(); ++i)
        for (std::size_t j = i + 1; j < table.size(); ++j) {
            CHECK(table[i].build != table[j].build);
            if (table[i].build == table[j].build) std::fprintf(stderr, "  '%s' and '%s' build the same scene\n", table[i].title, table[j].title);
        }
    // Unknown and empty titles
    bool known = true;
    CHECK(oked::FindBuilder("No Such Template") == nullptr);
    CHECK(oked::FindBuilder("") == nullptr);
    CHECK(oked::FindBuilder("3d scene") == nullptr);   // titles are matched exactly
    CHECK(oked::BuilderOrDefault("No Such Template", &known) == &EditorState::NewScene3D && !known);
    CHECK(oked::BuilderOrDefault("", &known) == &EditorState::NewScene3D && known);   // no --template: the quiet default
    CHECK(oked::BuilderOrDefault("Snake", &known) == &EditorState::NewSnake && known);
    CHECK(oked::BuilderOrDefault("Snake") == &EditorState::NewSnake);   // `known` is optional
}

// ---- every builder is offered, or listed as not offered ------------------------------------------------------------------

// CheckTable() proves the table and the launcher agree. This closes the other side: a `void New...()` in EditorState.hpp that
// is in neither (a scene nobody can start) fails here until it is offered or listed in MakeNotOffered() with a reason.
void CheckNotOffered(const std::vector<NotOffered>& listed) {
    const std::string hpp = ReadAll(fs::path(OKAY_SOURCE_DIR) / "editor" / "EditorState.hpp");
    const std::string tab = ReadAll(fs::path(OKAY_SOURCE_DIR) / "editor" / "TemplateTable.hpp");
    CHECK(!hpp.empty());
    CHECK(!tab.empty());
    auto ident = [](char ch) { return std::isalnum(static_cast<unsigned char>(ch)) || ch == '_'; };
    std::set<std::string> declared, inTable;
    for (std::size_t at = hpp.find("void New"); at != std::string::npos; at = hpp.find("void New", at + 1)) {   // "void NewSnake();"
        const std::size_t b = at + 5;
        std::size_t e = b;
        while (e < hpp.size() && ident(hpp[e])) ++e;
        if (hpp.compare(e, 3, "();") == 0) declared.insert(hpp.substr(b, e - b));
    }
    for (std::size_t at = tab.find("&E::"); at != std::string::npos; at = tab.find("&E::", at + 1)) {   // "{"Snake", &E::NewSnake}"
        const std::size_t b = at + 4;
        std::size_t e = b;
        while (e < tab.size() && ident(tab[e])) ++e;
        inTable.insert(tab.substr(b, e - b));
    }
    CHECK(declared.size() >= 20);   // the scan really found EditorState's builders (if the header changed shape, update this scan)
    CHECK(inTable.size() == oked::TemplateTable().size());
    std::set<std::string> listedNames;
    for (const NotOffered& n : listed) {
        listedNames.insert(n.method);
        CHECK(n.build != nullptr);
        CHECK(n.spec.title == std::string(n.method));
        const bool gone = declared.count(n.method) == 0, offered = inTable.count(n.method) != 0;
        CHECK(!gone);      // a stale entry: the builder is not in EditorState any more
        CHECK(!offered);   // a stale entry: the launcher offers it now
        if (gone) std::fprintf(stderr, "  MakeNotOffered() lists %s, which EditorState does not declare: remove the entry\n", n.method);
        if (offered) std::fprintf(stderr, "  MakeNotOffered() lists %s, which the table offers now: remove the entry\n", n.method);
    }
    for (const std::string& name : declared) {
        const bool ok = inTable.count(name) != 0 || listedNames.count(name) != 0;
        CHECK(ok);
        if (!ok) std::fprintf(stderr, "  EditorState::%s builds a scene that neither the launcher table offers nor MakeNotOffered() lists: offer it or delete it\n", name.c_str());
    }
    for (const std::string& name : inTable) CHECK(declared.count(name) == 1);
}

// ---- the version gate really looks ----------------------------------------------------------------------------------

// A project with no stamp, and one from an older editor, DO ask, so "no question" for a new project means something.
void CheckGateControls(const fs::path& dir) {
    std::error_code ec;
    fs::create_directories(dir / "Old" / "Assets", ec);
    { std::ofstream(dir / "Old" / "project.okayproj") << "engine_version=1.0.0\n"; }
    { std::ofstream(dir / "Old" / "Assets" / "Old.okayscene") << "okayscene 3\n"; }
    okayup::GateEnv env; env.running = kRunning;
    okayup::ProjectGate gate(env);
    int go = 0;
    gate.Open((dir / "Old" / "Assets" / "Old.okayscene").string(), [&] { ++go; });
    CHECK(gate.Busy() && gate.prompt().kind == okayup::PromptKind::Older && go == 0);
    fs::create_directories(dir / "Bare" / "Assets", ec);
    { std::ofstream(dir / "Bare" / "project.okayproj") << "company=x\n"; }
    { std::ofstream(dir / "Bare" / "Assets" / "Bare.okayscene") << "okayscene 3\n"; }
    okayup::ProjectGate gate2(env);
    gate2.Open((dir / "Bare" / "Assets" / "Bare.okayscene").string(), [&] { ++go; });
    CHECK(gate2.Busy() && gate2.prompt().kind == okayup::PromptKind::Unknown && go == 0);
}

}  // namespace

int main() {
    RUN_SUITE("templates_work");
    okaytest::Sandbox sandbox("templates_work", OKAY_SOURCE_DIR);   // projects, prefs and saves land in a temp folder
    Log::console = false;
    const bool verbose = std::getenv("OKAY_TEMPLATES_VERBOSE") != nullptr;

    CheckTable();
    CheckLauncherQuickList();
    CheckGateControls(sandbox.Dir() / "gate");

    const std::vector<Spec> specs = MakeSpecs();
    for (const Spec& sp : specs) CHECK(okayproj::FindTemplate(sp.title) != nullptr);   // a stale row is as bad as a missing one
    CHECK(specs.size() == okayproj::Templates().size());

    std::vector<Row> rows;
    const fs::path location = sandbox.Dir() / "projects";
    std::error_code ec;
    fs::create_directories(location, ec);
    for (const okayproj::TemplateInfo& info : okayproj::Templates()) {
        rows.emplace_back();
        const Spec* spec = nullptr;
        for (const Spec& sp : specs) if (std::string(info.title) == sp.title) spec = &sp;
        RunTemplate(info.title, oked::FindBuilder(info.title), spec, location, rows.back());
    }
    const std::size_t templateRows = rows.size();
    const std::vector<NotOffered> notOffered = MakeNotOffered();
    CheckNotOffered(notOffered);
    for (const NotOffered& n : notOffered) {   // the same checks for the scenes nothing offers, so "it still works" is a fact
        rows.emplace_back();
        RunTemplate(n.method, n.build, &n.spec, location, rows.back());
    }

    // ---- judge the rows: a failure is excused only when kKnownFailures names it ----
    int failedCells = 0, knownCells = 0;
    std::vector<bool> used(kKnownFailures.size(), false);
    for (Row& r : rows) {
        for (int c = 0; c < kColCount; ++c) {
            for (Note& n : r.cell[c].failures)
                for (std::size_t k = 0; k < kKnownFailures.size(); ++k)
                    if (r.title == kKnownFailures[k].title && kKnownFailures[k].col == c && n.text.find(kKnownFailures[k].needle) != std::string::npos) { n.known = true; used[k] = true; }
            bool bad = false;
            for (const Note& n : r.cell[c].failures) if (!n.known) bad = true;
            if (bad) {
                ++failedCells;
                std::fprintf(stderr, "  FAIL [%s / %s]:\n", r.title.c_str(), kColName[c]);
                for (const Note& n : r.cell[c].failures) if (!n.known) { ++okaytest::g_failures; std::fprintf(stderr, "      %s\n", n.text.c_str()); }
            } else if (!r.cell[c].failures.empty()) ++knownCells;
        }
    }
    for (std::size_t k = 0; k < kKnownFailures.size(); ++k)
        if (!used[k]) {
            ++okaytest::g_failures;
            std::fprintf(stderr, "  known failure [%s / %s] '%s' did not happen: remove it from kKnownFailures\n", kKnownFailures[k].title, kColName[kKnownFailures[k].col], kKnownFailures[k].needle);
        }

    auto printRow = [](const Row& r) {
        std::printf("%-28s", r.title.c_str());
        for (int c = 0; c < kColCount; ++c) {
            const Cell& cell = r.cell[c];
            bool bad = false, known = false;
            for (const Note& n : cell.failures) { if (n.known) known = true; else bad = true; }
            std::printf(" %-9s", !cell.ran ? "-" : bad ? "FAIL" : known ? "KNOWN" : "PASS");
        }
        std::printf("  %zu / %zu\n", r.consoleWarnings.size(), r.problemWarnings.size());
    };
    std::printf("\n%-28s", "template");
    for (int c = 0; c < kColCount; ++c) std::printf(" %-9s", kColName[c]);
    std::printf("  warnings (Play / Problems)\n");
    for (std::size_t i = 0; i < templateRows; ++i) printRow(rows[i]);
    if (rows.size() > templateRows) {
        std::printf("-- EditorState builders no menu or launcher entry reaches (MakeNotOffered) --\n");
        for (std::size_t i = templateRows; i < rows.size(); ++i) printRow(rows[i]);
    }
    std::printf("\n%zu templates + %zu builders nothing offers, %d cells with a failure, %d cells with only known failures\n",
                templateRows, rows.size() - templateRows, failedCells, knownCells);
    if (!notOffered.empty()) {
        std::printf("\nNot offered (a person cannot start these; offer them or delete them; their rows above say whether they still work):\n");
        for (const NotOffered& n : notOffered) std::printf("  EditorState::%s: %s\n", n.method, n.why);
    }
    if (!kKnownFailures.empty()) {
        std::printf("\nKnown failures (engine bugs, handed over):\n");
        for (const KnownFailure& k : kKnownFailures) std::printf("  %s / %s: %s\n", k.title, kColName[k.col], k.why);
    }
    std::printf("\nWhat was measured (fresh scene):\n");
    for (const Row& r : rows) {
        std::string line;
        for (const std::string& f : r.facts) line += (line.empty() ? "" : "; ") + f;
        std::printf("  %-28s %s\n", r.title.c_str(), line.empty() ? "-" : line.c_str());
    }
    if (verbose) {
        std::printf("\nWarnings:\n");
        for (const Row& r : rows) {
            for (const std::string& w : r.problemWarnings) std::printf("  %s problems: %s\n", r.title.c_str(), w.c_str());
            for (const std::string& w : r.consoleWarnings) std::printf("  %s console: %s\n", r.title.c_str(), w.c_str());
        }
    }

    CHECK(sandbox.Verify());
    TEST_MAIN_RESULT();
}
