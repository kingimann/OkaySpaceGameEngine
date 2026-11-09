// Scene serializer audit (1.5): key bindings in the one-token records, several components of one type on one
// object, asset paths of every instance, number and text round trips, and text that spans lines.
#include "test_framework.hpp"
#include <Okay.hpp>
#include <cstdint>
#include <algorithm>
#include <cstring>
#include <limits>
#include <sstream>
#include <string>
#include <vector>

using namespace okay;

static bool Has(const std::vector<std::string>& v, const std::string& needle) {
    for (const auto& w : v) if (w.find(needle) != std::string::npos) return true;
    return false;
}

static GameObject* Find(Scene& s, const std::string& name) {
    for (const auto& o : s.Objects()) if (o->name == name) return o.get();
    return nullptr;
}

template <class T>
static T* Comp(Scene& s, const std::string& name = "O") {
    GameObject* o = Find(s, name);
    return o ? o->GetComponent<T>() : nullptr;
}

static bool RoundTrip(Scene& from, Scene& to, std::string* text = nullptr, std::vector<std::string>* warnings = nullptr) {
    const std::string t = SceneSerializer::Serialize(from);
    if (text) *text = t;
    std::string err;
    const bool ok = SceneSerializer::Deserialize(to, t, &err, warnings);
    if (!ok) std::cerr << "  load failed: " << err << "\n";
    return ok;
}

// A one-object scene document around some record lines (what an older build wrote).
static std::string Doc(const std::string& records) {
    return "okayscene 3\ngameobject 0 \"O\"\n  active 1\n  transform 0 0 0 0 0 0 1 1 1 1\n" + records + "  parent -1\nend\n";
}

static std::string Replace(std::string s, const std::string& from, const std::string& to) {
    for (std::size_t at = 0; (at = s.find(from, at)) != std::string::npos; at += to.size()) s.replace(at, from.size(), to);
    return s;
}

// ---- Key bindings in the one-token records --------------------------------------------------------------

static void TestKeysEveryCode() {
    RUN_SUITE("key bindings: every key code survives, and the fields after it stay in place");
    int bad = 0;
    for (int code = 0; code < 256; ++code) {
        const char key = static_cast<char>(code);
        Scene a("a");
        GameObject* o = a.CreateGameObject("O");
        auto* td = o->AddComponent<TopDownController>();
        td->sprintKey = key; td->walkSpeed = 3.25f; td->runSpeed = 9.5f; td->driveAnimation = false;
        td->rotateToFace = (code % 2) == 0; td->turnSpeed = 11.5f + (float)(code % 5); td->acceleration = 21.0f;
        td->deceleration = 17.0f; td->cameraRelative = true; td->cameraDistance = 14.5f; td->cameraDamping = 0.75f; td->footIK = true;
        auto* t2 = o->AddComponent<TopDownController2D>();
        t2->sprintKey = key; t2->dashKey = static_cast<char>(255 - code); t2->fireKey = static_cast<char>((code * 7) & 255);
        t2->speed = 4.5f; t2->runSpeed = 8.5f; t2->dashSpeed = 13.5f; t2->turnSpeed = 321.0f; t2->fireRate = 6.5f; t2->projectile = "Assets/b.okayprefab";
        auto* cc = o->AddComponent<CharacterController2D>();
        cc->sprintKey = key; cc->runSpeed = 7.5f; cc->wallJumpPush = 3.5f; cc->maxJumps = 3; cc->jumpBuffer = 0.375f;
        auto* ts = o->AddComponent<ThirdPersonShooterController>();
        ts->sprintKey = key; ts->walkSpeed = 2.5f; ts->steerRate = 99.5f; ts->walkAcceleration = 5.5f; ts->maxJumps = 2;
        auto* cm = o->AddComponent<ClickToMoveController>();
        cm->runKey = key; cm->rotateLeftKey = static_cast<char>(code ^ 0x55); cm->rotateRightKey = static_cast<char>(code ^ 0xAA);
        cm->walkSpeed = 3.5f; cm->cameraDamping = 0.625f; cm->repathInterval = 0.875f; cm->groundY = 1.5f;

        Scene b("b");
        std::string text;
        if (!RoundTrip(a, b, &text)) { ++bad; continue; }
        GameObject* r = Find(b, "O");
        auto* rtd = r ? r->GetComponent<TopDownController>() : nullptr;
        auto* rt2 = r ? r->GetComponent<TopDownController2D>() : nullptr;
        auto* rcc = r ? r->GetComponent<CharacterController2D>() : nullptr;
        auto* rts = r ? r->GetComponent<ThirdPersonShooterController>() : nullptr;
        auto* rcm = r ? r->GetComponent<ClickToMoveController>() : nullptr;
        bool ok = rtd && rt2 && rcc && rts && rcm;
        if (ok) {
            ok = rtd->sprintKey == key && rtd->walkSpeed == 3.25f && rtd->runSpeed == 9.5f && !rtd->driveAnimation &&
                 rtd->rotateToFace == td->rotateToFace && rtd->turnSpeed == td->turnSpeed && rtd->acceleration == 21.0f &&
                 rtd->deceleration == 17.0f && rtd->cameraRelative && rtd->cameraDistance == 14.5f && rtd->cameraDamping == 0.75f && rtd->footIK;
            ok = ok && rt2->sprintKey == key && rt2->dashKey == t2->dashKey && rt2->fireKey == t2->fireKey && rt2->speed == 4.5f &&
                 rt2->runSpeed == 8.5f && rt2->dashSpeed == 13.5f && rt2->turnSpeed == 321.0f && rt2->fireRate == 6.5f &&
                 rt2->projectile == "Assets/b.okayprefab";
            ok = ok && rcc->sprintKey == key && rcc->runSpeed == 7.5f && rcc->wallJumpPush == 3.5f && rcc->maxJumps == 3 && rcc->jumpBuffer == 0.375f;
            ok = ok && rts->sprintKey == key && rts->walkSpeed == 2.5f && rts->steerRate == 99.5f && rts->walkAcceleration == 5.5f && rts->maxJumps == 2;
            ok = ok && rcm->runKey == key && rcm->rotateLeftKey == cm->rotateLeftKey && rcm->rotateRightKey == cm->rotateRightKey &&
                 rcm->walkSpeed == 3.5f && rcm->cameraDamping == 0.625f && rcm->repathInterval == 0.875f && rcm->groundY == 1.5f;
        }
        if (!ok) { ++bad; if (bad < 6) std::cerr << "  key code " << code << " did not round-trip\n"; }
        // A second save of the loaded scene is the same text: the form is stable.
        if (ok && SceneSerializer::Serialize(b) != text) { ++bad; if (bad < 6) std::cerr << "  key code " << code << " changed on the second save\n"; }
    }
    CHECK(bad == 0);
}

static void TestKeysTextForm() {
    RUN_SUITE("key bindings: the text form");
    auto record = [](char key) {
        Scene a("a");
        auto* td = a.CreateGameObject("O")->AddComponent<TopDownController>();
        td->sprintKey = key;
        const std::string text = SceneSerializer::Serialize(a);
        const std::size_t at = text.find("  tdctrl ");
        return at == std::string::npos ? std::string() : text.substr(at, text.find('\n', at) - at);
    };
    // A printable character other than '-' is written as itself, as every build always read it.
    CHECK(record('e').find(" e ") != std::string::npos);
    CHECK(record('W').find(" W ") != std::string::npos);
    CHECK(record('5').find(" 5 ") != std::string::npos);
    CHECK(record('#').find(" # ") != std::string::npos);
    // No key is '-'; the codes that cannot be one raw token are written by number.
    CHECK(record(0).find(" - ") != std::string::npos);
    CHECK(record(' ').find(" #32 ") != std::string::npos);
    CHECK(record('-').find(" #45 ") != std::string::npos);
    CHECK(record('\t').find(" #9 ") != std::string::npos);
    CHECK(record('\r').find(" #13 ") != std::string::npos);
    CHECK(record(Input::KeyShift).find(" #16 ") != std::string::npos);
    CHECK(record(Input::KeyF(5)).find(" #132 ") != std::string::npos);
    // No raw control byte and no byte above 127 is written any more.
    int rawBytes = 0;
    for (int c = 0; c < 256; ++c) {
        const std::string r = record(static_cast<char>(c));
        for (char ch : r) { const unsigned u = static_cast<unsigned char>(ch); if (u < 32 || u >= 127) ++rawBytes; }
    }
    CHECK(rawBytes == 0);
}

static void TestKeysOldFiles() {
    RUN_SUITE("key bindings: files older builds wrote still load as they did");
    // tdctrl: walk run key anim face turn accel decel camRel dist pitch yaw lookH damp [footIK]
    {
        Scene s("s");
        const std::string text = Doc("  tdctrl 4 7 q 1 0 12 20 25 1 8 50 0 1.5 0.1 1\n");
        std::vector<std::string> w;
        CHECK(SceneSerializer::Deserialize(s, text, nullptr, &w));
        auto* td = Comp<TopDownController>(s);
        CHECK(td && td->sprintKey == 'q' && td->walkSpeed == 4.0f && td->runSpeed == 7.0f && td->driveAnimation && !td->rotateToFace);
        CHECK(td && td->turnSpeed == 12.0f && td->acceleration == 20.0f && td->cameraRelative && td->cameraDistance == 8.0f && td->footIK);
        CHECK(w.empty());
    }
    // The raw control bytes and high bytes older builds wrote for Shift, Ctrl and the function keys: the text of
    // this build with each `#<n>` put back as the raw byte is what such a file looked like.
    {
        Scene a("a");
        GameObject* o = a.CreateGameObject("O");
        auto* td = o->AddComponent<TopDownController>();
        td->sprintKey = Input::KeyShift; td->turnSpeed = 12.0f;
        auto* t2 = o->AddComponent<TopDownController2D>();
        t2->sprintKey = Input::KeyF(2); t2->dashKey = Input::KeyCtrl; t2->fireKey = Input::KeyF(12); t2->dashSpeed = 12.0f; t2->dashDuration = 0.25f;
        auto* cc = o->AddComponent<CharacterController2D>();
        cc->sprintKey = Input::KeyShift; cc->wallJumpPush = 3.5f;
        auto* ts = o->AddComponent<ThirdPersonShooterController>();
        ts->sprintKey = Input::KeyLShift; ts->steerRate = 99.5f;
        auto* cm = o->AddComponent<ClickToMoveController>();
        cm->runKey = Input::KeyShift; cm->rotateLeftKey = Input::KeyLeft; cm->rotateRightKey = Input::KeyRight; cm->cameraDamping = 0.625f;
        std::string text = SceneSerializer::Serialize(a);
        for (int code : {3, 4, 16, 17, 129, 139, 146}) {
            const std::string token = " #" + std::to_string(code) + " ";
            CHECK(text.find(token) != std::string::npos);
            text = Replace(text, token, std::string(" ") + static_cast<char>(code) + " ");
        }
        CHECK(text.find('#') == std::string::npos);
        Scene s("s");
        CHECK(SceneSerializer::Deserialize(s, text));
        GameObject* r = Find(s, "O");
        auto* rtd = r ? r->GetComponent<TopDownController>() : nullptr;
        auto* rt2 = r ? r->GetComponent<TopDownController2D>() : nullptr;
        auto* rcc = r ? r->GetComponent<CharacterController2D>() : nullptr;
        auto* rts = r ? r->GetComponent<ThirdPersonShooterController>() : nullptr;
        auto* rcm = r ? r->GetComponent<ClickToMoveController>() : nullptr;
        CHECK(rtd && rtd->sprintKey == Input::KeyShift && rtd->turnSpeed == 12.0f);
        CHECK(rt2 && rt2->sprintKey == Input::KeyF(2) && rt2->dashKey == Input::KeyCtrl && rt2->fireKey == Input::KeyF(12) && rt2->dashSpeed == 12.0f && rt2->dashDuration == 0.25f);
        CHECK(rcc && rcc->sprintKey == Input::KeyShift && rcc->wallJumpPush == 3.5f);
        CHECK(rts && rts->sprintKey == Input::KeyLShift && rts->steerRate == 99.5f);
        CHECK(rcm && rcm->runKey == Input::KeyShift && rcm->rotateLeftKey == Input::KeyLeft && rcm->rotateRightKey == Input::KeyRight && rcm->cameraDamping == 0.625f);
    }
    // '-' was "no key" (and is still); a digit and '#' are keys, written as themselves as they always were.
    {
        Scene a("a");
        GameObject* o = a.CreateGameObject("O");
        o->AddComponent<ClickToMoveController>()->runKey = '5';
        o->AddComponent<ThirdPersonShooterController>()->sprintKey = '#';
        o->AddComponent<TopDownController>()->sprintKey = 0;
        const std::string text = SceneSerializer::Serialize(a);
        Scene s("s");
        CHECK(SceneSerializer::Deserialize(s, text));
        auto* td = Comp<TopDownController>(s);
        auto* cm = Comp<ClickToMoveController>(s);
        auto* ts = Comp<ThirdPersonShooterController>(s);
        CHECK(td && td->sprintKey == 0);
        CHECK(cm && cm->runKey == '5');
        CHECK(ts && ts->sprintKey == '#' && ts->canJump);
        // The same records with the key as a lone '-' (what a file of an older build holds for "no key").
        std::string old = text;
        const std::size_t at = old.find("  ctmctrl ");
        CHECK(at != std::string::npos);
        if (at != std::string::npos) {
            const std::size_t eol = old.find('\n', at);
            std::string line = old.substr(at, eol - at);
            const std::string with5 = " 5 ";
            const std::size_t k = line.find(with5);
            CHECK(k != std::string::npos);
            if (k != std::string::npos) line.replace(k, 3, " - ");
            old = old.substr(0, at) + line + old.substr(eol);
            Scene s2("s2");
            CHECK(SceneSerializer::Deserialize(s2, old));
            CHECK(Comp<ClickToMoveController>(s2) && Comp<ClickToMoveController>(s2)->runKey == 0);
        }
    }
    // The new form reads even where an old record had one character: `#<n>`, any code, with the fields after it in place.
    {
        Scene s("s");
        const std::string text = Doc("  tdctrl 4 7 #32 1 1 12 20 25 0 8 50 0 1.5 0.1 1\n");
        CHECK(SceneSerializer::Deserialize(s, text));
        auto* td = Comp<TopDownController>(s);
        CHECK(td && td->sprintKey == ' ' && td->rotateToFace && td->turnSpeed == 12.0f && td->footIK);
    }
    // A malformed `#` token is the character it starts with, as any one-token reader would take it; never a crash.
    {
        Scene s("s");
        CHECK(SceneSerializer::Deserialize(s, Doc("  tdctrl 4 7 #999 1 1 12 20 25 0 8 50 0 1.5 0.1\n")));
        auto* td = Comp<TopDownController>(s);
        CHECK(td && td->sprintKey == '#' && td->turnSpeed == 12.0f);
        Scene s2("s2");
        CHECK(SceneSerializer::Deserialize(s2, Doc("  tdctrl 4 7 #x 1 1 12 20 25 0 8 50 0 1.5 0.1\n")));
        Scene s3("s3");
        CHECK(SceneSerializer::Deserialize(s3, Doc("  tdctrl 4 7 #0 1 1 12 20 25 0 8 50 0 1.5 0.1\n")));
        CHECK(Comp<TopDownController>(s3) && Comp<TopDownController>(s3)->sprintKey == 0);
    }
}

// ---- Several components of one type -----------------------------------------------------------------------

static void TestPerInstanceTypes() {
    RUN_SUITE("duplicate components: more types are saved per instance");
    Scene a("a");
    GameObject* o = a.CreateGameObject("O");
    // Joints: one per neighbour.
    auto* j1 = o->AddComponent<Joint2D>(); j1->connectedBody = "Left"; j1->mode = (int)Joint2D::Mode::Hinge; j1->anchor = {1, 2};
    auto* j2 = o->AddComponent<Joint2D>(); j2->connectedBody = "Right"; j2->mode = (int)Joint2D::Mode::Spring; j2->spring = 77.0f; j2->enabled = false;
    auto* k1 = o->AddComponent<Joint3D>(); k1->connectedBody = "Up"; k1->axis = {0, 1, 0};
    auto* k2 = o->AddComponent<Joint3D>(); k2->connectedBody = "Down"; k2->breakable = true; k2->breakForce = 12.5f;
    // Spawners.
    auto* s1 = o->AddComponent<Spawner>(); s1->prefabPath = "Assets/Enemy.okayprefab"; s1->interval = 1.5f; s1->count = 3;
    auto* s2 = o->AddComponent<Spawner>(); s2->prefabPath = "Assets/Pickup.okayprefab"; s2->interval = 9.0f; s2->maxAlive = 2;
    // Lights.
    auto* l1 = o->AddComponent<Light>(); l1->type = Light::Type::Point; l1->intensity = 2.5f; l1->range = 7.0f;
    auto* l2 = o->AddComponent<Light>(); l2->type = Light::Type::Spot; l2->intensity = 0.75f; l2->spotAngle = 33.0f; l2->castShadows = false;
    // Particle systems keep their own gradient and wind.
    auto* p1 = o->AddComponent<ParticleSystem>(); p1->emissionRate = 11.0f; p1->texture = "Assets/fire.png"; p1->windInfluence = 0.25f;
    auto* p2 = o->AddComponent<ParticleSystem>(); p2->emissionRate = 22.0f; p2->texture = "Assets/smoke.png";
    p2->colorGradient.count = 2;
    p2->colorGradient.stops[0].time = 0.0f; p2->colorGradient.stops[0].color = Color(1, 0, 0, 1);
    p2->colorGradient.stops[1].time = 1.0f; p2->colorGradient.stops[1].color = Color(0, 0, 1, 0.5f);
    p2->noiseStrength = 3.5f;
    // Trigger zones, UI animations, cutscene players, quest triggers.
    auto* z1 = o->AddComponent<TriggerZone>(); z1->varName = "a"; z1->amount = 1.0f;
    auto* z2 = o->AddComponent<TriggerZone>(); z2->varName = "b"; z2->amount = 2.0f; z2->once = false;
    auto* u1 = o->AddComponent<UIAnimation>(); u1->duration = 0.25f;
    auto* u2 = o->AddComponent<UIAnimation>(); u2->duration = 0.75f;
    auto* q1 = o->AddComponent<SequencePlayer>(); q1->sequencePath = "Assets/A.okayseq";
    auto* q2 = o->AddComponent<SequencePlayer>(); q2->sequencePath = "Assets/B.okayseq"; q2->timeScale = 2.0f;
    auto* t1 = o->AddComponent<QuestTrigger>(); t1->questId = "one";
    auto* t2 = o->AddComponent<QuestTrigger>(); t2->questId = "two"; t2->objective = 3;

    Scene b("b");
    std::vector<std::string> warn;
    std::string text;
    CHECK(RoundTrip(a, b, &text, &warn));
    CHECK(warn.empty());
    GameObject* r = Find(b, "O");
    CHECK(r != nullptr);
    if (!r) return;
    auto js = r->GetComponents<Joint2D>();
    CHECK(js.size() == 2);
    if (js.size() == 2) {
        CHECK(js[0]->connectedBody == "Left" && js[0]->mode == (int)Joint2D::Mode::Hinge && js[0]->anchor.x == 1.0f && js[0]->anchor.y == 2.0f && js[0]->enabled);
        CHECK(js[1]->connectedBody == "Right" && js[1]->mode == (int)Joint2D::Mode::Spring && js[1]->spring == 77.0f && !js[1]->enabled);
    }
    auto ks = r->GetComponents<Joint3D>();
    CHECK(ks.size() == 2);
    if (ks.size() == 2) CHECK(ks[0]->connectedBody == "Up" && ks[1]->connectedBody == "Down" && ks[1]->breakable && ks[1]->breakForce == 12.5f);
    auto ss = r->GetComponents<Spawner>();
    CHECK(ss.size() == 2);
    if (ss.size() == 2) CHECK(ss[0]->prefabPath == "Assets/Enemy.okayprefab" && ss[0]->interval == 1.5f && ss[0]->count == 3 && ss[1]->prefabPath == "Assets/Pickup.okayprefab" && ss[1]->maxAlive == 2);
    auto ls = r->GetComponents<Light>();
    CHECK(ls.size() == 2);
    if (ls.size() == 2) CHECK(ls[0]->type == Light::Type::Point && ls[0]->intensity == 2.5f && ls[1]->type == Light::Type::Spot && ls[1]->spotAngle == 33.0f && !ls[1]->castShadows);
    auto ps = r->GetComponents<ParticleSystem>();
    CHECK(ps.size() == 2);
    if (ps.size() == 2) {
        CHECK(ps[0]->emissionRate == 11.0f && ps[0]->texture == "Assets/fire.png" && ps[0]->windInfluence == 0.25f && ps[0]->noiseStrength == 0.0f);
        CHECK(ps[1]->emissionRate == 22.0f && ps[1]->texture == "Assets/smoke.png" && ps[1]->windInfluence == 1.0f && ps[1]->noiseStrength == 3.5f);
        CHECK(ps[1]->colorGradient.count == 2 && ps[1]->colorGradient.stops[1].color.b == 1.0f);
        CHECK(ps[0]->colorGradient.count != 2 || ps[0]->colorGradient.stops[1].color.b != 1.0f);
    }
    auto zs = r->GetComponents<TriggerZone>();
    CHECK(zs.size() == 2);
    if (zs.size() == 2) CHECK(zs[0]->varName == "a" && zs[0]->once && zs[1]->varName == "b" && zs[1]->amount == 2.0f && !zs[1]->once);
    auto us = r->GetComponents<UIAnimation>();
    CHECK(us.size() == 2);
    if (us.size() == 2) CHECK(us[0]->duration == 0.25f && us[1]->duration == 0.75f);
    auto qs = r->GetComponents<SequencePlayer>();
    CHECK(qs.size() == 2);
    if (qs.size() == 2) CHECK(qs[0]->sequencePath == "Assets/A.okayseq" && qs[1]->sequencePath == "Assets/B.okayseq" && qs[1]->timeScale == 2.0f);
    auto ts = r->GetComponents<QuestTrigger>();
    CHECK(ts.size() == 2);
    if (ts.size() == 2) CHECK(ts[0]->questId == "one" && ts[1]->questId == "two" && ts[1]->objective == 3);
    (void)j1; (void)j2; (void)k1; (void)k2; (void)s1; (void)s2; (void)l1; (void)l2; (void)p1; (void)p2; (void)z1; (void)z2;
    (void)u1; (void)u2; (void)q1; (void)q2; (void)t1; (void)t2;

    // Saving the loaded scene again gives the same text.
    CHECK(SceneSerializer::Serialize(b) == text);
}

static void TestOneInstanceFilesStillLoad() {
    RUN_SUITE("duplicate components: a file with one record of each is unchanged, and several records in a text load");
    // One of each type: the text is what it always was (one record, one particlesfx).
    Scene a("a");
    GameObject* o = a.CreateGameObject("O");
    o->AddComponent<ParticleSystem>();
    o->AddComponent<Joint2D>();
    o->AddComponent<Spawner>();
    const std::string text = SceneSerializer::Serialize(a);
    std::size_t n = 0;
    for (std::size_t at = 0; (at = text.find("\n  particles ", at)) != std::string::npos; ++at) ++n;
    CHECK(n == 1);
    n = 0;
    for (std::size_t at = 0; (at = text.find("\n  particlesfx ", at)) != std::string::npos; ++at) ++n;
    CHECK(n == 1);
    // A hand-made text with two records of a type loads both, with no warning for the types saved per instance.
    std::vector<std::string> warn;
    Scene b("b");
    CHECK(SceneSerializer::Deserialize(b, Doc("  spawner \"\" 3 5 0 6 1 1 1 0 0 1 \"a.okayprefab\"\n  spawner \"\" 4 5 0 6 1 1 1 0 0 1 \"b.okayprefab\"\n"), nullptr, &warn));
    CHECK(Find(b, "O") && Find(b, "O")->GetComponents<Spawner>().size() == 2);
    CHECK(!Has(warn, "only the first is saved"));
    // A type that is still saved once keeps its warning.
    std::vector<std::string> w2;
    Scene c("c");
    CHECK(SceneSerializer::Deserialize(c, Doc("  sprite 65 1 1 1 1 1 1\n  sprite 66 1 1 1 1 1 1\n"), nullptr, &w2));
    CHECK(Has(w2, "two 'SpriteRenderer' components"));
    // Particle follow-up records belong to the system the record before them made.
    Scene d("d");
    GameObject* p = d.CreateGameObject("P");
    auto* first = p->AddComponent<ParticleSystem>(); first->noiseStrength = 1.5f;
    auto* second = p->AddComponent<ParticleSystem>(); second->noiseStrength = 2.5f; second->windInfluence = 0.5f;
    Scene e("e");
    CHECK(RoundTrip(d, e));
    auto ps = Find(e, "P") ? Find(e, "P")->GetComponents<ParticleSystem>() : std::vector<ParticleSystem*>{};
    CHECK(ps.size() == 2 && ps[0]->noiseStrength == 1.5f && ps[1]->noiseStrength == 2.5f && ps[0]->windInfluence == 1.0f && ps[1]->windInfluence == 0.5f);
    (void)first; (void)second;
}

static void TestAssetPaths() {
    RUN_SUITE("asset paths: every instance of a component counts");
    Scene a("a");
    GameObject* o = a.CreateGameObject("O");
    o->AddComponent<AudioSource>()->clipPath = "Assets/music.wav";
    o->AddComponent<AudioSource>()->clipPath = "Assets/jump.wav";
    o->AddComponent<ParticleSystem>()->texture = "Assets/fire.png";
    o->AddComponent<ParticleSystem>()->texture = "Assets/smoke.png";
    o->AddComponent<Spawner>()->prefabPath = "Assets/Enemy.okayprefab";
    o->AddComponent<Spawner>()->prefabPath = "Assets/Pickup.okayprefab";
    const std::vector<std::string> paths = SceneSerializer::CollectAssetPaths(a);
    auto has = [&](const char* p) { for (const auto& s : paths) if (s == p) return true; return false; };
    CHECK(has("Assets/music.wav") && has("Assets/jump.wav"));
    CHECK(has("Assets/fire.png") && has("Assets/smoke.png"));
    CHECK(has("Assets/Enemy.okayprefab") && has("Assets/Pickup.okayprefab"));
    CHECK(paths.size() == 6);
}

// ---- Numbers and text ----------------------------------------------------------------------------------------

static std::uint32_t Bits(float f) { std::uint32_t u; std::memcpy(&u, &f, 4); return u; }

static void TestFloatRoundTrip() {
    RUN_SUITE("numbers: every finite float reads back to the same bits");
    std::uint64_t s = 0x9E3779B97F4A7C15ull;
    auto next = [&]() { s ^= s << 13; s ^= s >> 7; s ^= s << 17; return s; };
    std::vector<float> values{0.0f, -0.0f, 1.0f, -1.0f, 0.1f, 1e-45f, -1e-45f, 1.17549435e-38f, 3.40282347e38f, -3.40282347e38f,
                              16777216.0f, 16777217.0f, 0.3f, 1.0f / 3.0f, 123456.789f, 1e-10f, 9.999999e37f};
    while (values.size() < 3000) {
        const std::uint32_t u = static_cast<std::uint32_t>(next() >> 16);
        float f; std::memcpy(&f, &u, 4);
        if (std::isfinite(f)) values.push_back(f);
    }
    Scene a("a");
    for (std::size_t i = 0; i + 2 < values.size(); i += 3) {
        GameObject* o = a.CreateGameObject("F" + std::to_string(i));
        o->AddComponent<Mover>()->velocity = {values[i], values[i + 1], values[i + 2]};
        o->AddComponent<Spinner>()->angularVelocity = {values[i + 2], values[i], values[i + 1]};
    }
    Scene b("b");
    CHECK(RoundTrip(a, b));
    int bad = 0;
    for (std::size_t i = 0; i + 2 < values.size(); i += 3) {
        GameObject* o = Find(b, "F" + std::to_string(i));
        auto* mv = o ? o->GetComponent<Mover>() : nullptr;
        auto* sp = o ? o->GetComponent<Spinner>() : nullptr;
        if (!mv || !sp || Bits(mv->velocity.x) != Bits(values[i]) || Bits(mv->velocity.y) != Bits(values[i + 1]) || Bits(mv->velocity.z) != Bits(values[i + 2]) ||
            Bits(sp->angularVelocity.x) != Bits(values[i + 2]) || Bits(sp->angularVelocity.y) != Bits(values[i]) || Bits(sp->angularVelocity.z) != Bits(values[i + 1]))
            ++bad;
    }
    CHECK(bad == 0);
}

static void TestNamesAndText() {
    RUN_SUITE("text: names, tags and strings with quotes, backslashes, unicode and line breaks");
    const std::vector<std::string> names{
        "plain", "caf\xC3\xA9", "\xE6\x97\xA5\xE6\x9C\xAC\xE8\xAA\x9E", "\xF0\x9F\x8E\xAE game", "say \"hi\"", "back\\slash", "a\\\"b\\\\", "tab\there",
        "two\nlines", "cr\r\nlf", "  padded  ", "end with backslash\\", std::string(5000, 'x'), "a/b/c", "#hash", "=eq"};
    Scene a("a");
    for (std::size_t i = 0; i < names.size(); ++i) {
        GameObject* o = a.CreateGameObject(names[i]);
        o->tag = names[i];
        o->AddComponent<TextRenderer>()->text = names[i];
        o->AddComponent<AudioSource>()->clipPath = names[i];
    }
    GameObject* last = a.CreateGameObject("after");   // whatever follows a bad record would be lost too
    last->AddComponent<Mover>()->velocity = {1, 2, 3};
    Scene b("b");
    CHECK(RoundTrip(a, b));
    CHECK(b.Objects().size() == a.Objects().size());
    for (std::size_t i = 0; i < names.size() && i < b.Objects().size(); ++i) {
        GameObject* o = b.Objects()[i].get();
        CHECK(o->name == names[i]);
        CHECK(o->tag == names[i]);
        auto* tr = o->GetComponent<TextRenderer>();
        CHECK(tr && tr->text == names[i]);
        auto* au = o->GetComponent<AudioSource>();
        CHECK(au && au->clipPath == names[i]);
    }
    CHECK(Find(b, "after") && Find(b, "after")->GetComponent<Mover>() && Find(b, "after")->GetComponent<Mover>()->velocity.z == 3.0f);
    // The scene's own name goes through the same quoting.
    Scene c("a \"quoted\"\nname");
    Scene d("d");
    CHECK(RoundTrip(c, d));
    CHECK(d.Name() == c.Name());
}

static void TestLineBreaksInKeyValueRecords() {
    RUN_SUITE("text: a line break inside a quoted value of a key=value record");
    Scene a("a");
    GameObject* o = a.CreateGameObject("Quests");
    auto* ql = o->AddComponent<QuestLog>();
    QuestDef q;
    q.id = "main"; q.title = "Find the key"; q.description = "First line.\nSecond line with \"quotes\".\n\nFourth.";
    QuestObjective ob; ob.description = "Look\nunder the mat"; ob.target = "key"; ob.count = 2;
    q.objectives.push_back(ob);
    ql->quests.push_back(q);
    QuestDef q2; q2.id = "side"; q2.title = "Second"; q2.description = "single";
    ql->quests.push_back(q2);
    GameObject* after = a.CreateGameObject("After");
    after->AddComponent<Mover>()->velocity = {4, 5, 6};

    Scene b("b");
    std::vector<std::string> warn;
    CHECK(RoundTrip(a, b, nullptr, &warn));
    CHECK(warn.empty());
    GameObject* r = Find(b, "Quests");
    auto* rq = r ? r->GetComponent<QuestLog>() : nullptr;
    CHECK(rq && rq->quests.size() == 2);
    if (rq && rq->quests.size() == 2) {
        CHECK(rq->quests[0].description == q.description);
        CHECK(rq->quests[0].objectives.size() == 1);
        if (rq->quests[0].objectives.size() == 1) CHECK(rq->quests[0].objectives[0].description == "Look\nunder the mat" && rq->quests[0].objectives[0].count == 2);
        CHECK(rq->quests[1].id == "side" && rq->quests[1].description == "single");
    }
    CHECK(Find(b, "After") && Find(b, "After")->GetComponent<Mover>() && Find(b, "After")->GetComponent<Mover>()->velocity.z == 6.0f);
}

static void TestUnknownRecordsKept() {
    RUN_SUITE("records of a newer build are warned about, kept and written back");
    const std::string text =
        "okayscene 3\n"
        "futuresetting 7 \"a b\" 2.5\n"
        "name \"S\"\n"
        "gameobject 0 \"O\"\n  active 1\n  transform 0 0 0 0 0 0 1 1 1 1\n"
        "  futurecomp 1 \"x y\" 3.25\n"
        "  sprite 65 1 1 1 1 1 1\n"
        "  futurecomp2 \"two\nlines\" 9\n"
        "  parent -1\nend\n"
        "gameobject 1 \"P\"\n  active 1\n  transform 0 0 0 0 0 0 1 1 1 1\n  parent -1\nend\n";
    std::vector<std::string> warn;
    Scene a("a");
    CHECK(SceneSerializer::Deserialize(a, text, nullptr, &warn));
    CHECK(Has(warn, "skipped unknown field 'futurecomp'") && Has(warn, "skipped unknown record 'futuresetting'"));
    GameObject* o = Find(a, "O");
    CHECK(o && o->unknownRecords.size() == 2);
    if (o && o->unknownRecords.size() == 2) {
        CHECK(o->unknownRecords[0] == "futurecomp 1 \"x y\" 3.25");
        CHECK(o->unknownRecords[1] == "futurecomp2 \"two\nlines\" 9");   // a value that spans lines is one record
    }
    CHECK(Find(a, "P") && Find(a, "P")->unknownRecords.empty());           // the object after it loaded as itself
    CHECK(a.unknownRecords.size() == 1 && a.unknownRecords[0] == "futuresetting 7 \"a b\" 2.5");
    const std::string saved = SceneSerializer::Serialize(a);
    CHECK(saved.find("\nfuturesetting 7 \"a b\" 2.5\n") != std::string::npos);
    CHECK(saved.find("\n  futurecomp 1 \"x y\" 3.25\n") != std::string::npos);
    CHECK(saved.find("\n  futurecomp2 \"two\nlines\" 9\n") != std::string::npos);
    Scene b("b");
    CHECK(SceneSerializer::Deserialize(b, saved));
    CHECK(SceneSerializer::Serialize(b) == saved);                          // stable from the second time on
    // A copy of the object carries them, and so does text pasted back in.
    GameObject* copy = a.Instantiate(*o);
    CHECK(copy && copy->unknownRecords == o->unknownRecords);
    GameObject* pasted = SceneSerializer::InstantiateFromText(a, SceneSerializer::SerializeObject(*o));
    CHECK(pasted && pasted->unknownRecords == o->unknownRecords);
    // Another scene merged in keeps its objects' records and leaves the host's scene-level ones alone.
    Scene host("host");
    CHECK(SceneSerializer::MergeFromText(host, saved));
    CHECK(Find(host, "O") && Find(host, "O")->unknownRecords.size() == 2 && host.unknownRecords.empty());
    // The validator still names what this build does not know.
    const SceneValidationReport report = SceneSerializer::Validate(saved);
    bool named = false;
    for (const auto& issue : report.issues) if (issue.message.find("futurecomp") != std::string::npos) named = true;
    CHECK(named);
    // A new load replaces the scene's records.
    CHECK(SceneSerializer::Deserialize(a, Doc("")));
    CHECK(a.unknownRecords.empty());
    // A stray word is not a record worth keeping; and an object cannot make a scene huge.
    Scene c("c");
    CHECK(SceneSerializer::Deserialize(c, "okayscene 3\nStray Words 1 2\nname \"C\"\n"));
    CHECK(c.unknownRecords.empty());
    std::string many = "okayscene 3\ngameobject 0 \"M\"\n  active 1\n  transform 0 0 0 0 0 0 1 1 1 1\n";
    for (int i = 0; i < 600; ++i) many += "  futurething " + std::to_string(i) + "\n";
    many += "  parent -1\nend\n";
    Scene d("d");
    CHECK(SceneSerializer::Deserialize(d, many));
    CHECK(Find(d, "M") && Find(d, "M")->unknownRecords.size() == 256);
}

static void TestMergeCollisions() {
    RUN_SUITE("merging and cloning with the same names and ids");
    Scene host("host");
    GameObject* x = host.CreateGameObject("X");
    x->AddComponent<Mover>()->velocity = {1, 0, 0};
    GameObject* y = host.CreateGameObject("Y");
    y->transform->SetParent(x->transform, false);
    y->transform->localPosition = {0, 1, 0};
    const std::string guidX = x->Guid(), guidY = y->Guid();
    const std::string text = SceneSerializer::Serialize(host);
    std::vector<GameObject*> roots;
    CHECK(SceneSerializer::MergeFromText(host, text, Vec3{10, 0, 0}, &roots));
    CHECK(roots.size() == 1 && host.Objects().size() == 4);
    CHECK(SceneSerializer::MergeFromText(host, text, Vec3{20, 0, 0}));
    CHECK(host.Objects().size() == 6);
    // The originals keep their ids; every object has its own; each can be found by it.
    CHECK(x->Guid() == guidX && y->Guid() == guidY);
    std::vector<std::string> guids;
    for (const auto& o : host.Objects()) guids.push_back(o->Guid());
    std::sort(guids.begin(), guids.end());
    CHECK(std::unique(guids.begin(), guids.end()) == guids.end());
    for (const auto& o : host.Objects()) CHECK(host.FindByGuid(o->Guid()) == o.get());
    // Names are not changed by a merge: three of each. Each merged child hangs under its own parent.
    CHECK(host.FindGameObjectsWithName("X").size() == 3 && host.FindGameObjectsWithName("Y").size() == 3);
    for (GameObject* child : host.FindGameObjectsWithName("Y")) {
        Transform* par = child->transform->Parent();
        CHECK(par && par->gameObject->name == "X");
    }
    std::vector<float> xs;
    for (GameObject* g : host.FindGameObjectsWithName("X")) xs.push_back(g->transform->Position().x);
    std::sort(xs.begin(), xs.end());
    CHECK(xs.size() == 3 && xs[0] == 0.0f && xs[1] == 10.0f && xs[2] == 20.0f);
    // A copy of an object and everything under it: names stay, ids are new.
    GameObject* copy = host.Instantiate(*x);
    CHECK(copy && copy->Guid() != guidX && copy->transform->Children().size() == 1);
    CHECK(copy && copy->transform->Children()[0]->gameObject->Guid() != guidY);
    GameObject* dup = host.Duplicate(*x);
    CHECK(dup && dup->name == "X (1)" && dup->Guid() != guidX);
    // Text where two objects share an id: both load, the second gets a new one, and it is reported.
    const std::string same =
        "okayscene 3\n"
        "gameobject 0 \"A\"\n  guid \"0123456789abcdef0123456789abcdef\"\n  active 1\n  transform 0 0 0 0 0 0 1 1 1 1\n  parent -1\nend\n"
        "gameobject 1 \"A\"\n  guid \"0123456789abcdef0123456789abcdef\"\n  active 1\n  transform 0 0 0 0 0 0 1 1 1 1\n  parent -1\nend\n";
    Scene d("d");
    std::vector<std::string> warn;
    CHECK(SceneSerializer::Deserialize(d, same, nullptr, &warn));
    CHECK(d.Objects().size() == 2 && d.Objects()[0]->Guid() != d.Objects()[1]->Guid() && Has(warn, "duplicate guid"));
}

int main() {
    TestKeysEveryCode();
    TestKeysTextForm();
    TestKeysOldFiles();
    TestPerInstanceTypes();
    TestOneInstanceFilesStillLoad();
    TestAssetPaths();
    TestFloatRoundTrip();
    TestNamesAndText();
    TestLineBreaksInKeyValueRecords();
    TestUnknownRecordsKept();
    TestMergeCollisions();
    TEST_MAIN_RESULT();
}
