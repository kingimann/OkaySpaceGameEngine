// The Add Component browser as plain data. See AddComponentModel.hpp. No ImGui, no scene.
#include "AddComponentModel.hpp"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <utility>

#if defined(__GNUC__)
#include <cxxabi.h>
#endif

namespace oked {
namespace addcomp {

namespace {

std::string Lower(std::string s) {
    for (auto& c : s) c = (char)std::tolower((unsigned char)c);
    return s;
}
bool IsAlnum(char c) { return std::isalnum((unsigned char)c) != 0; }
bool StartsWith(const std::string& s, const char* p) { return s.compare(0, std::char_traits<char>::length(p), p) == 0; }
bool Contains(const std::string& s, const char* p) { return s.find(p) != std::string::npos; }

std::vector<std::string> Words(const std::string& s) {
    std::vector<std::string> out; std::string cur;
    for (char c : s) { if (IsAlnum(c)) cur += c; else if (!cur.empty()) { out.push_back(cur); cur.clear(); } }
    if (!cur.empty()) out.push_back(cur);
    return out;
}

struct Pair { const char* name; const char* text; };

// One plain line for the components people reach for most. Others use the bracket text of their menu label.
const Pair kDescriptions[] = {
    {"Sprite Renderer", "Shows a flat picture (a 2D image) for this object."},
    {"Mesh Renderer (3D)", "Shows a 3D shape or model for this object."},
    {"LOD Group", "Swaps to simpler models when the object is far away, so the game runs faster."},
    {"Occluder", "Hides things behind it from the camera, so the game runs faster."},
    {"Modifiers", "Bend, mirror or repeat a mesh and change your mind later."},
    {"Character", "A ready-made person you can pose, dress and animate."},
    {"Text", "Shows words in the world or on the screen."},
    {"Sprite Animator", "Plays a flip-book of pictures, like a walking cycle."},
    {"Particle System", "Sparks, smoke, fire, rain and other little moving bits."},
    {"Water", "A pool or sea with waves and a see-through surface."},
    {"Flashlight", "A cone of light that follows the object."},
    {"First Person Hands", "Hands and a held item in front of the camera."},
    {"Pause Menu", "A menu that stops the game so the player can rest."},
    {"Auto Save", "Saves the game for the player every little while."},
    {"Terrain Digger", "Lets the ground be dug away."},
    {"Voxel Terrain", "Ground made of little blocks that can be dug."},
    {"Foliage", "Paint trees, bushes and rocks onto a surface."},
    {"Decal Projector", "Puts a picture, like a stain or a crack, onto surfaces."},
    {"Cable", "A rope or wire that hangs between two points."},
    {"Line Renderer", "Draws a line, for lasers, ropes or arcs."},
    {"Trail Renderer", "Leaves a ribbon behind a moving object."},
    {"Animator (keyframes)", "Moves, turns and scales this object with keyframes you set."},
    {"Animator (states + transitions)", "Switches between animations, like idle, walk and jump."},
    {"Aim IK", "Points a bone, like a head or a gun, at a target."},
    {"Foot IK", "Keeps feet on the ground, even on stairs and slopes."},
    {"Look-At IK", "Makes the head and eyes follow something."},
    {"Full Body IK", "Reaches hands, feet and head to targets and moves the body to match."},
    {"Tween Animation", "Move, grow, fade, punch or shake with no code."},
    {"Rigidbody2D", "Makes this object fall, bounce and get pushed around (2D)."},
    {"Rigidbody3D", "Makes this object fall, bounce and get pushed around (3D)."},
    {"Joint 2D", "Ties this object to another with a rod, spring or pin (2D)."},
    {"Joint 3D", "Ties this object to another with a rod, spring, hinge or slider (3D)."},
    {"Box Collider 2D", "A box-shaped solid that things can hit (2D)."},
    {"Circle Collider 2D", "A round solid that things can hit (2D)."},
    {"Capsule Collider 2D", "A pill-shaped solid, good for people (2D)."},
    {"Polygon Collider 2D", "A solid with any outline you draw (2D)."},
    {"Edge Collider 2D", "A line you can stand on or bump into (2D)."},
    {"Tilemap Collider 2D", "Makes the tiles of a Tilemap solid."},
    {"Box Collider 3D", "A box-shaped solid that things can hit."},
    {"Sphere Collider 3D", "A ball-shaped solid that things can hit."},
    {"Capsule Collider 3D", "A pill-shaped solid, good for people."},
    {"Cylinder Collider 3D", "A can-shaped solid that things can hit."},
    {"Mesh Collider 3D", "A solid that follows the exact shape of the mesh."},
    {"Directional Light", "A light like the sun: it shines the same way everywhere."},
    {"Camera", "The eye of the game: what the player sees."},
    {"Camera Follow", "Makes the camera follow an object smoothly."},
    {"Virtual Camera", "A camera angle you can switch to for a cutscene."},
    {"Cinemachine Brain", "Blends between virtual cameras."},
    {"Dolly Path", "A track a camera can ride along."},
    {"Dolly Cart", "Rides a camera along a Dolly Path."},
    {"Audio Source", "Plays a sound from this object."},
    {"Music Player", "Plays background music."},
    {"New Script...", "Makes a new script file and adds it to this object."},
    {"New Visual Script", "Makes a script out of nodes, with no typing."},
    {"Canvas", "The screen that holds all your buttons and menus."},
    {"Event System", "Lets buttons and sliders hear the mouse, finger and keys."},
    {"UI Button", "A button to press."},
    {"UI Panel", "A coloured box that holds other parts of the screen."},
    {"UI Image", "A picture on the screen."},
    {"UI Slider", "A slider you drag to pick a number."},
    {"UI Toggle", "A tick box or switch."},
    {"UI Progress Bar", "A bar that fills up, like health."},
    {"UI Dialogue Box", "A box that types out words, with choices."},
    {"Minimap", "A small map of the world on the screen."},
    {"Crosshair", "An aiming mark in the middle of the screen."},
    {"Character Controller 2D", "Walk and jump in 2D with simple controls."},
    {"Character Controller 3D", "Walk and jump in 3D with simple controls."},
    {"Player Controller", "Moves the player in first or third person."},
    {"Inventory", "A bag that holds items."},
    {"Equipment", "Slots for things a character wears or holds."},
    {"Spawner", "Makes new objects, again and again."},
    {"Mover", "Moves this object along a path, with no code."},
    {"Spinner", "Spins this object round and round."},
    {"Lifetime", "Removes this object after a set time."},
    {"Collectible", "Something to pick up that gives score."},
    {"Damage On Touch", "Hurts whatever touches it, like spikes."},
    {"Teleporter", "Sends whatever touches it to another place."},
    {"Jump Pad", "Launches whatever touches it into the air."},
    {"Trigger Zone", "An invisible area that tells you when something enters."},
    {"Ragdoll", "Makes a character go floppy when it falls or is hit."},
    {"Day / Night Cycle", "Moves the sun and changes the sky through the day."},
    {"NPC Controller", "Makes a character that walks about and talks by itself."},
    {"Behavior Tree Runner", "Gives a character a brain made from a tree of choices."},
    {"Nav Mesh Agent", "Walks around walls to get to a place."},
    {"Nav Mesh Surface", "Bakes the walkable ground the agents use."},
    {"Network Manager", "Starts and joins online games."},
    {"Network Sync", "Shares where this object is with every player."},
    {"Spline", "A curve you shape with handles."},
};

// Other words people use for a component (also its older names).
const Pair kAliases[] = {
    {"Sprite Renderer", "image, picture, 2d, sprite, texture"},
    {"Mesh Renderer (3D)", "model, mesh, 3d, shape, cube"},
    {"Particle System", "particles, smoke, fire, sparks, rain, effect, vfx"},
    {"Rigidbody2D", "physics body, rigid body, gravity, falling, mass"},
    {"Rigidbody3D", "physics body, rigid body, gravity, falling, mass"},
    {"Box Collider 2D", "hitbox, solid, wall, floor"},
    {"Box Collider 3D", "hitbox, solid, wall, floor"},
    {"Sphere Collider 3D", "hitbox, ball, solid"},
    {"Capsule Collider 3D", "hitbox, pill, solid"},
    {"Mesh Collider 3D", "hitbox, solid, exact shape"},
    {"Directional Light", "light, lamp, sun"},
    {"Camera", "view, screen, eye"},
    {"Camera Follow", "camera rig, follow cam, chase"},
    {"Virtual Camera", "camera rig, cutscene camera, cinemachine"},
    {"Cinemachine Brain", "camera rig, camera blend"},
    {"Audio Source", "sound, sfx, speaker, noise"},
    {"Music Player", "song, background music, soundtrack"},
    {"Text", "label, words, writing, font"},
    {"Animator", "animation, keyframes, tween, clip"},
    {"Tween Animation", "move, fade, shake, bounce, punch"},
    {"Canvas", "gui, screen, menu, hud"},
    {"UI Button", "press, click, menu"},
    {"Character Controller 3D", "player, walk, move, jump"},
    {"Character Controller 2D", "player, walk, move, jump, platformer"},
    {"Player Controller", "player, walk, move, jump, first person, third person"},
    {"Collectible", "coin, pickup, gem, star"},
    {"Damage On Touch", "spikes, hazard, lava, hurt"},
    {"Teleporter", "portal, warp"},
    {"Trigger Zone", "sensor, area, zone, event"},
    {"Spawner", "factory, make, respawn"},
    {"Inventory", "bag, backpack, items"},
    {"Water", "sea, pool, ocean, lake"},
    {"Flashlight", "torch, spotlight, lamp"},
    {"Line Renderer", "laser, rope, beam"},
    {"Trail Renderer", "ribbon, streak, swoosh"},
    {"Nav Mesh Agent", "pathfinding, walk to, ai move"},
    {"Behavior Tree Runner", "ai, brain, npc"},
    {"New Script...", "code, program, okayscript"},
    {"Voxel Terrain", "blocks, dig, minecraft style"},
    {"Mover", "platform, moving platform, path"},
    {"Spinner", "rotate, turn"},
    {"Joint 2D", "hinge, spring, rope, hold together"},
    {"Joint 3D", "hinge, spring, rope, hold together"},
};

struct Need { const char* name; const char* reason; };
// Components that only make sense next to another one (shown greyed with this reason when not on offer).
const Need kNeeds[] = {
    {"Tilemap Collider 2D", "Needs a Tilemap on this object (or it is already added)."},
    {"UI Inventory Bind", "Needs a UI Slot Grid on this object first (or it is already added)."},
    {"Player Controller", "This object already has a controller. One is enough."},
};

struct AlsoRow { const char* name; const char* extra; };
const AlsoRow kAlso[] = {
    {"LOD Group", "Mesh Renderer"},
    {"Modifiers", "Mesh Renderer"},
};

struct Goes { const char* have; const char* suggest; const char* why; };
// `have` and `suggest` are menu labels.
const Goes kGoes[] = {
    {"Rigidbody3D", "Box Collider 3D", "A Rigidbody needs a Collider to hit things"},
    {"Rigidbody3D", "Capsule Collider 3D", "A Rigidbody needs a Collider to hit things"},
    {"Rigidbody3D", "Sphere Collider 3D", "A Rigidbody needs a Collider to hit things"},
    {"Rigidbody2D", "Box Collider 2D", "A Rigidbody needs a Collider to hit things"},
    {"Rigidbody2D", "Circle Collider 2D", "A Rigidbody needs a Collider to hit things"},
    {"Box Collider 3D", "Rigidbody3D", "Makes the Collider fall and bounce"},
    {"Sphere Collider 3D", "Rigidbody3D", "Makes the Collider fall and bounce"},
    {"Capsule Collider 3D", "Rigidbody3D", "Makes the Collider fall and bounce"},
    {"Box Collider 2D", "Rigidbody2D", "Makes the Collider fall and bounce"},
    {"Circle Collider 2D", "Rigidbody2D", "Makes the Collider fall and bounce"},
    {"Mesh Renderer (3D)", "Box Collider 3D", "So things can hit your shape"},
    {"Mesh Renderer (3D)", "LOD Group", "Faster when far away"},
    {"Sprite Renderer", "Box Collider 2D", "So things can hit your picture"},
    {"Sprite Renderer", "Sprite Animator", "Flip through pictures"},
    {"Camera", "Camera Follow", "Makes the camera follow an object"},
    {"Camera", "Virtual Camera", "A camera rig: switch angles and blend"},
    {"Camera", "Cinemachine Brain", "A camera rig: blends the angles"},
    {"Virtual Camera", "Cinemachine Brain", "Blends between camera angles"},
    {"Canvas", "Event System", "Lets buttons hear clicks"},
    {"UI Button", "Event System", "Lets buttons hear clicks"},
    {"Audio Source", "Music Player", "For background music"},
    {"Character", "Animator (states + transitions)", "Gives the character moves"},
    {"Character", "Foot IK", "Keeps feet on the ground"},
    {"Particle System", "Audio Source", "Add a sound to go with it"},
    {"Character Controller 3D", "Camera Follow", "So the camera follows the player"},
    {"Nav Mesh Agent", "Behavior Tree Runner", "Gives the walker a brain"},
};

const char* const kOrder[] = {"Rendering", "Effects", "Physics", "Cameras", "Animation", "Audio", "Scripts", "Controllers",
                              "Gameplay", "Survival", "Multiplayer", "No-Code", "UI"};

const char* const kEffects[] = {"Particle System", "Trail Renderer", "Line Renderer", "Decal Projector", "Cable", "Foliage",
                                "Water", "Flashlight"};

const Pair* FindPair(const Pair* arr, std::size_t n, const std::string& name) {
    for (std::size_t i = 0; i < n; ++i) if (name == arr[i].name) return &arr[i];
    return nullptr;
}
#define COUNT(a) (sizeof(a) / sizeof((a)[0]))

}  // namespace

// ---- words ----------------------------------------------------------------------------------------------------------------

const std::vector<std::string>& CategoryOrder() {
    static const std::vector<std::string> v(kOrder, kOrder + COUNT(kOrder));
    return v;
}

std::string PlainCategory(const std::string& raw, const std::string& name) {
    for (const char* e : kEffects) if (name == e) return "Effects";
    if (raw == "Gameplay") {
        if (StartsWith(name, "Stat: ") || StartsWith(name, "Survival") || StartsWith(name, "Status Effects") ||
            StartsWith(name, "Consumables") || StartsWith(name, "Day / Night"))
            return "Survival";
        if (Contains(name, "Controller") && !StartsWith(name, "Network")) return "Controllers";
        if (StartsWith(name, "Network") || StartsWith(name, "Net ") || StartsWith(name, "Anti-Cheat")) return "Multiplayer";
        return "Gameplay";
    }
    if (raw == "Physics 2D" || raw == "Physics 3D") return "Physics";
    if (raw == "Lighting") return "Rendering";
    if (raw == "Camera") return "Cameras";
    if (raw == "RPG / Turn-Based") return "Gameplay";
    if (raw == "No-Code Mechanics") return "No-Code";
    return raw;
}

void SplitLabel(const std::string& label, std::string& name, std::string& hint) {
    name = label; hint.clear();
    std::size_t open = label.find(" (");
    if (open == std::string::npos || label.back() != ')') return;
    std::string inside = label.substr(open + 2, label.size() - open - 3);
    if (inside.size() <= 6) return;   // "(3D)", "(item)", "(legacy)" stay part of the name
    name = label.substr(0, open);
    hint = inside;
}

std::string Describe(const std::string& name, const std::string& hint, const std::string& category) {
    // Table first (by the name, and by the name without a short bracket such as "(3D)").
    if (!hint.empty())
        if (const Pair* p = FindPair(kDescriptions, COUNT(kDescriptions), name + " (" + hint + ")")) return p->text;
    if (const Pair* p = FindPair(kDescriptions, COUNT(kDescriptions), name)) return p->text;
    std::size_t open = name.find(" (");
    if (open != std::string::npos)
        if (const Pair* p = FindPair(kDescriptions, COUNT(kDescriptions), name.substr(0, open))) return p->text;
    if (!hint.empty()) {
        std::string h = hint;
        h[0] = (char)std::toupper((unsigned char)h[0]);
        if (h.back() != '.') h += '.';
        return h;
    }
    if (Contains(name, ".okay")) return "Attaches this script to the object.";
    if (StartsWith(name, "Stat: ")) return "A number the game tracks, like " + Lower(name.substr(6)) + ".";
    return "Adds " + name + " (" + category + ") to this object.";
}

std::vector<std::string> AliasesOf(const std::string& name) {
    std::vector<std::string> out;
    if (const Pair* p = FindPair(kAliases, COUNT(kAliases), name)) {
        std::string cur;
        for (const char* c = p->text;; ++c) {
            if (*c == ',' || *c == '\0') {
                while (!cur.empty() && cur.front() == ' ') cur.erase(cur.begin());
                if (!cur.empty()) out.push_back(cur);
                cur.clear();
                if (!*c) break;
            } else cur += *c;
        }
    }
    return out;
}

std::vector<std::string> AlsoAdds(const std::string& name) {
    std::vector<std::string> out;
    for (const AlsoRow& r : kAlso) if (name == r.name) out.push_back(r.extra);
    return out;
}

std::string Badge(const std::string& name) {
    std::string b;
    bool word = true;
    for (char c : name) {
        if (IsAlnum(c)) {
            if (word && !std::isdigit((unsigned char)c)) { b += (char)std::toupper((unsigned char)c); if (b.size() == 2) break; }
            word = false;
        } else word = true;
    }
    if (b.empty()) b = "+";
    return b;
}

std::string PrettyTypeName(const std::string& in) {
    std::string s = in;
    for (const char* p : {"class ", "struct "}) if (StartsWith(s, p)) s.erase(0, std::char_traits<char>::length(p));
    std::size_t colon = s.rfind("::");
    if (colon != std::string::npos) s.erase(0, colon + 2);
    std::string out;
    for (std::size_t i = 0; i < s.size(); ++i) {
        char c = s[i];
        if (i > 0 && std::isupper((unsigned char)c) &&
            (std::islower((unsigned char)s[i - 1]) ||
             (i + 1 < s.size() && std::islower((unsigned char)s[i + 1]) && std::isupper((unsigned char)s[i - 1]))))
            out += ' ';
        out += c;
    }
    return out;
}

std::string PrettyTypeNameFromTypeInfo(const char* raw) {
    std::string s = raw ? raw : "";
#if defined(__GNUC__)
    int status = 0;
    char* d = abi::__cxa_demangle(raw, nullptr, nullptr, &status);
    if (d && status == 0) s = d;
    std::free(d);
#endif
    return PrettyTypeName(s);
}

Entry MakeEntry(const std::string& rawCategory, const std::string& label, bool absent) {
    Entry e;
    e.label = label;
    std::string hint;
    SplitLabel(label, e.name, hint);
    e.category = PlainCategory(rawCategory, e.name);
    e.key = rawCategory + "|" + label;
    e.description = Describe(e.name, hint, e.category);
    e.aliases = AliasesOf(e.name);
    e.available = absent;
    if (!absent) {
        e.reason = "Already on this object (only one allowed).";
        e.present = true;
        for (const Need& n : kNeeds) if (e.name == n.name) {
            e.reason = n.reason; e.present = false; break;
        }
    }
    return e;
}

// ---- search ---------------------------------------------------------------------------------------------------------------

int EditDistance(const std::string& a, const std::string& b) {
    std::vector<int> prev(b.size() + 1), cur(b.size() + 1);
    for (std::size_t j = 0; j <= b.size(); ++j) prev[j] = (int)j;
    for (std::size_t i = 1; i <= a.size(); ++i) {
        cur[0] = (int)i;
        for (std::size_t j = 1; j <= b.size(); ++j) {
            int sub = prev[j - 1] + (a[i - 1] == b[j - 1] ? 0 : 1);
            cur[j] = std::min(std::min(prev[j] + 1, cur[j - 1] + 1), sub);
        }
        std::swap(prev, cur);
    }
    return prev[b.size()];
}

namespace {

// Score one word of the query against the name. Fills `hits` with the letters to highlight. 0 = no match.
int ScoreName(const std::string& lname, const std::string& tok, std::vector<int>& hits) {
    if (tok.empty()) return 1;
    if (lname == tok) { for (int i = 0; i < (int)tok.size(); ++i) hits.push_back(i); return 1100; }
    if (lname.compare(0, tok.size(), tok) == 0) { for (int i = 0; i < (int)tok.size(); ++i) hits.push_back(i); return 1000; }
    // start of a later word
    for (std::size_t i = 1; i < lname.size(); ++i)
        if (!IsAlnum(lname[i - 1]) && IsAlnum(lname[i]) && lname.compare(i, tok.size(), tok) == 0) {
            for (int k = 0; k < (int)tok.size(); ++k) hits.push_back((int)i + k);
            return 800;
        }
    std::size_t at = lname.find(tok);
    if (at != std::string::npos) { for (int k = 0; k < (int)tok.size(); ++k) hits.push_back((int)at + k); return 600; }
    // letters in order ("rbd" finds "Rigidbody"); the closer together the better
    if (tok.size() >= 2) {
        std::vector<int> pos; std::size_t t = 0;
        for (std::size_t i = 0; i < lname.size() && t < tok.size(); ++i)
            if (lname[i] == tok[t]) { pos.push_back((int)i); ++t; }
        if (t == tok.size()) {
            int span = pos.back() - pos.front() + 1;
            hits.insert(hits.end(), pos.begin(), pos.end());
            return 400 - std::min(span - (int)tok.size(), 90);
        }
    }
    return 0;
}

bool WordHas(const std::string& text, const std::string& tok, bool wholeStart) {
    for (const std::string& w : Words(text)) {
        if (wholeStart ? w.compare(0, tok.size(), tok) == 0 : w.find(tok) != std::string::npos) return true;
    }
    return false;
}

}  // namespace

Match MatchEntry(const Entry& e, const std::string& query) {
    Match m;
    std::vector<std::string> toks = Words(Lower(query));
    if (toks.empty()) { m.score = 1; return m; }
    const std::string lname = Lower(e.name), ldesc = Lower(e.description);
    long total = 0;
    std::vector<int> allHits;
    for (const std::string& tok : toks) {
        std::vector<int> hits;
        int s = ScoreName(lname, tok, hits);
        if (s == 0) {
            if (WordHas(ldesc, tok, true)) s = 300;
            else if (tok.size() >= 3 && WordHas(ldesc, tok, false)) s = 260;
        }
        if (s == 0) {
            for (const std::string& a : e.aliases) {
                std::string la = Lower(a);
                if (WordHas(la, tok, true)) { s = 220; break; }
                if (tok.size() >= 3 && la.find(tok) != std::string::npos) s = 200;
            }
        }
        if (s == 0) return Match{};   // every word must match somewhere
        total += s;
        allHits.insert(allHits.end(), hits.begin(), hits.end());
    }
    std::sort(allHits.begin(), allHits.end());
    allHits.erase(std::unique(allHits.begin(), allHits.end()), allHits.end());
    m.score = (int)(total / (long)toks.size());
    m.hits = allHits;
    return m;
}

std::vector<std::string> DidYouMean(const std::vector<Entry>& entries, const std::string& query) {
    const std::string q = Lower(query);
    std::vector<std::pair<int, std::string>> best;
    for (const Entry& e : entries) {
        int d = EditDistance(q, Lower(e.name));
        for (const std::string& w : Words(Lower(e.name))) d = std::min(d, EditDistance(q, w));
        for (const std::string& a : e.aliases) {
            d = std::min(d, EditDistance(q, Lower(a)));
            for (const std::string& w : Words(Lower(a))) d = std::min(d, EditDistance(q, w));
        }
        const int limit = std::max(2, (int)q.size() / 3);
        if (d <= limit) {
            bool dup = false;
            for (auto& b : best) if (b.second == e.name) dup = true;
            if (!dup) best.push_back({d, e.name});
        }
    }
    std::stable_sort(best.begin(), best.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
    std::vector<std::string> out;
    for (std::size_t i = 0; i < best.size() && i < 3; ++i) out.push_back(best[i].second);
    return out;
}

// ---- prefs ----------------------------------------------------------------------------------------------------------------

void Prefs::NoteUsed(const std::string& name) {
    recent.erase(std::remove(recent.begin(), recent.end(), name), recent.end());
    recent.insert(recent.begin(), name);
    if (recent.size() > 8) recent.resize(8);
}
bool Prefs::IsFavorite(const std::string& n) const { return std::find(favorites.begin(), favorites.end(), n) != favorites.end(); }
bool Prefs::IsRecent(const std::string& n) const { return std::find(recent.begin(), recent.end(), n) != recent.end(); }
void Prefs::ToggleFavorite(const std::string& n) {
    auto it = std::find(favorites.begin(), favorites.end(), n);
    if (it != favorites.end()) favorites.erase(it); else favorites.push_back(n);
}
std::string Prefs::Serialize() const {
    std::string out;
    for (auto it = recent.rbegin(); it != recent.rend(); ++it) out += "recent " + *it + "\n";   // oldest first: reading re-inserts in order
    for (const auto& f : favorites) out += "fav " + f + "\n";
    return out;
}
Prefs Prefs::Parse(const std::string& text) {
    Prefs p; std::istringstream in(text); std::string line;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (StartsWith(line, "recent ") && line.size() > 7) p.NoteUsed(line.substr(7));
        else if (StartsWith(line, "fav ") && line.size() > 4 && !p.IsFavorite(line.substr(4))) p.favorites.push_back(line.substr(4));
    }
    return p;
}
bool LoadPrefs(const std::string& path, Prefs& out) {
    std::ifstream f(path);
    if (!f) return false;
    std::stringstream ss; ss << f.rdbuf();
    out = Prefs::Parse(ss.str());
    return true;
}
bool SavePrefs(const std::string& path, const Prefs& p) {
    std::ofstream f(path, std::ios::trunc);
    if (!f) return false;
    f << p.Serialize();
    return (bool)f;
}

// ---- suggestions ------------------------------------------------------------------------------------------------------

std::vector<Suggestion> Suggest(const std::vector<std::string>& present) {
    std::vector<Suggestion> out;
    for (const Goes& g : kGoes) {
        if (std::find(present.begin(), present.end(), std::string(g.have)) == present.end()) continue;
        if (std::find(present.begin(), present.end(), std::string(g.suggest)) != present.end()) continue;
        bool dup = false;
        for (const auto& s : out) if (s.name == g.suggest) dup = true;
        if (!dup) out.push_back({g.suggest, std::string(g.why)});
    }
    return out;
}

// ---- the view ---------------------------------------------------------------------------------------------------------

namespace {

std::vector<std::string> PresentNames(const std::vector<Entry>& entries) {
    std::vector<std::string> v;
    for (const Entry& e : entries) if (e.present) v.push_back(e.label);
    return v;
}

// Entries with this name that can be shown once: the available one wins over a greyed copy.
int FirstByName(const std::vector<Entry>& entries, const std::string& label) {
    int found = -1;
    for (int i = 0; i < (int)entries.size(); ++i)
        if (entries[i].label == label) { if (entries[i].available) return i; if (found < 0) found = i; }
    return found;
}

}  // namespace

View Build(const std::vector<Entry>& entries, const State& s, const Prefs& prefs) {
    View v;
    const std::vector<Suggestion> sugg = Suggest(PresentNames(entries));
    std::vector<int> forObject;
    std::vector<std::string> notes;
    for (const Suggestion& g : sugg) {
        int i = FirstByName(entries, g.name);
        if (i >= 0 && entries[i].available) { forObject.push_back(i); notes.push_back(g.why); }
    }
    std::vector<int> recent, favs;
    for (const std::string& n : prefs.recent) { int i = FirstByName(entries, n); if (i >= 0) recent.push_back(i); }
    for (const std::string& n : prefs.favorites) { int i = FirstByName(entries, n); if (i >= 0) favs.push_back(i); }
    auto avail = [&](const std::vector<int>& ix) { int c = 0; for (int i : ix) if (entries[i].available) ++c; return c; };

    if (!forObject.empty()) v.cats.push_back({kCatForObject, (int)forObject.size(), true});
    if (!recent.empty()) v.cats.push_back({kCatRecent, avail(recent), true});
    if (!favs.empty()) v.cats.push_back({kCatFavorites, avail(favs), true});
    std::vector<std::string> order = CategoryOrder();
    for (const Entry& e : entries) if (std::find(order.begin(), order.end(), e.category) == order.end()) order.push_back(e.category);
    for (const std::string& c : order) {
        int total = 0, free = 0;
        for (const Entry& e : entries) if (e.category == c) { ++total; if (e.available) ++free; }
        if (total) v.cats.push_back({c, free, false});
    }

    v.searching = !Words(s.query).empty();

    if (v.searching) {
        std::vector<Row> rows;
        std::vector<std::string> seen;
        for (int i = 0; i < (int)entries.size(); ++i) {
            const Entry& e = entries[i];
            if (std::find(seen.begin(), seen.end(), e.label) != seen.end()) continue;
            int pick = FirstByName(entries, e.label);   // the copy that can be added, when there is one
            const Entry& ee = entries[pick];
            Match m = MatchEntry(ee, s.query);
            seen.push_back(e.label);
            if (!m.score) continue;
            Row r; r.entry = pick; r.score = m.score + (ee.available ? 50 : 0) + (prefs.IsFavorite(ee.label) ? 15 : 0) + (prefs.IsRecent(ee.label) ? 8 : 0);
            r.hits = m.hits;
            rows.push_back(r);
        }
        std::stable_sort(rows.begin(), rows.end(), [&](const Row& a, const Row& b) {
            if (a.score != b.score) return a.score > b.score;
            return entries[a.entry].name.size() < entries[b.entry].name.size();
        });
        v.rows = rows;
        if (rows.empty()) v.didYouMean = DidYouMean(entries, s.query);
        return v;
    }

    auto add = [&](int i, const std::string& note = std::string()) { Row r; r.entry = i; r.score = 1; r.note = note; v.rows.push_back(r); };
    if (s.category == kCatForObject) {
        for (std::size_t k = 0; k < forObject.size(); ++k) add(forObject[k], notes[k]);
    } else if (s.category == kCatRecent) {
        for (int i : recent) add(i);
    } else if (s.category == kCatFavorites) {
        for (int i : favs) add(i);
    } else {
        // real category: available first, greyed ones after
        for (int pass = 0; pass < 2; ++pass)
            for (int i = 0; i < (int)entries.size(); ++i)
                if (entries[i].category == s.category && entries[i].available == (pass == 0)) add(i);
    }
    return v;
}

std::string DefaultCategory(const std::vector<Entry>& entries, const Prefs& prefs) {
    State s; Prefs none;
    View v = Build(entries, s, prefs);
    for (const CatCount& c : v.cats) if (c.special && c.count > 0 && c.name == kCatForObject) return c.name;
    for (const CatCount& c : v.cats) if (c.special && c.count > 0 && c.name == kCatRecent) return c.name;
    for (const CatCount& c : v.cats) if (!c.special && c.count > 0) return c.name;
    return v.cats.empty() ? std::string() : v.cats.front().name;
}

void ClampCursor(State& s, const View& v) {
    if (v.rows.empty()) { s.cursor = 0; return; }
    if (s.cursor < 0) s.cursor = 0;
    if (s.cursor >= (int)v.rows.size()) s.cursor = (int)v.rows.size() - 1;
}

Outcome Choose(State& s, const std::vector<Entry>& entries, int idx) {
    Outcome o;
    if (idx < 0 || idx >= (int)entries.size()) return o;
    const Entry& e = entries[idx];
    if (!e.available) {
        s.notice = "Can't add " + e.name + ": " + e.reason;
        o.rebuild = true;
        return o;
    }
    o.activate = idx;
    return o;
}

Outcome HandleKey(State& s, const View& v, const std::vector<Entry>& entries, Key k) {
    Outcome o;
    ClampCursor(s, v);
    switch (k) {
    case Key::Escape: o.close = true; return o;
    case Key::Tab:
        s.column = s.column == Column::Cards ? Column::Categories : Column::Cards;
        o.rebuild = true; return o;
    case Key::Enter:
        if (s.column == Column::Categories && !v.searching) { s.column = Column::Cards; o.rebuild = true; return o; }
        if (v.rows.empty()) { s.notice = "Nothing to add here. Try another word."; o.rebuild = true; return o; }
        return Choose(s, entries, v.rows[s.cursor].entry);
    default: break;
    }
    const int step = (k == Key::Up) ? -1 : (k == Key::Down) ? 1 : (k == Key::PageUp) ? -5 : (k == Key::PageDown) ? 5 : 0;
    if (s.column == Column::Categories) {
        // Moving in the left column picks that category (and ends a search).
        int cur = -1;
        for (int i = 0; i < (int)v.cats.size(); ++i) if (v.cats[i].name == s.category) cur = i;
        int n = (int)v.cats.size();
        if (n == 0) return o;
        int next = cur;
        if (k == Key::Home) next = 0; else if (k == Key::End) next = n - 1;
        else next = ((cur < 0 ? 0 : cur) + step + n) % n;
        s.SelectCategory(v.cats[next].name);
        o.rebuild = true; return o;
    }
    const int n = (int)v.rows.size();
    if (n == 0) return o;
    if (k == Key::Home) s.cursor = 0;
    else if (k == Key::End) s.cursor = n - 1;
    else if (step == 1 || step == -1) s.cursor = (s.cursor + step + n) % n;   // wraps round
    else s.cursor = std::max(0, std::min(n - 1, s.cursor + step));
    s.notice.clear();
    o.rebuild = true;
    return o;
}

std::string AddedNotice(const std::string& name, const std::vector<std::string>& also) {
    std::string t = "Added " + name + ".";
    if (also.empty()) return t;
    std::string list;
    for (std::size_t i = 0; i < also.size(); ++i) {
        if (i) list += (i + 1 == also.size()) ? " and " : ", ";
        list += also[i];
    }
    return t + " It needs " + list + ", so that was added too.";
}

}  // namespace addcomp
}  // namespace oked
