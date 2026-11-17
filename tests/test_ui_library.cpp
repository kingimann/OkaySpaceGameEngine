// UI component library, slice 1: the range slider, the aspect-ratio container and
// the input validator. Headless: layout maths, pointer and keyboard logic, rules, and a
// scene save and load round trip for each.
#include "test_framework.hpp"
#include <Okay.hpp>
#include "okay/Components/UIWidgetsMore.hpp"
#include <string>

using namespace okay;

template <class Check>
static bool RoundTrip(Scene& s, const char* name, Check check) {
    const std::string text = SceneSerializer::Serialize(s);
    Scene back("back");
    std::string err;
    if (!SceneSerializer::Deserialize(back, text, &err, nullptr)) return false;
    if (SceneSerializer::Serialize(back) != text) return false;
    return check(back.Find(name));
}

int main() {
    RUN_SUITE("ui_library");

    // --- Range slider: values, gap, step ---------------------------------------
    {
        Scene s("Range"); s.physicsEnabled = false;
        auto* w = s.CreateGameObject("Price")->AddComponent<UIRangeSlider>();
        w->anchor = UIAnchor::TopLeft; w->position = {0, 0}; w->size = {200, 28};
        w->minValue = 0; w->maxValue = 100; w->low = 20; w->high = 80;
        CHECK_NEAR(w->LowFraction(), 0.2f, 1e-5);
        CHECK_NEAR(w->HighFraction(), 0.8f, 1e-5);
        CHECK(w->SetLow(30));
        CHECK_NEAR(w->low, 30.0f, 1e-5);
        CHECK(!w->SetLow(30));                         // no change reports false
        w->SetLow(95);                                 // cannot pass the high handle
        CHECK_NEAR(w->low, 80.0f, 1e-5);
        w->SetHigh(10);                                // cannot go under the low handle
        CHECK_NEAR(w->high, 80.0f, 1e-5);
        w->minGap = 10; w->low = 20; w->high = 80;
        w->SetLow(79);
        CHECK_NEAR(w->low, 70.0f, 1e-4);               // stops one gap below high
        w->SetHigh(71);
        CHECK_NEAR(w->high, 80.0f, 1e-4);              // 70 + gap = 80, so 71 is held at 80
        w->minGap = 0; w->step = 5; w->low = 0; w->high = 100;
        w->SetLow(22.4f);
        CHECK_NEAR(w->low, 20.0f, 1e-4);
        w->SetHigh(97.6f);
        CHECK_NEAR(w->high, 100.0f, 1e-4);
        w->step = 0; w->wholeNumbers = true;
        w->SetLow(12.7f);
        CHECK_NEAR(w->low, 13.0f, 1e-4);
        w->wholeNumbers = false;
        w->SetRange(70, 30);                           // reversed input is swapped
        CHECK_NEAR(w->low, 30.0f, 1e-4);
        CHECK_NEAR(w->high, 70.0f, 1e-4);
        w->SetRange(-50, 500);                         // and clamped to the track
        CHECK_NEAR(w->low, 0.0f, 1e-4);
        CHECK_NEAR(w->high, 100.0f, 1e-4);
        w->valueFormat = "${lo} to ${hi}";
        w->step = 1; w->SetRange(10, 90);
        CHECK(w->FormatValue() == "$10 to $90");
        w->valueFormat.clear();
        CHECK(w->FormatValue() == "10 - 90");
    }

    // --- Range slider: nearest handle and drag ----------------------------------
    {
        Scene s("RangeDrag"); s.physicsEnabled = false;
        auto* w = s.CreateGameObject("R")->AddComponent<UIRangeSlider>();
        w->low = 20; w->high = 80;
        CHECK(w->NearestHandle(0.1f) == 0);
        CHECK(w->NearestHandle(0.9f) == 1);
        CHECK(w->NearestHandle(0.45f) == 0);
        CHECK(w->NearestHandle(0.55f) == 1);
        w->PressAtFraction(0.9f);
        CHECK(w->ActiveHandle() == 1 && w->IsDragging());
        CHECK_NEAR(w->high, 90.0f, 1e-4);
        w->DragToFraction(0.1f);                       // pulled past the low handle: stops there
        CHECK_NEAR(w->high, 20.0f, 1e-4);
        w->Release();
        CHECK(!w->IsDragging());
        // Two handles stacked on the same value can still be separated either way.
        w->low = 50; w->high = 50;
        w->PressAtFraction(0.2f); CHECK(w->ActiveHandle() == 0);
        w->low = 50; w->high = 50;
        w->PressAtFraction(0.8f); CHECK(w->ActiveHandle() == 1);
        w->interactable = false;
        w->low = 20; w->high = 80;
        w->PressAtFraction(0.0f);
        CHECK_NEAR(w->low, 20.0f, 1e-4);               // a disabled slider ignores the pointer
    }

    // --- Range slider: the real mouse path through scene.Update -----------------
    {
        Scene s("RangeMouse"); s.physicsEnabled = false;
        auto* w = s.CreateGameObject("R")->AddComponent<UIRangeSlider>();
        w->anchor = UIAnchor::TopLeft; w->position = {0, 0}; w->size = {200, 28};
        w->low = 20; w->high = 80;
        s.Start();
        Input::FeedMouse({150, 10}, 0);
        Input::FeedMouse({150, 10}, 1u << 0);          // press near the high handle
        s.Update(0.016f);
        CHECK(w->ActiveHandle() == 1);
        CHECK_NEAR(w->high, 75.0f, 0.5f);
        Input::FeedMouse({180, 10}, 1u << 0);          // drag right
        s.Update(0.016f);
        CHECK_NEAR(w->high, 90.0f, 0.5f);
        Input::FeedMouse({180, 10}, 0);                // release
        s.Update(0.016f);
        CHECK(!w->IsDragging());
        Input::FeedMouse({0, 0}, 0);
    }

    // --- Range slider: keyboard and gamepad -------------------------------------
    {
        Scene s("RangeKeys"); s.physicsEnabled = false;
        auto* w = s.CreateGameObject("R")->AddComponent<UIRangeSlider>();
        w->low = 20; w->high = 80; w->step = 5;
        GameObject* g = w->gameObject;
        CHECK(MoreNav::Focusable(g));
        CHECK(MoreNav::UsesArrows(g));
        w->focusable = false;
        CHECK(!MoreNav::Focusable(g));
        w->focusable = true;
        MoreNav::SetFocused(g, true);
        CHECK(MoreNav::IsFocused(g) && w->IsFocused());
        MoreNav::OnKeys(g, false, true, false);        // right moves the low handle up by one step
        CHECK_NEAR(w->low, 25.0f, 1e-4);
        MoreNav::OnKeys(g, false, false, true);        // activate swaps to the high handle
        CHECK(w->ActiveHandle() == 1);
        MoreNav::OnKeys(g, true, false, false);
        CHECK_NEAR(w->high, 75.0f, 1e-4);
        CHECK(IsUIFocused(g));
        CHECK(UIDefaultLayer(g) == uilayers::K_RangeSlider);
    }

    // --- Range slider survives save and load ------------------------------------
    {
        Scene s("RangeSave"); s.physicsEnabled = false;
        auto* w = s.CreateGameObject("R")->AddComponent<UIRangeSlider>();
        w->minValue = 10; w->maxValue = 60; w->low = 15; w->high = 45; w->step = 5; w->minGap = 5;
        w->wholeNumbers = true; w->showValue = false; w->valueFormat = "{lo}..{hi} \"gold\"";
        w->fill = Color(0.1f, 0.9f, 0.3f, 1.0f); w->size = {321, 30};
        CHECK(RoundTrip(s, "R", [](GameObject* g) {
            auto* b = g ? g->GetComponent<UIRangeSlider>() : nullptr;
            return b && b->minValue == 10 && b->maxValue == 60 && b->low == 15 && b->high == 45 && b->step == 5
                && b->minGap == 5 && b->wholeNumbers && !b->showValue && b->valueFormat == "{lo}..{hi} \"gold\""
                && b->size.x == 321 && b->fill.g > 0.89f;
        }));
    }

    // --- Aspect ratio maths ------------------------------------------------------
    {
        UIAspectRatio a;
        Vec2 o, sz;
        a.ratio = 16.0f / 9.0f;
        a.Resolve({0, 0}, {1000, 1000}, o, sz);        // a tall space: width limits, bars above and below
        CHECK_NEAR(sz.x, 1000.0f, 1e-3); CHECK_NEAR(sz.y, 562.5f, 1e-2);
        CHECK_NEAR(o.x, 0.0f, 1e-3);     CHECK_NEAR(o.y, 218.75f, 1e-2);
        a.Resolve({0, 0}, {2000, 500}, o, sz);         // a wide space: height limits, bars at the sides
        CHECK_NEAR(sz.y, 500.0f, 1e-3);  CHECK_NEAR(sz.x, 888.889f, 1e-2);
        CHECK_NEAR(o.x, 555.5555f, 1e-2); CHECK_NEAR(o.y, 0.0f, 1e-3);
        a.Resolve({100, 50}, {1600, 900}, o, sz);      // exactly the right shape: fills it
        CHECK_NEAR(sz.x, 1600.0f, 1e-2); CHECK_NEAR(sz.y, 900.0f, 1e-2);
        CHECK_NEAR(o.x, 100.0f, 1e-2);   CHECK_NEAR(o.y, 50.0f, 1e-2);
        a.mode = UIAspectRatio::Mode::Fill;            // covers the space: bigger than it on one side
        a.Resolve({0, 0}, {1000, 1000}, o, sz);
        CHECK_NEAR(sz.y, 1000.0f, 1e-3); CHECK_NEAR(sz.x, 1777.78f, 1e-1);
        CHECK(o.x < 0.0f);
        a.mode = UIAspectRatio::Mode::WidthControls;
        a.Resolve({0, 0}, {800, 100}, o, sz);
        CHECK_NEAR(sz.x, 800.0f, 1e-3);  CHECK_NEAR(sz.y, 450.0f, 1e-2);
        a.mode = UIAspectRatio::Mode::HeightControls;
        a.Resolve({0, 0}, {100, 450}, o, sz);
        CHECK_NEAR(sz.y, 450.0f, 1e-3);  CHECK_NEAR(sz.x, 800.0f, 1e-2);
        a.mode = UIAspectRatio::Mode::Fit; a.ratio = 1.0f;
        a.alignX = 0.0f; a.alignY = 1.0f;              // square, left-top-bottom aligned
        a.Resolve({0, 0}, {1000, 400}, o, sz);
        CHECK_NEAR(sz.x, 400.0f, 1e-3); CHECK_NEAR(o.x, 0.0f, 1e-3); CHECK_NEAR(o.y, 0.0f, 1e-3);
        a.alignX = 1.0f;
        a.Resolve({0, 0}, {1000, 400}, o, sz);
        CHECK_NEAR(o.x, 600.0f, 1e-3);
        a.alignX = 0.5f; a.padding = 20.0f;
        a.Resolve({0, 0}, {1000, 400}, o, sz);
        CHECK_NEAR(sz.x, 360.0f, 1e-3); CHECK_NEAR(sz.y, 360.0f, 1e-3);
        CHECK_NEAR(o.y, 20.0f, 1e-3);
        a.Resolve({0, 0}, {10, 10}, o, sz);            // padding bigger than the space: never negative
        CHECK(sz.x >= 0.0f && sz.y >= 0.0f);
        a.ratio = -3.0f; a.padding = 0.0f;             // a bad ratio does not divide by zero
        a.Resolve({0, 0}, {100, 100}, o, sz);
        CHECK(sz.x > 0.0f && sz.y > 0.0f);
        a.SetRatio(4, 3); CHECK_NEAR(a.ratio, 1.3333f, 1e-3);
        a.SetRatio(0, 3); CHECK_NEAR(a.ratio, 1.3333f, 1e-3);
    }

    // --- Aspect ratio as a container: children follow it on three screens --------
    {
        Scene s("Aspect"); s.physicsEnabled = false;
        GameObject* canvas = s.CreateGameObject("Canvas");
        canvas->AddComponent<Canvas>();
        GameObject* box = s.CreateGameObject("Frame");
        box->transform->SetParent(canvas->transform);
        box->AddComponent<UIAspectRatio>()->ratio = 16.0f / 9.0f;
        GameObject* kid = s.CreateGameObject("Kid");
        kid->transform->SetParent(box->transform);
        auto* p = kid->AddComponent<UIPanel>();
        p->anchor = UIAnchor::StretchFull; p->position = {0, 0}; p->size = {0, 0};   // fills the frame
        struct Screen { float w, h; };
        const Screen screens[] = {{1920, 1080}, {1080, 1920}, {2560, 1080}, {800, 600}};
        for (const Screen& sc : screens) {
            Vec2 o, sz;
            CHECK(GetUIScreenRect(kid, sc.w, sc.h, o, sz));
            CHECK_NEAR(sz.x / sz.y, 16.0f / 9.0f, 1e-3);                              // always the right shape
            CHECK(o.x >= -0.01f && o.y >= -0.01f && o.x + sz.x <= sc.w + 0.5f && o.y + sz.y <= sc.h + 0.5f);   // inside the screen
            CHECK_NEAR(o.x + sz.x * 0.5f, sc.w * 0.5f, 0.5f);                          // centred
            CHECK_NEAR(o.y + sz.y * 0.5f, sc.h * 0.5f, 0.5f);
            const Vec2 ro = UIResolveOrigin(kid, sc.w, sc.h), rs = UIResolveSize(kid, sc.w, sc.h);
            CHECK_NEAR(rs.x / rs.y, 16.0f / 9.0f, 1e-3);
            CHECK_NEAR(ro.x + rs.x * 0.5f, sc.w * 0.5f, 0.5f);
        }
        box->GetComponent<UIAspectRatio>()->SetRatio(1, 1);
        box->GetComponent<UIAspectRatio>()->mode = UIAspectRatio::Mode::HeightControls;
        box->GetComponent<UIAspectRatio>()->alignX = 0.0f;
        box->GetComponent<UIAspectRatio>()->padding = 8.0f;
        CHECK(RoundTrip(s, "Frame", [](GameObject* g) {
            auto* a = g ? g->GetComponent<UIAspectRatio>() : nullptr;
            return a && a->ratio == 1.0f && a->mode == UIAspectRatio::Mode::HeightControls && a->alignX == 0.0f && a->padding == 8.0f;
        }));
    }

    // --- Input validator rules ----------------------------------------------------
    {
        UIInputValidator v;
        v.required = true;
        CHECK(!v.Validate("").empty());
        v.required = false;
        CHECK(v.Validate("").empty());                 // optional and empty is fine
        v.required = true;
        v.minLength = 3; v.maxLength = 6;
        CHECK(!v.Validate("ab").empty());
        CHECK(v.Validate("abc").empty());
        CHECK(!v.Validate("abcdefg").empty());
        CHECK(v.Validate("\xC3\xA9t\xC3\xA9").empty());  // "ete" with accents is 3 characters, not 5 bytes
        v.minLength = 0; v.maxLength = 0;
        v.kind = UIInputValidator::Kind::Email;
        CHECK(v.Validate("me@site.com").empty());
        CHECK(!v.Validate("me@site").empty());
        CHECK(!v.Validate("@site.com").empty());
        CHECK(!v.Validate("me@@site.com").empty());
        CHECK(!v.Validate("m e@site.com").empty());
        CHECK(!v.Validate("me@site.").empty());
        v.kind = UIInputValidator::Kind::Integer;
        CHECK(v.Validate("42").empty());
        CHECK(v.Validate("-7").empty());
        CHECK(!v.Validate("4.2").empty());
        CHECK(!v.Validate("-").empty());
        CHECK(!v.Validate("12a").empty());
        v.useRange = true; v.minNumber = 1; v.maxNumber = 10;
        CHECK(v.Validate("10").empty());
        CHECK(!v.Validate("11").empty());
        CHECK(!v.Validate("0").empty());
        CHECK(v.Validate("11") == "Between 1 and 10");
        v.kind = UIInputValidator::Kind::Number;
        CHECK(v.Validate("2.5").empty());
        CHECK(!v.Validate("nan").empty());             // text that strtod would accept is still refused
        CHECK(!v.Validate("0x10").empty());
        CHECK(!v.Validate("inf").empty());
        CHECK(!v.Validate("10.5").empty());
        v.useRange = false;
        v.kind = UIInputValidator::Kind::Letters;
        CHECK(v.Validate("Mary-Ann O'Neil").empty());
        CHECK(!v.Validate("R2D2").empty());
        v.kind = UIInputValidator::Kind::LettersAndDigits;
        CHECK(v.Validate("R2D2").empty());
        CHECK(!v.Validate("R2 D2").empty());
        v.kind = UIInputValidator::Kind::Any;
        v.mustMatch = "Pass";
        CHECK(!v.Validate("hunter2", "hunter3").empty());
        CHECK(v.Validate("hunter2", "hunter2").empty());
        v.message = "Pick a better one";
        CHECK(v.Validate("hunter2", "x") == "Pick a better one");
    }

    // --- Validator on a real field: typing, touched, match another field ----------
    {
        Scene s("Validate"); s.physicsEnabled = false;
        GameObject* pass = s.CreateGameObject("Pass");
        pass->AddComponent<UIInputField>()->text = "secret";
        GameObject* again = s.CreateGameObject("Again");
        auto* in = again->AddComponent<UIInputField>();
        auto* v = again->AddComponent<UIInputValidator>();
        v->mustMatch = "Pass";
        s.Start();
        s.Update(0.016f);
        CHECK(!v->IsValid());                          // empty and required
        CHECK(!v->ShowsError());                       // ...but quiet until the player has typed
        in->text = "sec";
        s.Update(0.016f);
        CHECK(v->Touched());
        CHECK(v->ShowsError());
        CHECK(v->Error() == "The two entries do not match");
        in->text = "secret";
        s.Update(0.016f);
        CHECK(v->IsValid() && !v->ShowsError());
        v->showOk = true;
        CHECK(v->ShowsOk());
        pass->GetComponent<UIInputField>()->text = "changed";
        s.Update(0.016f);
        CHECK(!v->IsValid());                          // follows the other field too
        CHECK(UIDefaultLayer(again) == uilayers::K_Input);
        std::vector<UIDrawItem> items;
        UIQueueDrawItems(again, 0, items);
        bool sawValidator = false;
        for (const auto& it : items) if (it.kind == uilayers::K_Validator) sawValidator = true;
        CHECK(sawValidator);
    }

    // --- Validator survives save and load ----------------------------------------
    {
        Scene s("ValSave"); s.physicsEnabled = false;
        GameObject* g = s.CreateGameObject("Mail");
        g->AddComponent<UIInputField>();
        auto* v = g->AddComponent<UIInputValidator>();
        v->kind = UIInputValidator::Kind::Number; v->required = false; v->minLength = 2; v->maxLength = 9;
        v->useRange = true; v->minNumber = -5; v->maxNumber = 5; v->mustMatch = "Other"; v->message = "Say \"no\"";
        v->quietUntilTouched = false; v->showMessage = false; v->showOk = true;
        CHECK(RoundTrip(s, "Mail", [](GameObject* o) {
            auto* b = o ? o->GetComponent<UIInputValidator>() : nullptr;
            return b && b->kind == UIInputValidator::Kind::Number && !b->required && b->minLength == 2 && b->maxLength == 9
                && b->useRange && b->minNumber == -5 && b->maxNumber == 5 && b->mustMatch == "Other"
                && b->message == "Say \"no\"" && !b->quietUntilTouched && !b->showMessage && b->showOk;
        }));
    }

    Input::FeedKeys({});
    Input::FeedMouse({0, 0}, 0);
    TEST_MAIN_RESULT();
}
