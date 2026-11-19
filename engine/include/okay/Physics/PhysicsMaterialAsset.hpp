#pragma once
// The Physics Material asset: one named bundle that says how a surface FEELS. Friction (grip when still, grip when
// sliding), bounciness, how the two surfaces of a contact are mixed, density (so a body's mass can come from its size),
// rolling friction, damping, grip on slopes, and the names of the sounds and effect a hit should use, plus a surface name
// ("Ice", "Grass") that gameplay and audio can ask for.
//
// Written from the plain facts of contact models: a surface has a static and a kinetic friction coefficient (static >=
// kinetic), a restitution between 0 and 1, and a density in kg per cubic metre (mass = density x volume). The presets use
// typical textbook numbers for the material (ice on steel about 0.03 kinetic, rubber on concrete about 1.0, wood on wood
// about 0.3 to 0.5, steel about 7800 kg/m3, wood about 600, stone about 2600) rounded for play, then checked by the tests
// in tests/test_physics_materials.cpp. Nothing here is taken from another engine.
//
// Header only. A material is found by NAME: the built-in presets always exist; project materials are `.okayphysmat` files
// (plain text, versioned) loaded with PhysicsMaterials::LoadFile / ScanFolder, and they win over a preset of the same name.
#include "okay/Physics/PhysicsMaterial.hpp"
#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <deque>
#include <filesystem>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

namespace okay {

struct PhysicsMaterialData {
    std::string name = "Default";
    std::string surface;                         ///< what gameplay and audio call it ("Ice", "Grass"); empty = same as the name
    float staticFriction  = 0.6f;                ///< grip while the surfaces are (nearly) still against each other
    float dynamicFriction = 0.5f;                ///< grip while sliding. 0 = like ice, 1 = like rubber
    float bounciness      = 0.0f;                ///< 0 = dead, 1 = a perfect bounce
    CombineMode frictionCombine = CombineMode::Default;
    CombineMode bounceCombine   = CombineMode::Default;
    float density         = 1000.0f;             ///< kg per cubic metre; used when the body takes its mass from its size
    float rollingFriction = -1.0f;               ///< how hard it is to roll over this surface; < 0 = use the body's own
    float linearDamping   = 0.0f;                ///< slows a body that touches it (mud, sand), per second
    float angularDamping  = 0.0f;                ///< slows its spin
    float slopeGrip       = 1.0f;                ///< multiplies the still grip, so a thing can stay on a steep slope (1 = normal)
    std::string impactSound;                     ///< sound name for a hit
    std::string slideSound;                      ///< sound name for sliding or a footstep
    std::string hitEffect;                       ///< particle effect name for a hit
};

namespace detail {
inline std::string LowerCopy(std::string s) { for (char& c : s) c = (char)std::tolower((unsigned char)c); return s; }
inline const char* CombineName(CombineMode m) {
    switch (m) { case CombineMode::Average: return "Average"; case CombineMode::Minimum: return "Minimum";
                 case CombineMode::Multiply: return "Multiply"; case CombineMode::Maximum: return "Maximum"; default: return "Default"; }
}
inline CombineMode CombineFromName(const std::string& n) {
    const std::string l = LowerCopy(n);
    if (l == "average") return CombineMode::Average; if (l == "minimum" || l == "min") return CombineMode::Minimum;
    if (l == "multiply") return CombineMode::Multiply; if (l == "maximum" || l == "max") return CombineMode::Maximum;
    return CombineMode::Default;
}
}

class PhysicsMaterials {
public:
    static constexpr int kFileVersion = 1;

    /// The built-in presets, in the order a menu shows them.
    static const std::vector<PhysicsMaterialData>& Presets() {
        static const std::vector<PhysicsMaterialData> p = BuildPresets();
        return p;
    }
    static std::vector<std::string> PresetNames() { std::vector<std::string> n; for (auto& m : Presets()) n.push_back(m.name); return n; }

    /// Find by name (any case): a project material first, then a preset. nullptr when there is none. The pointer stays valid
    /// (a re-registered material is updated in place).
    static const PhysicsMaterialData* Find(const std::string& name) {
        if (name.empty()) return nullptr;
        const std::string key = detail::LowerCopy(name);
        auto& pm = Project();
        auto it = pm.find(key);
        if (it != pm.end()) return it->second;
        for (const auto& m : Presets()) if (detail::LowerCopy(m.name) == key) return &m;
        return nullptr;
    }
    /// Every name a menu can offer: presets first, then project materials.
    static std::vector<std::string> AllNames() {
        std::vector<std::string> n = PresetNames();
        for (auto& kv : Project()) {
            bool dup = false; for (auto& x : n) if (detail::LowerCopy(x) == kv.first) dup = true;
            if (!dup) n.push_back(kv.second->name);
        }
        return n;
    }
    /// Add or replace a project material.
    static const PhysicsMaterialData* Register(const PhysicsMaterialData& d) {
        if (d.name.empty()) return nullptr;
        const std::string key = detail::LowerCopy(d.name);
        auto& pm = Project();
        auto it = pm.find(key);
        if (it != pm.end()) { *it->second = d; return it->second; }
        Store().push_back(d);
        pm[key] = &Store().back();
        return &Store().back();
    }
    static void ClearProject() { Project().clear(); }

    // ---- the .okayphysmat file: "okayphysmat 1" then one "key value" per line; the text values run to the end of the line ----
    static std::string ToText(const PhysicsMaterialData& d) {
        std::ostringstream o;
        o.precision(9);
        o << "okayphysmat " << kFileVersion << "\n";
        o << "name " << d.name << "\n";
        o << "surface " << d.surface << "\n";
        o << "staticFriction " << d.staticFriction << "\n";
        o << "dynamicFriction " << d.dynamicFriction << "\n";
        o << "bounciness " << d.bounciness << "\n";
        o << "frictionCombine " << detail::CombineName(d.frictionCombine) << "\n";
        o << "bounceCombine " << detail::CombineName(d.bounceCombine) << "\n";
        o << "density " << d.density << "\n";
        o << "rollingFriction " << d.rollingFriction << "\n";
        o << "linearDamping " << d.linearDamping << "\n";
        o << "angularDamping " << d.angularDamping << "\n";
        o << "slopeGrip " << d.slopeGrip << "\n";
        o << "impactSound " << d.impactSound << "\n";
        o << "slideSound " << d.slideSound << "\n";
        o << "hitEffect " << d.hitEffect << "\n";
        return o.str();
    }
    /// Read a file's text. False (out unchanged) when it is not an okayphysmat file or is from a newer version.
    /// Unknown lines are skipped, so a newer file with extra keys still loads; numbers are checked and clamped.
    static bool FromText(const std::string& text, PhysicsMaterialData& out) {
        std::istringstream in(text);
        std::string line;
        if (!std::getline(in, line)) return false;
        { std::istringstream h(line); std::string tag; int ver = 0; h >> tag >> ver; if (tag != "okayphysmat" || ver < 1 || ver > kFileVersion) return false; }
        PhysicsMaterialData d;
        d.name.clear();
        while (std::getline(in, line)) {
            if (!line.empty() && line.back() == '\r') line.pop_back();
            const std::size_t sp = line.find(' ');
            const std::string key = line.substr(0, sp);
            const std::string val = sp == std::string::npos ? std::string() : line.substr(sp + 1);
            auto num = [&](float lo, float hi, float& dst) { char* e = nullptr; float v = std::strtof(val.c_str(), &e); if (e != val.c_str() && std::isfinite(v)) dst = std::max(lo, std::min(hi, v)); };
            if (key == "name") d.name = val;
            else if (key == "surface") d.surface = val;
            else if (key == "staticFriction") num(0.0f, 10.0f, d.staticFriction);
            else if (key == "dynamicFriction") num(0.0f, 10.0f, d.dynamicFriction);
            else if (key == "bounciness") num(0.0f, 1.0f, d.bounciness);
            else if (key == "frictionCombine") d.frictionCombine = detail::CombineFromName(val);
            else if (key == "bounceCombine") d.bounceCombine = detail::CombineFromName(val);
            else if (key == "density") num(0.001f, 100000.0f, d.density);
            else if (key == "rollingFriction") num(-1.0f, 10.0f, d.rollingFriction);
            else if (key == "linearDamping") num(0.0f, 100.0f, d.linearDamping);
            else if (key == "angularDamping") num(0.0f, 100.0f, d.angularDamping);
            else if (key == "slopeGrip") num(0.0f, 100.0f, d.slopeGrip);
            else if (key == "impactSound") d.impactSound = val;
            else if (key == "slideSound") d.slideSound = val;
            else if (key == "hitEffect") d.hitEffect = val;
        }
        if (d.name.empty()) return false;
        out = d;
        return true;
    }
    static bool SaveFile(const PhysicsMaterialData& d, const std::string& path) {
        std::ofstream f(path, std::ios::binary | std::ios::trunc);
        if (!f) return false;
        f << ToText(d);
        return (bool)f;
    }
    /// Load one file and register it as a project material. Returns it, or nullptr.
    static const PhysicsMaterialData* LoadFile(const std::string& path) {
        std::ifstream f(path, std::ios::binary);
        if (!f) return nullptr;
        std::stringstream ss; ss << f.rdbuf();
        PhysicsMaterialData d;
        if (!FromText(ss.str(), d)) return nullptr;
        return Register(d);
    }
    /// Load every `.okayphysmat` under a folder (recursively). Returns how many were loaded.
    static int ScanFolder(const std::string& dir) {
        namespace fs = std::filesystem;
        std::error_code ec;
        if (dir.empty() || !fs::is_directory(dir, ec)) return 0;
        int n = 0;
        for (fs::recursive_directory_iterator it(dir, fs::directory_options::skip_permission_denied, ec), end; !ec && it != end; it.increment(ec)) {
            if (it->is_regular_file(ec) && it->path().extension() == ".okayphysmat" && LoadFile(it->path().string())) ++n;
        }
        return n;
    }
    /// What a gameplay or audio script reads: the surface name, or the material name when none was given.
    static std::string SurfaceName(const PhysicsMaterialData& d) { return d.surface.empty() ? d.name : d.surface; }

private:
    static std::deque<PhysicsMaterialData>& Store() { static std::deque<PhysicsMaterialData> s; return s; }
    static std::map<std::string, PhysicsMaterialData*>& Project() { static std::map<std::string, PhysicsMaterialData*> m; return m; }
    static PhysicsMaterialData Make(const char* name, float sFr, float dFr, float bounce, float density, float rolling,
                                    float lin, float ang, const char* impact, const char* slide, const char* fx,
                                    CombineMode fc = CombineMode::Default, CombineMode bc = CombineMode::Default, float grip = 1.0f) {
        PhysicsMaterialData d;
        d.name = name; d.surface = name; d.staticFriction = sFr; d.dynamicFriction = dFr; d.bounciness = bounce; d.density = density;
        d.rollingFriction = rolling; d.linearDamping = lin; d.angularDamping = ang; d.impactSound = impact; d.slideSound = slide; d.hitEffect = fx;
        d.frictionCombine = fc; d.bounceCombine = bc; d.slopeGrip = grip;
        return d;
    }
    static std::vector<PhysicsMaterialData> BuildPresets() {
        using C = CombineMode;
        return {
            // name        still  slide bounce  density roll   lin  ang  impact           slide           effect
            Make("Default",   0.60f, 0.50f, 0.00f, 1000, -1.0f, 0.0f, 0.0f, "",              "",             ""),
            Make("Ice",       0.05f, 0.02f, 0.05f,  917, 0.0f,  0.0f, 0.0f, "impact_ice",    "slide_ice",    "fx_ice_chips", C::Minimum),
            Make("Rubber",    1.20f, 1.00f, 0.60f, 1100, 0.04f, 0.0f, 0.0f, "impact_rubber", "slide_rubber", "",             C::Maximum, C::Maximum),
            Make("Bouncy Ball", 0.80f, 0.60f, 0.92f, 1100, 0.02f, 0.0f, 0.0f, "impact_rubber", "",           "",             C::Default, C::Maximum),
            Make("Metal",     0.50f, 0.40f, 0.20f, 7800, 0.01f, 0.0f, 0.0f, "impact_metal",  "slide_metal",  "fx_sparks"),
            Make("Wood",      0.55f, 0.45f, 0.30f,  600, 0.03f, 0.0f, 0.0f, "impact_wood",   "slide_wood",   "fx_splinters"),
            Make("Stone",     0.70f, 0.60f, 0.10f, 2600, 0.03f, 0.0f, 0.0f, "impact_stone",  "slide_stone",  "fx_dust"),
            Make("Glass",     0.45f, 0.35f, 0.40f, 2500, 0.01f, 0.0f, 0.0f, "impact_glass",  "slide_glass",  "fx_glass_shards"),
            Make("Mud",       1.00f, 0.90f, 0.00f, 1700, 0.30f, 1.5f, 3.0f, "impact_mud",    "slide_mud",    "fx_mud_splat", C::Default, C::Minimum),
            Make("Sand",      0.80f, 0.70f, 0.00f, 1600, 0.20f, 0.8f, 2.0f, "impact_sand",   "slide_sand",   "fx_sand_puff", C::Default, C::Minimum),
            Make("Sticky",    2.50f, 2.00f, 0.00f, 1200, 0.10f, 0.0f, 0.0f, "impact_soft",   "slide_soft",   "",             C::Maximum, C::Minimum, 2.0f),
            Make("Slippery",  0.02f, 0.01f, 0.00f, 1000, 0.0f,  0.0f, 0.0f, "",              "slide_ice",    "",             C::Minimum),
            Make("Trampoline", 0.60f, 0.50f, 0.97f,  400, 0.03f, 0.0f, 0.0f, "impact_spring", "",           "",             C::Default, C::Maximum),
            Make("Cloth",     0.80f, 0.70f, 0.02f,  300, 0.15f, 0.4f, 1.0f, "impact_cloth",  "slide_cloth",  "",             C::Default, C::Minimum),
        };
    }
};

} // namespace okay
