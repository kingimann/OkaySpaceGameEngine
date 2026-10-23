#pragma once
// One-click looks for every scene-UI component, and copy / paste of a widget's style.
// The editor shows the presets as a row of chips at the top of each UI component's
// inspector; "Copy style" / "Paste style" move just the look (colours, corners,
// borders, shadows, fonts) from one widget to another, never its position, size,
// text or behaviour. Presets use the launcher's Slate palette (UITemplates::slate).
#include "okay/Scene/UITemplates.hpp"
#include "okay/Components/UIInputField.hpp"
#include "okay/Components/UITabs.hpp"
#include "okay/Components/UIRadialProgress.hpp"
#include <string>
#include <type_traits>
#include <vector>

namespace okay { namespace UIStyles {

namespace slate = UITemplates::slate;

// ---- Presets --------------------------------------------------------------------
// Each component type has a list of preset names; Apply(component, index) sets the
// style fields only.

inline const std::vector<const char*>& ButtonPresets() {
    static const std::vector<const char*> v = {"Primary", "Secondary", "Danger", "Ghost", "Success", "Outline", "Pill", "Icon (round)"};
    return v;
}
inline void ApplyButton(UIButton* b, int i) {
    if (!b) return;
    b->shape = UIShape::Rounded; b->cornerRadius = 8.0f; b->borderWidth = 0.0f;
    b->shadow = false; b->hoverScale = 1.0f; b->transitionSpeed = 14.0f; b->pressOffset = 1.0f;
    switch (i) {
        case 0: UITemplates::detail::StyleButton(b, UITemplates::ButtonKind::Primary); break;
        case 1: UITemplates::detail::StyleButton(b, UITemplates::ButtonKind::Secondary); break;
        case 2: UITemplates::detail::StyleButton(b, UITemplates::ButtonKind::Danger); break;
        case 3: UITemplates::detail::StyleButton(b, UITemplates::ButtonKind::Ghost); break;
        case 4:
            b->color = slate::Success(); b->hoverColor = Color::FromBytes(96, 212, 148);
            b->pressedColor = Color::FromBytes(56, 168, 106); b->textColor = b->hoverTextColor = Color::White; break;
        case 5:   // outline: transparent fill, accent border + text
            b->color = Color::FromBytes(66, 150, 250, 0); b->hoverColor = Color::FromBytes(66, 150, 250, 36);
            b->pressedColor = Color::FromBytes(66, 150, 250, 60); b->borderWidth = 1.5f; b->borderColor = slate::Accent();
            b->textColor = b->hoverTextColor = Color::FromBytes(140, 192, 255); break;
        case 6:   // pill: primary colours, fully round ends, soft shadow
            UITemplates::detail::StyleButton(b, UITemplates::ButtonKind::Primary);
            b->shape = UIShape::Pill; b->shadow = true; b->shadowColor = Color::FromBytes(20, 60, 120, 120);
            b->shadowOffset = {0.0f, 4.0f}; b->shadowSoftness = 10.0f; break;
        case 7:   // round icon button (put an icon or a 1-2 character label on it)
            UITemplates::detail::StyleButton(b, UITemplates::ButtonKind::Secondary);
            b->shape = UIShape::Circle; b->hoverScale = 1.06f; break;
        default: break;
    }
    if (i != 3 && i != 5) b->disabledColor = Color::FromBytes(51, 56, 71);
}

inline const std::vector<const char*>& PanelPresets() {
    static const std::vector<const char*> v = {"Card", "Raised", "Glass", "Outline", "Pill", "Dim overlay", "Accent", "Gradient"};
    return v;
}
inline void ApplyPanel(UIPanel* p, int i) {
    if (!p) return;
    p->shape = UIShape::Rounded; p->cornerRadius = 12.0f; p->cornerMask = UICornerAll;
    p->borderWidth = 1.0f; p->borderColor = slate::Border();
    p->useGradient = false; p->outlineWidth = 0.0f; p->topHighlight = false; p->shadow = false;
    switch (i) {
        case 0: p->color = slate::Panel(); break;
        case 1:
            p->color = slate::Panel(); p->shadow = true; p->shadowColor = Color::FromBytes(0, 0, 0, 90);
            p->shadowOffset = {0.0f, 6.0f}; p->shadowSoftness = 16.0f; break;
        case 2:   // frosted glass over the game
            p->color = Color::FromBytes(255, 255, 255, 28); p->borderColor = Color::FromBytes(255, 255, 255, 60);
            p->topHighlight = true; p->highlightColor = Color::FromBytes(255, 255, 255, 36); break;
        case 3: p->color = Color::FromBytes(30, 33, 41, 0); p->borderWidth = 1.5f; p->borderColor = Color::FromBytes(255, 255, 255, 60); break;
        case 4: p->shape = UIShape::Pill; p->color = slate::Card(); break;
        case 5:   // full-screen dimmer behind a modal
            p->shape = UIShape::Rectangle; p->cornerRadius = 0.0f; p->borderWidth = 0.0f; p->color = Color(0.0f, 0.0f, 0.0f, 0.6f); break;
        case 6: p->color = Color::FromBytes(66, 150, 250, 40); p->borderColor = Color::FromBytes(66, 150, 250, 150); break;
        case 7:
            p->color = Color::FromBytes(44, 58, 96); p->useGradient = true;
            p->gradientDir = UIPanel::GradientDir::DiagonalDown; p->gradientHorizontal = false;
            p->colorBottom = Color::FromBytes(24, 27, 34); break;
        default: break;
    }
}

inline const std::vector<const char*>& TextPresets() {
    static const std::vector<const char*> v = {"Title", "Heading", "Body", "Caption", "Muted", "Number", "HUD (over game)"};
    return v;
}
inline void ApplyText(TextRenderer* t, int i) {
    if (!t) return;
    t->bold = false; t->italic = false; t->uppercase = false; t->letterSpacing = 0.0f;
    t->outline = false; t->shadow = false; t->gradient = false; t->background = false;
    switch (i) {
        case 0: t->pixelSize = 40.0f / 8.0f; t->bold = true; t->color = slate::Text(); break;
        case 1: t->pixelSize = 24.0f / 8.0f; t->bold = true; t->color = slate::Text(); break;
        case 2: t->pixelSize = 16.0f / 8.0f; t->color = slate::Text(); break;
        case 3: t->pixelSize = 12.0f / 8.0f; t->uppercase = true; t->letterSpacing = 1.0f; t->color = slate::Accent(); break;
        case 4: t->pixelSize = 14.0f / 8.0f; t->color = slate::Dim(); break;
        case 5: t->pixelSize = 28.0f / 8.0f; t->bold = true; t->color = slate::Text(); break;
        case 6:   // readable over any background
            t->pixelSize = 20.0f / 8.0f; t->bold = true; t->color = Color::White; t->shadow = true;
            t->shadowColor = Color::FromBytes(0, 0, 0, 170); t->shadowOffset = {0.25f, 0.25f}; break;
        default: break;
    }
}

inline const std::vector<const char*>& ProgressPresets() {
    static const std::vector<const char*> v = {"Launcher", "Health", "Energy", "XP", "Thin", "Danger"};
    return v;
}
inline void ApplyProgress(UIProgressBar* b, int i) {
    if (!b) return;
    b->shape = UIShape::Pill; b->cornerRadius = 6.0f; b->gradientFill = false;
    b->background = Color::FromBytes(255, 255, 255, 24); b->textColor = Color::White;
    switch (i) {
        case 0: b->fill = slate::Accent(); break;
        case 1: b->fill = slate::Success(); b->gradientFill = true; b->fillEnd = Color::FromBytes(150, 226, 120); break;
        case 2: b->fill = Color::FromBytes(240, 196, 70); b->gradientFill = true; b->fillEnd = Color::FromBytes(255, 226, 120); break;
        case 3: b->fill = Color::FromBytes(170, 110, 240); b->gradientFill = true; b->fillEnd = Color::FromBytes(110, 160, 255); break;
        case 4: b->fill = slate::Accent(); if (b->size.y > 6.0f) b->size.y = 6.0f; break;
        case 5: b->fill = slate::Danger(); b->gradientFill = true; b->fillEnd = Color::FromBytes(255, 140, 90); break;
        default: break;
    }
}

inline const std::vector<const char*>& SliderPresets() {
    static const std::vector<const char*> v = {"Launcher", "Thin", "Chunky", "Volume (green)"};
    return v;
}
inline void ApplySlider(UISlider* s, int i) {
    if (!s) return;
    s->trackShape = UIShape::Pill; s->roundKnob = true; s->cornerRadius = 6.0f;
    s->background = Color::FromBytes(255, 255, 255, 24); s->fill = slate::Accent();
    s->knob = Color::FromBytes(245, 247, 252); s->textColor = slate::Text(); s->knobSize = 1.4f;
    switch (i) {
        case 1: if (s->size.y > 6.0f) s->size.y = 6.0f; s->knobSize = 2.6f; break;
        case 2: if (s->size.y < 18.0f) s->size.y = 18.0f; s->knobSize = 1.2f; break;
        case 3: s->fill = slate::Success(); break;
        default: break;
    }
}

inline const std::vector<const char*>& TogglePresets() {
    static const std::vector<const char*> v = {"Switch", "Checkbox"};
    return v;
}
inline void ApplyToggle(UIToggle* t, int i) {
    if (!t) return;
    t->boxColor = Color::FromBytes(255, 255, 255, 30); t->checkColor = slate::Accent();
    t->textColor = slate::Text(); t->knobColor = Color::FromBytes(245, 247, 252);
    if (i == 0) { t->style = UIToggle::Style::Switch; if (t->size.x < t->size.y * 1.6f) t->size.x = t->size.y * 1.8f; }
    else { t->style = UIToggle::Style::Checkbox; t->cornerRadius = 5.0f; t->size.x = t->size.y; }
}

inline const std::vector<const char*>& InputPresets() {
    static const std::vector<const char*> v = {"Launcher", "Outlined", "Underline"};
    return v;
}
inline void ApplyInput(UIInputField* f, int i) {
    if (!f) return;
    f->shape = UIShape::Rounded; f->cornerRadius = 8.0f; f->textColor = slate::Text();
    f->placeholderColor = slate::Dim(); f->borderColor = slate::Accent();
    switch (i) {
        case 0: f->color = slate::Field(); f->focusColor = slate::FieldHover(); f->borderWidth = 1.5f; break;
        case 1: f->color = Color::FromBytes(30, 33, 41, 0); f->focusColor = Color::FromBytes(255, 255, 255, 10); f->borderWidth = 1.5f; break;
        case 2: f->shape = UIShape::Rectangle; f->cornerRadius = 0.0f; f->color = Color::FromBytes(255, 255, 255, 8);
                f->focusColor = Color::FromBytes(255, 255, 255, 16); f->borderWidth = 1.0f; break;
        default: break;
    }
}

inline const std::vector<const char*>& DropdownPresets() {
    static const std::vector<const char*> v = {"Launcher", "Pill"};
    return v;
}
inline void ApplyDropdown(UIDropdown* d, int i) {
    if (!d) return;
    d->color = slate::Field(); d->hoverColor = slate::FieldHover(); d->listColor = slate::Panel();
    d->textColor = slate::Text(); d->borderColor = slate::Border();
    d->shape = i == 1 ? UIShape::Pill : UIShape::Rounded; d->cornerRadius = 8.0f;
}

inline const std::vector<const char*>& TabsPresets() {
    static const std::vector<const char*> v = {"Pills", "Segmented", "Minimal"};
    return v;
}
inline void ApplyTabs(UITabs* t, int i) {
    if (!t) return;
    t->selectedTextColor = Color::White; t->textColor = Color::FromBytes(200, 205, 215);
    switch (i) {
        case 0: t->shape = UIShape::Pill; t->background = Color::FromBytes(23, 25, 31); t->selected = slate::Accent(); break;
        case 1: t->shape = UIShape::Rounded; t->cornerRadius = 8.0f; t->background = slate::Field(); t->selected = Color::FromBytes(78, 86, 106); break;
        case 2: t->shape = UIShape::Rounded; t->cornerRadius = 6.0f; t->background = Color::FromBytes(0, 0, 0, 0);
                t->selected = Color::FromBytes(66, 150, 250, 50); t->selectedTextColor = Color::FromBytes(150, 198, 255); break;
        default: break;
    }
}

inline const std::vector<const char*>& RadialPresets() {
    static const std::vector<const char*> v = {"Ring", "Spinner", "Cooldown (pie)"};
    return v;
}
inline void ApplyRadial(UIRadialProgress* r, int i) {
    if (!r) return;
    r->background = Color::FromBytes(255, 255, 255, 22); r->fill = slate::Accent(); r->textColor = slate::Text();
    switch (i) {
        case 0: r->thickness = 6.0f; r->spin = false; break;
        case 1: r->thickness = 4.0f; r->spin = true; r->value = 0.25f; break;
        case 2: r->thickness = 0.0f; r->spin = false; r->background = Color::FromBytes(0, 0, 0, 120);
                r->fill = Color::FromBytes(255, 255, 255, 70); break;
        default: break;
    }
}

// ---- Copy / paste style ---------------------------------------------------------
// The look only: colours, corners, borders, shadows, fonts. Never position, size,
// anchor, text, value or click actions.

inline void CopyStyle(const UIButton& s, UIButton& d) {
    d.fontPath = s.fontPath; d.color = s.color; d.hoverColor = s.hoverColor; d.pressedColor = s.pressedColor;
    d.disabledColor = s.disabledColor; d.textColor = s.textColor; d.hoverTextColor = s.hoverTextColor;
    d.shape = s.shape; d.cornerRadius = s.cornerRadius; d.fontScale = s.fontScale; d.borderWidth = s.borderWidth;
    d.borderColor = s.borderColor; d.shadow = s.shadow; d.shadowColor = s.shadowColor; d.shadowOffset = s.shadowOffset;
    d.shadowSoftness = s.shadowSoftness; d.hoverScale = s.hoverScale; d.transitionSpeed = s.transitionSpeed;
    d.pressOffset = s.pressOffset; d.iconSize = s.iconSize;
}
inline void CopyStyle(const UIPanel& s, UIPanel& d) {
    d.color = s.color; d.shape = s.shape; d.cornerRadius = s.cornerRadius; d.cornerMask = s.cornerMask;
    d.borderWidth = s.borderWidth; d.borderColor = s.borderColor; d.useGradient = s.useGradient;
    d.gradientHorizontal = s.gradientHorizontal; d.gradientDir = s.gradientDir; d.colorBottom = s.colorBottom;
    d.outlineWidth = s.outlineWidth; d.outlineColor = s.outlineColor; d.topHighlight = s.topHighlight;
    d.highlightColor = s.highlightColor; d.shadow = s.shadow; d.shadowColor = s.shadowColor;
    d.shadowOffset = s.shadowOffset; d.shadowSoftness = s.shadowSoftness;
}
inline void CopyStyle(const TextRenderer& s, TextRenderer& d) {
    d.fontPath = s.fontPath; d.color = s.color; d.pixelSize = s.pixelSize; d.align = s.align;
    d.italic = s.italic; d.gradient = s.gradient; d.colorBottom = s.colorBottom; d.background = s.background;
    d.backgroundColor = s.backgroundColor; d.shadow = s.shadow; d.shadowColor = s.shadowColor;
    d.shadowOffset = s.shadowOffset; d.outline = s.outline; d.outlineColor = s.outlineColor; d.bold = s.bold;
    d.letterSpacing = s.letterSpacing; d.lineSpacing = s.lineSpacing; d.uppercase = s.uppercase;
}
inline void CopyStyle(const UIProgressBar& s, UIProgressBar& d) {
    d.background = s.background; d.fill = s.fill; d.shape = s.shape; d.cornerRadius = s.cornerRadius;
    d.showPercent = s.showPercent; d.textColor = s.textColor; d.gradientFill = s.gradientFill; d.fillEnd = s.fillEnd;
}
inline void CopyStyle(const UISlider& s, UISlider& d) {
    d.background = s.background; d.fill = s.fill; d.knob = s.knob; d.trackShape = s.trackShape;
    d.cornerRadius = s.cornerRadius; d.knobSize = s.knobSize; d.roundKnob = s.roundKnob; d.textColor = s.textColor;
}
inline void CopyStyle(const UIToggle& s, UIToggle& d) {
    d.boxColor = s.boxColor; d.checkColor = s.checkColor; d.textColor = s.textColor;
    d.cornerRadius = s.cornerRadius; d.style = s.style; d.knobColor = s.knobColor;
}
inline void CopyStyle(const UIInputField& s, UIInputField& d) {
    d.color = s.color; d.focusColor = s.focusColor; d.textColor = s.textColor; d.placeholderColor = s.placeholderColor;
    d.shape = s.shape; d.cornerRadius = s.cornerRadius; d.borderWidth = s.borderWidth; d.borderColor = s.borderColor;
}
inline void CopyStyle(const UIDropdown& s, UIDropdown& d) {
    d.color = s.color; d.hoverColor = s.hoverColor; d.listColor = s.listColor; d.textColor = s.textColor;
    d.borderColor = s.borderColor; d.shape = s.shape; d.cornerRadius = s.cornerRadius;
}
inline void CopyStyle(const UITabs& s, UITabs& d) {
    d.background = s.background; d.selected = s.selected; d.textColor = s.textColor;
    d.selectedTextColor = s.selectedTextColor; d.shape = s.shape; d.cornerRadius = s.cornerRadius;
}
inline void CopyStyle(const UIRadialProgress& s, UIRadialProgress& d) {
    d.background = s.background; d.fill = s.fill; d.thickness = s.thickness; d.textColor = s.textColor;
}

// Every UI component present on both objects takes the source's style. Returns how
// many components were restyled.
inline int CopyStyle(GameObject* src, GameObject* dst) {
    if (!src || !dst || src == dst) return 0;
    int n = 0;
    auto one = [&](auto* tag) {
        using T = std::remove_pointer_t<decltype(tag)>;
        const T* s = src->GetComponent<T>();
        T* d = dst->GetComponent<T>();
        if (s && d) { CopyStyle(*s, *d); ++n; }
    };
    one((UIButton*)nullptr); one((UIPanel*)nullptr); one((TextRenderer*)nullptr); one((UIProgressBar*)nullptr);
    one((UISlider*)nullptr); one((UIToggle*)nullptr); one((UIInputField*)nullptr); one((UIDropdown*)nullptr);
    one((UITabs*)nullptr); one((UIRadialProgress*)nullptr);
    return n;
}

} } // namespace okay::UIStyles
