#pragma once
// The UI component library example scenes (File > Examples > UI library, and examples/ui_library/*.okayscene).
//
// BuildSlice1() fills a scene with the parts added in slice 1 (range slider, input validation, aspect-ratio
// container) next to plain on-screen instructions. Written for this engine from its own UI helpers
// (UITemplates, UIGallery's card helpers); no other project's code or assets were used.
#include "okay/Scene/UIGallery.hpp"
#include "okay/Components/UIWidgetsMore.hpp"
#include <string>

namespace okay { namespace UILibrary {

using namespace okay::UITemplates;
using namespace okay::UITemplates::detail;
namespace gx = okay::UIGallery::gx;   // the gallery's card, label and button helpers

namespace detail2 {
inline GameObject* Field(Scene& s, GameObject* parent, const char* name, Vec2 pos, Vec2 size, const char* placeholder,
                         UIInputValidator::Kind kind) {
    GameObject* g = Child(s, parent, name);
    auto* in = g->AddComponent<UIInputField>();
    in->anchor = UIAnchor::TopLeft; in->position = pos; in->size = size; in->placeholder = placeholder;
    in->color = slate::Field(); in->focusColor = slate::FieldHover(); in->borderWidth = 1.0f;
    auto* v = g->AddComponent<UIInputValidator>();
    v->kind = kind;
    return g;
}
/// A few lines of small grey text that wrap inside `width`.
inline GameObject* Note(Scene& s, GameObject* parent, const char* name, const std::string& t, Vec2 pos, float width, float height) {
    GameObject* g = Text(s, parent, name, t, UIAnchor::TopLeft, pos, {width, height}, 13.0f, slate::Dim());
    auto* tr = g->GetComponent<TextRenderer>();
    tr->wrap = true; tr->vcenter = false;
    return g;
}
} // namespace detail2

/// Slice 1: essentials and layout. Returns the page root.
inline GameObject* BuildSlice1(Scene& scene) {
    GameObject* canvas = EnsureCanvas(scene);
    if (Canvas* cv = canvas->GetComponent<Canvas>()) cv->referenceResolution = {1280.0f, 720.0f};
    GameObject* root = Group(scene, canvas, "UI Library 1");
    Panel(scene, root, "Backdrop", UIAnchor::StretchFull, {0, 0}, {0, 0}, slate::Backdrop(), 0.0f);
    scene.Find("Backdrop")->GetComponent<UIPanel>()->borderWidth = 0.0f;
    Text(scene, root, "Title", "UI Library 1", UIAnchor::TopLeft, {32, 12}, {600, 40}, 30.0f, slate::Text(), 0, true);
    detail2::Note(scene, root, "Subtitle",
         "Essentials and layout. Drag the handles. Click a box and type (try a wrong email). Make the window taller or wider: the blue frame keeps its shape.",
         {34, 50}, 1200, 40);

    // ---- Card 1: range slider -------------------------------------------------------------
    GameObject* c1 = gx::Card(scene, root, "Range", "Range slider", {32, 104}, {392, 330});
    gx::Label(scene, c1, "PriceLabel", "Price range", {20, 44}, 200);
    {
        GameObject* g = Child(scene, c1, "Price");
        auto* w = g->AddComponent<UIRangeSlider>();
        w->anchor = UIAnchor::TopLeft; w->position = {20, 96}; w->size = {352, 30};
        w->minValue = 0; w->maxValue = 500; w->low = 120; w->high = 380; w->step = 10;
        w->valueFormat = "${lo} to ${hi}"; w->fill = slate::Accent();
    }
    gx::Label(scene, c1, "LevelLabel", "Level range (3 apart)", {20, 156}, 352);
    {
        GameObject* g = Child(scene, c1, "Level");
        auto* w = g->AddComponent<UIRangeSlider>();
        w->anchor = UIAnchor::TopLeft; w->position = {20, 208}; w->size = {352, 30};
        w->minValue = 1; w->maxValue = 60; w->low = 10; w->high = 25; w->wholeNumbers = true; w->minGap = 3;
        w->valueFormat = "Levels {lo} to {hi}"; w->fill = slate::Success();
    }
    detail2::Note(scene, c1, "RangeHint", "Drag a handle. They never cross. Left and right move one, Enter swaps.", {20, 262}, 352, 60);

    // ---- Card 2: input validation ---------------------------------------------------------
    GameObject* c2 = gx::Card(scene, root, "Form", "Input validation", {448, 104}, {392, 500});
    gx::Label(scene, c2, "EmailLabel", "Email", {20, 44}, 200);
    GameObject* email = detail2::Field(scene, c2, "Email", {20, 66}, {352, 38}, "name@site.com", UIInputValidator::Kind::Email);
    email->GetComponent<UIInputValidator>()->showOk = true;
    gx::Label(scene, c2, "AgeLabel", "Age (13 to 120)", {20, 138}, 200);
    GameObject* age = detail2::Field(scene, c2, "Age", {20, 160}, {352, 38}, "a whole number", UIInputValidator::Kind::Integer);
    {
        auto* v = age->GetComponent<UIInputValidator>();
        v->useRange = true; v->minNumber = 13; v->maxNumber = 120; v->showOk = true;
        age->GetComponent<UIInputField>()->contentType = UIInputField::ContentType::Standard;
    }
    gx::Label(scene, c2, "PassLabel", "Password (6 or more)", {20, 232}, 352);
    GameObject* pass = detail2::Field(scene, c2, "Password", {20, 254}, {352, 38}, "choose a password", UIInputValidator::Kind::Any);
    pass->GetComponent<UIInputField>()->contentType = UIInputField::ContentType::Password;
    pass->GetComponent<UIInputValidator>()->minLength = 6;
    gx::Label(scene, c2, "RepeatLabel", "Repeat the password", {20, 326}, 352);
    GameObject* rep = detail2::Field(scene, c2, "Repeat", {20, 348}, {352, 38}, "type it again", UIInputValidator::Kind::Any);
    rep->GetComponent<UIInputField>()->contentType = UIInputField::ContentType::Password;
    {
        auto* v = rep->GetComponent<UIInputValidator>();
        v->mustMatch = "Password"; v->message = "That is not the same password"; v->showOk = true;
    }
    detail2::Note(scene, c2, "FormHint", "A red ring and a short sentence appear once you type a wrong value. A green ring means the box is fine.", {20, 404}, 352, 60);

    // ---- Card 3: aspect ratio, the card is as tall as the window ------------------------------
    GameObject* c3 = Panel(scene, root, "Aspect", UIAnchor::StretchVertical, {864, 104}, {384, 32}, slate::Panel(), 14.0f, true);
    c3->GetComponent<UIPanel>()->blocksPointer = true;
    Caption(scene, c3, "Caption", "Aspect ratio box (16:9)", UIAnchor::TopLeft, {20, 14}, 344, slate::Accent());
    {
        // A transparent holder that leaves room for the caption on top and the hint at the bottom; the box fits inside it.
        GameObject* holder = Panel(scene, c3, "Holder", UIAnchor::StretchFull, {0, 44}, {0, 90}, Color(0, 0, 0, 0), 0.0f);
        holder->GetComponent<UIPanel>()->borderWidth = 0.0f;
        GameObject* area = Child(scene, holder, "Area");
        auto* ar = area->AddComponent<UIAspectRatio>();
        ar->ratio = 16.0f / 9.0f; ar->padding = 20.0f; ar->alignY = 0.0f;
        GameObject* frame = Panel(scene, area, "Frame", UIAnchor::StretchFull, {0, 0}, {0, 0}, Color::FromBytes(54, 92, 150), 10.0f);
        auto* fp = frame->GetComponent<UIPanel>();
        fp->borderWidth = 3.0f; fp->borderColor = Color::FromBytes(230, 236, 250);
        GameObject* dot = Child(scene, frame, "Dot");
        auto* dp = dot->AddComponent<UIPanel>();
        dp->anchor = UIAnchor::Center; dp->position = {0, 0}; dp->size = {56, 56}; dp->shape = UIShape::Circle;
        dp->color = Color::FromBytes(250, 200, 80);
        Text(scene, frame, "Label", "16 : 9", UIAnchor::BottomCenter, {0, -14}, {200, 24}, 18.0f, Color::White, 1, true);
    }
    // The bottom of the card carries the hint so it stays readable at any window height.
    {
        GameObject* hint = detail2::Note(scene, c3, "Hint", "The frame stays 16:9 on a phone, a tablet and an ultrawide screen.", {0, 0}, 344, 50);
        auto* tr = hint->GetComponent<TextRenderer>();
        tr->anchor = UIAnchor::BottomLeft; tr->screenPos = {20, -70};
    }

    // ---- Notes ---------------------------------------------------------------------------------
    GameObject* c4 = gx::Card(scene, root, "More", "Also in the library", {32, 448}, {392, 170});
    detail2::Note(scene, c4, "More1", "More parts: File > Examples > UI Gallery.", {20, 44}, 352, 40);
    detail2::Note(scene, c4, "More2", "Add any part: GameObject > UI > Controls, or Layout & Overlays.", {20, 96}, 352, 60);
    return root;
}

}} // namespace okay::UILibrary
