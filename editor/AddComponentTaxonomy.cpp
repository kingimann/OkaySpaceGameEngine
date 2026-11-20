// Where every component lives in the Add Component browser: plain categories with sub-groups, chosen by rules on the
// component's key, so a new component needs no edit here when a rule already covers it. Written for the owner's request of
// 2026-11-20 (categories the way people think); no code from another project. See AddComponentModel.hpp.
#include "AddComponentModel.hpp"

#include <algorithm>
#include <cctype>
#include <map>
#include <set>

namespace oked {
namespace addcomp {

const char* const kUncategorised = "Uncategorised";

namespace {

// One rule: the keys (space separated) it claims, and where they go. A key starting with '=' must match exactly; '^' starts
// with, '$' ends with, '~' contains. Exact keys are checked first (all rules), then the other kinds in table order.
struct Rule { const char* pats; const char* category; const char* group; };

const Rule kRules[] = {
    // ---- Physics ----
    {"=rigidbody2d =rigidbody3d =ragdoll =breakable =destructible =debrisemitter", "Physics", "Bodies"},
    {"$collider3d", "Physics", "Colliders 3D"},
    {"$collider2d", "Physics", "Colliders 2D"},
    {"^joint", "Physics", "Joints"},
    {"=conveyorbelt =forcefield =gravityzone =explosion =turntable =movingplatform =physicssurface =winddrag", "Physics", "Forces & Toys"},
    {"^vehicle =boatcontroller =seat", "Physics", "Vehicles"},
    {"=water =buoyancy", "Physics", "Water"},
    // ---- Rendering ----
    {"=spriterenderer =meshrenderer =modifiers =linerenderer =trailrenderer =splinemesh =cable =decalprojector =lodgroup =occluder", "Rendering", "Meshes & Shapes"},
    {"=directionallight =flashlight", "Rendering", "Lights"},
    {"^camera =virtualcamera =cinemachinebrain ^dolly", "Rendering", "Camera"},
    {"=particlesystem", "Rendering", "Particles"},
    {"=daynightcycle =windzone =windsway", "Rendering", "Sky & Weather"},
    {"=volumeatmosphere", "Rendering", "Post & Volumes"},
    // ---- Audio ----
    {"=audiosource =musicplayer =windsound =volumeambience =volumereverb", "Audio", ""},
    // ---- UI ----
    {"=canvas =eventsystem =uidocument =uitheme =uistylevariant =uiuserstyle =uihotreload =uiconstraints =uiguides", "UI", "Setup"},
    {"=text =uipanel =uiimage =uirawimage =uibutton =uianimation =uiframe", "UI", "Basics"},
    {"=uislider =uistepper =uirating =uitoggle =uidropdown =uirangeslider =uicolorpicker =uicolorsquare", "UI", "Controls"},
    {"=uiinputfield =uikeybindfield =uisearchbar =uidateandtime =uifilepicker =uiinputvalidator =uiformgroup", "UI", "Text Input"},
    {"=uilayoutgroup =uiflexitem =uimask =uiaspectratio =uisafearea =uiscrollview =uilistview", "UI", "Layout"},
    {"=uimodal =uitoast =uicontextmenu =uibottomsheet =uimenubar =uitoolbar =uiwizard =uiradialmenu", "UI", "Menus & Dialogs"},
    {"=uitabs =uiaccordion =uibreadcrumbs =uipagination =uitreeview =uidatatable =uiskeleton", "UI", "Navigation & Data"},
    {"=uibind =uitextbind =uibarbind =uilistbind =uivisibleif =uifocushint =uirichlinks =uiinventorybind", "UI", "Binding"},
    {"=uiprogressbar =uiradialprogress =minimap =minimapblip =crosshair =uicompass =uiammocounter =uikillfeed =uidamagenumbers =uiabilityicon", "UI", "HUD"},
    {"=uislotgrid =loadingscreen =worldui =uiinteractionprompt =uiobjectivetracker =uiitemtooltipcard =uidialoguebox =uidraggable =uidroptarget", "UI", "Game Screens"},
    {"=virtualjoystick =dpad =touchbutton", "UI", "Touch"},
    {"^ui", "UI", "More"},
    // ---- Gameplay ----
    {"=character =npccontroller =playerview =playerinput =firstpersonhands =hand =followtarget2d", "Gameplay", "Characters & Input"},
    {"~controller", "Gameplay", "Player Controllers"},
    {"=stathealth =stats =meleeattacker =rangedweapon =projectile3d =damageontouch", "Gameplay", "Health & Combat"},
    {"=survivalstats =statuseffects ^survivalsave =survivalzone =consumables", "Gameplay", "Survival Rules"},
    {"^stat", "Gameplay", "Survival Stats"},
    {"=inventory =equipment =itempickup =lootsource =chestcontainer =netinventory =draggable =dropzone", "Gameplay", "Inventory & Items"},
    {"^crafting =proceduralobjectholding =holdprompt =holdableobject", "Gameplay", "Crafting & Holding"},
    {"^navmesh =behaviortreerunner =gridpathagent =gridmovementcosts =gridobstacle", "Gameplay", "AI & Navigation"},
    {"=collectible =teleporter =jumppad =mover =spinner =lifetime =spawner =logicsource =logictarget", "Gameplay", "Mechanics"},
    {"=sequenceplayer =dialoguerunner =questlog =questtrigger =storystate =turnmanager", "Gameplay", "Story & Turns"},
    {"=autosave =saveable =saveconfig =pausemenu", "Gameplay", "Saving & Menus"},
    // ---- Scripting ----
    {"=newscript =newvisualscript =scriptcomponent =visualscriptcomponent $okay $okayvs ^script", "Scripting", ""},
    // ---- Animation ----
    {"=animator =animatorstates =spriteanimator =tweenanimation =posecapture =humanoidretarget =aimoffset", "Animation", "Animators"},
    {"$ik =iktarget", "Animation", "IK"},
    // ---- Networking ----
    {"=anticheatconfig =netvalidator =syncvar", "Networking", "Rules & Safety"},
    {"^network", "Networking", "Players & Objects"},
    // ---- World ----
    {"=terraindigger =voxelterrain =voxeldigger =foliage =worldstreamer =terrain", "World", "Terrain"},
    {"=blockbuilder =structurebuilder =buildmode =buildable", "World", "Building"},
    {"^volume =triggerzone", "World", "Zones & Volumes"},
    {"=spline =splinefollower =splineinstancer", "World", "Paths & Splines"},
};

// Registry type names the menu rows spell differently, and the same part under its old and new row name.
struct Alias { const char* from; const char* to; };
const Alias kAliases[] = {
    {"light", "directionallight"}, {"textrenderer", "text"}, {"touchdpad", "dpad"}, {"uidatetimepicker", "uidateandtime"},
    {"gridcostlayer", "gridmovementcosts"}, {"gridobstacle2d", "gridobstacle"}, {"gridpathagent2d", "gridpathagent"},
    {"localplayerinput", "playerinput"}, {"windambience", "windsound"}, {"firstpersoncontroller", "playercontroller"},
    {"thirdpersoncontroller", "playercontroller"}, {"platformer25dcontroller", "25dplatformercontroller"},
    {"animstatemachine", "animatorstates"}, {"camerarig", "virtualcamera"}, {"volume", "volumecheckpoint"},
    {"savesettings", "saveconfig"}, {"okayscript", "scriptcomponent"}, {"visualscript", "visualscriptcomponent"},
};

// A component that only works with another one on the object.
struct Prereq { const char* key; const char* needs; };
const Prereq kPrereqs[] = {
    {"buoyancy", "rigidbody3d"}, {"winddrag", "rigidbody3d"}, {"joint2d", "rigidbody2d"}, {"joint3d", "rigidbody3d"},
};

std::vector<std::string> Split(const char* s) {
    std::vector<std::string> out; std::string cur;
    for (const char* c = s;; ++c) {
        if (*c == ' ' || *c == '\0') { if (!cur.empty()) out.push_back(cur); cur.clear(); if (!*c) break; }
        else cur += *c;
    }
    return out;
}
bool Ends(const std::string& s, const std::string& t) { return s.size() >= t.size() && s.compare(s.size() - t.size(), t.size(), t) == 0; }

}  // namespace

std::string KeyOf(const std::string& in) {
    std::string s = in;
    for (std::size_t open = s.find('('); open != std::string::npos; open = s.find('(')) {   // the bracket text is not part of the key
        const std::size_t close = s.find(')', open);
        s.erase(open, close == std::string::npos ? std::string::npos : close - open + 1);
    }
    std::string k;
    for (char c : s) if (std::isalnum((unsigned char)c)) k += (char)std::tolower((unsigned char)c);
    // the two Animator rows share a name; the second one is told apart by its bracket text
    if (k == "animator" && in.find("states") != std::string::npos) return "animatorstates";
    return k;
}

std::string Canon(const std::string& key) {
    for (const Alias& a : kAliases) if (key == a.from) return a.to;
    return key;
}

Place Classify(const std::string& key) {
    for (const Rule& r : kRules)
        for (const std::string& p : Split(r.pats))
            if (p[0] == '=' && key == p.substr(1)) return {r.category, r.group};
    for (const Rule& r : kRules)
        for (const std::string& p : Split(r.pats)) {
            const std::string v = p.substr(1);
            if (p[0] == '^' && key.compare(0, v.size(), v) == 0) return {r.category, r.group};
            if (p[0] == '$' && Ends(key, v)) return {r.category, r.group};
            if (p[0] == '~' && key.find(v) != std::string::npos) return {r.category, r.group};
        }
    return {kUncategorised, ""};
}

const std::vector<std::string>& CategoryOrder() {
    static const std::vector<std::string> v = {"Physics", "Rendering", "Audio", "UI", "Gameplay", "Scripting", "Animation", "Networking", "World", "Misc"};
    return v;
}

std::vector<std::string> GroupOrder(const std::string& category) {
    std::vector<std::string> out;
    for (const Rule& r : kRules)
        if (category == r.category && r.group[0] && std::find(out.begin(), out.end(), std::string(r.group)) == out.end()) out.push_back(r.group);
    return out;
}

std::vector<std::string> RuleExactKeys() {
    std::vector<std::string> out;
    for (const Rule& r : kRules) for (const std::string& p : Split(r.pats)) if (p[0] == '=') out.push_back(p.substr(1));
    return out;
}

std::string PlainCategory(const std::string&, const std::string& name) { return Classify(Canon(KeyOf(name))).category; }

std::string PrereqOf(const std::string& key) {
    for (const Prereq& p : kPrereqs) if (key == p.key) return p.needs;
    return std::string();
}

std::string Pretty(const std::string& key) {
    // "rigidbody3d" -> "Rigidbody3D": capitalise the first letter and any letter after a digit-run.
    std::string out;
    for (std::size_t i = 0; i < key.size(); ++i) {
        char c = key[i];
        out += (i == 0 || (i > 0 && std::isdigit((unsigned char)key[i - 1]) && std::isalpha((unsigned char)c))) ? (char)std::toupper((unsigned char)c) : c;
    }
    return out;
}

void ApplyPrereqs(std::vector<Entry>& entries) {
    std::set<std::string> present;
    for (const Entry& e : entries) if (e.present) present.insert(e.nkey);
    for (Entry& e : entries) {
        if (!e.available) continue;
        const std::string need = PrereqOf(e.nkey);
        if (need.empty() || present.count(need)) continue;
        std::string nm = Pretty(need);
        for (const Entry& o : entries) if (o.nkey == need) { nm = o.name; break; }
        e.available = false;
        e.reason = "Needs a " + nm + " first.";
    }
}

}  // namespace addcomp
}  // namespace oked
