#include <set>
#include <exception>
#include <functional>
#include <new>
#include <stdexcept>
#include <type_traits>
#include <unordered_map>
#include "okay/Render/ModelImport.hpp"
#include "okay/Render/ThreeMfReader.hpp"
#include "okay/Render/AmfReader.hpp"
#include "okay/Render/X3dReader.hpp"
#include "okay/Render/DxfReader.hpp"
#include "okay/Render/LwoReader.hpp"
#include "okay/Core/Log.hpp"
#include <cstdlib>
#include <cstring>
#include "okay/Core/ImportSidecar.hpp"
#include "okay/Render/Gltf.hpp"
#include "okay/Scene/Scene.hpp"
#include "okay/Scene/GameObject.hpp"
#include "okay/Scene/Transform.hpp"
#include "okay/Components/MeshRenderer.hpp"
#include "okay/Components/Animator.hpp"
#include "okay/Components/ModelAnimator.hpp"
#include "okay/Components/SkinnedMesh.hpp"
#include "okay/Components/BlendShapes.hpp"
#include "okay/Components/Character.hpp"
#include "okay/Animation/HumanoidLibrary.hpp"
#include "okay/Components/FootIK.hpp"
#include "okay/Components/HumanoidRetarget.hpp"
#include "okay/Components/LookAtIK.hpp"
#include "okay/Scene/SceneTemplates.hpp"
#include "okay/Components/Camera.hpp"
#include "okay/Components/PlayerController.hpp"
#include "okay/Components/ThirdPersonController.hpp"
#include "okay/Components/ThirdPersonShooterController.hpp"
#include "okay/Components/FirstPersonController.hpp"
#include "okay/Components/CharacterController3D.hpp"
#include "okay/Components/ClickToMoveController.hpp"
#include "okay/Components/TopDownController.hpp"
#include "okay/Components/NPCController.hpp"
#include "okay/Components/Platformer25DController.hpp"
#include "okay/Components/TopDownShooterController.hpp"
#include "okay/Physics/Collider3D.hpp"
#include "okay/Physics/Rigidbody3D.hpp"
#include "okay/Math/Quat.hpp"
#include "okay/Math/Mat4.hpp"
#include "okay/Render/FbxReader.hpp"
#include "okay/Core/ThirdParty.hpp"
#include <filesystem>
#include <fstream>
#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <functional>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <atomic>
#include <chrono>
#include "ModelImportInternal.hpp"
#include "FileOpen.hpp"
#include <cstring>
#include <cstdio>

namespace okay {
using model_import::Lower;
using model_import::BaseName;

// ---- Import report ----------------------------------------------------------
ModelReport AnalyzeModel(Scene& scene, GameObject* root) {
    ModelReport r;
    if (!root || !root->transform) return r;
    std::vector<GameObject*> sub;
    for (const auto& up : scene.Objects())
        if (up && up->IsSelfOrDescendantOf(root)) sub.push_back(up.get());
    r.objects = (int)sub.size();
    std::set<Transform*> bones;
    for (GameObject* g : sub) {
        if (auto* mr = g->GetComponent<MeshRenderer>()) {
            ++r.meshes;
            r.vertices += (int)mr->mesh.vertices.size();
            r.triangles += (int)mr->mesh.triangles.size() / 3;
        }
        auto* sm = g->GetComponent<SkinnedMesh>();
        // A morph-only mesh's pass-through skin (its one joint is itself) isn't a rig.
        if (sm && sm->joints.size() == 1 && sm->joints[0] == g->transform) sm = nullptr;
        if (sm) {
            ++r.skinnedMeshes;
            sm->ResolveJoints();
            for (Transform* t : sm->joints) if (t) bones.insert(t);
            r.missingBones += sm->MissingJoints();
            r.unweightedVertices += sm->UnweightedVertices();
            r.maxInfluences = std::max(r.maxInfluences, sm->MaxInfluences());
        }
        if (auto* bs = g->GetComponent<BlendShapes>()) r.blendShapes += (int)bs->shapes.size();
    }
    r.bones = (int)bones.size();
    // Skeleton roots: bones whose parent isn't a bone. One connected skeleton
    // has exactly one (the hips / root bone).
    for (Transform* b : bones) {
        Transform* p = b->Parent();
        if (!p || !bones.count(p)) {
            ++r.skeletonRoots;
            if (r.rootBone.empty() && b->gameObject) r.rootBone = b->gameObject->name;
        }
        if (b->gameObject && !b->gameObject->IsSelfOrDescendantOf(root))
            r.warnings.push_back("bone '" + b->gameObject->name + "' sits outside the model");
    }
    if (auto* ma = root->GetComponent<ModelAnimator>()) {
        r.clips = ma->ClipCount();
        for (const auto& c : ma->clips) for (const auto& nc : c.nodes) r.keys += nc.clip.KeyCount();
        r.keysBeforeReduction = ma->lastOptimizeBefore;
        r.clipNames = ma->ClipNames();
    }
    if (r.missingBones)        r.warnings.push_back(std::to_string(r.missingBones) + " bone(s) referenced by a skin weren't found");
    if (r.unweightedVertices)  r.warnings.push_back(std::to_string(r.unweightedVertices) + " vertices have no bone weights (they won't move)");
    if (r.skeletonRoots > 1)   r.warnings.push_back("the skeleton is split into " + std::to_string(r.skeletonRoots) + " separate bone trees");
    if (r.skinnedMeshes == 0 && r.blendShapes == 0 && r.clips > 0 && r.meshes > 0)
        r.warnings.push_back("animations but no skin: parts move rigidly (no mesh deformation)");
    return r;
}

std::string ModelReport::Summary() const {
    std::string s = std::to_string(meshes) + " mesh" + (meshes == 1 ? "" : "es") + ", " +
                    std::to_string(vertices) + " verts";
    if (skinnedMeshes)
        s += ", skinned (" + std::to_string(bones) + " bones" +
             (rootBone.empty() ? "" : ", root '" + rootBone + "'") +
             ", up to " + std::to_string(maxInfluences) + " weights/vertex)";
    if (clips) {
        s += ", " + std::to_string(clips) + " clip" + (clips == 1 ? "" : "s") + ":";
        for (int i = 0; i < (int)clipNames.size() && i < 5; ++i) s += (i ? ", " : " ") + clipNames[i];
        if ((int)clipNames.size() > 5) s += ", ...";
        if (keysBeforeReduction > keys && keysBeforeReduction > 0)
            s += " (" + std::to_string(keys) + " keys, " +
                 std::to_string((int)std::lround(100.0 * (double)(keysBeforeReduction - keys) / (double)keysBeforeReduction)) +
                 "% removed as redundant)";
    }
    if (blendShapes) s += ", " + std::to_string(blendShapes) + " blend shape" + (blendShapes == 1 ? "" : "s");
    return s;
}

// ---- Adding clips from another file (Mixamo animation downloads) ----------
// "mixamorig9:LeftUpLeg" / "Armature|LeftUpLeg" / "Left_Up_Leg" -> "leftupleg".
static std::string NormBoneName(const std::string& n) {
    std::string s = n;
    const std::size_t cut = s.find_last_of(":|");
    if (cut != std::string::npos) s = s.substr(cut + 1);
    std::string o;
    for (char c : s) if (std::isalnum((unsigned char)c)) o += (char)std::tolower((unsigned char)c);
    if (o.rfind("mixamorig", 0) == 0) o = o.substr(9);
    return o;
}
static GameObject* FindHips(GameObject* root, const std::vector<GameObject*>& sub) {
    for (GameObject* g : sub) { std::string k = NormBoneName(g->name); if (k == "hips" || k == "pelvis") return g; }
    for (GameObject* g : sub) { std::string k = NormBoneName(g->name); if (k.find("hips") != std::string::npos || k.find("pelvis") != std::string::npos) return g; }
    (void)root; return nullptr;
}
static std::vector<GameObject*> Subtree(Scene& scene, GameObject* root) {
    std::vector<GameObject*> out;
    for (const auto& up : scene.Objects()) if (up->IsSelfOrDescendantOf(root)) out.push_back(up.get());
    return out;
}
// Hips height above the model root, in the root's own (unscaled) space.
static float RestHipsHeight(GameObject* root, GameObject* hips) {
    if (!root || !hips) return 0.0f;
    Vec3 p = root->transform->LocalToWorldMatrix().Inverse().MultiplyPoint(hips->transform->Position());
    return std::fabs(p.y);
}
// Lower-case words of a clip name, split at separators, digits and camelCase,
// after any "Armature|" prefix: "Armature|RunningJump_2" -> running, jump.
static std::vector<std::string> ClipNameWords(const std::string& raw) {
    std::size_t cut = raw.find_last_of('|');
    std::vector<std::string> out;
    std::string cur;
    char prev = 0;
    for (char c : raw.substr(cut == std::string::npos ? 0 : cut + 1)) {
        bool alpha = std::isalpha((unsigned char)c) != 0;
        if (!alpha || (std::isupper((unsigned char)c) && std::islower((unsigned char)prev))) {
            if (!cur.empty()) out.push_back(cur);
            cur.clear();
        }
        if (alpha) cur += (char)std::tolower((unsigned char)c);
        prev = c;
    }
    if (!cur.empty()) out.push_back(cur);
    return out;
}

// Map idle/walk/run/jump/fall/land and the crouch / prone clips from the clip
// names, where not set yet.
void AutoMapLocomotion(ModelAnimator* ma) {
    if (!ma) return;
    // By whole words of the clip name, an exact name first ("Walk" before
    // "Walk Back"): "Stairs" isn't "air", and a "Running Jump" is a jump.
    static const std::vector<std::string> kWords[6] = {
        {"idle", "stand", "standing", "breathing"}, {"walk", "walking"},
        {"run", "running", "sprint", "sprinting", "jog", "jogging"},
        {"jump", "jumping", "leap"}, {"fall", "falling", "air", "inair", "drop"}, {"land", "landing"}};
    std::string* slot[6] = {&ma->idleClip, &ma->walkClip, &ma->runClip, &ma->jumpClip, &ma->fallClip, &ma->landClip};
    std::string* stanceSlot[5] = {&ma->crouchClip, &ma->crouchWalkClip, &ma->proneClip, &ma->crawlClip, &ma->getUpClip};
    std::string* dirSlot[4] = {&ma->strafeLeftClip, &ma->strafeRightClip, &ma->walkBackClip, &ma->runBackClip};
    auto is = [&](int role, const std::string& w) {
        for (const std::string& k : kWords[role]) if (k == w) return true;
        return false;
    };
    auto any = [](const std::vector<std::string>& ws, std::initializer_list<const char*> ks) {
        for (const std::string& w : ws) for (const char* k : ks) if (w == k) return true;
        return false;
    };
    std::vector<std::vector<std::string>> words;
    for (const auto& c : ma->clips) words.push_back(ClipNameWords(c.name));
    std::vector<bool> used(ma->clips.size(), false);
    for (std::size_t ci = 0; ci < ma->clips.size(); ++ci) {   // clips already given a state
        for (const std::string* sl : slot) if (*sl == ma->clips[ci].name) used[ci] = true;
        for (const std::string* sl : stanceSlot) if (*sl == ma->clips[ci].name) used[ci] = true;
        for (const std::string* sl : dirSlot) if (*sl == ma->clips[ci].name) used[ci] = true;
    }
    // Crouch / prone clips, and changes between stances ("Crouch To Stand", "Get Up").
    auto crouchy = [&](std::size_t ci) { return any(words[ci], {"crouch", "crouched", "crouching", "sneak", "sneaking", "squat", "squatting"}); };
    auto proney  = [&](std::size_t ci) { return any(words[ci], {"prone", "lying", "crawl", "crawling"}); };
    auto change  = [&](std::size_t ci) { return any(words[ci], {"to", "up", "down"}); };
    auto moving  = [&](std::size_t ci) { return any(words[ci], {"walk", "walking", "move", "moving", "forward", "run", "running"}); };
    auto crawl   = [&](std::size_t ci) { return any(words[ci], {"crawl", "crawling"}); };
    auto getUp   = [&](std::size_t ci) {
        return (any(words[ci], {"up"}) && any(words[ci], {"get", "getting", "stand", "standing"})) ||
               (proney(ci) && any(words[ci], {"to"}) && any(words[ci], {"stand", "standing", "idle"}));
    };
    // Back / left / right clips are kept out of the forward slots, unless the model turns
    // out to have no directional clips: then it maps exactly as it did before they existed.
    auto mapForward = [&](bool skipDir) {
    for (int pass = 0; pass < 2; ++pass)
        for (int role = 0; role < 6; ++role)
            for (std::size_t ci = 0; ci < ma->clips.size() && slot[role]->empty(); ++ci) {
                if (used[ci]) continue;
                bool hit = false, air = false;
                if (pass == 0) {
                    std::string whole;
                    for (const std::string& w : words[ci]) whole += w;
                    hit = is(role, whole);
                } else {
                    for (const std::string& w : words[ci]) {
                        hit = hit || is(role, w);
                        air = air || is(3, w) || is(4, w) || is(5, w);
                    }
                }
                // A crouch walk, a drop to prone or a get-up is a stance clip (below).
                const bool stance = crouchy(ci) || proney(ci) || getUp(ci);
                const bool backward = any(words[ci], {"back", "backward", "backwards"});
                const bool sideways = any(words[ci], {"left", "right", "strafe", "strafing"});
                if (hit && !stance && !(skipDir && (backward || sideways)) && !(role < 3 && air)) { *slot[role] = ma->clips[ci].name; used[ci] = true; }
            }
    };
    mapForward(true);
    const std::function<bool(std::size_t)> roles[5] = {
        [&](std::size_t ci) { return crouchy(ci) && !change(ci) && !moving(ci) && !any(words[ci], {"sneak", "sneaking"}); },
        [&](std::size_t ci) { return crouchy(ci) && !change(ci) && (moving(ci) || any(words[ci], {"sneak", "sneaking"})); },
        [&](std::size_t ci) { return proney(ci) && !change(ci) && !crawl(ci) && !moving(ci); },
        [&](std::size_t ci) { return (crawl(ci) || (proney(ci) && moving(ci))) && !change(ci); },
        getUp};
    for (int role = 0; role < 5; ++role)
        for (std::size_t ci = 0; ci < ma->clips.size() && stanceSlot[role]->empty(); ++ci)
            if (!used[ci] && roles[role](ci)) { *stanceSlot[role] = ma->clips[ci].name; used[ci] = true; }
    // "Walk Back" / "Run Back" are locomotion. "Back Flip" and similar tricks are not.
    auto locomotes = [&](std::size_t ci) { return any(words[ci], {"walk", "walking", "run", "running", "strafe", "strafing", "move", "moving"}); };
    auto trick = [&](std::size_t ci) { return any(words[ci], {"flip", "flipping", "dodge", "roll", "rolling", "attack", "kick", "punch"}); };
    auto back = [&](std::size_t ci) {
        return any(words[ci], {"back", "backward", "backwards"}) && locomotes(ci) && !trick(ci);
    };
    auto left = [&](std::size_t ci) { return any(words[ci], {"left"}); };
    auto right = [&](std::size_t ci) { return any(words[ci], {"right"}); };
    auto runny = [&](std::size_t ci) { return any(words[ci], {"run", "running", "sprint", "sprinting"}); };
    auto side = [&](std::size_t ci) { return any(words[ci], {"strafe", "strafing", "sidestep", "side", "walk", "walking"}); };
    const std::function<bool(std::size_t)> dirRoles[4] = {
        [&](std::size_t ci) { return left(ci) && side(ci) && !back(ci); },
        [&](std::size_t ci) { return right(ci) && side(ci) && !back(ci); },
        [&](std::size_t ci) { return back(ci) && !runny(ci); },
        [&](std::size_t ci) { return back(ci) && runny(ci); },
    };
    for (int role = 0; role < 4; ++role)
        for (std::size_t ci = 0; ci < ma->clips.size() && dirSlot[role]->empty(); ++ci)
            if (!used[ci] && dirRoles[role](ci)) { *dirSlot[role] = ma->clips[ci].name; used[ci] = true; }
    if (!ma->HasDirectionalClips()) mapForward(false);
}

std::string SuggestUnassignedSlot(const ModelAnimator& ma, int slot) {
    if (slot < 0 || slot >= ModelAnimator::SlotCount) return {};
    if (!ma.SlotClip(slot).empty()) return {};
    ModelAnimator scratch;
    scratch.clips.resize(ma.clips.size());
    for (std::size_t i = 0; i < ma.clips.size(); ++i) scratch.clips[i].name = ma.clips[i].name;
    for (int s = 0; s < ModelAnimator::SlotCount; ++s) scratch.SlotClip(s) = ma.SlotClip(s);
    AutoMapLocomotion(&scratch);
    return scratch.SlotClip(slot);
}

// Switch a retargeted model's locomotion between its clips and the built-in
// animation to match which slots have a clip: none = pure built-in (the
// HumanoidRetarget owns every frame), any = clips drive the slots they fill and
// the built-in animation covers the rest. Inert for a model that isn't retargeted.
void SettleCharacterLocomotion(ModelAnimator* ma) {
    if (!ma || !ma->BuiltinRigPresent()) return;
    bool any = false;
    for (int s = 0; s < ModelAnimator::SlotCount; ++s) any = any || ma->SlotFilled(s);
    if (any && !ma->driveByMovement) ma->smoothLocomotion = true;
    ma->driveByMovement = any;
    ma->inPlace = true;     // the controller moves the capsule: clips only cycle the limbs
    ma->autoPlay = false;   // the slots decide what plays (a clip 0 on the first frame would show the wrong pose)
}

// "idle='Idle' walk='Walking'" - the clip names in the slots a model's clips fill.
static std::string SlotSummary(const ModelAnimator* ma) {
    std::string out;
    if (!ma) return out;
    for (int s = 0; s < ModelAnimator::SlotCount; ++s) {
        if (!ma->SlotFilled(s)) continue;
        std::string label = ModelAnimator::SlotName(s);
        for (char& c : label) c = (char)std::tolower((unsigned char)c);
        out += (out.empty() ? "" : " ") + label + "='" + ma->SlotClip(s) + "'";
    }
    return out;
}

// Put the built-in animation on the model under `root` (the player's Character in
// drive-external mode + a HumanoidRetarget wired to the bones). Returns how many
// of the 15 bones matched, or -1 when the player has no Character.
static int EnableBuiltinFallback(Scene& scene, GameObject* player, GameObject* root) {
    auto* ch = player->GetComponent<Character>();
    if (!ch) return -1;
    HumanoidRetarget* rt = player->GetComponent<HumanoidRetarget>();
    if (!rt) rt = player->AddComponent<HumanoidRetarget>();
    rt->Rewire();
    const int found = rt->DetectAndWire(scene, root);
    ch->driveExternal = true;
    ch->enabled = true;
    if (auto* mr = player->GetComponent<MeshRenderer>()) mr->enabled = false;
    for (const auto& up : scene.Objects())   // the blocky part rig
        if (up->name == "Rig" && up->transform && up->transform->Parent() == player->transform) up->active = false;
    return found;
}

// One clip through the rest poses (see "Rest poses" in AddAnimationsFromFile):
// sampled at every key time, each mapped target bone gets the local rotation
// that puts it at (source root-space rotation) * conv; the hips also get the
// source's travel away from its rest, scaled by `ratio` into the target's
// parent space. Other position and scale tracks keep the target's lengths.
static void RetargetByRest(const ModelAnimator::Clip& c, ModelAnimator::Clip& out, GameObject* src,
                           GameObject* modelRoot, const std::vector<GameObject*>& mapped,
                           const std::unordered_map<GameObject*, GameObject*>& srcOf,
                           const std::unordered_map<GameObject*, Quat>& conv,
                           GameObject* sHips, GameObject* tHips, float ratio) {
    std::unordered_map<std::string, const AnimationClip*> track;   // source node -> its clip
    std::vector<float> times;
    for (const ModelAnimator::NodeClip& nc : c.nodes) {
        track[nc.node] = &nc.clip;
        for (const auto& kv : nc.clip.Tracks()) for (const auto& k : kv.second.Keys()) times.push_back(k.time);
    }
    std::sort(times.begin(), times.end());
    times.erase(std::unique(times.begin(), times.end(), [](float a, float b) { return b - a < 1e-5f; }), times.end());

    float t = 0.0f;
    std::unordered_map<GameObject*, Quat> sAnim, tAnim;
    std::function<Quat(Transform*)> S = [&](Transform* x) -> Quat {   // source root-space, at t
        if (!x || x == src->transform) return Quat::Identity;
        auto f = sAnim.find(x->gameObject);
        if (f != sAnim.end()) return f->second;
        Vec3 p = x->localPosition, sc = x->localScale; Quat q = x->localRotation;
        auto it = track.find(x->gameObject->name);
        if (it != track.end()) ModelAnimator::EvalClipInto(*it->second, t, p, q, sc);
        return sAnim[x->gameObject] = S(x->Parent()) * q;
    };
    std::function<Quat(Transform*)> T = [&](Transform* x) -> Quat {   // target root-space, at t
        if (!x || x == modelRoot->transform) return Quat::Identity;
        auto f = tAnim.find(x->gameObject);
        if (f != tAnim.end()) return f->second;
        auto m = srcOf.find(x->gameObject);
        Quat r = m != srcOf.end() ? S(m->second->transform) * conv.at(x->gameObject) : T(x->Parent()) * x->localRotation;
        return tAnim[x->gameObject] = r;
    };
    // Hips travel: source parent space -> root -> target parent space.
    auto parentRot = [](GameObject* g, GameObject* root) {
        Quat r = Quat::Identity;
        for (Transform* x = g->transform->Parent(); x && x != root->transform; x = x->Parent()) r = x->localRotation * r;
        return r;
    };
    const AnimationClip* hipsTrack = nullptr;
    if (sHips && tHips && srcOf.count(tHips)) {
        auto it = track.find(sHips->name);
        if (it != track.end() && it->second->HasTrack("position.x")) hipsTrack = it->second;
    }
    const Quat hipsTo = (tHips ? parentRot(tHips, modelRoot).Inverse() : Quat::Identity) *
                        (sHips ? parentRot(sHips, src) : Quat::Identity);

    std::vector<AnimationClip> clips(mapped.size());
    std::vector<Quat> prev(mapped.size());
    for (float tm : times) {
        t = tm; sAnim.clear(); tAnim.clear();
        for (std::size_t i = 0; i < mapped.size(); ++i) {
            GameObject* g = mapped[i];
            Quat q = (T(g->transform->Parent()).Inverse() * T(g->transform)).Normalized();
            if (tm != times.front() && Quat::Dot(prev[i], q) < 0.0f) q = Quat{-q.x, -q.y, -q.z, -q.w};
            prev[i] = q;
            AnimationClip& ac = clips[i];
            ac.smoothKeys = false;
            ac.AddKey("rotation.qx", t, q.x); ac.AddKey("rotation.qy", t, q.y);
            ac.AddKey("rotation.qz", t, q.z); ac.AddKey("rotation.qw", t, q.w);
        }
        if (hipsTrack) {
            Vec3 p = sHips->transform->localPosition, sc; Quat q;
            ModelAnimator::EvalClipInto(*hipsTrack, t, p, q, sc);
            Vec3 d = hipsTo * ((p - sHips->transform->localPosition) * ratio);
            Vec3 tp = tHips->transform->localPosition + d;
            AnimationClip& ac = clips[std::find(mapped.begin(), mapped.end(), tHips) - mapped.begin()];
            ac.AddKey("position.x", t, tp.x); ac.AddKey("position.y", t, tp.y); ac.AddKey("position.z", t, tp.z);
        }
    }
    for (std::size_t i = 0; i < mapped.size(); ++i) out.nodes.push_back({mapped[i]->name, std::move(clips[i])});
}

int AddAnimationsFromFile(Scene& scene, GameObject* modelRoot, const std::string& path, std::string* outLog) {
    auto say = [&](const std::string& m) { if (outLog) *outLog = m; };
    if (!modelRoot) { say("Pick the model to add animations to."); return 0; }
    Scene tmp;
    bool ok = false;
    GameObject* src = ImportModelScene(tmp, path, &ok);
    if (!ok || !src) { say("Couldn't read " + BaseName(path) + "."); return 0; }
    ModelAnimator* sma = src->GetComponent<ModelAnimator>();
    if (!sma || sma->clips.empty()) { say(BaseName(path) + " has no animations in it."); return 0; }

    // Target bones by normalized name.
    std::vector<GameObject*> tsub = Subtree(scene, modelRoot);
    std::unordered_map<std::string, std::string> tName;   // norm -> real name in this model
    for (GameObject* g : tsub) tName.emplace(NormBoneName(g->name), g->name);
    ModelAnimator* ma = modelRoot->GetComponent<ModelAnimator>();
    if (!ma) for (GameObject* g : tsub) if ((ma = g->GetComponent<ModelAnimator>())) break;
    if (!ma) ma = modelRoot->AddComponent<ModelAnimator>();

    // Proportions: same skeleton (Mixamo clip downloaded for THIS character) ->
    // copy everything; different -> rotations + scaled hips motion.
    std::vector<GameObject*> ssub = Subtree(tmp, src);
    GameObject* sHips = FindHips(src, ssub);
    GameObject* tHips = FindHips(modelRoot, tsub);
    // Compare the hips' offset in their parent's space: the same space the hips
    // position track lives in (so the ratio scales it directly).
    float sh = sHips ? sHips->transform->localPosition.Magnitude() : 0.0f;
    float th = tHips ? tHips->transform->localPosition.Magnitude() : 0.0f;
    if (sh < 1e-5f || th < 1e-5f) { sh = RestHipsHeight(src, sHips); th = RestHipsHeight(modelRoot, tHips); }
    float ratio = (sh > 1e-5f && th > 1e-5f) ? th / sh : 1.0f;
    const bool retarget = std::fabs(ratio - 1.0f) > 0.03f;
    const std::string sHipsKey = sHips ? NormBoneName(sHips->name) : std::string();

    // Rest poses. When they differ (an A-pose rig, other joint axes), local
    // rotations can't be copied: each bone instead gets the source bone's
    // root-space rotation away from ITS rest, put on this model's rest after
    // turning that bone to aim where the source's did (so a T-pose clip
    // brings an A-pose rig's arms down to the same place, not 45 deg lower).
    std::unordered_map<std::string, GameObject*> sByKey;
    for (GameObject* g : ssub) if (g != src) sByKey.emplace(NormBoneName(g->name), g);
    std::unordered_map<GameObject*, GameObject*> srcOf;   // target bone -> source bone
    std::vector<GameObject*> mapped;                      // the same, in scene order
    bool sameRest = true;
    for (GameObject* g : tsub) {
        auto it = g == modelRoot ? sByKey.end() : sByKey.find(NormBoneName(g->name));
        if (it == sByKey.end()) continue;
        srcOf[g] = it->second;
        mapped.push_back(g);
        if (std::fabs(Quat::Dot(g->transform->localRotation, it->second->transform->localRotation)) < 0.9998f)
            sameRest = false;
    }
    // Avatar slots: when the names don't line up (a Mixamo clip on a mannequin
    // rig), a humanoid's bones pair up by what they are instead: hips with hips,
    // left forearm with left forearm, the spine chains link for link from the
    // hips (the shorter chain's extra links keep their rest), the necks. Only
    // when both skeletons are humanoids (Animation/HumanoidBones.hpp), and
    // always through the rest poses below: different rigs, different rests.
    int bySlot = 0;
    {
        const humanoid::BoneMap tm = humanoid::MapHumanoidBones(modelRoot), sm = humanoid::MapHumanoidBones(src);
        if (tm.humanoid && sm.humanoid) {
            std::unordered_set<GameObject*> taken;
            for (const auto& kv : srcOf) taken.insert(kv.second);
            auto link = [&](GameObject* t, GameObject* sb) {
                if (!t || !sb || t == modelRoot || srcOf.count(t) || taken.count(sb)) return;
                srcOf[t] = sb; taken.insert(sb); ++bySlot;
            };
            for (int b = 0; b < humanoid::BoneCount; ++b) if (b != humanoid::Torso) link(tm.bone[b], sm.bone[b]);
            for (std::size_t i = 0; i < tm.spine.size() && i < sm.spine.size(); ++i) link(tm.spine[i], sm.spine[i]);
            link(tm.neck, sm.neck);
            if (!tHips) tHips = tm.bone[humanoid::Hips];
            if (!sHips) sHips = sm.bone[humanoid::Hips];
        }
        if (bySlot) {
            sameRest = false;
            mapped.clear();   // scene order again
            for (GameObject* g : tsub) if (srcOf.count(g)) mapped.push_back(g);
        }
    }
    auto rootRot = [](GameObject* g, GameObject* root) {
        Quat r = Quat::Identity;
        for (Transform* t = g->transform; t && t != root->transform; t = t->Parent()) r = t->localRotation * r;
        return r;
    };
    const Mat4 sInv = src->transform->LocalToWorldMatrix().Inverse();
    const Mat4 tInv = modelRoot->transform->LocalToWorldMatrix().Inverse();
    std::unordered_map<GameObject*, Quat> aim;   // turns a target bone's rest to aim like the source's
    std::function<Quat(GameObject*)> Aim = [&](GameObject* g) -> Quat {
        auto f = aim.find(g);
        if (f != aim.end()) return f->second;
        Quat a = Quat::Identity;
        Transform* pt = g->transform->Parent();
        if (pt && srcOf.count(pt->gameObject)) a = Aim(pt->gameObject);   // a leaf follows its parent
        for (Transform* ct : g->transform->Children()) {
            auto c = srcOf.find(ct->gameObject);
            if (c == srcOf.end()) continue;
            Vec3 td = tInv.MultiplyPoint(ct->Position()) - tInv.MultiplyPoint(g->transform->Position());
            Vec3 sd = sInv.MultiplyPoint(c->second->transform->Position()) - sInv.MultiplyPoint(srcOf[g]->transform->Position());
            if (td.SqrMagnitude() > 1e-10f && sd.SqrMagnitude() > 1e-10f) a = Quat::FromToRotation(td.Normalized(), sd.Normalized());
            break;
        }
        return aim[g] = a;
    };
    // Target root-space rotation = source root-space rotation * conv.
    std::unordered_map<GameObject*, Quat> conv;
    if (!sameRest)
        for (const auto& pr : srcOf)
            conv[pr.first] = rootRot(pr.second, src).Inverse() * Aim(pr.first) * rootRot(pr.first, modelRoot);

    std::unordered_set<std::string> srcNames;   // source bones that drive a bone of this model
    for (const auto& kv : srcOf) srcNames.insert(kv.second->name);
    int added = 0, matched = 0, total = 0;
    std::string names;
    for (const ModelAnimator::Clip& c : sma->clips) {
        ModelAnimator::Clip out;
        out.name = c.name;
        for (int n = 2; ; ++n) {   // unique within the target library
            bool clash = false;
            for (const auto& e : ma->clips) if (e.name == out.name) { clash = true; break; }
            if (!clash) break;
            out.name = c.name + " " + std::to_string(n);
        }
        out.events = c.events;
        if (!sameRest) {   // see "Rest poses" above
            for (const ModelAnimator::NodeClip& nc : c.nodes) { ++total; matched += (int)srcNames.count(nc.node); }
            RetargetByRest(c, out, src, modelRoot, mapped, srcOf, conv, sHips, tHips, ratio);
        } else for (const ModelAnimator::NodeClip& nc : c.nodes) {
            ++total;
            const std::string key = NormBoneName(nc.node);
            auto it = tName.find(key);
            if (it == tName.end()) continue;
            ++matched;
            const bool isHips = !sHipsKey.empty() && key == sHipsKey;
            if (!retarget) { out.nodes.push_back({it->second, nc.clip}); continue; }
            AnimationClip ac; ac.smoothKeys = false;
            for (const auto& kv : nc.clip.Tracks()) {
                const bool pos = kv.first.rfind("position.", 0) == 0, scl = kv.first.rfind("scale.", 0) == 0;
                if (scl || (pos && !isHips)) continue;   // other bones keep this model's lengths
                for (const auto& k : kv.second.Keys()) ac.AddKey(kv.first, k.time, pos ? k.value * ratio : k.value);
            }
            if (!ac.Tracks().empty()) out.nodes.push_back({it->second, std::move(ac)});
        }
        if (out.nodes.empty()) continue;
        ModelAnimator::NormalizeClip(out);
        names += (names.empty() ? "" : ", ") + out.name;
        ma->clips.push_back(std::move(out));
        ++added;
    }
    if (!added) { say("None of the bones in " + BaseName(path) + " match this model's skeleton."); return 0; }
    AutoMapLocomotion(ma);
    SettleCharacterLocomotion(ma);   // a retargeted player starts using the new clips in their slots
    char buf[200];
    std::snprintf(buf, sizeof(buf), " (%d/%d bone tracks matched%s)", matched, total,
                  bySlot ? ", paired by humanoid bone and converted to this model's rest pose" :
                  !sameRest ? ", converted to this model's rest pose" : retarget ? ", retargeted to this model's proportions" : "");
    say("Added " + std::to_string(added) + " clip" + (added == 1 ? "" : "s") + ": " + names + buf);
    return added;
}

int CountHumanoidBones(Scene&, GameObject* modelRoot, std::string* why, const std::string* byHand) {
    const humanoid::BoneMap m = humanoid::MapHumanoidBones(modelRoot, byHand);
    if (!m.humanoid) { if (why) *why = m.why; return 0; }
    return m.found;
}

int EnableBuiltinAnimations(Scene& scene, GameObject* modelRoot, std::string* outLog, const std::string* byHand) {
    auto say = [&](const std::string& m) { if (outLog) *outLog = m; };
    if (!modelRoot || !modelRoot->transform) { say("Select an imported model first."); return 0; }
    if (auto* mr = modelRoot->GetComponent<MeshRenderer>(); mr && !mr->mesh.vertices.empty() &&
        !modelRoot->GetComponent<Character>()) {
        say("'" + modelRoot->name + "' is a single merged mesh with no skeleton - use Rig Model (Humanoid) instead.");
        return 0;
    }
    std::string why;
    const int bones = CountHumanoidBones(scene, modelRoot, &why, byHand);
    if (bones < 8) {
        say("'" + modelRoot->name + "' isn't a humanoid skeleton" +
            (why.empty() ? " (" + std::to_string(bones) + "/15 bones recognised)" : ": " + why) +
            ". Rig Model (Humanoid) can bind its mesh to the built-in skeleton instead.");
        return bones;
    }
    auto* ch = modelRoot->GetComponent<Character>();
    const bool addedCh = !ch;
    if (!ch) ch = modelRoot->AddComponent<Character>();
    ch->driveExternal = true;
    ch->separateParts = false;
    ch->anim = 1;   // idle
    auto* rt = modelRoot->GetComponent<HumanoidRetarget>();
    if (!rt) rt = modelRoot->AddComponent<HumanoidRetarget>();
    rt->Rewire();
    for (int b = 0; b < humanoid::BoneCount; ++b) rt->boneMap[b] = byHand ? byHand[b] : std::string();
    const int found = rt->DetectAndWire(scene, modelRoot);
    if (found < 8) {
        if (addedCh) ch->enabled = false;
        say("Only " + std::to_string(found) + "/15 humanoid bones matched on '" + modelRoot->name + "'.");
        return found;
    }
    // The retarget owns the pose: the model's own clips stay for one-shots but
    // mustn't auto-play over it every frame.
    for (const auto& up : scene.Objects())
        if (up->IsSelfOrDescendantOf(modelRoot))
            if (auto* ma = up->GetComponent<ModelAnimator>()) { ma->autoPlay = false; ma->driveByMovement = false; }
    HumanoidLibrary::EnsureAnimator(modelRoot);
    say("Built-in animations on '" + modelRoot->name + "' (" + std::to_string(found) + "/15 bones matched)");
    return found;
}

// Bounds of every mesh under `root` as currently posed, in the space `toSpace`
// maps world into. Skinned meshes are deformed here from their bind pose and
// joints: with GPU skinning the renderer's mesh holds the raw bind vertices
// (centimetres on Mixamo rigs, before the armature's scale), so measuring it
// sized characters 100x off.
static bool SubtreeBounds(Scene& scene, GameObject* root, const Mat4& toSpace, Vec3& lo, Vec3& hi) {
    bool any = false;
    auto grow = [&](const Vec3& w) {
        if (!any) { lo = hi = w; any = true; return; }
        lo.x = std::fmin(lo.x, w.x); hi.x = std::fmax(hi.x, w.x);
        lo.y = std::fmin(lo.y, w.y); hi.y = std::fmax(hi.y, w.y);
        lo.z = std::fmin(lo.z, w.z); hi.z = std::fmax(hi.z, w.z);
    };
    for (const auto& up : scene.Objects()) {
        GameObject* g = up.get();
        if (!g || !g->transform || !g->IsSelfOrDescendantOf(root)) continue;
        auto* mr = g->GetComponent<MeshRenderer>();
        if (!mr) continue;
        auto* sm = g->GetComponent<SkinnedMesh>();
        if (sm) sm->ResolveJoints();
        if (sm && sm->Ready() && sm->jointIdx.size() == sm->bind.vertices.size() &&
            sm->jointWt.size() == sm->bind.vertices.size()) {
            // World position per vertex: sum of w * jointWorld * inverseBind * v.
            std::vector<Mat4> skin(sm->joints.size());
            for (std::size_t j = 0; j < skin.size(); ++j) {
                Mat4 jw = sm->joints[j] ? sm->joints[j]->LocalToWorldMatrix() : g->transform->LocalToWorldMatrix();
                Mat4 ib = j < sm->inverseBind.size() ? sm->inverseBind[j] : Mat4::Identity();
                skin[j] = toSpace * jw * ib;
            }
            Mat4 rigid = toSpace * g->transform->LocalToWorldMatrix();
            for (std::size_t v = 0; v < sm->bind.vertices.size(); ++v) {
                const Vec3& p = sm->bind.vertices[v];
                Vec3 acc{0, 0, 0}; float wsum = 0.0f;
                for (int k = 0; k < 4; ++k) {
                    float w = sm->jointWt[v][k]; int j = sm->jointIdx[v][k];
                    if (w <= 1e-4f || j < 0 || j >= (int)skin.size()) continue;
                    acc = acc + skin[j].MultiplyPoint(p) * w; wsum += w;
                }
                grow(wsum > 1e-4f ? acc * (1.0f / wsum) : rigid.MultiplyPoint(p));
            }
            continue;
        }
        if (mr->mesh.vertices.empty()) continue;
        Vec3 blo, bhi; mr->mesh.Bounds(blo, bhi);
        Mat4 m = toSpace * g->transform->LocalToWorldMatrix();
        for (int c = 0; c < 8; ++c)
            grow(m.MultiplyPoint({c & 1 ? bhi.x : blo.x, c & 2 ? bhi.y : blo.y, c & 4 ? bhi.z : blo.z}));
    }
    return any;
}

// Which way an imported humanoid faces, read from its skeleton: the
// character's right side (right thigh/arm/foot minus the left one) crossed
// with up. Flattened, in `toSpace` coordinates. False when the rig has no
// recognisable left/right pairs (static meshes, non-humanoids).
static bool DetectModelForward(Scene& scene, GameObject* root, const Mat4& toSpace, Vec3& fwd) {
    Vec3 pos[15]; int best[15];
    for (int i = 0; i < 15; ++i) best[i] = -1;
    for (const auto& up : scene.Objects()) {
        GameObject* g = up.get();
        if (!g || !g->transform || g == root || !g->IsSelfOrDescendantOf(root)) continue;
        int sc = 0;
        int b = HumanoidRetarget::ClassifyBone(g->name, sc);
        if (b < 3 || b >= 15 || sc <= best[b]) continue;
        best[b] = sc;
        pos[b] = toSpace.MultiplyPoint(g->transform->Position());
    }
    const int pairs[4][2] = {{9, 12}, {3, 6}, {11, 14}, {4, 7}};   // thighs, upper arms, feet, forearms
    Vec3 right{0, 0, 0}; int n = 0;
    for (const auto& p : pairs)
        if (best[p[0]] >= 0 && best[p[1]] >= 0) { right = right + (pos[p[1]] - pos[p[0]]); ++n; }
    if (!n) return false;
    float len = std::sqrt(right.x * right.x + right.z * right.z);
    if (len < 1e-6f) return false;
    // right = forward x up  =>  forward = up x right = (r.z, 0, -r.x).
    fwd = {right.z / len, 0.0f, -right.x / len};
    // Exported characters almost always face an axis: snap when close so a
    // slightly asymmetric bind pose doesn't leave the model a few degrees off.
    float a = std::atan2(fwd.x, fwd.z), q = 1.5707963f;
    float snapped = std::round(a / q) * q;
    if (std::fabs(a - snapped) < 0.35f) fwd = {std::sin(snapped), 0.0f, std::cos(snapped)};
    return true;
}

// Turn the model about the player's vertical axis by `fix` (player-local).
// When the model IS the player (a controller added straight onto a model),
// its children turn instead, so the controller keeps owning the root.
static void TurnModel(GameObject* player, GameObject* modelRoot, const Quat& fix) {
    if (modelRoot != player) {
        modelRoot->transform->localPosition = fix * modelRoot->transform->localPosition;
        modelRoot->transform->localRotation = (fix * modelRoot->transform->localRotation).Normalized();
        return;
    }
    for (Transform* c : player->transform->Children()) {
        if (!c || !c->gameObject || c->gameObject->GetComponent<Camera>()) continue;
        c->localPosition = fix * c->localPosition;
        c->localRotation = (fix * c->localRotation).Normalized();
    }
}

bool ModelBounds(Scene& scene, GameObject* root, Vec3& lo, Vec3& hi) {
    return root && SubtreeBounds(scene, root, Mat4::Identity(), lo, hi);
}

GameObject* FindCharacterModelRoot(Scene& scene, GameObject* player) {
    if (!player || !player->transform) return nullptr;
    // How much `root` looks like the character model, or -1 when it isn't one.
    // A held hat is often the first mesh child; a skinned rig or a real
    // hierarchy outranks that prop so a model swap removes the old model.
    auto scoreModel = [&](GameObject* root) {
        int score = 0, nodes = 0;
        bool mesh = false;
        for (const auto& up : scene.Objects()) {
            GameObject* g = up.get();
            if (!g || !g->IsSelfOrDescendantOf(root)) continue;
            if (g->GetComponent<Camera>()) return -1;
            ++nodes;
            if (g->GetComponent<SkinnedMesh>()) score += 100;
            if (g->GetComponent<ModelAnimator>()) score += 40;
            if (g != player) if (auto* mr = g->GetComponent<MeshRenderer>(); mr && !mr->mesh.vertices.empty()) mesh = true;
        }
        if (!mesh && score == 0) return -1;
        if (mesh) score += 1;
        if (nodes > 1) score += std::min(nodes, 30);   // a rig beats a single prop
        return score;
    };
    GameObject* best = nullptr;
    int bestScore = -1;
    for (Transform* c : player->transform->Children()) {
        GameObject* g = c ? c->gameObject : nullptr;
        if (!g || g->name == "Rig" || !g->active) continue;
        const int sc = scoreModel(g);
        if (sc > bestScore) { bestScore = sc; best = g; }
    }
    if (best) return best;
    // A controller put straight onto an imported model: the player IS the model.
    if (!player->GetComponent<Character>())
        for (const auto& up : scene.Objects())
            if (up.get() != player && up->IsSelfOrDescendantOf(player) &&
                (up->GetComponent<SkinnedMesh>() || up->GetComponent<MeshRenderer>()))
                return player;
    return nullptr;
}

int FaceCharacterModel(Scene& scene, GameObject* player, GameObject* modelRoot, float turnDegrees) {
    if (!player || !player->transform) return -1;
    if (!modelRoot) modelRoot = FindCharacterModelRoot(scene, player);
    if (!modelRoot || !modelRoot->transform) return -1;
    if (turnDegrees != 0.0f) {   // manual turn
        TurnModel(player, modelRoot, Quat::Euler(0.0f, turnDegrees, 0.0f));
        return (int)std::lround(turnDegrees);
    }
    Mat4 toPlayer = player->transform->LocalToWorldMatrix().Inverse();
    Vec3 f;
    if (!DetectModelForward(scene, modelRoot, toPlayer, f)) return -1;
    // Controllers move a character along its LOCAL -Z: the rotation taking
    // the model's forward onto -Z (LookRotation maps +Z onto its argument).
    Quat fix = Quat::LookRotation(Vec3{-f.x, 0.0f, -f.z}).Inverse();
    TurnModel(player, modelRoot, fix);
    float deg = std::atan2(-f.x, -f.z) * 57.29578f;   // how far it was off
    return (int)std::lround(std::fabs(deg));
}

std::string FitCharacterCollider(Scene& scene, GameObject* player, GameObject* modelRoot) {
    if (!player || !player->transform) return {};
    if (!modelRoot) modelRoot = FindCharacterModelRoot(scene, player);
    float h = 1.8f, base = 0.0f, side = 0.6f;
    if (auto* ch = player->GetComponent<Character>()) h = 1.8f * (ch->height > 0.1f ? ch->height : 1.0f);
    Vec3 lo, hi;
    Mat4 toPlayer = player->transform->LocalToWorldMatrix().Inverse();
    if (modelRoot && SubtreeBounds(scene, modelRoot, toPlayer, lo, hi) && hi.y - lo.y > 1e-3f) {
        h = hi.y - lo.y; base = lo.y;
        // The narrower horizontal extent is the body's depth (a T-pose's arms
        // stretch the other one): a person-shaped column, never a wide slab.
        float d = std::fmin(hi.x - lo.x, hi.z - lo.z);
        side = std::fmin(std::fmax(d, 0.3f * h), 0.45f * h);
    } else {
        side = h / 3.0f;
    }
    std::string what;
    if (auto* box = player->GetComponent<BoxCollider3D>()) {
        box->size = {side, h, side};
        box->offset = {0.0f, base + h * 0.5f, 0.0f};
        what = "box collider fitted";
    } else if (auto* cap = player->GetComponent<CapsuleCollider3D>()) {
        cap->radius = side * 0.5f; cap->height = h;
        cap->offset = {0.0f, base + h * 0.5f, 0.0f};
        what = "capsule collider fitted";
    } else if (!player->GetComponent<Collider3D>()) {
        auto* cap2 = player->AddComponent<CapsuleCollider3D>();
        cap2->radius = side * 0.5f; cap2->height = h;
        cap2->offset = {0.0f, base + h * 0.5f, 0.0f};
        what = "capsule collider added";
    } else {
        what = "kept its collider";
    }
    if (!player->GetComponent<Rigidbody3D>()) { player->AddComponent<Rigidbody3D>(); what += " + rigidbody added"; }
    char buf[64]; std::snprintf(buf, sizeof(buf), " (%.2f x %.2fu)", side, h);
    return what + buf;
}

// The walk / run speeds of the controller moving `player` (false: none known).
static bool ControllerSpeeds(GameObject* player, float& walk, float& run) {
    if (auto* c = player->GetComponent<ThirdPersonController>())       { walk = c->walkSpeed; run = c->runSpeed; return true; }
    if (auto* c = player->GetComponent<ThirdPersonShooterController>()) { walk = c->walkSpeed; run = c->runSpeed; return true; }
    if (auto* c = player->GetComponent<FirstPersonController>())       { walk = c->walkSpeed; run = c->runSpeed; return true; }
    if (auto* c = player->GetComponent<ClickToMoveController>())       { walk = c->walkSpeed; run = c->runSpeed; return true; }
    if (auto* c = player->GetComponent<TopDownController>())           { walk = c->walkSpeed; run = c->runSpeed; return true; }
    if (auto* c = player->GetComponent<CharacterController3D>())       { walk = c->speed;     run = c->runSpeed; return true; }
    if (auto* c = player->GetComponent<NPCController>())               { walk = c->moveSpeed; run = c->runSpeed; return true; }
    if (auto* c = player->GetComponent<Platformer25DController>())     { walk = c->walkSpeed; run = c->runSpeed; return true; }
    if (auto* c = player->GetComponent<TopDownShooterController>())    { walk = c->walkSpeed; run = c->runSpeed; return true; }
    return false;
}

GameObject* AttachCharacterModel(Scene& scene, GameObject* player,
                                 const std::string& path, std::string* outLog) {
    if (!player || !player->transform) return nullptr;
    bool ok = false;
    GameObject* root = ImportModelScene(scene, path, &ok);
    if (!ok || !root || !root->transform) return nullptr;
    return AttachCharacterModelRoot(scene, player, root, outLog);
}

GameObject* AttachCharacterModelRoot(Scene& scene, GameObject* player, GameObject* root, std::string* outLog) {
    if (!player || !player->transform || !root || !root->transform) return nullptr;
    // A swap: the previous model goes, or it stays inside the new one (frozen, or
    // still animating on its own) and keeps the old wiring busy.
    if (GameObject* old = FindCharacterModelRoot(scene, player); old && old != player)
        scene.Destroy(old);

    // World bounds of the imported subtree (the import root sits at the origin).
    Vec3 lo{0, 0, 0}, hi{0, 0, 0};
    bool any = SubtreeBounds(scene, root, Mat4::Identity(), lo, hi);

    // Size the model to the character's height and stand its feet on the player's
    // origin (the controllers/capsules all treat the origin as ground level).
    float want = 1.8f;
    if (auto* ch = player->GetComponent<Character>())
        want = 1.8f * (ch->height > 0.1f ? ch->height : 1.0f);
    float h = any ? (hi.y - lo.y) : 0.0f;
    float s = h > 1e-4f ? want / h : 1.0f;
    root->transform->localScale = root->transform->localScale * s;
    root->transform->SetParent(player->transform, /*worldPositionStays=*/false);
    root->transform->localPosition = {0.0f, any ? -lo.y * s : 0.0f, 0.0f};
    // The engine's character bodies face LOCAL -Z. Read which way THIS model
    // faces from its skeleton (exporters disagree: Mixamo/glTF +Z, some
    // Blender / other exporters use -Z or +X); a rig without recognisable
    // left/right bones falls back to the common +Z.
    int faced = FaceCharacterModel(scene, player, root, 0.0f);
    if (faced < 0) root->transform->localRotation = Quat::Euler({0.0f, 180.0f, 0.0f}) * root->transform->localRotation;

    // Locomotion: auto-switch idle/walk/run from how fast the player moves, with
    // the clips mapped by name (idle/stand, walk, run/sprint/jog).
    ModelAnimator* ma = root->GetComponent<ModelAnimator>();
    if (!ma)
        for (const auto& up : scene.Objects())
            if (up->IsSelfOrDescendantOf(root))
                if ((ma = up->GetComponent<ModelAnimator>())) break;
    std::string mapped;
    if (ma && !ma->clips.empty()) AutoMapLocomotion(ma);
    // A humanoid skeleton (enough recognisable bones, on a player with a Character)
    // gets the BUILT-IN animations on every locomotion slot its clips don't fill:
    // the Character stays ON in drive-external mode and a HumanoidRetarget plays
    // the engine's walk, run, jump, crouch... on the imported bones, while the
    // clips the file does carry (a Mixamo walk, an idle) play in their own slots.
    // A file with no usable clips at all is the plain built-in case. Without a
    // humanoid skeleton the clips drive locomotion alone. Decided HERE so the clip
    // path below (including its animated re-grounding) runs when it should.
    bool wantRetarget = false;
    if (player->GetComponent<Character>()) {
        HumanoidRetarget probe;
        probe.gameObject = player;
        wantRetarget = probe.DetectAndWire(scene, root) >= 8;
    }
    const bool hasIdleClip = ma && ma->SlotFilled(ModelAnimator::SlotIdle);
    if (ma && !ma->clips.empty() && (!wantRetarget || hasIdleClip)) {
      if (!wantRetarget) {
        if (ma->idleClip.empty()) ma->idleClip = ma->clips.front().name;  // something is better than T-pose
        if (ma->walkClip.empty()) ma->walkClip = !ma->runClip.empty() ? ma->runClip : ma->idleClip;
        ma->driveByMovement = true;
        ma->smoothLocomotion = true;
        // The blend is fully "walk" at the walk threshold and fully "run" at the
        // run threshold: put them at the speeds this player actually moves.
        float walk = 0.0f, run = 0.0f;
        if (ControllerSpeeds(player, walk, run) && walk > 0.0f && run > walk) {
            ma->walkThreshold = walk; ma->runThreshold = run;
        }
        // The controller moves the capsule — the clips must animate in place, or
        // their baked forward travel slides the model out of the collider.
        ma->inPlace = true;
        mapped = "idle='" + ma->idleClip + "' walk='" + ma->walkClip + "'" +
                 (ma->runClip.empty() ? "" : " run='" + ma->runClip + "'");
        for (const auto& [label, clip] : {std::pair<const char*, const std::string*>{"crouch", &ma->crouchClip},
                                          {"crouch walk", &ma->crouchWalkClip}, {"prone", &ma->proneClip},
                                          {"crawl", &ma->crawlClip}, {"get up", &ma->getUpClip}})
            if (!clip->empty()) mapped += std::string(" ") + label + "='" + *clip + "'";
      }

        // Re-ground and re-center on the ANIMATED pose. Some rigs' clips sit at
        // a constant offset from the bind pose (glTF CesiumMan-style skeleton
        // roots), so grounding on the bind pose parked the playing model a metre
        // outside its capsule with its feet in the air — the "glitchy character"
        // report. Pose the mapped idle clip's first frame, measure, fold the
        // offset into the root, then restore the bind pose.
        int ci = ma->FindClip(ma->idleClip); if (ci < 0) ci = 0;
        struct SavedTRS { Transform* tr; Vec3 p, s; Quat r; };
        std::vector<SavedTRS> savedPose;
        for (const auto& nc : ma->clips[ci].nodes)
            for (const auto& up : scene.Objects()) {
                GameObject* g = up.get();
                if (!g || g->name != nc.node || !g->IsSelfOrDescendantOf(root) || !g->transform) continue;
                Transform* tr = g->transform;
                savedPose.push_back({tr, tr->localPosition, tr->localScale, tr->localRotation});
                Vec3 p = tr->localPosition, sc = tr->localScale; Quat rq = tr->localRotation;
                ModelAnimator::EvalClipInto(nc.clip, 0.0f, p, rq, sc);
                tr->localPosition = p; tr->localScale = sc; tr->localRotation = rq;
                break;
            }
        if (!savedPose.empty()) {
            // Deform skinned meshes onto the posed joints, so the measurement sees
            // the pose (the renderer mesh is otherwise still the bind mesh).
            for (const auto& up : scene.Objects())
                if (up->IsSelfOrDescendantOf(root))
                    if (auto* sm = up->GetComponent<SkinnedMesh>()) { sm->ResolveJoints(); sm->Skin(); }
            Vec3 alo{0, 0, 0}, ahi{0, 0, 0};
            bool aany = SubtreeBounds(scene, root, Mat4::Identity(), alo, ahi);
            if (aany) {
                Vec3 pw = player->transform->Position();
                Vec3 dWorld{pw.x - (alo.x + ahi.x) * 0.5f, pw.y - alo.y, pw.z - (alo.z + ahi.z) * 0.5f};
                // Fold the world-space correction into the root's local position
                // (the root's parent is the player — rotate the delta into its space).
                Mat4 pInv = player->transform->LocalToWorldMatrix().Inverse();
                Vec3 dLocal = pInv.MultiplyVector(dWorld);
                root->transform->localPosition = root->transform->localPosition + dLocal;
            }
            for (const SavedTRS& sp : savedPose) {
                sp.tr->localPosition = sp.p;
                sp.tr->localScale    = sp.s;
                sp.tr->localRotation = sp.r;
            }
            for (const auto& up : scene.Objects())   // deform back to the bind pose
                if (up->IsSelfOrDescendantOf(root))
                    if (auto* sm = up->GetComponent<SkinnedMesh>()) sm->Skin();
        }
    }

    // Retarget path: the built-in animation system drives the imported
    // skeleton. Added BEFORE FootIK so its LateUpdate writes the pose first
    // and the feet plant on top of it.
    bool retargeted = false;
    if (wantRetarget) {
        const int found = EnableBuiltinFallback(scene, player, root);
        retargeted = found >= 0;
        if (retargeted) {
            SettleCharacterLocomotion(ma);
            mapped = SlotSummary(ma) + (mapped.empty() ? "" : ", ") + "built-in animations retargeted (" +
                     std::to_string(found) + "/15 bones)";
        }
    }

    // Foot IK, wired automatically: detect the model's leg bones by name (Mixamo
    // etc.) and plant the feet on slopes/steps with the full setup — the same
    // out-of-the-box treatment the Humanoid template gets. One already on the
    // player is re-pointed at THIS model: the Humanoid template's drives the
    // blocky part rig (and rebuilds it, visible, when the scene loads), and after
    // a model swap it would still be bending the old model's legs.
    {
        FootIK probe;
        FootIK* fik = player->GetComponent<FootIK>();
        if (probe.DetectHumanoidBones(scene, root) >= 6) {
            const bool fresh = !fik;
            if (fresh) {
                fik = player->AddComponent<FootIK>();
                fik->useRaycast = true; fik->weight = 1.0f;
                fik->adjustPelvis = true; fik->plantDown = true; fik->alignToGround = true;
                fik->maxPelvisShift = 0.35f;
            }
            fik->DetectHumanoidBones(scene, root);
            if (!fresh) fik->Start();   // resolve the new names now: it may be running already
            fik->orient = true;   // turn the thigh and shin with the joints, or the skinned leg tears at the knee
            fik->hipLimit = IKJointLimit::Hip(); fik->kneeLimit = IKJointLimit::Knee();
            // Each sole's normal in its own foot bone's axes, off the bind pose: a
            // Blender / glTF foot points its Y down the foot, so tilting a fixed
            // local +Y onto the slope stood those feet on their toes; and a
            // mirrored rig's right foot isn't the left one's axis.
            for (const auto& up : scene.Objects()) {
                if (!up->IsSelfOrDescendantOf(root)) continue;
                if (up->name == fik->leftFootName)
                    fik->footUpAxis = (up->transform->Rotation().Inverse() * Vec3::Up).Normalized();
                else if (up->name == fik->rightFootName)
                    fik->footUpAxisR = (up->transform->Rotation().Inverse() * Vec3::Up).Normalized();
            }
            mapped += mapped.empty() ? "foot IK wired" : ", foot IK wired";
        } else if (fik) {
            player->RemoveComponent<FootIK>();   // no legs to plant on this model
        }
    }

    // Hide the default blocky body — the imported model IS the character now.
    // Clip path: the Character stops animating entirely. Retarget path: it stays
    // ON (drive-external computes the pose for the HumanoidRetarget) but renders
    // nothing. Both hide the baked mesh and any part rig.
    if (auto* ch = player->GetComponent<Character>()) {
        ch->enabled = retargeted;
        if (auto* mr = player->GetComponent<MeshRenderer>()) mr->enabled = false;
        for (const auto& up : scene.Objects())
            if (up->name == "Rig" && up->transform && up->transform->Parent() == player->transform)
                up->active = false;
    }

    // Physics: a player added from Add Component (or an old scene) may have no
    // body at all, and the template's box was sized for the blocky character.
    // Fit (or add) the collider + rigidbody around the model actually shown.
    std::string phys = FitCharacterCollider(scene, player, root);

    // Look-At IK is on for every imported player: the head (and spine, when
    // one was found) is wired, and it waits until a script gives it something
    // to look at, so the head does not stare at the world origin.
    {
        const humanoid::BoneMap bm = humanoid::MapHumanoidBones(root, nullptr);
        GameObject* head = bm.bone[humanoid::Head];
        GameObject* torso = bm.bone[humanoid::Torso];
        if (head) {
            LookAtIK* look = player->GetComponent<LookAtIK>();
            if (!look) look = player->AddComponent<LookAtIK>();
            look->weight = 1.0f;
            look->ikActive = true;
            look->aimAtPoint = false;
            look->chainNames.clear();
            if (torso) look->chainNames.push_back(torso->name);
            look->chainNames.push_back(head->name);
            look->chain.clear();
            mapped += mapped.empty() ? "look-at IK on" : ", look-at IK on";
        }
    }

    // The controllers' default 4.5 m/s "walk" is a jog: a downloaded walk clip is
    // made for about 1.4 m/s, so at 4.5 the stride match picks the RUN clip for
    // plain W and Shift plays it at its fastest. Bring a default speed down to
    // the pace the file's clip can show (its playback rate stays inside the
    // 0.6x-1.6x stride limits); speeds the user has already changed are kept.
    // The fitted values are remembered per player: a later swap refits a speed
    // still at them, and one the user set back to the default since is left alone.
    if (ma && ma->strideMatch && !ma->rootMotion) {
        static std::unordered_map<std::uint64_t, std::array<float, 2>> sFitted;  // {walk, run}; 0 = not fitted
        const std::array<float, 2> prev = sFitted[player->InstanceId()];
        std::array<float, 2> now = prev;
        auto fit = [&](float& walk, float& run, float dWalk, float dRun) {
            bool changed = false;
            // Untouched = the controller's own default, or (never fitted before) one of the OLD defaults a scene
            // saved before the natural walk and run still spells out (4.5 / 8.0 / 7.5).
            const bool walkUntouched = walk == (prev[0] > 0.0f ? prev[0] : dWalk) ||
                                       (prev[0] <= 0.0f && NaturalGait::IsOldDefaultWalk(walk));
            const bool runUntouched = run == (prev[1] > 0.0f ? prev[1] : dRun) ||
                                      (prev[1] <= 0.0f && NaturalGait::IsOldDefaultRun(run));
            if (ma->SlotFilled(ModelAnimator::SlotWalk) && walkUntouched) {
                walk = now[0] = 1.45f * ma->SlotClipSpeed(ModelAnimator::SlotWalk); changed = true;
            }
            if (ma->SlotFilled(ModelAnimator::SlotRun) && runUntouched) {
                run = now[1] = 1.25f * ma->SlotClipSpeed(ModelAnimator::SlotRun); changed = true;
            }
            return changed;
        };
        bool fitted = false;
        // Each controller type's own defaults are the "untouched" speeds.
        auto fitC = [&](auto* c) {
            using T = std::remove_pointer_t<decltype(c)>;
            if (c) { const T d; fitted = fit(c->walkSpeed, c->runSpeed, d.walkSpeed, d.runSpeed) || fitted; }
        };
        fitC(player->GetComponent<ThirdPersonController>());
        fitC(player->GetComponent<ThirdPersonShooterController>());
        fitC(player->GetComponent<FirstPersonController>());
        fitC(player->GetComponent<ClickToMoveController>());
        fitC(player->GetComponent<TopDownController>());
        fitC(player->GetComponent<Platformer25DController>());
        fitC(player->GetComponent<TopDownShooterController>());
        sFitted[player->InstanceId()] = now;
        if (fitted) {
            mapped += mapped.empty() ? "move speeds fitted to the clips" : ", move speeds fitted to the clips";
            // The clip-only path keys its walk/run blend to the controller speeds.
            float w = 0.0f, r = 0.0f;
            if (!wantRetarget && ControllerSpeeds(player, w, r) && w > 0.0f && r > w) {
                ma->walkThreshold = w; ma->runThreshold = r;
            }
        }
    }

    if (outLog) {
        *outLog = "Character model '" + root->name + "' on " + player->name +
                  " (height " + std::to_string(want).substr(0, 4) + "u" +
                  (faced > 0 ? ", turned " + std::to_string(faced) + " deg to face forward" : "") +
                  (mapped.empty() ? ", no animation clips" : ", " + mapped) +
                  (phys.empty() ? "" : ", " + phys) + ")";
    }
    return root;
}

bool IsPlayableAvatar(GameObject* go) {
    if (!go) return false;
    return go->GetComponent<PlayerController>()
        || go->GetComponent<FirstPersonController>()
        || go->GetComponent<ThirdPersonController>()
        || go->GetComponent<ThirdPersonShooterController>()
        || go->GetComponent<TopDownController>()
        || go->GetComponent<TopDownShooterController>()
        || go->GetComponent<Platformer25DController>()
        || go->GetComponent<ClickToMoveController>()
        || go->GetComponent<CharacterController3D>();
}

GameObject* MakePlayableCharacter(Scene& scene, const std::string& path, std::string* outLog,
                                  GameObject* onto) {
    if (onto && IsPlayableAvatar(onto)) {
        GameObject* model = AttachCharacterModel(scene, onto, path, outLog);
        return model ? onto : nullptr;
    }
    GameObject* player = Templates::AddThirdPersonPlayer(scene, {0.0f, 1.0f, 0.0f});
    if (!player) return nullptr;
    GameObject* model = AttachCharacterModel(scene, player, path, outLog);
    if (!model) { scene.Destroy(player); return nullptr; }
    // One played character. Other active avatars are switched off, not deleted,
    // so the template player they overlapped stays in the scene for undo.
    std::string parked;
    for (const auto& up : scene.Objects()) {
        GameObject* g = up.get();
        if (!g || g == player || !g->active || g->IsSelfOrDescendantOf(player)) continue;
        if (!IsPlayableAvatar(g)) continue;
        g->active = false;
        if (!parked.empty()) parked += ", ";
        parked += "'" + g->name + "'";
    }
    if (outLog && !parked.empty())
        *outLog += " Deactivated " + parked + " so this character is the one that's played.";
    return player;
}

const char* CharacterAnimationSourceName(int source) {
    static const char* const n[] = {"None", "Imported clips", "Built-in retargeted", "Mixed"};
    return source >= 0 && source < 4 ? n[source] : n[0];
}

bool DescribeCharacterModel(Scene& scene, GameObject* player, CharacterModelInfo& out) {
    out = CharacterModelInfo{};
    if (!player || !player->transform) return false;
    GameObject* root = FindCharacterModelRoot(scene, player);
    if (!root || root == player) return false;
    out.valid = true;
    out.model = root->name;
    ModelAnimator* ma = root->GetComponent<ModelAnimator>();
    if (!ma)
        for (const auto& up : scene.Objects())
            if (up->IsSelfOrDescendantOf(root) && (ma = up->GetComponent<ModelAnimator>())) break;
    out.animator = ma;
    auto* ch = player->GetComponent<Character>();
    auto* hr = player->GetComponent<HumanoidRetarget>();
    out.characterOff = ch && !ch->enabled;
    out.builtinAvailable = ch && ch->enabled && ch->driveExternal && hr && hr->weight > 0.0f;
    out.mappedBones = hr && hr->Wired() && hr->MappedBones() > 0 ? hr->MappedBones() : humanoid::MapHumanoidBones(root, hr ? hr->boneMap : nullptr).found;
    for (int s = 0; s < ModelAnimator::SlotCount; ++s) {
        CharacterModelInfo::Slot sl;
        sl.slot = s;
        sl.label = ModelAnimator::SlotName(s);
        if (ma) sl.clip = ma->SlotClip(s);
        if (ma && ma->driveByMovement && ma->SlotFilled(s)) { sl.source = 1; ++out.importedSlots; }
        else if (out.builtinAvailable && (!ma || !ma->driveByMovement || ma->builtinFallback)) { sl.source = 2; ++out.builtinSlots; }
        out.slots.push_back(std::move(sl));
    }
    out.source = out.importedSlots && out.builtinSlots ? 3 : (out.importedSlots ? 1 : (out.builtinSlots ? 2 : 0));
    return true;
}

std::string FixCharacterAnimations(Scene& scene, GameObject* player) {
    CharacterModelInfo before;
    if (!DescribeCharacterModel(scene, player, before)) return "No imported character model on '" + (player ? player->name : std::string()) + "'.";
    GameObject* root = FindCharacterModelRoot(scene, player);
    ModelAnimator* ma = before.animator;
    int cleared = 0;
    if (ma) {
        for (int s = 0; s < ModelAnimator::SlotCount; ++s)   // a slot naming a clip the model doesn't have
            if (!ma->SlotClip(s).empty() && ma->FindClip(ma->SlotClip(s)) < 0) { ma->SlotClip(s).clear(); ++cleared; }
        AutoMapLocomotion(ma);
    }
    int found = 0;
    if (player->GetComponent<Character>()) {
        HumanoidRetarget probe;
        probe.gameObject = player;
        if (auto* hr = player->GetComponent<HumanoidRetarget>())   // bones the user mapped by hand count
            for (int b = 0; b < humanoid::BoneCount; ++b) probe.boneMap[b] = hr->boneMap[b];
        found = probe.DetectAndWire(scene, root);
    }
    if (found >= 8) {
        EnableBuiltinFallback(scene, player, root);
        if (ma) { ma->builtinFallback = true; SettleCharacterLocomotion(ma); }
    } else if (ma && !ma->clips.empty()) {   // no humanoid skeleton: the clips alone
        if (ma->idleClip.empty()) ma->idleClip = ma->clips.front().name;
        if (ma->walkClip.empty()) ma->walkClip = !ma->runClip.empty() ? ma->runClip : ma->idleClip;
        ma->driveByMovement = true; ma->inPlace = true;
    }
    CharacterModelInfo after;
    DescribeCharacterModel(scene, player, after);
    std::string log = "Animations on '" + after.model + "': " + CharacterAnimationSourceName(after.source) + " (" +
                      std::to_string(after.importedSlots) + " slot" + (after.importedSlots == 1 ? "" : "s") + " from clips, " +
                      std::to_string(after.builtinSlots) + " built-in";
    if (cleared) log += ", " + std::to_string(cleared) + " stale slot" + (cleared == 1 ? "" : "s") + " cleared";
    return log + ")";
}

} // namespace okay
