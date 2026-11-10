// Property registrations of the UI widgets: button, text, image, slider, toggle,
// progress bar, input field, stepper, dropdown, the loading screen and more. (Canvas and UIPanel are in
// BuiltinProperties.cpp.) Each type registers every field its scene record keeps; lists
// (the dropdown's options) and the localisation key are left out. See docs/properties.md.
#include "BuiltinPropertiesShared.hpp"

#include "okay/Components/Crosshair.hpp"
#include "okay/Components/LoadingScreen.hpp"
#include "okay/Components/TextRenderer.hpp"
#include "okay/Components/UIButton.hpp"
#include "okay/Components/UIDraggable.hpp"
#include "okay/Components/UIDropdown.hpp"
#include "okay/Components/UIFlexItem.hpp"
#include "okay/Components/UIFrame.hpp"
#include "okay/Components/UIImage.hpp"
#include "okay/Components/UIInputField.hpp"
#include "okay/Components/UIKeybindField.hpp"
#include "okay/Components/UILayoutGroup.hpp"
#include "okay/Components/UIProgressBar.hpp"
#include "okay/Components/UIRadialProgress.hpp"
#include "okay/Components/UIRating.hpp"
#include "okay/Components/UIRawImage.hpp"
#include "okay/Components/UIScrollView.hpp"
#include "okay/Components/UISlider.hpp"
#include "okay/Components/UIStepper.hpp"
#include "okay/Components/UIToggle.hpp"
#include "okay/Components/UITheme.hpp"
#include "okay/Components/UIStyleVariant.hpp"
#include "okay/Components/UIDamageNumbers.hpp"
#include "okay/Components/UIDialogueBox.hpp"

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
     .AddEnum("style", &UIToggle::style, {"Checkbox", "Switch", "Radio"}).Tooltip("Look of the toggle: a check box, a switch or a round radio button")
     .AddString("radioGroup", &UIToggle::radioGroup).Tooltip("Radio buttons with the same group name are one choice. Empty = the radio buttons under the same parent")
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

void RegisterLoadingScreen() {
    PropertyBuilder<LoadingScreen> b("LoadingScreen", "loadingscreen", "UI");
    b.Section("Look")
     .AddEnum("layout", &LoadingScreen::layout, {"Classic", "Minimal", "Cinematic", "Splash", "Card", "Corner", "Ring"})
     .AddEnum("barStyle", &LoadingScreen::barStyle, {"Solid", "Segmented", "Shimmer", "Indeterminate"})
     .AddEnum("spinnerStyle", &LoadingScreen::spinnerStyle, {"Dots", "Arc", "Bars", "Pulse"})
     .AddString("subtitle", &LoadingScreen::subtitle).Tooltip("Smaller line with the title (level name, chapter...)")
     .AddAssetPath("logoImage", &LoadingScreen::logoImage, "image").Tooltip("Optional logo (Splash shows it big)")
     .AddFloat("logoSize", &LoadingScreen::logoSize).Range(0, kBig, 0.05).Tooltip("Logo size as a fraction of screen height")
     .AddBool("logoPulse", &LoadingScreen::logoPulse).Tooltip("Logo gently breathes")
     .AddBool("kenBurns", &LoadingScreen::kenBurns).Tooltip("Background image slowly zooms in")
     .AddColor("cardColor", &LoadingScreen::cardColor).Range(0, 1, 0.01).Tooltip("Card layout's panel")
     .AddFloat("tipInterval", &LoadingScreen::tipInterval).Range(0, 3600, 0.05).Tooltip("> 0: show the next tip every N seconds")
     .AddColor("backgroundColor", &LoadingScreen::backgroundColor).Range(0, 1, 0.01).Tooltip("Colour of the box behind the text")
     .AddColor("backgroundColor2", &LoadingScreen::backgroundColor2).Range(0, 1, 0.01).Tooltip("Gradient bottom colour")
     .AddBool("gradientBackground", &LoadingScreen::gradientBackground).Tooltip("Vertical gradient backgroundColor -> backgroundColor2")
     .AddAssetPath("backgroundImage", &LoadingScreen::backgroundImage, "image").Tooltip("Optional full-screen image (else a flat colour)")
     .AddString("title", &LoadingScreen::title)
     .AddColor("textColor", &LoadingScreen::textColor).Range(0, 1, 0.01).Tooltip("Colour of the text")
     .Section("Title")
     .AddBool("showTitle", &LoadingScreen::showTitle)
     .AddColor("titleColor", &LoadingScreen::titleColor).Range(0, 1, 0.01)
     .AddFloat("titleScale", &LoadingScreen::titleScale).Range(0, 1000, 0.01).Tooltip("Multiplier on the auto title size")
     .AddFloat("titleY", &LoadingScreen::titleY).Range(-kBig, kBig, 0.1).Tooltip("Vertical position (0=top, 1=bottom)")
     .Section("Tip line")
     .AddColor("tipColor", &LoadingScreen::tipColor).Range(0, 1, 0.01)
     .AddFloat("tipScale", &LoadingScreen::tipScale).Range(0, 1000, 0.01)
     .AddFloat("tipY", &LoadingScreen::tipY).Range(-kBig, kBig, 0.1)
     .Section("Progress bar")
     .AddColor("barBackground", &LoadingScreen::barBackground).Range(0, 1, 0.01)
     .AddColor("barFill", &LoadingScreen::barFill).Range(0, 1, 0.01)
     .AddBool("showBar", &LoadingScreen::showBar)
     .AddFloat("barWidth", &LoadingScreen::barWidth).Range(0, kBig, 0.05).Tooltip("Fraction of screen width")
     .AddFloat("barHeight", &LoadingScreen::barHeight).Range(0, kBig, 0.05).Tooltip("Fraction of screen height")
     .AddFloat("barY", &LoadingScreen::barY).Range(-kBig, kBig, 0.1).Tooltip("Vertical position (0..1)")
     .AddFloat("barRadius", &LoadingScreen::barRadius).Range(0, kBig, 0.05).Tooltip("Rounded corners (px)")
     .AddBool("barBorder", &LoadingScreen::barBorder)
     .AddColor("barBorderColor", &LoadingScreen::barBorderColor).Range(0, 1, 0.01)
     .AddBool("showPercent", &LoadingScreen::showPercent).Tooltip("Draw \"NN%\" next to the bar")
     .Section("Spinner")
     .AddBool("showSpinner", &LoadingScreen::showSpinner)
     .AddColor("spinnerColor", &LoadingScreen::spinnerColor).Range(0, 1, 0.01)
     .AddFloat("spinnerRadius", &LoadingScreen::spinnerRadius).Range(0, kBig, 0.05).Tooltip("Fraction of screen height")
     .AddFloat("spinnerDotSize", &LoadingScreen::spinnerDotSize).Range(0, kBig, 0.05).Tooltip("Dot size (px, scaled by height)")
     .AddFloat("spinnerSpeed", &LoadingScreen::spinnerSpeed).Range(0, 10000, 0.1).Tooltip("Revolutions-ish per second")
     .AddFloat("spinnerY", &LoadingScreen::spinnerY).Range(-kBig, kBig, 0.1)
     .Section("Loading")
     .AddBool("asyncLoad", &LoadingScreen::asyncLoad).Tooltip("Load targetScene over several frames, so the screen keeps animating while it loads")
     .AddInt("objectsPerFrame", &LoadingScreen::objectsPerFrame).Range(1, 100000, 1).Tooltip("Objects made per frame by a background load")
     .AddFloat("duration", &LoadingScreen::duration).Range(0, 3600, 0.05).Tooltip("Minimum seconds the screen stays up")
     .AddAssetPath("targetScene", &LoadingScreen::targetScene, "scene").Tooltip("Scene to load when done (empty = reveal this one)")
     .AddBool("showOnStart", &LoadingScreen::showOnStart).Tooltip("Show automatically when the scene starts")
     .AddFloat("fadeTime", &LoadingScreen::fadeTime).Range(0, 3600, 0.05).Tooltip("Fade-in / fade-out length (seconds)");
    b.Commit();
}
void RegisterCrosshair() {
    PropertyBuilder<Crosshair> b("Crosshair", "crosshair", "UI");
    b.AddVec2("position", &Crosshair::position).Range(-kBig, kBig, 0.1).Tooltip("Offset from the anchor (px)")
     .AddVec2("size", &Crosshair::size).Range(-kBig, kBig, 0.1).Tooltip("Overall reticle extent (px)")
     .AddEnum("anchor", &Crosshair::anchor, AnchorNames()).Tooltip("The screen point the position is measured from")
     .AddColor("color", &Crosshair::color).Range(0, 1, 0.01).Tooltip("Main colour")
     .AddFloat("thickness", &Crosshair::thickness).Range(0, 1000, 0.5).Tooltip("Line width (px)")
     .AddFloat("gap", &Crosshair::gap).Range(-1000, 1000, 0.5).Tooltip("Center gap (px)")
     .AddFloat("length", &Crosshair::length).Range(0, kBig, 0.05).Tooltip("Length of each of the 4 lines (px)")
     .AddBool("showLines", &Crosshair::showLines).Tooltip("The 4 ticks")
     .AddBool("lineUp", &Crosshair::lineUp).Tooltip("Per-tick (T-style: Up off)")
     .AddBool("lineDown", &Crosshair::lineDown).Tooltip("Per-tick (T-style: Up off)")
     .AddBool("lineLeft", &Crosshair::lineLeft).Tooltip("Per-tick (T-style: Up off)")
     .AddBool("lineRight", &Crosshair::lineRight).Tooltip("Per-tick (T-style: Up off)")
     .AddFloat("spread", &Crosshair::spread).Range(0, kBig, 0.1).Tooltip("Extra gap added to every tick (aim spread, px)")
     .AddFloat("spreadRecover", &Crosshair::spreadRecover).Range(0, 1000, 0.1)
     .AddFloat("spreadMax", &Crosshair::spreadMax).Range(0, kBig, 0.1)
     .AddBool("dot", &Crosshair::dot)
     .AddFloat("dotSize", &Crosshair::dotSize).Range(0, kBig, 0.05)
     .AddColor("dotColor", &Crosshair::dotColor).Range(0, 1, 0.01)
     .AddBool("circle", &Crosshair::circle).Tooltip("A ring around the centre")
     .AddFloat("circleRadius", &Crosshair::circleRadius).Range(0, kBig, 0.05).Tooltip("Ring radius (px)")
     .AddFloat("circleThickness", &Crosshair::circleThickness).Range(0, 1000, 0.5).Tooltip("Ring line width (px)")
     .AddColor("circleColor", &Crosshair::circleColor).Range(0, 1, 0.01)
     .AddBool("outline", &Crosshair::outline).Tooltip("A 1px dark edge under the marks")
     .AddColor("outlineColor", &Crosshair::outlineColor).Range(0, 1, 0.01).Tooltip("Outline colour");
    b.Commit();
}

void RegisterUIRadialProgress() {
    PropertyBuilder<UIRadialProgress> b("UIRadialProgress", "uiradial", "UI");
    b.AddVec2("position", &UIRadialProgress::position).Range(-kBig, kBig, 0.1).Tooltip("Pixels from the anchor (margins for a stretch anchor)")
     .AddVec2("size", &UIRadialProgress::size).Range(-kBig, kBig, 0.1).Tooltip("Width and height in pixels")
     .AddFloat("value", &UIRadialProgress::value).Range(0, 1, 0.01).Tooltip("0..1 filled fraction")
     .AddFloat("thickness", &UIRadialProgress::thickness).Range(0, kBig, 0.5).Tooltip("Ring width in px (<=0 = pie)")
     .AddFloat("startAngle", &UIRadialProgress::startAngle).Range(0, 360, 1).Tooltip("0 = 12 o'clock; degrees")
     .AddBool("clockwise", &UIRadialProgress::clockwise)
     .AddColor("background", &UIRadialProgress::background).Range(0, 1, 0.01).Tooltip("Background colour of the track")
     .AddColor("fill", &UIRadialProgress::fill).Range(0, 1, 0.01).Tooltip("Colour of the filled part")
     .AddEnum("anchor", &UIRadialProgress::anchor, AnchorNames()).Tooltip("The screen point the position is measured from")
     .AddBool("showPercent", &UIRadialProgress::showPercent).Tooltip("Draw the percentage on the bar")
     .AddColor("textColor", &UIRadialProgress::textColor).Range(0, 1, 0.01).Tooltip("Colour of the text")
     .AddBool("spin", &UIRadialProgress::spin)
     .AddFloat("spinSpeed", &UIRadialProgress::spinSpeed).Range(0, 10000, 0.1);
    b.Commit();
}

void RegisterUIRating() {
    PropertyBuilder<UIRating> b("UIRating", "uirating", "UI");
    b.AddVec2("position", &UIRating::position).Range(-kBig, kBig, 0.1).Tooltip("Pixels from the anchor (margins for a stretch anchor)")
     .AddVec2("size", &UIRating::size).Range(-kBig, kBig, 0.1).Tooltip("Width and height in pixels")
     .AddInt("count", &UIRating::count).Range(1, 50, 1).Tooltip("Number of stars")
     .AddFloat("value", &UIRating::value).Range(0, 50, 0.5).Tooltip("Filled stars (may be fractional)")
     .AddBool("allowHalf", &UIRating::allowHalf).Tooltip("Half-star precision")
     .AddBool("readOnly", &UIRating::readOnly).Tooltip("Display only (no click to set)")
     .AddColor("on", &UIRating::on).Range(0, 1, 0.01).Tooltip("Filled star")
     .AddColor("off", &UIRating::off).Range(0, 1, 0.01).Tooltip("Empty star")
     .AddEnum("anchor", &UIRating::anchor, AnchorNames()).Tooltip("The screen point the position is measured from");
    b.Commit();
}

void RegisterUIScrollView() {
    PropertyBuilder<UIScrollView> b("UIScrollView", "uiscroll", "UI");
    b.AddVec2("position", &UIScrollView::position).Range(-kBig, kBig, 0.1).Tooltip("Viewport top-left (pixels, from anchor)")
     .AddVec2("size", &UIScrollView::size).Range(-kBig, kBig, 0.1).Tooltip("Viewport size")
     .AddEnum("anchor", &UIScrollView::anchor, AnchorNames()).Tooltip("The screen point the position is measured from")
     .AddColor("background", &UIScrollView::background).Range(0, 1, 0.01).Tooltip("Background colour of the track")
     .AddEnum("axis", &UIScrollView::axis, {"Vertical", "Horizontal", "Both"})
     .AddFloat("contentHeight", &UIScrollView::contentHeight).Range(0, kBig, 0.05).Tooltip("Total height of the scrollable content")
     .AddFloat("contentWidth", &UIScrollView::contentWidth).Range(0, kBig, 0.05).Tooltip("Total width (horizontal scrolling)")
     .AddBool("autoContent", &UIScrollView::autoContent)
     .AddFloat("inertia", &UIScrollView::inertia).Range(0, 1000, 0.1)
     .AddBool("elastic", &UIScrollView::elastic).Tooltip("Let a drag or a glide pass the ends, then spring back")
     .AddEnum("barStyle", &UIScrollView::barStyle, {"Standard", "Thin", "AutoHide"}).Tooltip("Scrollbar: 0 = the current bar, 1 = thin, 2 = hidden until the view is scrolling");
    b.Commit();
}

void RegisterUILayoutGroup() {
    PropertyBuilder<UILayoutGroup> b("UILayoutGroup", "uilayout", "UI");
    b.AddEnum("direction", &UILayoutGroup::direction, {"Vertical", "Horizontal", "Grid", "FlexRow", "FlexColumn"})
     .AddEnum("anchor", &UILayoutGroup::anchor, AnchorNames()).Tooltip("The screen point the position is measured from")
     .AddVec2("origin", &UILayoutGroup::origin).Range(-kBig, kBig, 0.1).Tooltip("Where the first child is placed")
     .AddFloat("spacing", &UILayoutGroup::spacing).Range(0, kBig, 0.05).Tooltip("Gap between children (row gap in Grid)")
     .AddFloat("padding", &UILayoutGroup::padding).Range(-kBig, kBig, 0.1).Tooltip("Extra inset before the first child")
     .AddInt("columns", &UILayoutGroup::columns).Range(1, 64, 1).Tooltip("Grid: children per row")
     .AddFloat("spacingY", &UILayoutGroup::spacingY).Range(0, kBig, 0.05).Tooltip("Grid / Flex: gap between rows or lines (`spacing` is the main gap)")
     .AddVec2("boxSize", &UILayoutGroup::boxSize).Range(-kBig, kBig, 0.1).Tooltip("The flex container's size")
     .AddEnum("justify", &UILayoutGroup::justify, {"Start", "Center", "End", "SpaceBetween", "SpaceAround", "SpaceEvenly"}).Tooltip("Spare room along a line")
     .AddEnum("align", &UILayoutGroup::align, {"Start", "Center", "End", "Stretch"}).Tooltip("Placement across a line")
     .AddBool("wrap", &UILayoutGroup::wrap).Tooltip("Start a new line when the box is full");
    b.Commit();
}

void RegisterUIFrame() {
    PropertyBuilder<UIFrame> b("UIFrame", "uiframe", "UI");
    b.AddVec2("anchorMin", &UIFrame::anchorMin).Range(-10, 10, 0.01)
     .AddVec2("anchorMax", &UIFrame::anchorMax).Range(-10, 10, 0.01)
     .AddVec2("pivot", &UIFrame::pivot).Range(-10, 10, 0.01)
     .AddVec2("position", &UIFrame::position).Range(-kBig, kBig, 0.1).Tooltip("Pixels from the anchor (margins for a stretch anchor)")
     .AddVec2("size", &UIFrame::size).Range(-kBig, kBig, 0.1).Tooltip("Width and height in pixels");
    b.Commit();
}

void RegisterUIFlexItem() {
    PropertyBuilder<UIFlexItem> b("UIFlexItem", "uiflexitem", "UI");
    b.AddFloat("grow", &UIFlexItem::grow).Range(0, 1000, 0.1).Tooltip("Share of the line's spare room this item takes (0 = none)")
     .AddFloat("shrink", &UIFlexItem::shrink).Range(0, 1000, 0.1).Tooltip("Share of an overflow it gives up (0 = never shrinks)")
     .AddVec2("baseSize", &UIFlexItem::baseSize).Range(-kBig, kBig, 0.1)
     .AddEnum("alignSelf", &UIFlexItem::alignSelf, {"Auto", "Start", "Center", "End", "Stretch"}).Tooltip("Auto = the layout's alignment");
    b.Commit();
}

void RegisterUIRawImage() {
    PropertyBuilder<UIRawImage> b("UIRawImage", "uirawimage", "UI");
    b.AddVec2("position", &UIRawImage::position).Range(-kBig, kBig, 0.1).Tooltip("Pixels from the anchor (margins for a stretch anchor)")
     .AddVec2("size", &UIRawImage::size).Range(-kBig, kBig, 0.1).Tooltip("Width and height in pixels")
     .AddEnum("anchor", &UIRawImage::anchor, AnchorNames()).Tooltip("The screen point the position is measured from")
     .AddAssetPath("texture", &UIRawImage::texture, "image").Tooltip("Image path (PNG/JPG/BMP)")
     .AddColor("color", &UIRawImage::color).Range(0, 1, 0.01).Tooltip("Tint")
     .AddFloat("uvX", &UIRawImage::uvX).Range(-1000, 1000, 0.01)
     .AddFloat("uvY", &UIRawImage::uvY).Range(-1000, 1000, 0.01)
     .AddFloat("uvW", &UIRawImage::uvW).Range(-1000, 1000, 0.01)
     .AddFloat("uvH", &UIRawImage::uvH).Range(-1000, 1000, 0.01)
     .AddVec2("scroll", &UIRawImage::scroll).Range(-kBig, kBig, 0.1).Tooltip("UV units per second added to the offset (a scrolling background)");
    b.Commit();
}

void RegisterUIKeybindField() {
    PropertyBuilder<UIKeybindField> b("UIKeybindField", "uikeybind", "UI");
    b.AddVec2("position", &UIKeybindField::position).Range(-kBig, kBig, 0.1).Tooltip("Pixels from the anchor (margins for a stretch anchor)")
     .AddVec2("size", &UIKeybindField::size).Range(-kBig, kBig, 0.1).Tooltip("Width and height in pixels")
     .AddEnum("anchor", &UIKeybindField::anchor, AnchorNames()).Tooltip("The screen point the position is measured from")
     .AddString("action", &UIKeybindField::action)
     .AddKey("key", &UIKeybindField::key)
     .AddKey("defaultKey", &UIKeybindField::defaultKey)
     .AddString("prefKey", &UIKeybindField::prefKey).Tooltip("Prefs name to persist the binding under (\"\" = don't)")
     .AddString("inputAction", &UIKeybindField::inputAction).Tooltip("An InputActions action to rebind (\"\" = use `key`)")
     .AddBool("gamepad", &UIKeybindField::gamepad).Tooltip("With inputAction: bind a controller button")
     .AddInt("part", &UIKeybindField::part).Range(-1, 3, 1).Tooltip("With inputAction: 0..3 = one key of a four-key direction")
     .AddFloat("textSize", &UIKeybindField::textSize).Range(0, kBig, 0.05)
     .AddColor("background", &UIKeybindField::background).Range(0, 1, 0.01).Tooltip("Background colour of the track")
     .AddColor("keyColor", &UIKeybindField::keyColor).Range(0, 1, 0.01)
     .AddColor("listenColor", &UIKeybindField::listenColor).Range(0, 1, 0.01)
     .AddColor("textColor", &UIKeybindField::textColor).Range(0, 1, 0.01).Tooltip("Colour of the text")
     .AddBool("interactable", &UIKeybindField::interactable).Tooltip("When off the widget is greyed out and ignores input")
     .AddBool("focusable", &UIKeybindField::focusable);
    b.Commit();
}

void RegisterUIDraggable() {
    PropertyBuilder<UIDraggable> b("UIDraggable", "uidraggable", "UI");
    b.AddBool("returnToStart", &UIDraggable::returnToStart).Tooltip("Snap back if not dropped on a target")
     .AddBool("anyTarget", &UIDraggable::anyTarget).Tooltip("Any widget is a drop target (not just UIDropTarget)")
     .AddBool("snapToSlot", &UIDraggable::snapToSlot).Tooltip("Center the item in the slot it's dropped on")
     .AddEnum("axis", &UIDraggable::axis, {"Both", "Horizontal", "Vertical"})
     .AddFloat("dragThreshold", &UIDraggable::dragThreshold).Range(0, 3600, 0.05)
     .AddBool("dragParent", &UIDraggable::dragParent).Tooltip("Drag the parent widget instead of this one (a window title bar)")
     .AddBool("keepOnScreen", &UIDraggable::keepOnScreen).Tooltip("The dragged widget never leaves the screen")
     .AddBool("bringToFront", &UIDraggable::bringToFront).Tooltip("Draw on top of its siblings while being dragged");
    b.Commit();
}

void RegisterUITheme() {
    PropertyBuilder<UITheme> b("UITheme", "uitheme", "UI");
    b.AddString("themeName", &UITheme::themeName).Tooltip("Name scripts and UI Style Variant use to find this theme")
     .AddBool("applyOnStart", &UITheme::applyOnStart).Tooltip("Restyle the UI when Play starts")
     .AddBool("global", &UITheme::global).Tooltip("Style every widget in the scene, not only the ones under this object")
     .AddInt("preset", &UITheme::preset).Range(0, UITheme::kPresetCount - 1, 1).Tooltip("Which built-in look the colours came from (0 = custom)")
     .AddColor("panel", &UITheme::panel).Range(0, 1, 0.01)
     .AddColor("panelBottom", &UITheme::panelBottom).Range(0, 1, 0.01)
     .AddColor("accent", &UITheme::accent).Range(0, 1, 0.01)
     .AddColor("text", &UITheme::text).Range(0, 1, 0.01)
     .AddColor("muted", &UITheme::muted).Range(0, 1, 0.01)
     .AddColor("button", &UITheme::button).Range(0, 1, 0.01)
     .AddColor("buttonHover", &UITheme::buttonHover).Range(0, 1, 0.01)
     .AddColor("buttonPressed", &UITheme::buttonPressed).Range(0, 1, 0.01)
     .AddColor("buttonText", &UITheme::buttonText).Range(0, 1, 0.01)
     .AddColor("disabled", &UITheme::disabled).Range(0, 1, 0.01)
     .AddColor("danger", &UITheme::danger).Range(0, 1, 0.01)
     .AddColor("success", &UITheme::success).Range(0, 1, 0.01)
     .AddColor("border", &UITheme::border).Range(0, 1, 0.01)
     .AddColor("shadowCol", &UITheme::shadowCol).Range(0, 1, 0.01)
     .AddColor("glowColor", &UITheme::glowColor).Range(0, 1, 0.01)
     .AddFloat("cornerRadius", &UITheme::cornerRadius).Range(0, kBig, 0.5)
     .AddInt("cornerMask", &UITheme::cornerMask).Range(0, 15, 1)
     .AddFloat("borderWidth", &UITheme::borderWidth).Range(0, 100, 0.5)
     .AddBool("gradient", &UITheme::gradient)
     .AddInt("gradientDir", &UITheme::gradientDir).Range(0, 3, 1)
     .AddBool("shadow", &UITheme::shadow)
     .AddFloat("shadowSoftness", &UITheme::shadowSoftness).Range(0, 1000, 0.5)
     .AddVec2("shadowOffset", &UITheme::shadowOffset).Range(-1000, 1000, 0.5)
     .AddFloat("glow", &UITheme::glow).Range(0, 100, 0.5)
     .AddBool("sheen", &UITheme::sheen)
     .AddFloat("opacity", &UITheme::opacity).Range(0, 1, 0.01)
     .AddFloat("padding", &UITheme::padding).Range(0, 1000, 0.5)
     .AddFloat("fontScale", &UITheme::fontScale).Range(0, 100, 0.05)
     .AddFloat("textPixelSize", &UITheme::textPixelSize).Range(0, 100, 0.05)
     .AddFloat("transitionSpeed", &UITheme::transitionSpeed).Range(0, 1000, 0.5)
     .AddFloat("hoverScale", &UITheme::hoverScale).Range(0, 10, 0.01)
     .AddAssetPath("fontPath", &UITheme::fontPath, "font").Tooltip("A font put on every button and text the theme styles (empty = leave them)");
    b.Commit();
}

void RegisterUIStyleVariant() {
    PropertyBuilder<UIStyleVariant> b("UIStyleVariant", "uivariant", "UI");
    b.AddInt("variant", &UIStyleVariant::variant).Range(0, UITheme::kVariantCount - 1, 1)
        .Tooltip("0 Default, 1 Primary, 2 Secondary, 3 Danger, 4 Ghost, 5 Outline, 6 Pill")
     .AddString("themeName", &UIStyleVariant::themeName).Tooltip("Theme to take colours from (empty = the scene's first theme)");
    b.Commit();
}

void RegisterUIDamageNumbers() {
    PropertyBuilder<UIDamageNumbers> b("UIDamageNumbers", "uidamage", "UI");
    b.AddFloat("lifetime", &UIDamageNumbers::lifetime).Range(0.05, 60, 0.05).Tooltip("Seconds a number lives")
     .AddFloat("riseSpeed", &UIDamageNumbers::riseSpeed).Range(-2000, 2000, 1).Tooltip("Pixels per second upward")
     .AddFloat("gravity", &UIDamageNumbers::gravity).Range(-5000, 5000, 1).Tooltip("Pulls the number down after it rises (a lob)")
     .AddFloat("spread", &UIDamageNumbers::spread).Range(0, 1000, 1).Tooltip("Sideways drift in pixels, different for each number")
     .AddFloat("textSize", &UIDamageNumbers::textSize).Range(0.5, 50, 0.1)
     .AddFloat("critScale", &UIDamageNumbers::critScale).Range(0.1, 10, 0.05)
     .AddFloat("popScale", &UIDamageNumbers::popScale).Range(0.1, 10, 0.05)
     .AddInt("maxActive", &UIDamageNumbers::maxActive).Range(1, 500, 1)
     .AddVec2("spawnAt", &UIDamageNumbers::spawnAt).Range(-kBig, kBig, 1).Tooltip("Where a button's Hit, Crit and Heal actions put a number")
     .AddColor("normalColor", &UIDamageNumbers::normalColor).Range(0, 1, 0.01)
     .AddColor("critColor", &UIDamageNumbers::critColor).Range(0, 1, 0.01)
     .AddColor("healColor", &UIDamageNumbers::healColor).Range(0, 1, 0.01)
     .AddColor("missColor", &UIDamageNumbers::missColor).Range(0, 1, 0.01)
     .AddColor("infoColor", &UIDamageNumbers::infoColor).Range(0, 1, 0.01);
    b.Commit();
}

void RegisterUIDialogueBox() {
    PropertyBuilder<UIDialogueBox> b("UIDialogueBox", "uidialoguebox", "UI");
    b.AddFloat("charsPerSecond", &UIDialogueBox::charsPerSecond).Range(0, 2000, 1).Tooltip("Typewriter speed (0 = the whole line at once)")
     .AddBool("hideWhenDone", &UIDialogueBox::hideWhenDone).Tooltip("Close the box when a line without choices is advanced")
     .AddBool("advanceOnClick", &UIDialogueBox::advanceOnClick).Tooltip("A click on the box, Space or Enter finishes or advances the line")
     .AddBool("startHidden", &UIDialogueBox::startHidden).Tooltip("The box is hidden until a script shows a line");
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
    RegisterLoadingScreen();
    RegisterCrosshair();
    RegisterUIRadialProgress();
    RegisterUIRating();
    RegisterUIScrollView();
    RegisterUILayoutGroup();
    RegisterUIFrame();
    RegisterUIFlexItem();
    RegisterUIRawImage();
    RegisterUIKeybindField();
    RegisterUIDraggable();
    RegisterUITheme();
    RegisterUIStyleVariant();
    RegisterUIDamageNumbers();
    RegisterUIDialogueBox();
}

} // namespace detail
} // namespace okay
