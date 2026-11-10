#pragma once
// Built-in Node Playground: one demo scene per high-level gameplay category.
// The scenes live in examples/visual_script/playground. File > Examples and the
// "Try it" button on a node's help both open one.
#include "VisualScriptLevels.hpp"
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

namespace okay::editor::vsed {

struct VsPlaygroundDemo {
    const char* id;
    const char* title;
    const char* scene;   // file next to the other demos
    const char* graph;   // the graph the node editor opens
    const char* focus;   // object to select so Play shows that graph's live pins
    const char* blurb;
};

inline const VsPlaygroundDemo* VsPlaygroundDemos(int* count = nullptr) {
    static const VsPlaygroundDemo kDemos[] = {
        {"movement", "Movement", "movement.okayscene", "movement.okayvs", "Orbiter",
         "Orbit, bob, spin, follow a path, knockback, smooth follow and tweens."},
        {"animation", "Animation", "animation.okayscene", "animation.okayvs", "Actor",
         "Play a clip, wait for it to finish, stop, and tween the colour."},
        {"interaction", "Interaction", "interaction.okayscene", "interaction.okayvs", "Door",
         "Door, lever, pickup, drop, collect and proximity. Press E to pick up."},
        {"combat", "Combat", "combat.okayscene", "combat.okayvs", "Gun",
         "Damage, heal, death, respawn, shoot, ammo and a projectile."},
        {"ai", "AI", "ai.okayscene", "ai.okayvs", "Chaser",
         "Patrol, chase, flee, wander, and a lookout that can see the player."},
        {"spawning", "Spawning", "spawning.okayscene", "spawning.okayvs", "Spawner",
         "Spawn on a timer, destroy after a delay, and a small object pool."},
        {"gameflow", "Game Flow", "gameflow.okayscene", "gameflow.okayvs", "Director",
         "Score, checkpoint and a countdown. Win, Lose and Restart are on the graph."},
        {"ui", "UI", "ui.okayscene", "ui.okayvs", "Hud",
         "A progress bar, floating text and a screen fade."},
        {"camera", "Camera", "camera.okayscene", "camera.okayvs", "Director",
         "The camera follows the player and zooms. Press Space to switch cameras."},
        {"audio", "Audio", "audio.okayscene", "audio.okayvs", "Speaker",
         "Play a sound at an object and stop it. Press E to play it again."},
        {"input", "Input", "input.okayscene", "input.okayvs", "Player",
         "WASD moves. Click to fire, release Space, hold E."},
        {"random", "Random", "random.okayscene", "random.okayvs", "Marker",
         "Pick from a list, shuffle it, and jump to a random point."},
        {"math", "Math", "math.okayscene", "math.okayvs", "Marker",
         "Min and max of a list, plus add, clamp and lerp."},
        {"logic", "Logic", "logic.okayscene", "logic.okayvs", "Marker",
         "The cube steps on even counts, then the state becomes done."},
        {"smart", "Smart Logic", "smart.okayscene", "smart.okayvs", "Guide",
         "A cube slides, a guide walks toward a post, and a chooser moves because that score won."},
        {"library", "Logic Library", "library.okayscene", "library.okayvs", "Marker",
         "The white cube approaches the right. The rest of the logic library is in the graph."},
        {"logic_basics", "Logic - Basics", "logic_basics.okayscene", "logic_basics.okayvs", "Marker",
         "The white cube moves right with Approach, then turns green once that number is positive."},
        {"logic_mid", "Logic - Intermediate", "logic_mid.okayscene", "logic_mid.okayvs", "Marker",
         "The white cube eases back and forth. Smooth Step shapes the move."},
        {"logic_advanced", "Logic - Advanced", "logic_advanced.okayscene", "logic_advanced.okayvs", "Marker",
         "The white cube springs toward the option with the highest weighted score."},
#include "generated/gplib_demos.inc"
    };
    if (count) *count = (int)(sizeof(kDemos) / sizeof(kDemos[0]));
    return kDemos;
}

inline const VsPlaygroundDemo* VsPlaygroundById(const char* id) {
    if (!id) return nullptr;
    int n = 0;
    const VsPlaygroundDemo* d = VsPlaygroundDemos(&n);
    for (int i = 0; i < n; ++i) if (std::strcmp(d[i].id, id) == 0) return &d[i];
    return nullptr;
}

// Which demo shows this node. Null for nodes that are not part of the playground.
inline const char* VsPlaygroundForNode(const char* type) {
    struct Row { const char* type; const char* demo; };
    static const Row kRows[] = {
        {"RotateTowards", "movement"}, {"OrbitAround", "movement"}, {"FollowPath", "movement"},
        {"Bob", "movement"}, {"Spin", "movement"}, {"Knockback", "movement"}, {"SmoothFollow", "movement"},
        {"TweenPosition", "movement"}, {"TweenRotation", "movement"}, {"TweenScale", "movement"},
        {"CrossfadeClip", "animation"}, {"StopAnimation", "animation"}, {"WaitAnimEnd", "animation"},
        {"TweenColor", "animation"},
        {"OnInteract", "interaction"}, {"PickUp", "interaction"}, {"Drop", "interaction"},
        {"DoorOpen", "interaction"}, {"DoorClose", "interaction"}, {"Lever", "interaction"},
        {"Collect", "interaction"}, {"OnTagEnter", "interaction"}, {"OnTagStay", "interaction"},
        {"OnTagExit", "interaction"}, {"Proximity", "interaction"},
        {"Damage", "combat"}, {"Heal", "combat"}, {"IsDead", "combat"}, {"OnDeath", "combat"},
        {"OnHealthChanged", "combat"}, {"Respawn", "combat"}, {"SpawnProjectile", "combat"},
        {"Shoot", "combat"}, {"Ammo", "combat"}, {"OnCollisionTag", "combat"},
        {"Patrol", "ai"}, {"Chase", "ai"}, {"Flee", "ai"}, {"Wander", "ai"}, {"CanSee", "ai"},
        {"SpawnEvery", "spawning"}, {"DestroyAfter", "spawning"}, {"PoolGet", "spawning"}, {"PoolRelease", "spawning"},
        {"ScoreAdd", "gameflow"}, {"ScoreSet", "gameflow"}, {"ScoreGet", "gameflow"}, {"Countdown", "gameflow"},
        {"Win", "gameflow"}, {"Lose", "gameflow"}, {"RestartScene", "gameflow"},
        {"CheckpointSave", "gameflow"}, {"CheckpointLoad", "gameflow"},
        {"SetProgress", "ui"}, {"ShowFloatingText", "ui"}, {"FadeScreen", "ui"},
        {"SetCameraTarget", "camera"}, {"Zoom", "camera"}, {"SwitchCamera", "camera"},
        {"PlaySoundAt", "audio"}, {"StopSound", "audio"},
        {"OnAction", "input"}, {"OnActionUp", "input"}, {"OnActionHeld", "input"}, {"GetMoveAxis", "input"},
        {"RandomPick", "random"}, {"RandomPoint", "random"}, {"ShuffleList", "random"},
        {"ListMin", "math"}, {"ListMax", "math"},
        {"NotAnd", "logic"}, {"NotOr", "logic"}, {"AllTrue", "logic"}, {"AnyTrue", "logic"},
        {"IsEven", "logic"}, {"IsOdd", "logic"}, {"PickByIndex", "logic"}, {"FirstValid", "logic"},
        {"BranchInOrder", "logic"}, {"DoWhile", "logic"}, {"ForStep", "logic"},
        {"WaitUntil", "logic"}, {"WaitWhile", "logic"}, {"BecameTrue", "logic"}, {"BecameFalse", "logic"},
        {"CountUntil", "logic"}, {"WeightedBranch", "logic"}, {"WaitSteady", "logic"},
        {"StepThrough", "logic"}, {"WaitLimit", "logic"}, {"SetState", "logic"}, {"GetState", "logic"},
        {"IsInState", "logic"}, {"WhenStateBecomes", "logic"}, {"WhenStateLeaves", "logic"},
        {"HeldLong", "smart"}, {"HappenedNear", "smart"}, {"EveryWhile", "smart"},
        {"EaseToward", "smart"}, {"BlendTime", "smart"}, {"Stopwatch", "smart"},
        {"IsSlightly", "smart"}, {"IsMostly", "smart"}, {"IsVery", "smart"},
        {"PickBest", "smart"}, {"ShuffleBag", "smart"}, {"ChanceEachSecond", "smart"},
        {"TryFirst", "smart"}, {"AllSucceed", "smart"}, {"FlipOutcome", "smart"},
        {"AlwaysSucceed", "smart"}, {"RepeatUntilFail", "smart"}, {"WaitBetween", "smart"},
        {"SucceedNow", "smart"}, {"FailNow", "smart"}, {"CheckOutcome", "smart"},
        {"InCone", "smart"}, {"LineOfSight", "smart"}, {"InsideArea", "smart"},
        {"CountNearby", "smart"}, {"NearestObject", "smart"}, {"FarthestObject", "smart"},
        {"SortByDistance", "smart"}, {"WhenChanged", "smart"}, {"DoUnless", "smart"},
        {"RememberValue", "smart"}, {"RecallValue", "smart"}, {"HealthEmpty", "smart"},
        {"HealthFull", "smart"}, {"AddBuff", "smart"}, {"AdvanceObjective", "smart"},
        {"AssertThis", "smart"}, {"LogOnce", "smart"}, {"DrawDebugLine", "smart"},
    };
    if (!type) return nullptr;
    static const Row kGameplayLib[] = {
#include "generated/gplib_playground.inc"
    };
    for (const Row& r : kGameplayLib) if (std::strcmp(r.type, type) == 0) return r.demo;
    if (std::strncmp(type, "Lb", 2) == 0) {
        const VsLevel lv = VsLevelForNode(type);
        if (lv == VsLevel::Basic) return "logic_basics";
        if (lv == VsLevel::Advanced) return "logic_advanced";
        return "logic_mid";
    }
    for (const Row& r : kRows) if (std::strcmp(r.type, type) == 0) return r.demo;
    return nullptr;
}

// Folder of demo scenes, found by walking up from the process and the working directory.
inline std::string VsPlaygroundDir() {
    namespace fs = std::filesystem;
    std::error_code ec;
    std::vector<fs::path> seeds;
    seeds.push_back(fs::current_path(ec));
#if !defined(_WIN32)
    seeds.push_back(fs::read_symlink("/proc/self/exe", ec).parent_path());
#endif
    for (fs::path seed : seeds) {
        for (int i = 0; i < 8 && !seed.empty(); ++i) {
            const fs::path dir = seed / "examples/visual_script/playground";
            if (fs::exists(dir / "movement.okayscene", ec)) return dir.string();
            if (seed == seed.parent_path()) break;
            seed = seed.parent_path();
        }
    }
    return {};
}

} // namespace okay::editor::vsed
