// Property registrations of the UI widgets: button, text, image, slider, toggle,
// progress bar, input field, stepper and dropdown. (Canvas and UIPanel are in
// BuiltinProperties.cpp.) Each type registers every field its scene record keeps; lists
// (the dropdown's options) and the localisation key are left out. See docs/properties.md.
#include "BuiltinPropertiesShared.hpp"

#include "okay/Components/TextRenderer.hpp"
#include "okay/Components/UIButton.hpp"
#include "okay/Components/UIDropdown.hpp"
#include "okay/Components/UIImage.hpp"
#include "okay/Components/UIInputField.hpp"
#include "okay/Components/UIProgressBar.hpp"
#include "okay/Components/UISlider.hpp"
#include "okay/Components/UIStepper.hpp"
#include "okay/Components/UIToggle.hpp"

namespace okay {
namespace detail {
namespace {

void RegisterUIButton() {
    PropertyBuilder<UIButton> b("UIButton", "uibutton", "UI");
    b.AddVec2("position", &UIButton::position).Range(-kBig, kBig, 0.1).Tooltip("Pixels from the anchor (margins for a stretch anchor)")
     .AddVec2("size", &UIButton::size).Range(-kBig, kBig, 0.1).Tooltip("Width and height in pixels")
     .AddString("label", &UIButton::label).Tooltip("Text of the label")
     .AddAssetPath("fontPath", &UIButton::fontPath, "font").Tooltip("Optional TTF/OTF font for the label (e.g. \"Assets/MyFont.ttf\"). Empty = the built-in 8x8 bitmap font")
     .AddAssetPath("icon", &UIButton::icon, "image").Tooltip("Icon image drawn beside the label")
     .AddFloat("iconSize", &UIButton::iconSize).Range(0, kBig, 0.05).Tooltip("Square size of the icon in pixels (0 hides it)")
     .AddString("clickTarget", &UIButton::clickTarget).Tooltip("Object whose function is called on click (blank = this object)")
     .AddString("clickFunction", &UIButton::clickFunction).Tooltip("Function called on click (blank = none)")
     .AddFloat("clickArg", &UIButton::clickArg).Range(-kBig, kBig, 0.1).Tooltip("Number passed to a native click action")
     .AddColor("color", &UIButton::color).Range(0, 1, 0.01).Tooltip("Main colour")
     .AddColor("hoverColor", &UIButton::hoverColor).Range(0, 1, 0.01).Tooltip("Colour while hovered")
     .AddColor("pressedColor", &UIButton::pressedColor).Range(0, 1, 0.01).Tooltip("Shown while held down")
     .AddColor("disabledColor", &UIButton::disabledColor).Range(0, 1, 0.01).Tooltip("Shown when !interactable")
     .AddColor("textColor", &UIButton::textColor).Range(0, 1, 0.01).Tooltip("Colour of the text")
     .AddEnum("anchor", &UIButton::anchor, AnchorNames()).Tooltip("The screen point the position is measured from")
     .AddEnum("shape", &UIButton::shape, ShapeNames()).Tooltip("Silhouette: rounded (default), hard rectangle, circle (icon button) or pill")
     .AddFloat("cornerRadius", &UIButton::cornerRadius).Range(0, kBig, 0.05).Tooltip("Corner rounding, in pixels")
     .AddFloat("fontScale", &UIButton::fontScale).Range(0, 1000, 0.01).Tooltip("Text pixel size multiplier")
     .AddFloat("borderWidth", &UIButton::borderWidth).Range(0, kBig, 0.05).Tooltip("0 = no border")
     .AddColor("borderColor", &UIButton::borderColor).Range(0, 1, 0.01).Tooltip("Border colour")
     .AddBool("shadow", &UIButton::shadow).Tooltip("Optional drop shadow (lifts the button off the background — clean-UI style)")
     .AddColor("shadowColor", &UIButton::shadowColor).Range(0, 1, 0.01).Tooltip("Shadow colour")
     .AddVec2("shadowOffset", &UIButton::shadowOffset).Range(-kBig, kBig, 0.1).Tooltip("Shadow shift in pixels")
     .AddFloat("shadowSoftness", &UIButton::shadowSoftness).Range(0, 1000, 0.5).Tooltip("0 = crisp; higher = a soft blurred shadow")
     .AddFloat("hoverScale", &UIButton::hoverScale).Range(0, 1000, 0.01).Tooltip("Grow factor while hovered (1 = none)")
     .AddBool("interactable", &UIButton::interactable).Tooltip("When off the widget is greyed out and ignores input")
     .AddBool("focusable", &UIButton::focusable).Tooltip("Whether keyboard/gamepad menu navigation can focus this button")
     .AddColor("hoverTextColor", &UIButton::hoverTextColor).Range(0, 1, 0.01).Tooltip("Text color while hovered/focused (defaults to the normal text color)")
     .AddFloat("transitionSpeed", &UIButton::transitionSpeed).Range(0, 10000, 0.1).Tooltip("Colour change speed on hover and press (0 = instant)")
     .AddEnum("transitionEase", &UIButton::transitionEase, {"Linear", "Smooth", "EaseOut", "EaseIn"}).Tooltip("0 linear, 1 smooth, 2 ease-out, 3 ease-in")
     .AddFloat("transitionDuration", &UIButton::transitionDuration).Range(0, 3600, 0.05).Tooltip("Seconds to reach the state color; 0 = use transitionSpeed")
     .AddBool("separateFocus", &UIButton::separateFocus).Tooltip("When set, keyboard focus uses focusColor instead of hover")
     .AddColor("focusColor", &UIButton::focusColor).Range(0, 1, 0.01).Tooltip("Colour while focused with the keyboard or pad")
     .AddBool("toggleMode", &UIButton::toggleMode).Tooltip("A click flips the button on or off")
     .AddBool("isOn", &UIButton::isOn).Tooltip("Whether a toggle button starts on")
     .AddFloat("pressOffset", &UIButton::pressOffset).Range(-kBig, kBig, 0.05).Tooltip("Pixels the label/icon shift down while pressed (tactile \"push\"). 0 = none")
     .AddBool("iconRight", &UIButton::iconRight).Tooltip("Draw the icon on the right of the label instead of the left")
     .AddBool("holdRepeat", &UIButton::holdRepeat).Tooltip("Fire again repeatedly while held")
     .AddFloat("repeatDelay", &UIButton::repeatDelay).Range(0, 3600, 0.05).Tooltip("Seconds before the first repeat while held")
     .AddFloat("repeatInterval", &UIButton::repeatInterval).Range(0, 3600, 0.05).Tooltip("Seconds between repeats while held");
    b.Commit();
}

void RegisterTextRenderer() {
    PropertyBuilder<TextRenderer> b("TextRenderer", "text", "UI");
    b.AddString("text", &TextRenderer::text).Tooltip("The text shown")
     .AddAssetPath("fontPath", &TextRenderer::fontPath, "font").Tooltip("TTF or OTF font file (empty = the built-in bitmap font)")
     .AddColor("color", &TextRenderer::color).Range(0, 1, 0.01).Tooltip("Main colour")
     .AddFloat("pixelSize", &TextRenderer::pixelSize).Range(0, kBig, 0.05).Tooltip("World units per font pixel (world space) or window pixels per font pixel (screen space)")
     .AddBool("screenSpace", &TextRenderer::screenSpace).Tooltip("When true, `screenPos` (pixels, top-left origin) is used instead of the Transform — handy for a fixed HUD")
     .AddVec2("screenPos", &TextRenderer::screenPos).Range(-kBig, kBig, 0.1).Tooltip("Screen position in pixels (top-left origin) when screenSpace is on")
     .AddVec2("size", &TextRenderer::size).Range(-kBig, kBig, 0.1).Tooltip("Width and height in pixels")
     .AddEnum("anchor", &TextRenderer::anchor, AnchorNames()).Tooltip("The screen point the position is measured from")
     .AddEnum("align", &TextRenderer::align, {"Left", "Center", "Right"}).Tooltip("Screen-space horizontal alignment inside the box: 0 = left, 1 = center, 2 = right")
     .AddBool("vcenter", &TextRenderer::vcenter).Tooltip("Vertically center the text in its box (else top-aligned)")
     .AddBool("alignBottom", &TextRenderer::alignBottom).Tooltip("Align the text to the bottom of its box")
     .AddBool("italic", &TextRenderer::italic).Tooltip("Faux-italic: shear the glyphs to the right for emphasis")
     .AddBool("gradient", &TextRenderer::gradient).Tooltip("Blend the colour toward colorBottom")
     .AddColor("colorBottom", &TextRenderer::colorBottom).Range(0, 1, 0.01).Tooltip("Colour at the bottom when the gradient is on")
     .AddInt("visibleChars", &TextRenderer::visibleChars).Range(-1, 1000000, 1).Tooltip("Typewriter reveal: only this many characters are drawn (-1 = all)")
     .AddFloat("typeSpeed", &TextRenderer::typeSpeed).Range(0, 10000, 0.1).Tooltip("Typewriter speed, characters per second (0 = all at once)")
     .AddBool("background", &TextRenderer::background).Tooltip("Optional filled box drawn behind the text (a label background / chip)")
     .AddColor("backgroundColor", &TextRenderer::backgroundColor).Range(0, 1, 0.01).Tooltip("Colour of the box behind the text")
     .AddBool("shadow", &TextRenderer::shadow).Tooltip("Draw a drop shadow")
     .AddColor("shadowColor", &TextRenderer::shadowColor).Range(0, 1, 0.01).Tooltip("Shadow colour")
     .AddVec2("shadowOffset", &TextRenderer::shadowOffset).Range(-kBig, kBig, 0.1).Tooltip("Shadow shift in pixels")
     .AddBool("outline", &TextRenderer::outline).Tooltip("Draw an outline around the glyphs")
     .AddColor("outlineColor", &TextRenderer::outlineColor).Range(0, 1, 0.01).Tooltip("Outline colour")
     .AddBool("bold", &TextRenderer::bold).Tooltip("Draw the text heavier")
     .AddFloat("letterSpacing", &TextRenderer::letterSpacing).Range(0, kBig, 0.05).Tooltip("Extra spacing (in font pixels) between glyphs and between lines")
     .AddFloat("lineSpacing", &TextRenderer::lineSpacing).Range(0, kBig, 0.05).Tooltip("Extra space between lines, font pixels")
     .AddBool("uppercase", &TextRenderer::uppercase).Tooltip("Render the text UPPER-CASED without changing the stored string")
     .AddBool("wrap", &TextRenderer::wrap).Tooltip("Word-wrap the text to the box width (screen space). Long words overflow")
     .AddBool("autoSize", &TextRenderer::autoSize).Tooltip("Pick the largest pixelSize that fits the box")
     .AddFloat("autoSizeMin", &TextRenderer::autoSizeMin).Range(0, kBig, 0.05).Tooltip("Smallest pixelSize autoSize may pick")
     .AddFloat("autoSizeMax", &TextRenderer::autoSizeMax).Range(0, kBig, 0.05).Tooltip("Largest pixelSize autoSize may pick");
    b.Commit();
}

void RegisterUIImage() {
    PropertyBuilder<UIImage> b("UIImage", "uiimage", "UI");
    b.AddVec2("position", &UIImage::position).Range(-kBig, kBig, 0.1).Tooltip("Pixels from the anchor (margins for a stretch anchor)")
     .AddVec2("size", &UIImage::size).Range(-kBig, kBig, 0.1).Tooltip("Width and height in pixels")
     .AddAssetPath("texture", &UIImage::texture, "image").Tooltip("Image path (PNG/JPG/BMP); empty = colored rect")
     .AddColor("color", &UIImage::color).Range(0, 1, 0.01).Tooltip("Tint (or fill color when no texture)")
     .AddEnum("anchor", &UIImage::anchor, AnchorNames()).Tooltip("The screen point the position is measured from")
     .AddBool("nineSlice", &UIImage::nineSlice).Tooltip("Stretch the image as nine regions so the corners stay crisp")
     .AddFloat("border", &UIImage::border).Range(0, kBig, 1).Tooltip("Source-pixel inset for the 9 regions")
     .AddEnum("fillMode", &UIImage::fillMode, {"None", "Left", "Right", "Up", "Down"}).Tooltip("Draw only part of the image")
     .AddFloat("fillAmount", &UIImage::fillAmount).Range(0, 1000, 0.01).Tooltip("How much of the image is drawn (0 to 1) when fillMode is on")
     .AddFloat("cornerRadius", &UIImage::cornerRadius).Range(0, kBig, 0.05).Tooltip("Rounded corners for the colored-rect fallback (pixels)")
     .AddEnum("shape", &UIImage::shape, ShapeNames()).Tooltip("Silhouette of the widget")
     .AddBool("flipX", &UIImage::flipX).Tooltip("Mirror horizontally")
     .AddBool("flipY", &UIImage::flipY).Tooltip("Mirror vertically")
     .AddBool("preserveAspect", &UIImage::preserveAspect).Tooltip("Keep the image proportions when fitting the box")
     .AddFloat("borderWidth", &UIImage::borderWidth).Range(0, kBig, 0.05).Tooltip("Optional frame drawn around the image rect (a matte/keyline). 0 = no frame")
     .AddColor("borderColor", &UIImage::borderColor).Range(0, 1, 0.01).Tooltip("Border colour")
     .AddInt("cornerMask", &UIImage::cornerMask).Range(0, 15, 1).Tooltip("Which corners are rounded (bit 0 top-left, 1 top-right, 2 bottom-right, 3 bottom-left)")
     .AddBool("shadow", &UIImage::shadow).Tooltip("Draw a drop shadow")
     .AddColor("shadowColor", &UIImage::shadowColor).Range(0, 1, 0.01).Tooltip("Shadow colour")
     .AddVec2("shadowOffset", &UIImage::shadowOffset).Range(-kBig, kBig, 0.1).Tooltip("Shadow shift in pixels")
     .AddFloat("shadowSoftness", &UIImage::shadowSoftness).Range(0, 1000, 0.5).Tooltip("Blur of the shadow (0 = crisp)");
    b.Commit();
}

void RegisterUISlider() {
    PropertyBuilder<UISlider> b("UISlider", "uislider", "UI");
    b.AddVec2("position", &UISlider::position).Range(-kBig, kBig, 0.1).Tooltip("Pixels from the anchor (margins for a stretch anchor)")
     .AddVec2("size", &UISlider::size).Range(-kBig, kBig, 0.1).Tooltip("Width and height in pixels")
     .AddFloat("value", &UISlider::value).Range(-kBig, kBig, 0.1).Tooltip("Current value")
     .AddFloat("minValue", &UISlider::minValue).Range(-kBig, kBig, 0.1).Tooltip("Lowest value")
     .AddFloat("maxValue", &UISlider::maxValue).Range(-kBig, kBig, 0.1).Tooltip("Highest value")
     .AddColor("background", &UISlider::background).Range(0, 1, 0.01).Tooltip("Colour of the track")
     .AddColor("fill", &UISlider::fill).Range(0, 1, 0.01).Tooltip("Colour of the filled part")
     .AddColor("knob", &UISlider::knob).Range(0, 1, 0.01).Tooltip("Colour of the handle")
     .AddEnum("anchor", &UISlider::anchor, AnchorNames()).Tooltip("The screen point the position is measured from")
     .AddEnum("trackShape", &UISlider::trackShape, ShapeNames()).Tooltip("Silhouette of the track + fill (Pill = clean capsule slider)")
     .AddFloat("cornerRadius", &UISlider::cornerRadius).Range(0, kBig, 0.05).Tooltip("Corner rounding, in pixels")
     .AddFloat("knobSize", &UISlider::knobSize).Range(0, kBig, 0.05).Tooltip("Knob width = size.y * this")
     .AddBool("roundKnob", &UISlider::roundKnob).Tooltip("Draw a circular handle")
     .AddBool("showValue", &UISlider::showValue).Tooltip("Draw the value next to the slider")
     .AddBool("wholeNumbers", &UISlider::wholeNumbers).Tooltip("Snap the value to integers")
     .AddBool("vertical", &UISlider::vertical).Tooltip("Drag/fill bottom->top")
     .AddColor("textColor", &UISlider::textColor).Range(0, 1, 0.01).Tooltip("Colour of the text")
     .AddFloat("step", &UISlider::step).Range(0, kBig, 0.05).Tooltip("0 = continuous; >0 snaps to this spacing")
     .AddString("valueFormat", &UISlider::valueFormat).Tooltip("Empty = \"%.2f\"; \"{v}\" is the number")
     .AddBool("fillGradient", &UISlider::fillGradient).Tooltip("Blend the fill toward fillEnd")
     .AddColor("fillEnd", &UISlider::fillEnd).Range(0, 1, 0.01).Tooltip("Colour at the far end of the gradient")
     .AddInt("knobShape", &UISlider::knobShape).Range(-1, 16, 1).Tooltip("-1 = roundKnob decides; else a UIShape")
     .AddBool("interactable", &UISlider::interactable).Tooltip("When false: greyed out, ignores drag");
    b.Commit();
}

void RegisterUIToggle() {
    PropertyBuilder<UIToggle> b("UIToggle", "uitoggle", "UI");
    b.AddVec2("position", &UIToggle::position).Range(-kBig, kBig, 0.1).Tooltip("Pixels from the anchor (margins for a stretch anchor)")
     .AddVec2("size", &UIToggle::size).Range(-kBig, kBig, 0.1).Tooltip("Width and height in pixels")
     .AddBool("on", &UIToggle::on).Tooltip("Whether the toggle is on")
     .AddString("label", &UIToggle::label).Tooltip("Text of the label")
     .AddColor("boxColor", &UIToggle::boxColor).Range(0, 1, 0.01).Tooltip("Colour of the box")
     .AddColor("checkColor", &UIToggle::checkColor).Range(0, 1, 0.01).Tooltip("Colour of the check mark")
     .AddColor("textColor", &UIToggle::textColor).Range(0, 1, 0.01).Tooltip("Colour of the text")
     .AddEnum("anchor", &UIToggle::anchor, AnchorNames()).Tooltip("The screen point the position is measured from")
     .AddFloat("cornerRadius", &UIToggle::cornerRadius).Range(0, kBig, 0.05).Tooltip("Rounded box corners")
     .AddEnum("style", &UIToggle::style, {"Checkbox", "Switch"}).Tooltip("Look of the toggle: a check box or a switch")
     .AddColor("knobColor", &UIToggle::knobColor).Range(0, 1, 0.01).Tooltip("Switch knob")
     .AddBool("interactable", &UIToggle::interactable).Tooltip("When false: greyed out, ignores clicks")
     .AddFloat("animSpeed", &UIToggle::animSpeed).Range(0, 10000, 0.1).Tooltip("Speed of the on and off animation");
    b.Commit();
}

void RegisterUIProgressBar() {
    PropertyBuilder<UIProgressBar> b("UIProgressBar", "uiprogress", "UI");
    b.AddVec2("position", &UIProgressBar::position).Range(-kBig, kBig, 0.1).Tooltip("Pixels from the anchor (margins for a stretch anchor)")
     .AddVec2("size", &UIProgressBar::size).Range(-kBig, kBig, 0.1).Tooltip("Width and height in pixels")
     .AddFloat("value", &UIProgressBar::value).Range(0, 1, 0.01).Tooltip("Clamped to [0, 1]")
     .AddColor("background", &UIProgressBar::background).Range(0, 1, 0.01).Tooltip("Colour of the track")
     .AddColor("fill", &UIProgressBar::fill).Range(0, 1, 0.01).Tooltip("Colour of the filled part")
     .AddEnum("anchor", &UIProgressBar::anchor, AnchorNames()).Tooltip("The screen point the position is measured from")
     .AddEnum("shape", &UIProgressBar::shape, ShapeNames()).Tooltip("Silhouette of the track/fill (Pill gives the clean capsule health-bar look)")
     .AddFloat("cornerRadius", &UIProgressBar::cornerRadius).Range(0, kBig, 0.05).Tooltip("Corner rounding, in pixels")
     .AddBool("showPercent", &UIProgressBar::showPercent).Tooltip("Draw the percentage on the bar")
     .AddColor("textColor", &UIProgressBar::textColor).Range(0, 1, 0.01).Tooltip("Colour of the text")
     .AddBool("gradientFill", &UIProgressBar::gradientFill).Tooltip("Optional gradient along the fill (e.g. green->yellow): `fill` is the start, `fillEnd` the far end")
     .AddColor("fillEnd", &UIProgressBar::fillEnd).Range(0, 1, 0.01).Tooltip("Colour at the far end of the gradient")
     .AddEnum("fillDir", &UIProgressBar::fillDir, {"LeftRight", "RightLeft", "BottomTop", "TopBottom"}).Tooltip("Direction the bar fills")
     .AddInt("segments", &UIProgressBar::segments).Range(0, 1000, 1).Tooltip("0 or 1 = one solid fill; >=2 = chunks")
     .AddFloat("segmentGap", &UIProgressBar::segmentGap).Range(0, 1000, 0.5).Tooltip("Gap between segments, pixels")
     .AddFloat("trailDelay", &UIProgressBar::trailDelay).Range(0, 3600, 0.05).Tooltip("Seconds for a damage trail to catch a drop; 0 = off")
     .AddColor("trailColor", &UIProgressBar::trailColor).Range(0, 1, 0.01).Tooltip("Colour of the damage trail")
     .AddBool("colorByValue", &UIProgressBar::colorByValue).Tooltip("Lerp lowColor -> highColor by the fill")
     .AddColor("lowColor", &UIProgressBar::lowColor).Range(0, 1, 0.01).Tooltip("Colour at low values")
     .AddColor("highColor", &UIProgressBar::highColor).Range(0, 1, 0.01).Tooltip("Colour at high values")
     .AddString("textFormat", &UIProgressBar::textFormat).Tooltip("Empty = \"%d%%\"; \"{n}\" is the percent, \"{v}\" is 0..1");
    b.Commit();
}

void RegisterUIInputField() {
    PropertyBuilder<UIInputField> b("UIInputField", "uiinput", "UI");
    b.AddVec2("position", &UIInputField::position).Range(-kBig, kBig, 0.1).Tooltip("Pixels from the anchor (margins for a stretch anchor)")
     .AddVec2("size", &UIInputField::size).Range(-kBig, kBig, 0.1).Tooltip("Width and height in pixels")
     .AddEnum("anchor", &UIInputField::anchor, AnchorNames()).Tooltip("The screen point the position is measured from")
     .AddColor("color", &UIInputField::color).Range(0, 1, 0.01).Tooltip("Main colour")
     .AddString("text", &UIInputField::text).Tooltip("The text shown")
     .AddString("placeholder", &UIInputField::placeholder).Tooltip("Hint shown while empty")
     .AddInt("maxLength", &UIInputField::maxLength).Range(0, kBig, 1).Tooltip("Most characters the field accepts (0 = no limit)")
     .AddEnum("contentType", &UIInputField::contentType, {"Standard", "Integer", "Decimal", "Password"}).Tooltip("What the field accepts")
     .AddEnum("shape", &UIInputField::shape, ShapeNames()).Tooltip("Silhouette + corners and an optional focus ring (clean form-field styling)")
     .AddFloat("cornerRadius", &UIInputField::cornerRadius).Range(0, kBig, 0.05).Tooltip("Corner rounding, in pixels")
     .AddFloat("borderWidth", &UIInputField::borderWidth).Range(0, kBig, 0.05).Tooltip("Focus ring thickness (0 = none)")
     .AddColor("borderColor", &UIInputField::borderColor).Range(0, 1, 0.01).Tooltip("Shown while focused")
     .AddBool("multiline", &UIInputField::multiline).Tooltip("Allow several lines");
    b.Commit();
}

void RegisterUIStepper() {
    PropertyBuilder<UIStepper> b("UIStepper", "uistepper", "UI");
    b.AddVec2("position", &UIStepper::position).Range(-kBig, kBig, 0.1).Tooltip("Pixels from the anchor (margins for a stretch anchor)")
     .AddVec2("size", &UIStepper::size).Range(-kBig, kBig, 0.1).Tooltip("Width and height in pixels")
     .AddFloat("value", &UIStepper::value).Range(-kBig, kBig, 0.1).Tooltip("Current value")
     .AddFloat("minValue", &UIStepper::minValue).Range(-kBig, kBig, 0.1).Tooltip("Lowest value")
     .AddFloat("maxValue", &UIStepper::maxValue).Range(-kBig, kBig, 0.1).Tooltip("Highest value")
     .AddFloat("step", &UIStepper::step).Range(0, kBig, 0.05).Tooltip("Amount one click adds or removes")
     .AddBool("wholeNumbers", &UIStepper::wholeNumbers).Tooltip("Round to integers")
     .AddBool("wrap", &UIStepper::wrap).Tooltip("Wrap past the ends instead of clamping")
     .AddColor("background", &UIStepper::background).Range(0, 1, 0.01).Tooltip("Background colour")
     .AddColor("button", &UIStepper::button).Range(0, 1, 0.01).Tooltip("Colour of the + and - buttons")
     .AddColor("textColor", &UIStepper::textColor).Range(0, 1, 0.01).Tooltip("Colour of the text")
     .AddEnum("anchor", &UIStepper::anchor, AnchorNames()).Tooltip("The screen point the position is measured from")
     .AddEnum("shape", &UIStepper::shape, ShapeNames()).Tooltip("Silhouette of the widget")
     .AddFloat("cornerRadius", &UIStepper::cornerRadius).Range(0, kBig, 0.05).Tooltip("Corner rounding, in pixels")
     .AddBool("interactable", &UIStepper::interactable).Tooltip("When off the widget is greyed out and ignores input");
    b.Commit();
}

void RegisterUIDropdown() {
    PropertyBuilder<UIDropdown> b("UIDropdown", "uidropdown", "UI");
    b.AddVec2("position", &UIDropdown::position).Range(-kBig, kBig, 0.1).Tooltip("Pixels from the anchor (margins for a stretch anchor)")
     .AddVec2("size", &UIDropdown::size).Range(-kBig, kBig, 0.1).Tooltip("Width and height in pixels")
     .AddInt("value", &UIDropdown::value).Range(-1, kBig, 1).Tooltip("Selected option index")
     .AddColor("color", &UIDropdown::color).Range(0, 1, 0.01).Tooltip("Main colour")
     .AddColor("hoverColor", &UIDropdown::hoverColor).Range(0, 1, 0.01).Tooltip("Highlighted option")
     .AddColor("listColor", &UIDropdown::listColor).Range(0, 1, 0.01).Tooltip("Open list background")
     .AddColor("textColor", &UIDropdown::textColor).Range(0, 1, 0.01).Tooltip("Colour of the text")
     .AddColor("borderColor", &UIDropdown::borderColor).Range(0, 1, 0.01).Tooltip("Border colour")
     .AddEnum("anchor", &UIDropdown::anchor, AnchorNames()).Tooltip("The screen point the position is measured from")
     .AddEnum("shape", &UIDropdown::shape, ShapeNames()).Tooltip("Silhouette + corner radius for the header (and list) — clean rounded selects")
     .AddFloat("cornerRadius", &UIDropdown::cornerRadius).Range(0, kBig, 0.05).Tooltip("Corner rounding, in pixels")
     .AddString("placeholder", &UIDropdown::placeholder).Tooltip("Shown in the header (dimmed) when nothing is chosen (value < 0)")
     .AddBool("searchable", &UIDropdown::searchable).Tooltip("The open list has a search box")
     .AddInt("maxVisible", &UIDropdown::maxVisible).Range(0, 1000, 1).Tooltip("How many matching options the open list shows. 0 = every match")
     .AddBool("interactable", &UIDropdown::interactable).Tooltip("When false: greyed out, ignores clicks");
    b.Commit();
}

} // namespace

void RegisterUIProperties() {
    RegisterUIButton();
    RegisterTextRenderer();
    RegisterUIImage();
    RegisterUISlider();
    RegisterUIToggle();
    RegisterUIProgressBar();
    RegisterUIInputField();
    RegisterUIStepper();
    RegisterUIDropdown();
}

} // namespace detail
} // namespace okay
