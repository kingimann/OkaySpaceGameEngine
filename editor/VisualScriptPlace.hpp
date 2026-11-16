#pragma once
// The small pieces every place table uses: what a place is, and the two "same job as a hand-written node" tests.
// No topic names live here (VisualScriptCategories.hpp has the topics, VisualScriptPlaceRules.hpp the rules).
#include <cctype>
#include <cstring>
#include <string>

namespace okay::editor::vsed {

struct VsPlace {
    const char* category = nullptr;
    const char* subcategory = "";   // never null
    bool browse = true;             // false: still found by search, not listed while browsing
};

inline VsPlace VsAt(const char* category, const char* sub = "") {
    VsPlace p; p.category = category; p.subcategory = sub ? sub : ""; return p;
}

inline bool VsIs(const char* type, const char* a) { return std::strcmp(type, a) == 0; }
inline bool VsPre(const char* type, const char* p) { return std::strncmp(type, p, std::strlen(p)) == 0; }

// True when this script member is the same job as a hand-written node (Abs, Translate, GetPosition).
inline bool VsApiDuplicatesHandNode(const char* member, const char* handType) {
    if (!member || !handType || !member[0] || !handType[0]) return false;
    std::string m, h;
    for (const char* s = member; *s; ++s) m += (char)std::tolower((unsigned char)*s);
    for (const char* s = handType; *s; ++s) h += (char)std::tolower((unsigned char)*s);
    return h == m || h == "get" + m || h == "set" + m;
}

// True when this Actions step is the same job as a hand-written node (Wait, Play Sound, Set Position).
// It stays searchable by its old label, and it is not a second row while browsing.
inline bool VsStepSameAsNode(const char* op) {
    if (!op) return false;
    static const char* k[] = {
        "wait", "play_sound", "set_pos", "look_at", "move", "move_toward", "spawn", "spawn3", "spawn_at",
        "destroy", "set_var", "add_var", "toggle_var", "log", "load_scene", "camera_shake", "set_timescale",
        "pause", "quit", "set_text", "play_clip", "play_anim", "play_action", "set_color", "velocity",
        "velocity3", "impulse", "impulse3", "force3", "set_visible", "set_active", "set_rotation",
        "set_rotation3", "rotate", "set_scale", "die", "revive", "dive", "sit", "stand_up", "interact",
        "hit_react", "roll", "dodge", "vault", "mantle", "ledge_grab", "climb_up", "let_go", "shimmy",
    };
    for (const char* s : k) if (std::strcmp(op, s) == 0) return true;
    return false;
}

} // namespace okay::editor::vsed
