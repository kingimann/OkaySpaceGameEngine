#include "test_framework.hpp"
#include <Okay.hpp>
#include <cmath>
#include <cstdio>
#include <string>

using namespace okay;

// The Animation window's clip editing tools, which live on AnimClip: per-key
// easing (and its .okayanim and scene round-trips, plus older files without it),
// moving and retiming selected keys, inserting / removing time, mirroring, and
// offsetting / amplifying pose values.
int main() {
    RUN_SUITE("anim_edit");

    const int NB = Character::BoneCount();
    const int RU = Character::BoneIndex("r_uparm");
    const int LU = Character::BoneIndex("l_uparm");
    // A key that turns r_uparm to `z` degrees about Z at time `t`.
    auto key = [&](float t, float z, AnimKeyEase e = AnimKeyEase::Clip) {
        AnimKey k; k.time = t; k.ease = e;
        k.pose.assign(NB, Vec3{0, 0, 0});
        k.pose[RU] = Vec3{0, 0, z};
        return k;
    };
    auto times = [](const AnimClip& c) {
        std::string s;
        for (const AnimKey& k : c.keys) { char b[16]; std::snprintf(b, sizeof(b), "%.2f ", k.time); s += b; }
        return s;
    };

    // ---- A key's own easing shapes only the span out of that key ----
    {
        AnimClip c; c.loop = false;   // clip mode: linear
        c.keys = {key(0, 0, AnimKeyEase::Step), key(1, 10), key(2, 20, AnimKeyEase::EaseIn),
                  key(3, 30, AnimKeyEase::EaseOut), key(4, 40, AnimKeyEase::Smooth), key(5, 50)};
        CHECK_NEAR(c.Sample(0.5f)[RU].z, 0.0f, 1e-3f);       // step holds key 0 until key 1
        CHECK_NEAR(c.Sample(1.25f)[RU].z, 12.5f, 0.05f);     // the clip's (linear) mode
        CHECK_NEAR(c.Sample(2.5f)[RU].z, 22.5f, 0.05f);      // ease in: u^2 = 0.25
        CHECK_NEAR(c.Sample(3.5f)[RU].z, 37.5f, 0.05f);      // ease out: 1 - (1-u)^2 = 0.75
        CHECK_NEAR(c.Sample(4.25f)[RU].z, 41.5625f, 0.05f);  // smoothstep(0.25)
        CHECK_NEAR(c.Sample(0.999f)[RU].z, 0.0f, 1e-3f);
        CHECK_NEAR(c.Sample(1.0f)[RU].z, 10.0f, 1e-3f);      // keys themselves come back exactly

        // A Linear key overrides a Smooth clip, and a Step key holds inside a
        // Cubic clip (its span is taken out of the curve; the others stay cubic).
        AnimClip s; s.loop = false; s.interp = AnimInterp::Smooth;
        s.keys = {key(0, 0, AnimKeyEase::Linear), key(1, 10)};
        CHECK_NEAR(s.Sample(0.25f)[RU].z, 2.5f, 0.05f);
        AnimClip q; q.loop = false; q.interp = AnimInterp::Cubic;
        q.keys = {key(0, 0, AnimKeyEase::Step), key(1, 10), key(2, 20), key(3, 30)};
        CHECK_NEAR(q.Sample(0.5f)[RU].z, 0.0f, 1e-3f);
        CHECK_NEAR(q.Sample(1.5f)[RU].z, 15.0f, 0.05f);      // a straight ramp stays straight
        CHECK_NEAR(q.Sample(2.5f)[RU].z, 25.0f, 0.05f);

        // The root track follows the same per-key easing, and so does the wrap of
        // a seamless loop (the last key's easing).
        AnimClip r; r.loop = true; r.length = 2.0f; r.hasRoot = true;
        r.keys = {key(0, 0), key(1, 10, AnimKeyEase::Step)};
        r.keys[1].root = Vec3{0, 1, 0};
        CHECK_NEAR(r.SampleRoot(0.5f).y, 0.5f, 1e-3f);
        CHECK_NEAR(r.Sample(1.5f)[RU].z, 10.0f, 1e-3f);      // held, not eased back to key 0
        CHECK_NEAR(r.SampleRoot(1.5f).y, 1.0f, 1e-3f);
    }

    // ---- .okayanim: older files load unchanged; easing words round-trip ----
    {
        std::string err;
        auto old = AnimClip::ParseAll("clip wave loop smooth\nkey 0\n  r_uparm 0 0 0\nkey 1\n  r_uparm 0 0 -90\n",
                                      &Character::BoneIndex, NB, &err);
        CHECK(err.empty());
        CHECK(old.size() == 1 && old[0].keys.size() == 2);
        for (const AnimKey& k : old[0].keys) CHECK(k.ease == AnimKeyEase::Clip);
        CHECK_NEAR(old[0].Sample(0.5f)[RU].z, -45.0f, 0.05f);   // smooth, as before

        const char* src = "clip hold once\nkey 0 step\n  r_uparm 0 0 10\nkey 0.5 easein\nkey 1 easeout\n"
                          "key 1.5 smooth\nkey 2 linear\nkey 2.5\n";
        auto clips = AnimClip::ParseAll(src, &Character::BoneIndex, NB, &err);
        CHECK(err.empty());
        CHECK(clips.size() == 1 && clips[0].keys.size() == 6);
        const AnimKeyEase want[] = {AnimKeyEase::Step, AnimKeyEase::EaseIn, AnimKeyEase::EaseOut,
                                    AnimKeyEase::Smooth, AnimKeyEase::Linear, AnimKeyEase::Clip};
        for (int i = 0; i < 6; ++i) CHECK(clips[0].keys[i].ease == want[i]);

        std::string text = AnimClip::WriteAll(clips, &Character::BoneToken);
        CHECK(text.find("key 0 step\n") != std::string::npos);
        CHECK(text.find("key 0.5 easein\n") != std::string::npos);
        CHECK(text.find("key 1 easeout\n") != std::string::npos);
        CHECK(text.find("key 1.5 smooth\n") != std::string::npos);
        CHECK(text.find("key 2 linear\n") != std::string::npos);
        CHECK(text.find("key 2.5\n") != std::string::npos);   // no word for the clip's mode
        auto again = AnimClip::ParseAll(text, &Character::BoneIndex, NB, &err);
        CHECK(err.empty());
        CHECK(again.size() == 1 && again[0].keys.size() == 6);
        for (int i = 0; i < 6; ++i) CHECK(again[0].keys[i].ease == want[i]);
        CHECK_NEAR(again[0].keys[0].pose[RU].z, 10.0f, 1e-4f);

        // A misspelt easing word is reported, like a misspelt bone.
        AnimClip::ParseAll("clip x\nkey 0 smoth\n", &Character::BoneIndex, NB, &err);
        CHECK(!err.empty());
    }

    // ---- Scene save/load keeps per-key easing (and a clip's cubic mode) ----
    {
        Character a;
        AnimClip c; c.name = "hold"; c.interp = AnimInterp::Cubic;
        c.keys = {key(0, 0, AnimKeyEase::Step), key(1, 10), key(2, 20, AnimKeyEase::EaseOut)};
        a.AddClip(c);
        Character b;
        b.FromText(a.ToText());
        const AnimClip* r = b.FindClip("hold");
        CHECK(r != nullptr);
        if (r) {
            CHECK(r->interp == AnimInterp::Cubic);
            CHECK(r->keys.size() == 3);
            CHECK(r->keys[0].ease == AnimKeyEase::Step);
            CHECK(r->keys[1].ease == AnimKeyEase::Clip);
            CHECK(r->keys[2].ease == AnimKeyEase::EaseOut);
        }
        // An older save ends before the easing section: drop that trailing count.
        Character plain;
        AnimClip p; p.name = "plain"; p.keys = {key(0, 0), key(1, 10)};
        plain.AddClip(p);
        std::string legacy = plain.ToText();
        legacy.erase(legacy.find_last_of(' '));
        Character d;
        d.FromText(legacy);
        const AnimClip* pr = d.FindClip("plain");
        CHECK(pr != nullptr && pr->keys.size() == 2);
        if (pr) for (const AnimKey& k : pr->keys) CHECK(k.ease == AnimKeyEase::Clip);
    }

    // ---- Moving keys: spacing kept, stops at 0, lands on and replaces a key ----
    {
        AnimClip c;
        c.keys = {key(0, 0), key(0.5f, 10), key(1, 20), key(1.5f, 30)};
        std::vector<int> idx = c.MoveKeys({1, 2}, 0.25f);
        CHECK(times(c) == "0.00 0.75 1.25 1.50 ");
        CHECK(idx.size() == 2 && idx[0] == 1 && idx[1] == 2);
        CHECK_NEAR(c.keys[1].pose[RU].z, 10.0f, 1e-4f);
        // Too far left: the earliest moved key stops at 0 and replaces the key
        // there; the other keeps its distance from it.
        idx = c.MoveKeys({1, 2}, -5.0f);
        CHECK(times(c) == "0.00 0.50 1.50 ");
        CHECK(idx.size() == 2 && idx[0] == 0 && idx[1] == 1);
        CHECK_NEAR(c.keys[0].pose[RU].z, 10.0f, 1e-4f);
        CHECK_NEAR(c.keys[1].pose[RU].z, 20.0f, 1e-4f);
        CHECK(c.MoveKeys({}, 1.0f).empty());
        CHECK(times(c) == "0.00 0.50 1.50 ");
        // A loop length the keys move past is dropped (loop at the last key).
        c.loop = true; c.length = 1.6f;
        c.MoveKeys({2}, 0.05f);
        CHECK_NEAR(c.length, 1.6f, 1e-4f);
        c.MoveKeys({2}, 0.1f);
        CHECK_NEAR(c.length, 0.0f, 1e-4f);
    }

    // ---- Retiming about a pivot ----
    {
        AnimClip c;
        c.keys = {key(0, 0), key(0.5f, 10), key(1, 20), key(1.5f, 30)};
        std::vector<int> idx = c.ScaleKeyTimes({1, 2, 3}, 0.0f, 2.0f);    // twice as slow from the start
        CHECK(times(c) == "0.00 1.00 2.00 3.00 ");
        CHECK(idx.size() == 3 && idx[0] == 1 && idx[2] == 3);
        idx = c.ScaleKeyTimes({0, 1, 2, 3}, 3.0f, 0.5f);                  // twice as fast, ending where it did
        CHECK(times(c) == "1.50 2.00 2.50 3.00 ");
        CHECK(idx.size() == 4);
        c.ScaleKeyTimes({0, 1, 2, 3}, 0.0f, 0.0f);                        // no factor: nothing moves
        CHECK(times(c) == "1.50 2.00 2.50 3.00 ");
    }

    // ---- Insert / remove time ripples keys, events and the loop length ----
    {
        AnimClip c; c.loop = true; c.length = 1.5f;
        c.keys = {key(0, 0), key(0.5f, 10), key(1, 20)};
        c.AddEvent(0.5f, "a"); c.AddEvent(1.0f, "b");
        c.InsertTime(0.5f, 0.25f);
        CHECK(times(c) == "0.00 0.75 1.25 ");
        CHECK(c.events.size() == 2);
        CHECK_NEAR(c.events[0].time, 0.75f, 1e-4f);
        CHECK_NEAR(c.events[1].time, 1.25f, 1e-4f);
        CHECK_NEAR(c.length, 1.75f, 1e-4f);
        c.RemoveTime(0.5f, 0.5f);          // the key and event at 0.75 go; 1.25 slides to 0.75
        CHECK(times(c) == "0.00 0.75 ");
        CHECK(c.events.size() == 1 && c.events[0].name == "b");
        CHECK_NEAR(c.events[0].time, 0.75f, 1e-4f);
        CHECK_NEAR(c.length, 1.25f, 1e-4f);
        CHECK_NEAR(c.keys[1].pose[RU].z, 20.0f, 1e-4f);
        c.RemoveTime(0.6f, 10.0f);         // cuts the tail: the loop length ends where the cut began
        CHECK(times(c) == "0.00 ");
        CHECK(c.events.empty());
        CHECK_NEAR(c.length, 0.6f, 1e-4f);
        c.RemoveTime(0.0f, 1.0f);          // nothing left past the last key: loop at it
        CHECK(c.keys.empty());
        CHECK_NEAR(c.length, 0.0f, 1e-4f);
    }

    // ---- Mirror swaps the sides, flips yaw / roll and the root's sideways offset ----
    {
        AnimClip c; c.hasRoot = true;
        c.keys = {key(0, 0), key(1, 0)};
        c.keys[1].pose[RU] = Vec3{30, 10, 5};
        c.keys[1].root = Vec3{0.1f, 0.2f, 0.0f};
        c.Mirror(&Character::MirrorPose);
        CHECK_NEAR(c.keys[1].pose[LU].x, 30.0f, 1e-4f);
        CHECK_NEAR(c.keys[1].pose[LU].y, -10.0f, 1e-4f);
        CHECK_NEAR(c.keys[1].pose[LU].z, -5.0f, 1e-4f);
        CHECK_NEAR(c.keys[1].pose[RU].x, 0.0f, 1e-4f);
        CHECK_NEAR(c.keys[1].root.x, -0.1f, 1e-4f);
        CHECK_NEAR(c.keys[1].root.y, 0.2f, 1e-4f);
        c.Mirror(&Character::MirrorPose);  // and back
        CHECK_NEAR(c.keys[1].pose[RU].y, 10.0f, 1e-4f);
        CHECK_NEAR(c.keys[1].root.x, 0.1f, 1e-4f);
    }

    // ---- Offset one bone (or all) on chosen keys; amplify about the mean pose ----
    {
        AnimClip c;
        c.keys = {key(0, 0), key(1, 20), key(2, 40)};
        c.OffsetPose({0, 2}, RU, Vec3{5, 0, 0});
        CHECK_NEAR(c.keys[0].pose[RU].x, 5.0f, 1e-4f);
        CHECK_NEAR(c.keys[1].pose[RU].x, 0.0f, 1e-4f);
        CHECK_NEAR(c.keys[2].pose[RU].x, 5.0f, 1e-4f);
        CHECK_NEAR(c.keys[2].pose[RU].z, 40.0f, 1e-4f);     // the other angles stay
        c.OffsetPose({1}, -1, Vec3{0, 0, 1});
        CHECK_NEAR(c.keys[1].pose[RU].z, 21.0f, 1e-4f);
        CHECK_NEAR(c.keys[1].pose[LU].z, 1.0f, 1e-4f);
        CHECK_NEAR(c.keys[0].pose[LU].z, 0.0f, 1e-4f);

        AnimClip s;
        s.keys = {key(0, 0), key(1, 20), key(2, 40)};      // mean z = 20
        s.ScalePose({0, 1, 2}, RU, 1.5f);
        CHECK_NEAR(s.keys[0].pose[RU].z, -10.0f, 1e-4f);   // the swing grows about its centre
        CHECK_NEAR(s.keys[1].pose[RU].z, 20.0f, 1e-4f);
        CHECK_NEAR(s.keys[2].pose[RU].z, 50.0f, 1e-4f);
        AnimClip d;
        d.keys = {key(0, 0), key(1, 20), key(2, 40)};
        d.ScalePose({0}, -1, 0.5f);                        // dampen one key, every bone
        CHECK_NEAR(d.keys[0].pose[RU].z, 10.0f, 1e-4f);
        CHECK_NEAR(d.keys[0].pose[LU].z, 0.0f, 1e-4f);     // a bone at rest everywhere stays there
        CHECK_NEAR(d.keys[1].pose[RU].z, 20.0f, 1e-4f);
    }

    // ---- Dope sheet and transport queries: the key at a time, its neighbours,
    //      keys that hold a bone still (drawn hollow), frame snapping ----
    {
        AnimClip c; c.loop = false;
        c.keys = {key(0, 0), key(0.5f, 0), key(1, 20), key(1.5f, 20)};
        CHECK(c.KeyAt(0.5f) == 1);
        CHECK(c.KeyAt(0.5004f) == 1);
        CHECK(c.KeyAt(0.7f) == -1);
        CHECK(c.PrevKey(0.7f) == 1);
        CHECK(c.PrevKey(0.5f) == 0);
        CHECK(c.PrevKey(0.0f) == -1);
        CHECK(c.NextKey(0.7f) == 2);
        CHECK(c.NextKey(1.0f) == 3);
        CHECK(c.NextKey(1.5f) == -1);
        // r_uparm goes 0, 0, 20, 20: the first and last keys hold it, the middle two move it.
        CHECK(c.KeyHoldsBone(0, RU));
        CHECK(!c.KeyHoldsBone(1, RU));
        CHECK(!c.KeyHoldsBone(2, RU));
        CHECK(c.KeyHoldsBone(3, RU));
        CHECK(c.KeyHoldsBone(2, LU));               // a bone at rest everywhere holds on every key
        // A seamless loop compares its last key with its first too.
        AnimClip l; l.loop = true; l.length = 2.0f;
        l.keys = {key(0, 0), key(1, 20)};
        CHECK(!l.KeyHoldsBone(1, RU));
        AnimClip l2 = l; l2.keys[1].pose[RU] = Vec3{0, 0, 0};
        CHECK(l2.KeyHoldsBone(0, RU));
        CHECK(l2.KeyHoldsBone(1, RU));
        // The root track: none means every key holds; a changing offset is a move.
        CHECK(c.KeyHoldsRoot(1));
        AnimClip r; r.hasRoot = true; r.keys = {key(0, 0), key(1, 0), key(2, 0)};
        r.keys[1].root = Vec3{0, -0.1f, 0};
        CHECK(!r.KeyHoldsRoot(0));
        CHECK(!r.KeyHoldsRoot(1));
        CHECK(!r.KeyHoldsRoot(2));
        AnimClip r2 = r; r2.keys[1].root = Vec3{0, 0, 0};
        CHECK(r2.KeyHoldsRoot(1));
        CHECK_NEAR(AnimClip::SnapToFrame(0.52f, 30), 16.0f / 30.0f, 1e-5f);
        CHECK_NEAR(AnimClip::SnapToFrame(0.52f, 0), 0.52f, 1e-6f);
        CHECK_NEAR(AnimClip::SnapToFrame(0.0f, 24), 0.0f, 1e-6f);
    }

    // ---- Keying one bone (record mode): a new key samples the rest of the pose ----
    {
        AnimClip c; c.loop = false;
        c.keys = {key(0, 0), key(1, 20)};
        c.keys[1].pose[LU] = Vec3{0, 0, 40};             // l_uparm 0 -> 40 over the clip
        int at = c.SetBoneKey(0.5f, RU, Vec3{0, 0, 5});
        CHECK(at == 1);
        CHECK(c.keys.size() == 3);
        CHECK_NEAR(c.keys[1].time, 0.5f, 1e-6f);
        CHECK_NEAR(c.keys[1].pose[RU].z, 5.0f, 1e-5f);
        CHECK_NEAR(c.keys[1].pose[LU].z, 20.0f, 0.05f);   // the other bone keeps its interpolated value
        CHECK_NEAR(c.Sample(1.0f)[LU].z, 40.0f, 1e-4f);
        at = c.SetBoneKey(1.0f, RU, Vec3{0, 0, 30});      // at an existing key: just that bone there
        CHECK(at == 2);
        CHECK(c.keys.size() == 3);
        CHECK_NEAR(c.keys[2].pose[RU].z, 30.0f, 1e-5f);
        CHECK_NEAR(c.keys[2].pose[LU].z, 40.0f, 1e-5f);
        // A root key turns the root track on; its new key samples the pose too.
        AnimClip r; r.loop = false; r.keys = {key(0, 0), key(1, 20)};
        CHECK(!r.hasRoot);
        at = r.SetRootKey(0.5f, Vec3{0, -0.2f, 0});
        CHECK(r.hasRoot);
        CHECK(at == 1);
        CHECK_NEAR(r.keys[1].root.y, -0.2f, 1e-6f);
        CHECK_NEAR(r.keys[1].pose[RU].z, 10.0f, 0.05f);
        CHECK_NEAR(r.SampleRoot(0.25f).y, -0.1f, 1e-4f);
        // An empty clip gets its first key.
        AnimClip e;
        at = e.SetBoneKey(0.2f, RU, Vec3{1, 2, 3});
        CHECK(at == 0);
        CHECK(e.keys.size() == 1);
        CHECK((int)e.keys[0].pose.size() > RU);
        CHECK_NEAR(e.keys[0].pose[RU].y, 2.0f, 1e-6f);
        // Clearing a bone at a key puts it on the line between its neighbours.
        AnimClip h; h.loop = false; h.keys = {key(0, 0), key(1, 50), key(2, 20)};
        h.ClearBoneAtKey(1, RU);
        CHECK_NEAR(h.keys[1].pose[RU].z, 10.0f, 0.05f);
        CHECK_NEAR(h.Sample(0.5f)[RU].z, 5.0f, 0.05f);
        CHECK_NEAR(h.Sample(1.5f)[RU].z, 15.0f, 0.05f);
        AnimClip h2; h2.keys = {key(0, 33)};
        h2.ClearBoneAtKey(0, RU);
        CHECK_NEAR(h2.keys[0].pose[RU].z, 0.0f, 1e-6f);   // no neighbours: rest
        AnimClip h3; h3.loop = false; h3.keys = {key(0, 0), key(1, 50)};
        h3.ClearBoneAtKey(1, RU);
        CHECK_NEAR(h3.keys[1].pose[RU].z, 0.0f, 1e-6f);   // one neighbour: copy it
        // SetKey replaces a key at the same time (keeping that key's easing), else inserts in order.
        AnimClip k; k.keys = {key(0, 0), key(1, 10, AnimKeyEase::Step)};
        CHECK(k.SetKey(key(0.5f, 5)) == 1);
        CHECK(k.keys.size() == 3);
        CHECK(k.SetKey(key(1.0f, 99)) == 2);
        CHECK(k.keys.size() == 3);
        CHECK_NEAR(k.keys[2].pose[RU].z, 99.0f, 1e-6f);
        CHECK(k.keys[2].ease == AnimKeyEase::Step);
    }

    // ---- Character: removing and renaming clips keeps the bindings and a playing clip straight ----
    {
        Scene s("A");
        auto* ch = s.CreateGameObject("Hero")->AddComponent<Character>();
        AnimClip a; a.name = "Wave"; a.keys = {key(0, 0), key(1, 30)};
        AnimClip b; b.name = "Walk"; b.keys = {key(0, 0), key(1, 30)};
        ch->AddClip(a); ch->AddClip(b);
        ch->clipWalk = "Walk"; ch->autoPlayClip = "Walk"; ch->clipIdle = "Wave";
        CHECK(ch->RenameClip("Walk", "Stride"));
        CHECK(!ch->HasClip("Walk"));
        CHECK(ch->HasClip("Stride"));
        CHECK(ch->FindClip("Stride") && ch->FindClip("Stride")->name == "Stride");
        CHECK(ch->clipWalk == "Stride");
        CHECK(ch->autoPlayClip == "Stride");
        CHECK(ch->clipIdle == "Wave");
        CHECK(!ch->RenameClip("Stride", "Wave"));   // the name is taken
        CHECK(!ch->RenameClip("Nope", "X"));        // no such clip
        CHECK(!ch->RenameClip("Stride", ""));
        CHECK(ch->PlayClip("Stride"));
        CHECK(ch->RenameClip("Stride", "Walk"));    // a playing clip keeps playing under its new name
        CHECK(ch->IsPlayingClip());
        CHECK(ch->PlayingClip() == "Walk");
        CHECK(ch->RemoveClip("Walk"));
        CHECK(!ch->IsPlayingClip());
        CHECK(!ch->HasClip("Walk"));
        CHECK(ch->clipWalk.empty());
        CHECK(ch->autoPlayClip.empty());
        CHECK(ch->clipIdle == "Wave");
        CHECK(!ch->RemoveClip("Walk"));
        CHECK(ch->ClipCount() == 1);
    }

    // ---- Posed joints: where the rig's joints are under a pose (the onion skin) ----
    {
        Scene s("B");
        auto* ch = s.CreateGameObject("Hero")->AddComponent<Character>();
        std::vector<Character::Bone> sk = ch->Skeleton();
        std::vector<Vec3> rest(NB, Vec3{0, 0, 0});
        std::vector<Vec3> j = ch->PosedJoints(rest);
        CHECK(j.size() == sk.size());
        for (std::size_t i = 0; i < j.size() && i < sk.size(); ++i) CHECK((j[i] - sk[i].joint).Magnitude() < 1e-4f);
        // Swing the right upper arm 90 degrees: the elbow and the hand swing with
        // it, keeping their distance from the shoulder; the spine stays put.
        const int RF = Character::BoneIndex("r_fore"), RH = Character::BoneIndex("r_hand");
        std::vector<Vec3> p = rest; p[RU] = Vec3{0, 0, 90};
        std::vector<Vec3> q = ch->PosedJoints(p);
        CHECK((q[RU] - sk[RU].joint).Magnitude() < 1e-4f);
        CHECK_NEAR((q[RH] - q[RU]).Magnitude(), (sk[RH].joint - sk[RU].joint).Magnitude(), 1e-3f);
        CHECK(std::fabs(q[RF].y - sk[RF].joint.y) > 0.1f);
        for (int i : {0, 1, 2}) CHECK((q[i] - sk[i].joint).Magnitude() < 1e-4f);
    }

    // ---- The part rig lands where PosedJoints says: the Rig root's half turn and
    //      height scale under the character's own transform ----
    {
        Scene s("C");
        GameObject* go = s.CreateGameObject("Hero");
        auto* ch = go->AddComponent<Character>();
        go->transform->localPosition = Vec3{2, 1, -3};
        go->transform->localRotation = Quat::Euler(0, 40, 0);
        ch->height = 1.5f;
        ch->Apply(); ch->separateParts = true; ch->BuildParts();
        CHECK(ch->PartsBuilt());
        std::vector<Vec3> p(NB, Vec3{0, 0, 0});
        p[RU] = Vec3{30, 10, -20};
        p[Character::BoneIndex("torso")] = Vec3{0, 0, 15};
        ch->PreviewPose(p);
        ch->EditorPreviewTick();   // drives the parts from the previewed pose
        std::vector<Vec3> j = ch->PosedJoints(p);
        Mat4 M = go->transform->LocalToWorldMatrix() * Mat4::Rotate(Quat::Euler(0.0f, 180.0f, 0.0f)) * Mat4::Scale(Vec3{1.5f, 1.5f, 1.5f});
        for (int b = 0; b < NB; ++b)
            if (GameObject* part = ch->Part(b)) {
                Vec3 w = part->transform->Position(), e = M.MultiplyPoint(j[b]);
                if ((w - e).Magnitude() >= 1e-3f) std::printf("  bone %d: part (%.3f %.3f %.3f) vs joints (%.3f %.3f %.3f)\n", b, w.x, w.y, w.z, e.x, e.y, e.z);
                CHECK((w - e).Magnitude() < 1e-3f);
            }
    }

    TEST_MAIN_RESULT();
}
