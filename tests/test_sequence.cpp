// Cutscene sequences: the .okayseq text round trip, evaluation at any time
// (transform keys with easing, camera cuts and blends, the overlay), the
// SequencePlayer's once-only events, loop, seek, skip, wait, built-in actions,
// its scene record, and an older scene without one.
#include "test_framework.hpp"
#include <Okay.hpp>
#include "okay/Components/AnimStateMachine.hpp"
#include "okay/Components/ModelAnimator.hpp"
#include "okay/Components/Sequence.hpp"
#include "okay/Components/SequencePlayer.hpp"
#include <cmath>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

using namespace okay;

static float G(ScriptComponent* sc, const char* n) { return sc->VM()->GetGlobal(n).AsFloat(); }
static std::string GS(ScriptComponent* sc, const char* n) { return sc->VM()->GetGlobal(n).AsString(); }

// The sequence every suite uses: three shots (an object, an inline pose blended
// in, a cut back), a transform track on Hero, animation and audio cues, and an
// event track with every action.
static Sequence MakeSequence() {
    Sequence s; s.name = "Intro"; s.length = 6.0f;
    s.cameraTrack = true;
    SeqShot a; a.time = 0.0f; a.camera = "Cam A";
    SeqShot b; b.time = 2.0f; b.position = {0, 5, 10}; b.rotation = {0, 180, 0}; b.fov = 40.0f; b.blend = 1.0f; b.ease = Ease::Linear;
    SeqShot c; c.time = 4.0f; c.camera = "Cam A";
    s.shots = {a, b, c};
    SeqTransformTrack tr; tr.object = "Hero";
    tr.position = {{0.0f, {0, 0, 0}, Ease::Linear}, {2.0f, {4, 0, 0}, Ease::QuadIn}, {4.0f, {4, 2, 0}, Ease::Linear}};
    tr.rotation = {{0.0f, {0, 0, 0}, Ease::Linear}, {2.0f, {0, 90, 0}, Ease::Linear}};
    tr.scale = {{1.0f, {1, 1, 1}, Ease::Linear}, {3.0f, {2, 2, 2}, Ease::Linear}};
    s.transforms.push_back(tr);
    SeqAnimTrack an; an.object = "Hero";
    an.cues = {{0.5f, SeqAnimCue::Kind::SetFloat, "speed", 2.5f}, {1.5f, SeqAnimCue::Kind::GoToState, "Attack", 0.0f},
               {2.0f, SeqAnimCue::Kind::SetBool, "armed", 1.0f}};
    s.animations.push_back(an);
    SeqAudioTrack au; au.object = "Hero"; au.cues = {{1.0f, "", 0.8f}};
    s.audio.push_back(au);
    SeqEventTrack ev; ev.name = "Main";
    ev.events = {{0.0f, SeqEvent::Kind::Named, "start", "", 0.0f},
                 {1.0f, SeqEvent::Kind::SetActive, "Door", "", 0.0f},
                 {1.5f, SeqEvent::Kind::Teleport, "Crate", "Spawn", 0.0f},
                 {2.0f, SeqEvent::Kind::FadeOut, "", "", 1.0f},
                 {2.5f, SeqEvent::Kind::Subtitle, "Guide", "Welcome \"home\".", 2.0f},
                 {3.0f, SeqEvent::Kind::TimeScale, "", "", 0.5f},
                 {4.0f, SeqEvent::Kind::FadeIn, "", "", 0.5f},
                 {5.0f, SeqEvent::Kind::Named, "done", "", 0.0f}};
    s.events.push_back(ev);
    return s;
}

static ModelAnimator::Clip ConstClip(const char* name, float x) {
    ModelAnimator::Clip c; c.name = name;
    ModelAnimator::NodeClip nc{"bone", AnimationClip{}};
    nc.clip.AddKey("position.x", 0.0f, x);
    nc.clip.AddKey("position.x", 1.0f, x);
    c.nodes.push_back(nc);
    return c;
}

// The scene the player runs in: a main camera "Cam A", Hero (with an Animator,
// an Audio Source and a script counting events), Door, Crate and Spawn.
struct World {
    Scene scene{"T"};
    GameObject *cam = nullptr, *hero = nullptr, *door = nullptr, *crate = nullptr, *spawn = nullptr, *director = nullptr;
    SequencePlayer* sp = nullptr;
    AnimStateMachine* sm = nullptr;
    ScriptComponent* sc = nullptr;
    int events = 0; std::string last;
    World() {
        cam = scene.CreateGameObject("Cam A");
        cam->transform->localPosition = {1, 2, 3};
        cam->AddComponent<Camera>()->fieldOfView = 60.0f;
        hero = scene.CreateGameObject("Hero");
        GameObject* bone = scene.CreateGameObject("bone");
        bone->transform->SetParent(hero->transform, false);
        auto* ma = hero->AddComponent<ModelAnimator>();
        ma->autoPlay = false; ma->blendTime = 0.0f;
        ma->clips = {ConstClip("idle", 0.0f), ConstClip("attack", 9.0f)};
        sm = hero->AddComponent<AnimStateMachine>();
        AnimStateMachine::State idle; idle.name = "Idle"; idle.clip = "idle";
        AnimStateMachine::State attack; attack.name = "Attack"; attack.clip = "attack";
        sm->states = {idle, attack};
        hero->AddComponent<AudioSource>()->clip.samples.assign(4800, 0.0f);
        hero->GetComponent<AudioSource>()->clip.sampleRate = 48000;
        sc = hero->AddComponent<ScriptComponent>("okayscript");
        std::string err;
        CHECK(sc->LoadSource("var count = 0; var last = \"\";\n"
                             "function on_sequence_event(name) { count = count + 1; last = name; }\n", &err));
        door = scene.CreateGameObject("Door");
        crate = scene.CreateGameObject("Crate");
        spawn = scene.CreateGameObject("Spawn");
        spawn->transform->localPosition = {7, 0, 0};
        director = scene.CreateGameObject("Director");
        sp = director->AddComponent<SequencePlayer>();
        sp->onEvent = [this](const std::string& n) { ++events; last = n; };
        sp->SetSequence(MakeSequence());
        scene.Start();
        scene.Update(1.0f / 60.0f);
    }
};

int main() {
    RUN_SUITE("sequence");

    // ---- Text round trip ----
    {
        const Sequence s = MakeSequence();
        const std::string text = s.Write();
        Sequence p; std::string err;
        CHECK(Sequence::Parse(text, p, &err));
        CHECK(err.empty());
        CHECK(p.name == "Intro" && std::fabs(p.length - 6.0f) < 1e-6f);
        CHECK(p.cameraTrack && p.shots.size() == 3);
        CHECK(p.shots[0].camera == "Cam A" && p.shots[1].camera.empty() && p.shots[2].camera == "Cam A");
        CHECK_NEAR(p.shots[1].position.z, 10.0f, 1e-5f);
        CHECK_NEAR(p.shots[1].rotation.y, 180.0f, 1e-5f);
        CHECK_NEAR(p.shots[1].fov, 40.0f, 1e-5f);
        CHECK_NEAR(p.shots[1].blend, 1.0f, 1e-5f);
        CHECK(p.shots[1].ease == Ease::Linear && p.shots[0].ease == Ease::SineInOut);
        CHECK(p.transforms.size() == 1 && p.transforms[0].object == "Hero");
        CHECK(p.transforms[0].position.size() == 3 && p.transforms[0].position[1].ease == Ease::QuadIn);
        CHECK_NEAR(p.transforms[0].position[2].value.y, 2.0f, 1e-5f);
        CHECK(p.transforms[0].rotation.size() == 2 && p.transforms[0].scale.size() == 2);
        CHECK(p.animations.size() == 1 && p.animations[0].cues.size() == 3);
        CHECK(p.animations[0].cues[0].kind == SeqAnimCue::Kind::SetFloat && std::fabs(p.animations[0].cues[0].value - 2.5f) < 1e-6f);
        CHECK(p.animations[0].cues[1].kind == SeqAnimCue::Kind::GoToState && p.animations[0].cues[1].name == "Attack");
        CHECK(p.animations[0].cues[2].kind == SeqAnimCue::Kind::SetBool && p.animations[0].cues[2].value == 1.0f);
        CHECK(p.audio.size() == 1 && p.audio[0].cues.size() == 1 && std::fabs(p.audio[0].cues[0].volume - 0.8f) < 1e-6f);
        CHECK(p.events.size() == 1 && p.events[0].name == "Main" && p.events[0].events.size() == 8);
        CHECK(p.events[0].events[4].kind == SeqEvent::Kind::Subtitle && p.events[0].events[4].name == "Guide");
        CHECK(p.events[0].events[4].text == "Welcome \"home\".");    // quotes survive
        CHECK_NEAR(p.events[0].events[4].value, 2.0f, 1e-6f);
        CHECK(p.events[0].events[5].kind == SeqEvent::Kind::TimeScale && std::fabs(p.events[0].events[5].value - 0.5f) < 1e-6f);
        CHECK(p.Write() == text);                                   // writing is stable
        // Comments, blank lines and a loop line.
        Sequence q;
        CHECK(Sequence::Parse("okayseq 1\nsequence \"Loop\"   # a comment\n\nloop 1 0.5 2\ncamera\n  shot 0 pose 1 2 3 0 90 0 blend 0.5 ease sineinout\n", q, &err));
        CHECK(q.loop && std::fabs(q.loopStart - 0.5f) < 1e-6f && std::fabs(q.loopEnd - 2.0f) < 1e-6f);
        CHECK(q.shots.size() == 1 && q.shots[0].ease == Ease::SineInOut && std::fabs(q.shots[0].rotation.y - 90.0f) < 1e-6f);
        // Errors name the line.
        CHECK(!Sequence::Parse("sequence \"x\"\nshot 0 cam \"a\"\n", q, &err));
        CHECK(err.find("line 2") != std::string::npos);
        CHECK(!Sequence::Parse("bogus 1\n", q, &err));
        // File round trip.
        const std::string path = (std::filesystem::temp_directory_path() / "okay_test_sequence.okayseq").string();
        CHECK(s.SaveToFile(path, &err));
        Sequence f;
        CHECK(Sequence::LoadFromFile(path, f, &err) && f.Write() == text);
        std::filesystem::remove(path);
    }

    // ---- Transform evaluation: easing, holds at the ends, any order of time ----
    {
        const Sequence s = MakeSequence();
        const SeqTransformTrack& tr = s.transforms[0];
        Vec3 v; Quat q;
        CHECK(Sequence::ChannelAt(tr.position, 1.0f, v)); CHECK_NEAR(v.x, 2.0f, 1e-4f);    // linear span
        CHECK(Sequence::ChannelAt(tr.position, 3.0f, v)); CHECK_NEAR(v.y, 0.5f, 1e-4f);    // QuadIn: 0.25 of the way at the middle
        CHECK_NEAR(v.x, 4.0f, 1e-4f);
        CHECK(Sequence::ChannelAt(tr.position, -1.0f, v)); CHECK_NEAR(v.x, 0.0f, 1e-6f);   // held before the first key
        CHECK(Sequence::ChannelAt(tr.position, 9.0f, v)); CHECK_NEAR(v.y, 2.0f, 1e-6f);    // and after the last
        CHECK(Sequence::ChannelAt(tr.scale, 0.0f, v)); CHECK_NEAR(v.x, 1.0f, 1e-6f);
        CHECK(Sequence::ChannelAt(tr.scale, 2.0f, v)); CHECK_NEAR(v.x, 1.5f, 1e-4f);
        CHECK(Sequence::RotationAt(tr.rotation, 1.0f, q)); CHECK_NEAR(q.ToEuler().y, 45.0f, 1e-2f);
        // Seeking backwards gives the same answer as going forwards: pure in t.
        Vec3 a, b;
        Sequence::ChannelAt(tr.position, 3.5f, a);
        Sequence::ChannelAt(tr.position, 1.0f, b); CHECK_NEAR(b.x, 2.0f, 1e-4f);
        Sequence::ChannelAt(tr.position, 3.5f, b); CHECK_NEAR(a.x, b.x, 1e-6f); CHECK_NEAR(a.y, b.y, 1e-6f);
        std::vector<SeqKey> none;
        CHECK(!Sequence::ChannelAt(none, 1.0f, v));
        // Editing helpers.
        std::vector<SeqKey> keys = tr.position;
        CHECK(Sequence::KeyAt(keys, 2.0f) == 1 && Sequence::KeyAt(keys, 2.5f) == -1);
        CHECK(Sequence::SetKey(keys, 3.0f, Vec3{9, 9, 9}) == 2 && keys.size() == 4 && keys[3].time == 4.0f);
        CHECK(Sequence::SetKey(keys, 3.0f, Vec3{1, 1, 1}) == 2 && keys.size() == 4 && keys[2].value.x == 1.0f);
        CHECK_NEAR(Sequence::SnapToFrame(0.34f, 30), 10.0f / 30.0f, 1e-6f);
    }

    // ---- Camera: object shots, the blend's middle, the cut ----
    {
        World w;
        const Sequence& s = w.sp->sequence();
        SeqCameraPose before; before.valid = true; before.position = {9, 9, 9}; before.fov = 60.0f;
        SeqCameraPose p = s.CameraAt(0.0f, &w.scene, before);
        CHECK(p.valid); CHECK_NEAR(p.position.x, 1.0f, 1e-6f); CHECK_NEAR(p.fov, 60.0f, 1e-6f);   // Cam A's pose and lens
        p = s.CameraAt(1.99f, &w.scene, before);
        CHECK_NEAR(p.position.z, 3.0f, 1e-6f);                                                   // still Cam A
        p = s.CameraAt(2.5f, &w.scene, before);                                                  // halfway into the 1 s blend
        CHECK_NEAR(p.position.x, 0.5f, 1e-4f); CHECK_NEAR(p.position.y, 3.5f, 1e-4f); CHECK_NEAR(p.position.z, 6.5f, 1e-4f);
        CHECK_NEAR(p.fov, 50.0f, 1e-4f);
        CHECK_NEAR(std::fabs(p.rotation.ToEuler().y), 90.0f, 1e-2f);                           // the short arc from 0 to 180
        p = s.CameraAt(3.0f, &w.scene, before);
        CHECK_NEAR(p.position.z, 10.0f, 1e-4f); CHECK_NEAR(p.fov, 40.0f, 1e-4f);
        p = s.CameraAt(4.0f, &w.scene, before);                                                  // the cut
        CHECK_NEAR(p.position.x, 1.0f, 1e-6f); CHECK_NEAR(p.position.z, 3.0f, 1e-6f);
        p = s.CameraAt(-1.0f, &w.scene, before);                                                 // before the first shot: as it was
        CHECK_NEAR(p.position.x, 9.0f, 1e-6f);
        SeqShot missing; missing.camera = "Nope";
        CHECK_NEAR(Sequence::ShotPose(missing, &w.scene, before).position.x, 9.0f, 1e-6f);
        // A path finds a child.
        CHECK(Sequence::FindObject(w.scene, "Hero/bone") != nullptr && Sequence::FindObject(w.scene, "Hero/nope") == nullptr);
    }

    // ---- The overlay is a function of time ----
    {
        const Sequence s = MakeSequence();
        SeqOverlay o = s.OverlayAt(1.0f);
        CHECK(o.fade == 0.0f && o.text.empty());
        o = s.OverlayAt(2.5f);  CHECK_NEAR(o.fade, 0.5f, 1e-4f);
        o = s.OverlayAt(3.5f);  CHECK_NEAR(o.fade, 1.0f, 1e-6f);
        CHECK(o.text == "Welcome \"home\"." && o.speaker == "Guide" && o.subtitle > 0.99f);
        o = s.OverlayAt(4.25f); CHECK_NEAR(o.fade, 0.5f, 1e-4f);
        o = s.OverlayAt(5.0f);  CHECK_NEAR(o.fade, 0.0f, 1e-6f); CHECK(o.text.empty());
        o = s.OverlayAt(2.5f);  CHECK(o.subtitle < 0.01f);                       // the line fades in over 0.2 s
        const auto cmds = SequencePlayer::BuildOverlay(s.OverlayAt(3.5f), 1600.0f, 900.0f);
        CHECK(cmds.size() >= 4 && cmds[0].kind == LoadingDrawCmd::Kind::Rect && std::fabs(cmds[0].alpha - 1.0f) < 1e-6f);
        bool text = false;
        for (const auto& c : cmds) if (c.kind == LoadingDrawCmd::Kind::Text && c.text == "Welcome \"home\".") text = true;
        CHECK(text);
    }

    // ---- Playback: events once per crossing, actions applied, the camera handed back ----
    {
        World w;
        SequencePlayer* sp = w.sp;
        const Vec3 camPos = w.cam->transform->localPosition;
        CHECK(!sp->IsPlaying() && !sp->LocksInput());
        CHECK(sp->Play());
        CHECK(sp->IsPlaying() && sp->LocksInput());
        sp->Step(0.5f);
        CHECK(w.events == 1 && w.last == "start");                 // what sits at 0 fires on the first step
        CHECK(G(w.sc, "count") == 1.0f && GS(w.sc, "last") == "start");   // and reaches the script
        CHECK_NEAR(w.sm->GetFloat("speed"), 2.5f, 1e-6f);           // the animation cue at 0.5
        CHECK(w.door->active);
        sp->Step(0.5f);                                              // 1.0: door off, sound played
        CHECK(!w.door->active);
        CHECK(w.hero->GetComponent<AudioSource>()->IsPlaying());
        sp->Step(0.5f);                                              // 1.5: crate teleported, state entered
        CHECK_NEAR(w.crate->transform->Position().x, 7.0f, 1e-6f);
        CHECK(w.sm->Current() == "Attack");
        CHECK_NEAR(w.hero->transform->localPosition.x, 3.0f, 1e-4f);  // the transform track at 1.5
        sp->Step(0.5f);                                              // 2.0
        CHECK(w.sm->GetBool("armed"));
        CHECK_NEAR(sp->Time(), 2.0f, 1e-6f);
        sp->Step(0.5f);                                              // 2.5: mid-blend, the camera follows
        CHECK_NEAR(w.cam->transform->Position().y, 3.5f, 1e-4f);
        CHECK_NEAR(w.cam->GetComponent<Camera>()->fieldOfView, 50.0f, 1e-4f);
        CHECK_NEAR(sp->Overlay().fade, 0.5f, 1e-4f);
        sp->Step(0.5f);                                              // 3.0: the time scale action
        CHECK_NEAR(Time::TimeScale(), 0.5f, 1e-6f);
        sp->Step(2.2f);                                              // 5.2: "done" once
        CHECK(w.events == 2 && w.last == "done");
        sp->Step(0.1f);
        CHECK(w.events == 2);
        sp->Seek(4.5f);                                              // back before it: it fires again on the way forward
        CHECK_NEAR(sp->Time(), 4.5f, 1e-6f);
        sp->Step(0.6f);
        CHECK(w.events == 3 && w.last == "done");
        sp->Step(1.0f);                                              // past the end: finished, camera back, time scale back
        CHECK(!sp->IsPlaying() && !sp->LocksInput());
        CHECK_NEAR(w.cam->transform->localPosition.x, camPos.x, 1e-6f);
        CHECK_NEAR(w.cam->transform->localPosition.y, camPos.y, 1e-6f);
        CHECK_NEAR(w.cam->GetComponent<Camera>()->fieldOfView, 60.0f, 1e-6f);
        CHECK_NEAR(Time::TimeScale(), 1.0f, 1e-6f);
        CHECK_NEAR(w.hero->transform->localPosition.y, 2.0f, 1e-4f);  // moved objects stay where the cutscene left them
        CHECK(sp->Overlay().text.empty());
    }

    // ---- Time scale, loop and a wait for input ----
    {
        World w;
        SequencePlayer* sp = w.sp;
        sp->timeScale = 2.0f;
        sp->Play();
        sp->Step(0.5f);
        CHECK_NEAR(sp->Time(), 1.0f, 1e-6f);
        sp->timeScale = 1.0f;
        sp->loop = true;
        sp->Seek(5.5f);
        sp->Step(1.0f);                                              // wraps: 5.5 + 1 - 6 = 0.5, "start" fires again
        CHECK(sp->IsPlaying());
        CHECK_NEAR(sp->Time(), 0.5f, 1e-5f);
        CHECK(w.last == "start");
        sp->Stop();
        CHECK(!sp->IsPlaying());
        // Loop markers.
        Sequence s = MakeSequence(); s.loop = true; s.loopStart = 1.0f; s.loopEnd = 3.0f;
        sp->loop = false;
        sp->SetSequence(s);
        sp->Play();
        sp->Step(2.9f);                                              // 2.9
        sp->Step(0.3f);                                              // 3.2 -> 1.2
        CHECK_NEAR(sp->Time(), 1.2f, 1e-5f);
        sp->Stop();
        // A wait holds the playhead until the confirm key.
        Sequence wait = MakeSequence();
        wait.events[0].events.push_back({1.0f, SeqEvent::Kind::WaitForInput, "", "", 0.0f});
        sp->SetSequence(wait);
        sp->Play();
        sp->Step(1.5f);
        CHECK(sp->IsWaiting()); CHECK_NEAR(sp->Time(), 1.0f, 1e-6f);
        CHECK(sp->Overlay().waiting);
        sp->Step(1.0f);
        CHECK(sp->IsWaiting()); CHECK_NEAR(sp->Time(), 1.0f, 1e-6f);
        CHECK(!w.door->active);                                      // the action at the same instant still ran
        Input::FeedKeys({'\r'});
        sp->Step(0.5f);
        Input::FeedKeys({});
        CHECK(!sp->IsWaiting()); CHECK_NEAR(sp->Time(), 1.5f, 1e-6f);
        CHECK_NEAR(w.crate->transform->Position().x, 7.0f, 1e-6f);    // the teleport at 1.5 fired after the resume
        sp->Stop();
        // An "end" action finishes early.
        Sequence early = MakeSequence();
        early.events[0].events.push_back({2.2f, SeqEvent::Kind::End, "", "", 0.0f});
        sp->SetSequence(early);
        sp->Play();
        sp->Step(2.0f);
        CHECK(sp->IsPlaying());
        sp->Step(0.5f);
        CHECK(!sp->IsPlaying());
    }

    // ---- Skip: the remaining actions apply, nothing waits, the camera comes back ----
    {
        World w;
        SequencePlayer* sp = w.sp;
        Sequence s = MakeSequence();
        s.events[0].events.push_back({3.5f, SeqEvent::Kind::WaitForInput, "", "", 0.0f});
        sp->SetSequence(s);
        const Vec3 camPos = w.cam->transform->localPosition;
        sp->Play();
        sp->Step(0.25f);
        sp->Skip();
        CHECK(!sp->IsPlaying() && !sp->IsWaiting());
        CHECK(w.events == 2 && w.last == "done");
        CHECK(!w.door->active);
        CHECK_NEAR(w.crate->transform->Position().x, 7.0f, 1e-6f);
        CHECK_NEAR(Time::TimeScale(), 1.0f, 1e-6f);
        CHECK_NEAR(w.cam->transform->localPosition.z, camPos.z, 1e-6f);
        CHECK_NEAR(w.hero->transform->localPosition.y, 2.0f, 1e-4f);   // at its end pose
        // The skip key does the same from Update.
        sp->Play();
        w.scene.Update(0.1f);
        CHECK(sp->IsPlaying());
        Input::FeedKeys({(char)27});
        w.scene.Update(0.1f);
        Input::FeedKeys({});
        CHECK(!sp->IsPlaying());
    }

    // ---- Through the scene: play on start, the camera written after the frame, scripts' play_sequence ----
    {
        World w;
        SequencePlayer* sp = w.sp;
        sp->playOnStart = true;
        sp->Start();
        CHECK(sp->IsPlaying());
        for (int i = 0; i < 150; ++i) w.scene.Update(1.0f / 60.0f);   // 2.5 s
        CHECK_NEAR(sp->Time(), 2.5f, 1e-3f);
        CHECK_NEAR(w.cam->transform->Position().y, 3.5f, 2e-2f);
        CHECK(SequencePlayer::Playing(w.scene) == sp && SequencePlayer::AnyLocksInput(w.scene));
        SequencePlayer::StopAll(w.scene);
        CHECK(!sp->IsPlaying() && !SequencePlayer::AnyLocksInput(w.scene));
        // play_sequence / stop_sequence / sequence_playing / sequence_time from a script.
        const std::string dir = (std::filesystem::temp_directory_path() / "okay_test_seqproj").string();
        std::filesystem::create_directories(dir + "/Assets");
        CHECK(MakeSequence().SaveToFile(dir + "/Assets/Intro.okayseq"));
        Character::SetAssetBaseDir(dir);
        sp->sequencePath = "Assets/Intro.okayseq";
        auto* s2 = w.director->AddComponent<ScriptComponent>("okayscript");
        std::string err;
        CHECK(s2->LoadSource("var ok = -1; var playing = -1; var t = -1;\n"
                             "function go() { ok = play_sequence(\"Intro\"); playing = sequence_playing(); }\n"
                             "function later() { t = sequence_time(); stop_sequence(); playing = sequence_playing(); }\n", &err));
        w.scene.Update(1.0f / 60.0f);
        s2->VM()->CallEvent("go");
        CHECK(G(s2, "ok") == 1.0f && G(s2, "playing") == 1.0f && sp->IsPlaying());
        sp->Step(1.25f);
        s2->VM()->CallEvent("later");
        CHECK_NEAR(G(s2, "t"), 1.25f, 1e-4f);
        CHECK(G(s2, "playing") == 0.0f && !sp->IsPlaying());
        Character::SetAssetBaseDir("");
        std::filesystem::remove_all(dir);
    }

    // ---- The scene record, and an older scene without one ----
    {
        Scene s("T");
        GameObject* d = s.CreateGameObject("Director");
        auto* sp = d->AddComponent<SequencePlayer>();
        sp->sequencePath = "Assets/Intro.okayseq"; sp->playOnStart = true; sp->loop = true;
        sp->timeScale = 1.5f; sp->skipKey = 'q'; sp->lockPlayer = false;
        const std::string text = SceneSerializer::Serialize(s);
        CHECK(text.find("sequenceplayer") != std::string::npos);
        Scene r("R");
        std::string err;
        CHECK(SceneSerializer::Deserialize(r, text, &err));
        auto* rp = r.Find("Director") ? r.Find("Director")->GetComponent<SequencePlayer>() : nullptr;
        CHECK(rp != nullptr);
        if (rp) {
            CHECK(rp->sequencePath == "Assets/Intro.okayseq" && rp->playOnStart && rp->loop);
            CHECK_NEAR(rp->timeScale, 1.5f, 1e-6f);
            CHECK(rp->skipKey == 'q' && !rp->lockPlayer && !rp->IsPlaying());   // the playback state is not saved
        }
        // The same scene from before the record existed loads unchanged.
        std::string older;
        {
            std::istringstream in(text); std::string line;
            while (std::getline(in, line)) if (line.find("  sequenceplayer") != 0) older += line + "\n";
        }
        Scene o("O");
        CHECK(SceneSerializer::Deserialize(o, older, &err));
        CHECK(o.Find("Director") && !o.Find("Director")->GetComponent<SequencePlayer>());
        // Missing keys keep the defaults.
        std::string minimal;
        {
            std::istringstream in(text); std::string line;
            while (std::getline(in, line)) minimal += (line.find("  sequenceplayer") == 0 ? std::string("  sequenceplayer path=\"Assets/X.okayseq\"") : line) + "\n";
        }
        Scene m("M");
        CHECK(SceneSerializer::Deserialize(m, minimal, &err));
        auto* mp = m.Find("Director") ? m.Find("Director")->GetComponent<SequencePlayer>() : nullptr;
        CHECK(mp && !mp->playOnStart && mp->skipKey == 27 && mp->lockPlayer && std::fabs(mp->timeScale - 1.0f) < 1e-6f);
    }

    TEST_MAIN_RESULT();
}
