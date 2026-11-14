// Subtitles, hold-to-toggle and aim assist: each one has to change what the game
// does. A caption the widget does not place, a hold that still releases, or a
// stick that is not pulled toward a target fails here.
#include "test_framework.hpp"
#include <Okay.hpp>
#include "okay/Components/CaptionOverlay.hpp"
#include "okay/Components/SettingsScreen.hpp"
#include "okay/Components/AudioSource.hpp"
#include "okay/Scene/UITemplates.hpp"
#include "okay/UI/UIRenderer.hpp"

#include <cmath>
#include <string>
#include <vector>

using namespace okay;

namespace {

class RecPainter : public ui2d::Painter {
public:
    int fills = 0;
    int glyphs = 0;
    void SetDrawColor(std::uint8_t, std::uint8_t, std::uint8_t, std::uint8_t) override {}
    void FillRect(const ui2d::Rect* r) override { if (r && r->w > 0 && r->h > 0) ++fills; }
    void DrawRect(const ui2d::Rect*) override {}
    void DrawLine(int, int, int, int) override {}
    void DrawPoint(int, int) override {}
    void SetClipRect(const ui2d::Rect*) override {}
    void GetClipRect(ui2d::Rect* r) override { if (r) *r = {}; }
    bool IsClipEnabled() override { return false; }
    void SetScale(float, float) override {}
    ui2d::Tex LoadTexture(const std::string&) override { return nullptr; }
    ui2d::Tex FontAtlas(TtfFont*) override { return reinterpret_cast<ui2d::Tex>(1); }
    void QueryTexture(ui2d::Tex, int* w, int* h) override { if (w) *w = 0; if (h) *h = 0; }
    void SetTextureColorMod(ui2d::Tex, std::uint8_t, std::uint8_t, std::uint8_t) override {}
    void SetTextureAlphaMod(ui2d::Tex, std::uint8_t) override {}
    void Copy(ui2d::Tex, const ui2d::Rect*, const ui2d::Rect*) override {}
    void CopyF(ui2d::Tex, const ui2d::Rect*, const ui2d::FRect*) override { ++glyphs; }
    void CopyEx(ui2d::Tex, const ui2d::Rect*, const ui2d::Rect*, double, const ui2d::Point*, int) override {}
    void CopyExF(ui2d::Tex, const ui2d::Rect*, const ui2d::FRect*, double, const ui2d::FPoint*, int) override {}
    void Geometry(ui2d::Tex, const ui2d::Vertex*, int, const int*, int) override {}
};

bool MarkHas(const std::vector<CaptionOverlay::Mark>& marks, const char* bit) {
    for (const auto& m : marks)
        if (!m.box && m.text.find(bit) != std::string::npos) return true;
    return false;
}

FirstPersonController* MakeFp(Scene& s) {
    s.physicsEnabled = false;
    GameObject* p = s.CreateGameObject("Player");
    auto* fp = p->AddComponent<FirstPersonController>();
    fp->lockCursor = false;
    fp->toggleRun = false;
    fp->toggleStance = false;
    fp->headBob = false;
    GameObject* cam = s.CreateGameObject("Eye");
    cam->AddComponent<Camera>();
    cam->transform->SetParent(p->transform, false);
    cam->transform->localPosition = {0, fp->standEyeHeight, 0};
    s.Start();
    Input::SetUICaptured(false);
    Input::FeedKeys({});
    Input::FeedMouse({200, 200}, 0);
    Input::FeedGamepad({0, 0}, 0);
    Input::FeedGamepadExtra({0, 0}, 0, 0, true);
    s.Update(1.0f / 60.0f);
    return fp;
}

} // namespace

int main() {
    RUN_SUITE("accessibility_remaining");
    UICanvas::Set(1280, 720);
    Accessibility::Reset();
    CaptionManager::Main().Clear();
    Input::SetUICaptured(false);

    {   // The widget places a spoken line, and draws it. Off, it places nothing.
        Accessibility::Reset();
        CaptionManager::Main().Clear();
        Scene s("caps"); s.physicsEnabled = false;
        CaptionOverlay::Ensure(s);
        s.Start();
        CHECK(CaptionManager::Main().Say("Ada", "Hello there") != 0);
        s.Update(0.12f);
        auto* cap = s.FindObjectOfType<CaptionOverlay>();
        CHECK(cap != nullptr);
        std::vector<CaptionOverlay::Mark> marks;
        if (cap) cap->Layout(1280, 720, marks);
        CHECK(MarkHas(marks, "Hello there"));
        CHECK(MarkHas(marks, "Ada"));
        bool box = false;
        float low = 0.0f;
        for (const auto& m : marks) { if (m.box) box = true; low = std::max(low, m.y); }
        CHECK(box);
        CHECK(low > 400.0f);                                          // bottom of a 720-tall screen
        std::vector<UIDrawItem> queued;
        if (cap && cap->gameObject) UIQueueDrawItems(cap->gameObject, 0, queued);
        bool queuedCaption = false;
        for (const auto& it : queued) if (it.kind == uilayers::K_Caption) queuedCaption = true;
        CHECK(queuedCaption);
        RecPainter on;
        ui2d::RenderSceneUI(s, on, 1280, 720);
        CHECK(on.fills >= 1);                                         // the caption box was painted
        CHECK(on.glyphs > 8);                                         // the words were submitted as glyphs

        SubtitleSettings off = Accessibility::Get().subtitles;
        off.enabled = false;
        Accessibility::SetSubtitles(off);
        CaptionManager::Main().Clear();
        CHECK(CaptionManager::Main().Say("Ada", "Hello there") == 0);
        s.Update(0.12f);
        marks.clear();
        if (cap) cap->Layout(1280, 720, marks);
        CHECK(marks.empty());
        RecPainter quiet;
        ui2d::RenderSceneUI(s, quiet, 1280, 720);
        CHECK(quiet.fills == 0 && quiet.glyphs == 0);
        Accessibility::Reset();
        CaptionManager::Main().Clear();
    }

    {   // A sound caption is an audio event, and it waits for the setting.
        Accessibility::Reset();
        CaptionManager::Main().Clear();
        Scene s("snd"); s.physicsEnabled = false;
        CaptionOverlay::Ensure(s);
        auto* src = s.CreateGameObject("Door")->AddComponent<AudioSource>();
        src->caption = "door slams";
        s.Start();
        src->Play();
        s.Update(0.12f);
        auto* cap = s.FindObjectOfType<CaptionOverlay>();
        std::vector<CaptionOverlay::Mark> marks;
        if (cap) cap->Layout(800, 600, marks);
        CHECK(marks.empty());                                         // sound captions default off
        SubtitleSettings sub = Accessibility::Get().subtitles;
        sub.soundCaptions = true;
        Accessibility::SetSubtitles(sub);
        src->Play();
        s.Update(0.12f);
        if (cap) cap->Layout(800, 600, marks);
        CHECK(MarkHas(marks, "door slams"));
        Accessibility::Reset();
        CaptionManager::Main().Clear();
    }

    {   // A cutscene subtitle is handed to the caption manager.
        Accessibility::Reset();
        CaptionManager::Main().Clear();
        Scene s("seq"); s.physicsEnabled = false;
        CaptionOverlay::Ensure(s);
        Sequence seq;
        seq.length = 4.0f;
        SeqEventTrack tr;
        SeqEvent e;
        e.kind = SeqEvent::Kind::Subtitle;
        e.time = 0.0f;
        e.name = "Guide";
        e.text = "Welcome home";
        e.value = 3.0f;
        tr.events.push_back(e);
        seq.events.push_back(tr);
        auto* sp = s.CreateGameObject("Cut")->AddComponent<SequencePlayer>();
        sp->lockPlayer = false;
        sp->skipKey = 0;
        sp->SetSequence(seq);
        s.Start();
        CHECK(sp->Play());
        s.Update(0.25f);
        auto* cap = s.FindObjectOfType<CaptionOverlay>();
        std::vector<CaptionOverlay::Mark> marks;
        if (cap) cap->Layout(1280, 720, marks);
        CHECK(MarkHas(marks, "Welcome home"));
        CHECK(MarkHas(marks, "Guide"));
        const auto card = SequencePlayer::BuildOverlay(sp->Overlay(), 1280, 720, true);
        const auto bare = SequencePlayer::BuildOverlay(sp->Overlay(), 1280, 720, false);
        bool cardText = false, bareText = false;
        for (const auto& c : card) if (c.kind == LoadingDrawCmd::Kind::Text && c.text == "Welcome home") cardText = true;
        for (const auto& c : bare) if (c.kind == LoadingDrawCmd::Kind::Text) bareText = true;
        CHECK(cardText && !bareText);                                 // the widget replaces the old card
        Accessibility::Reset();
        CaptionManager::Main().Clear();
    }

    {   // Settings rows set the three new choices and Done saves them. Missing rows still load.
        Accessibility::Reset();
        Prefs::Clear();
        CaptionManager::Main().Clear();
        Scene s("Settings");
        GameObject* settings = UITemplates::AddSettings(s);
        s.Start();
        settings->active = true;
        auto* screen = settings->GetComponent<SettingsScreen>();
        CHECK(screen != nullptr);
        screen->Update(0.016f);
        auto tg = [&](const char* n) { GameObject* g = s.Find(n); return g ? g->GetComponent<UIToggle>() : nullptr; };
        auto dd = [&](const char* n) { GameObject* g = s.Find(n); return g ? g->GetComponent<UIDropdown>() : nullptr; };
        auto* subs = tg("SubtitlesToggle");
        auto* sounds = tg("SoundCaptionsToggle");
        auto* holds = tg("HoldToggleToggle");
        auto* aim = dd("AimAssistDropdown");
        CHECK(subs && sounds && holds && aim);
        if (subs && sounds && holds && aim) {
            CHECK(subs->on && !sounds->on && !holds->on && aim->value == 2);   // speech on, captions off, assist medium
            subs->on = false;     screen->Update(0.016f); CHECK(!Accessibility::Get().subtitles.enabled);
            sounds->on = true;    screen->Update(0.016f); CHECK(Accessibility::Get().subtitles.soundCaptions);
            holds->on = true;     screen->Update(0.016f); CHECK(Accessibility::Get().toggleAllHolds);
            aim->value = 0;       screen->Update(0.016f); CHECK(Accessibility::Get().aimAssist == AimAssist::Off);
            CHECK(Accessibility::AimAssistStrength() == 0.0f);
            aim->value = 3;       screen->Update(0.016f); CHECK(Accessibility::Get().aimAssist == AimAssist::High);
            CHECK(Accessibility::AimAssistStrength() == 1.0f);

            GameObject* done = s.Find("BackButton");
            auto* btn = done ? done->GetComponent<UIButton>() : nullptr;
            UIWorld().rectOf = [](GameObject* go, Vec2& o, Vec2& sz) { return GetUIScreenRect(go, 1280, 720, o, sz); };
            Vec2 o, sz;
            if (btn && GetUIScreenRect(done, 1280, 720, o, sz)) {
                const Vec2 c{o.x + sz.x * 0.5f, o.y + sz.y * 0.5f};
                Input::FeedMouse(c, 0);
                Input::FeedMouse(c, 1u);
                btn->Update(0.016f);
                screen->LateUpdate(0.016f);
            }
            UIWorld().rectOf = nullptr;
            Input::FeedMouse({0, 0}, 0);
            CHECK(Prefs::GetInt("a11y.subtitles.enabled") == 0);
            CHECK(Prefs::GetInt("a11y.subtitles.captions") == 1);
            CHECK(Prefs::GetInt("a11y.toggleAll") == 1);
            CHECK(Prefs::GetString("a11y.aimAssist") == "high");
            Accessibility::Reset();
            Accessibility::Load();
            CHECK(!Accessibility::Get().subtitles.enabled && Accessibility::Get().subtitles.soundCaptions);
            CHECK(Accessibility::Get().toggleAllHolds && Accessibility::Get().aimAssist == AimAssist::High);
        }

        // An older Settings object has none of the new rows. It still runs.
        Scene old("old");
        GameObject* root = old.CreateGameObject("Settings");
        root->AddComponent<SettingsScreen>();
        old.Start();
        auto* legacy = root->GetComponent<SettingsScreen>();
        legacy->Update(0.016f);
        CHECK(legacy != nullptr);
        Accessibility::Reset();
        Prefs::Clear();
        CaptionManager::Main().Clear();
    }

    {   // Hold-to-toggle: sprint and aim release when it is off, and stay when it is on.
        Accessibility::Reset();
        auto distanceAfterRelease = [](bool toggle) {
            Scene s("run");
            auto* fp = MakeFp(s);
            if (toggle) Accessibility::SetToggleAction("Sprint", true);
            Input::FeedKeys({'w', Input::KeyShift});
            for (int i = 0; i < 5; ++i) s.Update(1.0f / 60.0f);
            Input::FeedKeys({'w'});
            const Vec3 a = fp->gameObject->transform->Position();
            for (int i = 0; i < 30; ++i) s.Update(1.0f / 60.0f);
            const Vec3 b = fp->gameObject->transform->Position();
            return (b - a).Magnitude();
        };
        const float held = distanceAfterRelease(false);
        const float latched = distanceAfterRelease(true);
        CHECK(held < 3.0f);                                           // walk, about 4.5 m/s * 0.5 s
        CHECK(latched > 3.4f);                                        // still running, about 8 m/s * 0.5 s
        CHECK(latched > held * 1.4f);

        Accessibility::Reset();
        {
            Scene s("aim");
            auto* fp = MakeFp(s);
            Input::FeedMouse({80, 80}, 1u << 1);
            s.Update(1.0f / 60.0f);
            CHECK(fp->IsZooming());
            Input::FeedMouse({80, 80}, 0);
            s.Update(1.0f / 60.0f);
            CHECK(!fp->IsZooming());                                  // hold: releasing the button stops aiming
            Accessibility::SetToggleAction("Aim", true);
            Input::FeedMouse({80, 80}, 1u << 1);
            s.Update(1.0f / 60.0f);
            Input::FeedMouse({80, 80}, 0);
            s.Update(1.0f / 60.0f);
            CHECK(fp->IsZooming());                                   // toggle: the aim stays
            Input::FeedMouse({80, 80}, 1u << 1);
            s.Update(1.0f / 60.0f);
            CHECK(!fp->IsZooming());
        }

        Accessibility::Reset();
        {
            Scene s("crouch");
            auto* fp = MakeFp(s);
            fp->toggleStance = false;
            Input::FeedKeys({Input::KeyCtrl});
            s.Update(1.0f / 60.0f);
            CHECK(fp->stance() == FirstPersonController::Stance::Crouch);
            Input::FeedKeys({});
            s.Update(1.0f / 60.0f);
            CHECK(fp->stance() == FirstPersonController::Stance::Stand);
            Accessibility::SetToggleAllHolds(true);
            Input::FeedKeys({Input::KeyCtrl});
            s.Update(1.0f / 60.0f);
            Input::FeedKeys({});
            s.Update(1.0f / 60.0f);
            CHECK(fp->stance() == FirstPersonController::Stance::Crouch);
        }

        Accessibility::Reset();
        InputActions::Restore();
        Input::FeedKeys({});
        InputActions::Tick();
        CHECK(!InputActions::Get("Sprint"));
        Accessibility::SetToggleAction("Sprint", true);
        Input::FeedKeys({Input::KeyShift});
        InputActions::Tick();
        CHECK(InputActions::Get("Sprint"));
        Input::FeedKeys({});
        InputActions::Tick();
        CHECK(InputActions::Get("Sprint"));                           // the action stays down after the key
        Input::FeedKeys({Input::KeyShift});
        InputActions::Tick();
        CHECK(!InputActions::Get("Sprint"));
        Accessibility::Reset();
        Input::FeedKeys({});
        InputActions::Tick();
        InputActions::Restore();
    }

    {   // Aim assist pulls a moving stick toward a target. Off, and any mouse look, it does not.
        LookAim target;
        target.yaw = -8.0f;
        target.pitch = 0.0f;
        const StickAim plain = ApplyStickAim(0, 0, -0.4f, 0, 0.0f, 1.0f / 60.0f, &target, 1);
        const StickAim helped = ApplyStickAim(0, 0, -0.4f, 0, 1.0f, 1.0f / 60.0f, &target, 1);
        const StickAim idle = ApplyStickAim(0, 0, 0, 0, 1.0f, 1.0f / 60.0f, &target, 1);
        CHECK(!plain.pulled && helped.pulled);
        CHECK(helped.yaw < plain.yaw);                                // closer to -8 than the stick alone
        CHECK(std::fabs(helped.yaw - target.yaw) < std::fabs(plain.yaw - target.yaw));
        CHECK(!idle.pulled && idle.yaw == 0.0f && idle.pitch == 0.0f);

        auto stickDelta = [](AimAssist level) {
            Accessibility::SetAimAssist(level);
            Scene s("stick");
            auto* fp = MakeFp(s);
            GameObject* foe = s.CreateGameObject("Foe");
            foe->AddComponent<HealthStat>();
            const Vec3 eye = s.Find("Eye")->transform->Position();
            const LookAim want = LookAimAt(eye, eye + Vec3{1.392f, 0.0f, -9.903f}, true);
            foe->transform->SetPosition(eye + Vec3{1.392f, -1.2f, -9.903f});   // chest height is +1.2
            fp->yaw = 0.0f;
            fp->pitch = 0.0f;
            Input::FeedGamepadExtra({0.35f, 0.0f}, 0, 0, true);
            for (int i = 0; i < 8; ++i) s.Update(1.0f / 60.0f);
            const float yaw = fp->yaw;
            Input::FeedGamepadExtra({0, 0}, 0, 0, true);
            return std::pair<float, float>{yaw, want.yaw};
        };
        const auto off = stickDelta(AimAssist::Off);
        const auto high = stickDelta(AimAssist::High);
        CHECK(off.first < -0.5f);                                     // the stick did turn the view
        CHECK(std::fabs(high.first - high.second) < std::fabs(off.first - off.second));

        Accessibility::SetAimAssist(AimAssist::High);
        Scene s("mouse");
        auto* fp = MakeFp(s);
        GameObject* foe = s.CreateGameObject("Foe");
        foe->AddComponent<HealthStat>();
        foe->transform->SetPosition({1.4f, 0.4f, -10.0f});
        auto mouseYaw = [&](AimAssist level) {
            Accessibility::SetAimAssist(level);
            Input::FeedGamepadExtra({0, 0}, 0, 0, true);
            Input::FeedMouse({100, 100}, 0);
            s.Update(1.0f / 60.0f);
            fp->yaw = 0.0f;
            Input::FeedMouse({180, 100}, 0);
            s.Update(1.0f / 60.0f);
            return fp->yaw;
        };
        const float mouseOff = mouseYaw(AimAssist::Off);
        const float mouseOn = mouseYaw(AimAssist::High);
        CHECK(std::fabs(mouseOff) > 1.0f);
        CHECK_NEAR(mouseOff, mouseOn, 0.05f);                         // mouse-only feel is not pulled
        Accessibility::Reset();
        Input::FeedGamepadExtra({0, 0}, 0, 0, false);
        Input::FeedKeys({});
        Input::FeedMouse({0, 0}, 0);
    }

    TEST_MAIN_RESULT();
}
