#include "okay/Scene/SceneMigration.hpp"
#include "okay/Scene/Scene.hpp"
#include "okay/Scene/SceneSerializer.hpp"

#include <algorithm>

namespace okay {

namespace {

bool HasRecord(const SceneText::Document& d, const char* key) {
    for (const auto& o : d.objects)
        for (const auto& r : o.records)
            if (r.key == key) return true;
    return false;
}

// The format versions this build can read, with the evidence for each change in the repo
// history (git log -S"okayscene 2" / "okayscene 3" on SceneSerializer.cpp):
//  1 -> 2  PR #346: a Character body gets a visible Animator with the default humanoid graph.
//          The loader gives one to every Character of a file older than version 2.
//  2 -> 3  PR #510: a switchable first / third person player gets both view controllers and a
//          camera mounted on it. The loader completes such a player when the file is older than 3.
// Everything else that was added to the format since (guid, sibling, compoff, override, layermatrix,
// ...) is optional and was added without a version bump: older files simply lack those records.
std::vector<SceneMigration::Step> Builtins() {
    std::vector<SceneMigration::Step> v;
    {
        SceneMigration::Step s;
        s.from = 1;
        s.name = "character-animator";
        s.description = "A Character body gets the visible Animator with the default humanoid graph (PR #346). "
                        "The loader adds it to every Character of an older file.";
        s.affects = [](const SceneText::Document& d) { return HasRecord(d, "character"); };
        s.needsEngine = true;
        v.push_back(std::move(s));
    }
    {
        SceneMigration::Step s;
        s.from = 2;
        s.name = "view-toggle-controllers";
        s.description = "A switchable first / third person player gets both view controllers and a camera on the player (PR #510). "
                        "The loader completes such a player when it opens an older file.";
        s.affects = [](const SceneText::Document& d) { return HasRecord(d, "playerview"); };
        s.needsEngine = true;
        v.push_back(std::move(s));
    }
    return v;
}

std::vector<SceneMigration::Step>& Registry() {
    static std::vector<SceneMigration::Step> steps = Builtins();
    return steps;
}

std::string Arrow(int from) { return "okayscene " + std::to_string(from) + " -> " + std::to_string(from + 1); }

} // namespace

int SceneMigration::CurrentVersion() { return SceneText::CurrentVersion(); }

bool SceneMigration::Register(const Step& step) {
    if (step.from < 1) return false;
    auto& reg = Registry();
    for (const auto& s : reg) if (s.from == step.from) return false;
    reg.push_back(step);
    std::stable_sort(reg.begin(), reg.end(), [](const Step& a, const Step& b) { return a.from < b.from; });
    return true;
}

bool SceneMigration::Unregister(int from) {
    auto& reg = Registry();
    for (auto it = reg.begin(); it != reg.end(); ++it)
        if (it->from == from) { reg.erase(it); return true; }
    return false;
}

std::vector<SceneMigration::Step> SceneMigration::Steps() { return Registry(); }

std::string SceneMigration::Describe() {
    std::string out;
    for (const auto& s : Registry())
        out += Arrow(s.from) + "  " + s.name + ": " + s.description + (s.needsEngine ? " [needs the engine loader]" : "") + "\n";
    return out;
}

SceneMigration::Result SceneMigration::MigrateToCurrent(const std::string& text) {
    return MigrateTo(text, CurrentVersion());
}

SceneMigration::Result SceneMigration::MigrateTo(const std::string& text, int target) {
    Result r;
    r.text = text;
    SceneText::Document d;
    std::string err;
    if (!SceneText::Parse(text, d, &err)) { r.error = "not a scene: " + err; return r; }
    r.fromVersion = r.toVersion = d.version;
    if (d.version > target) {
        r.error = "okayscene " + std::to_string(d.version) + " is newer than this build reads (" + std::to_string(target) +
                  "); left untouched";
        return r;
    }
    const std::vector<Step> steps = Registry();
    bool any = false;
    while (d.version < target) {
        const Step* step = nullptr;
        for (const auto& s : steps) if (s.from == d.version) { step = &s; break; }
        if (!step) {
            r.error = "no migration is registered for " + Arrow(d.version) + "; left untouched";
            r.text = text;
            r.toVersion = r.fromVersion;
            r.log.clear();
            return r;
        }
        const bool affected = !step->affects || step->affects(d);
        if (step->needsEngine && affected) {
            r.needsEngine = true;
            r.log.push_back(Arrow(d.version) + " (" + step->name + "): " + step->description +
                            " This file has content it applies to, and only the engine's loader can do that; stopped here.");
            break;
        }
        if (affected && step->apply) {
            std::string e;
            if (!step->apply(d, r.log, e)) {
                r.error = Arrow(d.version) + " (" + step->name + ") failed: " + e + "; left untouched";
                r.text = text;
                r.toVersion = r.fromVersion;
                r.log.clear();
                return r;
            }
            r.log.push_back(Arrow(d.version) + " (" + step->name + "): applied");
        } else {
            r.log.push_back(Arrow(d.version) + " (" + step->name + "): nothing in this scene needs it, version number raised");
        }
        ++d.version;
        r.toVersion = d.version;
        any = true;
    }
    r.ok = true;
    if (any) r.text = SceneText::Canonicalize(SceneText::Write(d));
    r.changed = r.text != text;
    r.complete = r.toVersion == target;
    return r;
}

SceneMigration::Result SceneMigration::MigrateThroughEngine(const std::string& text) {
    Result r = MigrateToCurrent(text);
    if (!r.ok || !r.needsEngine) return r;
    Scene scratch("migration");
    std::string err;
    std::vector<std::string> warnings;
    if (!SceneSerializer::Deserialize(scratch, r.text, &err, &warnings)) {
        Result f;
        f.text = text;
        f.fromVersion = f.toVersion = r.fromVersion;
        f.error = "the engine could not load the scene: " + err + "; left untouched";
        return f;
    }
    r.log.push_back("loaded the scene with the engine's loader, which applied the upgrades above, and wrote it back out as okayscene " +
                    std::to_string(CurrentVersion()));
    for (const auto& w : warnings) r.log.push_back("loader warning: " + w);
    r.text = SceneText::Canonicalize(SceneSerializer::Serialize(scratch));
    r.toVersion = CurrentVersion();
    r.needsEngine = false;
    r.complete = true;
    r.changed = r.text != text;
    return r;
}

} // namespace okay
