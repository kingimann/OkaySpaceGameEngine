// Property registrations of the UI widgets: button, text, image, slider, toggle,
// progress bar, input field, stepper, dropdown, the loading screen and more. (Canvas and UIPanel are in
// BuiltinProperties.cpp.) Each type registers every field its scene record keeps; lists
// (the dropdown's options) and the localisation key are left out. See docs/properties.md.
#include "BuiltinPropertiesShared.hpp"

#include "okay/Components/Crosshair.hpp"
#include "okay/Components/LoadingScreen.hpp"
#include "okay/Components/TextRenderer.hpp"
#include "okay/Components/UIButton.hpp"
#include "okay/Components/TouchControls.hpp"
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
#include "okay/Components/UIInventoryBind.hpp"
#include "okay/Components/UIWidgetsMore.hpp"
#include "okay/Components/UIToast.hpp"
#include "okay/Components/UITooltip.hpp"
#include "okay/Components/UIModal.hpp"
#include "okay/Components/UIContextMenu.hpp"
#include "okay/Components/WorldSpaceUI.hpp"
#include "okay/Components/PauseMenu.hpp"
#include "okay/Components/ChestInventory.hpp"
#include "okay/Components/Minimap.hpp"
#include "okay/Components/UITextBind.hpp"
#include "okay/Components/UIBarBind.hpp"
#include "okay/Components/UIAnimation.hpp"
#include "okay/Components/CraftingMenu.hpp"
#include "okay/Components/InventoryUI.hpp"
#include "okay/Components/GridInventoryUI.hpp"
#include "okay/Components/CraftingStation.hpp"
#include "okay/Components/UIListView.hpp"
#include "okay/Components/UISlotGrid.hpp"
#include "okay/Components/UITabs.hpp"
#include "okay/Math/Easing.hpp"

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
    b.AddBool("richText", &TextRenderer::richText).Tooltip("Read [color=#ff0000]...[/color], [b]...[/b] and [i]...[/i] in the text. Screen-space text only: 3D text ignores it")
     .AddString("text", &TextRenderer::text).Tooltip("The text shown")
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
     .AddVec2("size", &UIFrame::size).Range(-kBig, kBig, 0.1).Tooltip("Width and height in pixels")
     .AddFloat("rotation", &UIFrame::rotation).Range(-3600, 3600, 1).Tooltip("Degrees clockwise around the pivot. Children turn with the frame")
     .AddVec2("scale", &UIFrame::scale).Range(-100, 100, 0.01).Tooltip("Width and height multipliers around the pivot. 1, 1 is unchanged. Children scale with the frame");
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

// WorldSpaceUI (record "worldui3d")
void RegisterWorldSpaceUI() {
    PropertyBuilder<WorldSpaceUI> b("WorldSpaceUI", "worldui3d", "UI");
    b.AddFloat("pixelsPerUnit", &WorldSpaceUI::pixelsPerUnit).Range(0.001, 1e6, 1).Tooltip("Design pixels per world unit (bigger = smaller in the world)")
     .AddBool("billboard", &WorldSpaceUI::billboard).Tooltip("Always face the camera (off = use the object's rotation)")
     .AddBool("constantSize", &WorldSpaceUI::constantSize).Tooltip("Keep the same size on screen at any distance")
     .AddFloat("constantScale", &WorldSpaceUI::constantScale).Range(0, 1e3, 0.05).Tooltip("Constant size: 1 draws the widget at its design pixels");
    b.Commit();
}

// PauseMenu (record "pausemenu"; `localized` is in the localization record). Only
// the fields that are saved: hint, showRestart, showVolume and restartScene are not.
void RegisterPauseMenu() {
    PropertyBuilder<PauseMenu> b("PauseMenu", "pausemenu", "UI");
    b.AddKey("toggleKey", &PauseMenu::toggleKey).Tooltip("Key that opens and closes the menu (Escape by default)")
     .AddString("title", &PauseMenu::title).Tooltip("Heading of the menu")
     .AddBool("showResume", &PauseMenu::showResume).Tooltip("Show the Resume button")
     .AddBool("showQuit", &PauseMenu::showQuit).Tooltip("Show the Quit button")
     .AddString("mainMenuScene", &PauseMenu::mainMenuScene).Tooltip("Scene the Main Menu button loads (empty = no button)")
     .AddColor("dimColor", &PauseMenu::dimColor).Range(0, 1, 0.01).Tooltip("Tint laid over the game")
     .AddColor("panelColor", &PauseMenu::panelColor).Range(0, 1, 0.01).Tooltip("Colour of the menu card")
     .AddBool("localized", &PauseMenu::localized).Tooltip("Take the texts from the string table in the current language");
    b.Commit();
}

// ChestInventory (record "chestinventory"). `open` and the drag state are runtime.
void RegisterChestInventory() {
    PropertyBuilder<ChestInventory> b("ChestInventory", "chestinventory", "UI");
    b.Section("Chest")
     .AddKey("openKey", &ChestInventory::openKey).Tooltip("Opens and closes the chest when in range")
     .AddFloat("range", &ChestInventory::range).Range(0, kBig, 0.1).Tooltip("How close the player must be (0 = any distance)")
     .AddString("playerName", &ChestInventory::playerName).Tooltip("Who counts as the player (empty = the default player)")
     .AddString("title", &ChestInventory::title).Tooltip("Heading of the chest window")
     .AddInt("cols", &ChestInventory::cols).Range(1, 256, 1).Tooltip("Columns in the chest grid")
     .AddBool("darkenWhenOpen", &ChestInventory::darkenWhenOpen).Tooltip("Darken the game while the chest is open")
     .Section("Style")
     .AddFloat("slotSize", &ChestInventory::slotSize).Range(1, 1e3, 1).Tooltip("Slot size in pixels")
     .AddFloat("gap", &ChestInventory::gap).Range(0, 1e3, 1).Tooltip("Pixels between slots")
     .AddFloat("cornerRadius", &ChestInventory::cornerRadius).Range(0, 1e3, 0.5).Tooltip("Corner radius of the slots")
     .AddString("iconFolder", &ChestInventory::iconFolder).Tooltip("Folder the item icons are loaded from")
     .AddColor("panelColor", &ChestInventory::panelColor).Range(0, 1, 0.01).Tooltip("Window colour")
     .AddColor("titleBar", &ChestInventory::titleBar).Range(0, 1, 0.01).Tooltip("Title bar colour")
     .AddColor("slotColor", &ChestInventory::slotColor).Range(0, 1, 0.01).Tooltip("Slot colour")
     .AddColor("slotBorder", &ChestInventory::slotBorder).Range(0, 1, 0.01).Tooltip("Slot border colour")
     .AddColor("textColor", &ChestInventory::textColor).Range(0, 1, 0.01).Tooltip("Text colour")
     .AddColor("hoverColor", &ChestInventory::hoverColor).Range(0, 1, 0.01).Tooltip("Tint on the slot under the pointer");
    b.Commit();
}

// UIDropTarget (record "uidroptarget")
void RegisterUIDropTarget() {
    PropertyBuilder<UIDropTarget> b("UIDropTarget", "uidroptarget", "UI");
    b.AddString("acceptTag", &UIDropTarget::acceptTag).Tooltip("Only items with this tag are accepted (empty = any)")
     .Section("Background")
     .AddBool("drawBackground", &UIDropTarget::drawBackground).Tooltip("Draw a slot box behind the items")
     .AddColor("background", &UIDropTarget::background).Range(0, 1, 0.01).Tooltip("Slot box colour")
     .AddFloat("cornerRadius", &UIDropTarget::cornerRadius).Range(0, 1e3, 0.5).Tooltip("Corner radius of the slot box")
     .AddFloat("borderWidth", &UIDropTarget::borderWidth).Range(0, 1e3, 0.5).Tooltip("Outline thickness in pixels (0 = none)")
     .AddColor("borderColor", &UIDropTarget::borderColor).Range(0, 1, 0.01).Tooltip("Outline colour")
     .Section("Hover")
     .AddBool("showHighlight", &UIDropTarget::showHighlight).Tooltip("Tint the slot while an item hovers over it")
     .AddColor("highlight", &UIDropTarget::highlight).Range(0, 1, 0.01).Tooltip("Tint when the item is accepted")
     .AddColor("rejectHighlight", &UIDropTarget::rejectHighlight).Range(0, 1, 0.01).Tooltip("Tint when the item has the wrong tag")
     .AddBool("snapToCenter", &UIDropTarget::snapToCenter).Tooltip("Centre a dropped item in the slot");
    b.Commit();
}

// Minimap (record "minimap"). The markers and map shapes are lists; the placed
// waypoint (hasUserWaypoint, userWaypoint) is play state.
void RegisterMinimap() {
    PropertyBuilder<Minimap> b("Minimap", "minimap", "UI");
    b.Section("Layout")
     .AddVec2("position", &Minimap::position).Range(-kBig, kBig, 1).Tooltip("Pixels from the anchor")
     .AddVec2("size", &Minimap::size).Range(-kBig, kBig, 1).Tooltip("Width and height in pixels")
     .AddEnum("anchor", &Minimap::anchor, AnchorNames()).Tooltip("The screen point the position is measured from")
     .AddColor("background", &Minimap::background).Range(0, 1, 0.01).Tooltip("Map background colour")
     .AddColor("border", &Minimap::border).Range(0, 1, 0.01).Tooltip("Border colour")
     .AddFloat("borderWidth", &Minimap::borderWidth).Range(0, 1e3, 0.5).Tooltip("Border thickness in pixels")
     .AddBool("circular", &Minimap::circular).Tooltip("Round map instead of a rectangle")
     .Section("View")
     .AddString("target", &Minimap::target).Tooltip("Object the map centres on (empty = the world origin)")
     .AddColor("targetColor", &Minimap::targetColor).Range(0, 1, 0.01).Tooltip("Colour of the centre marker")
     .AddFloat("worldPerPixel", &Minimap::worldPerPixel).Range(0.0001, kBig, 0.01).Tooltip("Zoom: world units per map pixel")
     .AddBool("useXZ", &Minimap::useXZ).Tooltip("3D: plot X and Z (off = 2D X and Y)")
     .AddBool("rotateWithTarget", &Minimap::rotateWithTarget).Tooltip("Turn the map so the target faces up")
     .AddBool("showSelf", &Minimap::showSelf).Tooltip("Draw the centre marker")
     .AddBool("playerArrow", &Minimap::playerArrow).Tooltip("Draw the centre marker as an arrow showing the facing")
     .Section("Blips")
     .AddFloat("blipSize", &Minimap::blipSize).Range(0, 1e3, 0.5).Tooltip("Default half-size of a blip in pixels")
     .AddFloat("blipRange", &Minimap::blipRange).Range(0, kBig, 1).Tooltip("Hide blips farther than this (0 = no limit)")
     .AddBool("clampBlips", &Minimap::clampBlips).Tooltip("Pin blips outside the map to its edge")
     .Section("Grid, cone, rings and north")
     .AddBool("showGrid", &Minimap::showGrid).Tooltip("Draw a faint reference grid")
     .AddColor("gridColor", &Minimap::gridColor).Range(0, 1, 0.01).Tooltip("Grid colour")
     .AddFloat("gridSpacing", &Minimap::gridSpacing).Range(1, 1e4, 1).Tooltip("Grid line spacing in map pixels")
     .AddBool("viewCone", &Minimap::viewCone).Tooltip("Draw the target's view cone")
     .AddFloat("viewConeAngle", &Minimap::viewConeAngle).Range(0, 360, 1).Tooltip("Full cone angle in degrees")
     .AddFloat("viewConeLength", &Minimap::viewConeLength).Range(0, 1e4, 1).Tooltip("Cone reach in map pixels")
     .AddColor("viewConeColor", &Minimap::viewConeColor).Range(0, 1, 0.01).Tooltip("Cone colour")
     .AddInt("rangeRings", &Minimap::rangeRings).Range(0, 64, 1).Tooltip("Number of range rings (0 = off)")
     .AddColor("ringColor", &Minimap::ringColor).Range(0, 1, 0.01).Tooltip("Ring colour")
     .AddBool("showNorth", &Minimap::showNorth).Tooltip("Draw a north mark")
     .AddColor("northColor", &Minimap::northColor).Range(0, 1, 0.01).Tooltip("North mark colour")
     .Section("Labels and route")
     .AddBool("showLabels", &Minimap::showLabels).Tooltip("Draw name labels by blips and markers")
     .AddColor("labelColor", &Minimap::labelColor).Range(0, 1, 0.01).Tooltip("Label colour")
     .AddFloat("labelSize", &Minimap::labelSize).Range(1, 512, 0.5).Tooltip("Label text height in pixels")
     .AddInt("routeMarker", &Minimap::routeMarker).Range(-1, 1000000, 1).Tooltip("Marker to draw a route line to (-1 = none)")
     .AddColor("routeColor", &Minimap::routeColor).Range(0, 1, 0.01).Tooltip("Route line colour")
     .AddFloat("routeWidth", &Minimap::routeWidth).Range(0, 1e3, 0.5).Tooltip("Route line width in pixels")
     .Section("Map image and full screen")
     .AddAssetPath("mapTexture", &Minimap::mapTexture, "image").Tooltip("Image drawn under the blips (empty = none)")
     .AddFloat("mapWorldSize", &Minimap::mapWorldSize).Range(0.001, kBig, 1).Tooltip("World units the image spans")
     .AddVec2("mapWorldCenter", &Minimap::mapWorldCenter).Range(-kBig, kBig, 1).Tooltip("World point (east, north) at the image centre")
     .AddKey("fullscreenKey", &Minimap::fullscreenKey).Tooltip("Opens the full-screen map (0 = off)")
     .AddFloat("fullscreenZoom", &Minimap::fullscreenZoom).Range(0.001, 1e4, 0.1).Tooltip("Zoom multiplier while full screen")
     .AddFloat("fullscreenFrac", &Minimap::fullscreenFrac).Range(0.05, 1, 0.01).Tooltip("Part of the screen the full map fills")
     .AddBool("mapClickWaypoint", &Minimap::mapClickWaypoint).Tooltip("Left-click the open map to place a waypoint, right-click to clear it")
     .AddColor("userWaypointColor", &Minimap::userWaypointColor).Range(0, 1, 0.01).Tooltip("Placed waypoint colour")
     .Section("Zoom keys")
     .AddKey("zoomInKey", &Minimap::zoomInKey).Tooltip("Zooms in (0 = off)")
     .AddKey("zoomOutKey", &Minimap::zoomOutKey).Tooltip("Zooms out (0 = off)")
     .AddFloat("minZoom", &Minimap::minZoom).Range(0.0001, kBig, 0.01).Tooltip("Smallest world units per pixel")
     .AddFloat("maxZoom", &Minimap::maxZoom).Range(0.0001, kBig, 0.1).Tooltip("Largest world units per pixel")
     .AddFloat("zoomStep", &Minimap::zoomStep).Range(1, 100, 0.01).Tooltip("Zoom multiplier per key press");
    b.Commit();
}

// UITextBind (record "uibind")
void RegisterUITextBind() {
    PropertyBuilder<UITextBind> b("UITextBind", "uibind", "UI");
    b.AddString("format", &UITextBind::format).Tooltip("Text with {name} parts filled from values, for example \"HP {health}\"");
    b.Commit();
}

// UIBarBind (record "uibarbind")
void RegisterUIBarBind() {
    PropertyBuilder<UIBarBind> b("UIBarBind", "uibarbind", "UI");
    b.AddString("var", &UIBarBind::var).Tooltip("Value that fills the bar")
     .AddFloat("min", &UIBarBind::min).Range(-kBig, kBig, 0.1).Tooltip("Value at an empty bar")
     .AddFloat("max", &UIBarBind::max).Range(-kBig, kBig, 0.1).Tooltip("Value at a full bar");
    b.Commit();
}

// UIAnimation (record "uianim")
void RegisterUIAnimation() {
    std::vector<std::string> eases;
    for (int i = 0; i < (int)Ease::Count; ++i) eases.push_back(Easing::Name((Ease)i));
    PropertyBuilder<UIAnimation> b("UIAnimation", "uianim", "UI");
    b.AddEnum("intro", &UIAnimation::intro, {"None", "FadeIn", "SlideLeft", "SlideRight", "SlideTop", "SlideBottom", "Pop", "Drop", "Grow"}).Tooltip("How the widget appears")
     .AddEnum("loop", &UIAnimation::loop, {"None", "Pulse", "Bob", "Sway", "Blink", "Breathe"}).Tooltip("Movement while it is shown")
     .AddEnum("outro", &UIAnimation::outro, {"FadeOut", "SlideLeft", "SlideRight", "SlideTop", "SlideBottom", "Shrink"}).Tooltip("How it leaves")
     .AddFloat("duration", &UIAnimation::duration).Range(0, 1e4, 0.01).Tooltip("Intro and outro length in seconds")
     .AddFloat("delay", &UIAnimation::delay).Range(0, 1e4, 0.01).Tooltip("Wait before the intro, in seconds")
     .AddFloat("distance", &UIAnimation::distance).Range(0, kBig, 1).Tooltip("Slide distance in design pixels")
     .AddEnum("ease", &UIAnimation::ease, eases).Tooltip("Easing curve")
     .AddFloat("loopSpeed", &UIAnimation::loopSpeed).Range(0, 1e3, 0.05).Tooltip("Loop cycles per second")
     .AddFloat("loopAmount", &UIAnimation::loopAmount).Range(0, 1e3, 0.01).Tooltip("Pulse and breathe: scale amount; bob and sway: pixels / 100")
     .AddBool("playOnStart", &UIAnimation::playOnStart).Tooltip("Run the intro when the scene starts")
     .AddBool("replayOnShow", &UIAnimation::replayOnShow).Tooltip("Run it again each time the object is shown")
     .AddBool("hideAfterOutro", &UIAnimation::hideAfterOutro).Tooltip("Hide the object when the outro ends");
    b.Commit();
}

// CraftingMenu (record "craftmenu")
void RegisterCraftingMenu() {
    PropertyBuilder<CraftingMenu> b("CraftingMenu", "craftmenu", "UI");
    b.AddKey("toggleKey", &CraftingMenu::toggleKey).Tooltip("Shows and hides the panel (0 = always shown)")
     .AddBool("open", &CraftingMenu::open).Tooltip("Shown at the start")
     .AddVec2("position", &CraftingMenu::position).Range(-kBig, kBig, 1).Tooltip("Pixels from the anchor")
     .AddVec2("buttonSize", &CraftingMenu::buttonSize).Range(0, kBig, 1).Tooltip("Size of each recipe button in pixels")
     .AddFloat("spacing", &CraftingMenu::spacing).Range(0, 1e3, 1).Tooltip("Pixels between buttons")
     .AddEnum("anchor", &CraftingMenu::anchor, AnchorNames()).Tooltip("The screen point the position is measured from");
    b.Commit();
}

// InventoryUI (record "inventoryui"). The rarity rules are a list; open, scroll and
// the drag and navigation state are play state. gamepadNav is not saved by the
// record, so it is not a property yet.
void RegisterInventoryUI() {
    PropertyBuilder<InventoryUI> b("InventoryUI", "inventoryui", "UI");
    b.Section("Hotbar and backpack")
     .AddInt("hotbarSlots", &InventoryUI::hotbarSlots).Range(0, 64, 1).Tooltip("Slots on the hotbar")
     .AddInt("backpackRows", &InventoryUI::backpackRows).Range(0, 256, 1).Tooltip("Extra rows when open, if the Inventory has no capacity")
     .AddInt("selected", &InventoryUI::selected).Range(0, 63, 1).Tooltip("Selected hotbar slot at the start")
     .AddKey("toggleKey", &InventoryUI::toggleKey).Tooltip("Opens and closes the backpack")
     .AddBool("selectHotkeys", &InventoryUI::selectHotkeys).Tooltip("Keys 1 to 9 select a hotbar slot")
     .AddBool("scrollSelect", &InventoryUI::scrollSelect).Tooltip("The mouse wheel changes the selection")
     .AddBool("dragItems", &InventoryUI::dragItems).Tooltip("Drag items between slots while the backpack is open")
     .AddBool("darkenWhenOpen", &InventoryUI::darkenWhenOpen).Tooltip("Darken the game while the backpack is open")
     .AddString("inventoryTarget", &InventoryUI::inventoryTarget).Tooltip("Object whose Inventory it shows, by name (empty = this object)")
     .Section("Keys and clicks")
     .AddKey("sortKey", &InventoryUI::sortKey).Tooltip("Sorts the bag (0 = off)")
     .AddEnum("sortMode", &InventoryUI::sortMode, {"Compact", "Name", "Count", "Rarity", "Category", "Weight", "Value"}).Tooltip("How the sort key orders the bag")
     .AddBool("sortOnClose", &InventoryUI::sortOnClose).Tooltip("Sort the bag when the backpack closes")
     .AddKey("dropKey", &InventoryUI::dropKey).Tooltip("Drops one of the selected item (0 = off)")
     .AddKey("useKey", &InventoryUI::useKey).Tooltip("Uses the selected item (0 = off)")
     .AddBool("clickToUse", &InventoryUI::clickToUse).Tooltip("A left click uses the selected item while the backpack is closed")
     .AddBool("splitRightClick", &InventoryUI::splitRightClick).Tooltip("Right-click a stack to pick up part of it")
     .AddBool("splitHalf", &InventoryUI::splitHalf).Tooltip("Right-click lifts half the stack (off = one)")
     .AddBool("shiftQuickMove", &InventoryUI::shiftQuickMove).Tooltip("Shift-click moves a stack between hotbar and backpack")
     .Section("Layout")
     .AddFloat("slotSize", &InventoryUI::slotSize).Range(1, 1e3, 1).Tooltip("Slot size in pixels at the reference height")
     .AddFloat("slotGap", &InventoryUI::slotGap).Range(0, 1e3, 1).Tooltip("Pixels between slots")
     .AddBool("scaleToScreen", &InventoryUI::scaleToScreen).Tooltip("Scale the whole UI with the screen height")
     .AddFloat("referenceHeight", &InventoryUI::referenceHeight).Range(1, 1e5, 1).Tooltip("Screen height the sizes are set for")
     .AddFloat("marginX", &InventoryUI::marginX).Range(-1e5, 1e5, 1).Tooltip("Sideways nudge from the centre in pixels")
     .AddFloat("marginY", &InventoryUI::marginY).Range(-1e5, 1e5, 1).Tooltip("Distance from the screen edge in pixels")
     .AddBool("anchorTop", &InventoryUI::anchorTop).Tooltip("Put the hotbar at the top")
     .AddEnum("hAlign", &InventoryUI::hAlign, {"Left", "Center", "Right"}).Tooltip("Hotbar alignment")
     .AddFloat("selectedScale", &InventoryUI::selectedScale).Range(0.1, 10, 0.01).Tooltip("Size of the selected slot (1 = the same as the others)")
     .AddFloat("cornerRadius", &InventoryUI::cornerRadius).Range(0, 1e3, 0.5).Tooltip("Slot corner radius")
     .AddFloat("borderWidth", &InventoryUI::borderWidth).Range(0, 1e3, 0.5).Tooltip("Slot border thickness in pixels")
     .AddFloat("iconInset", &InventoryUI::iconInset).Range(0, 1e3, 0.5).Tooltip("Pixels between a slot's border and its icon")
     .Section("Text")
     .AddString("iconFolder", &InventoryUI::iconFolder).Tooltip("Folder of <item>.png icons")
     .AddFloat("labelScale", &InventoryUI::labelScale).Range(0.1, 20, 0.05).Tooltip("Size of the name and count text")
     .AddBool("showCounts", &InventoryUI::showCounts).Tooltip("Draw stack counts")
     .AddBool("showNames", &InventoryUI::showNames).Tooltip("Draw the item name when there is no icon")
     .AddInt("nameChars", &InventoryUI::nameChars).Range(0, 256, 1).Tooltip("Characters of the item name shown in a slot")
     .AddBool("showSelectedName", &InventoryUI::showSelectedName).Tooltip("Show the held item's name above the hotbar")
     .AddBool("slotNumbers", &InventoryUI::slotNumbers).Tooltip("Draw 1 to 9 in the hotbar slots")
     .AddBool("showTooltips", &InventoryUI::showTooltips).Tooltip("Hovering a slot shows the item's name and count")
     .AddBool("showTitle", &InventoryUI::showTitle).Tooltip("Show a title over the backpack")
     .AddString("backpackTitle", &InventoryUI::backpackTitle).Tooltip("Backpack title text")
     .Section("Panel")
     .AddBool("showPanel", &InventoryUI::showPanel).Tooltip("Draw a rounded panel behind the slots")
     .AddFloat("panelPad", &InventoryUI::panelPad).Range(0, 1e3, 1).Tooltip("Panel padding around the slots")
     .AddFloat("panelBorderWidth", &InventoryUI::panelBorderWidth).Range(0, 1e3, 0.5).Tooltip("Panel outline thickness in pixels")
     .Section("Colours")
     .AddColor("slotColor", &InventoryUI::slotColor).Range(0, 1, 0.01).Tooltip("Slot colour")
     .AddColor("emptySlotColor", &InventoryUI::emptySlotColor).Range(0, 1, 0.01).Tooltip("Empty slot colour")
     .AddColor("hotbarColor", &InventoryUI::hotbarColor).Range(0, 1, 0.01).Tooltip("Hotbar slot colour")
     .AddColor("slotBorder", &InventoryUI::slotBorder).Range(0, 1, 0.01).Tooltip("Slot border colour")
     .AddColor("selectedColor", &InventoryUI::selectedColor).Range(0, 1, 0.01).Tooltip("Selected slot colour")
     .AddColor("hoverColor", &InventoryUI::hoverColor).Range(0, 1, 0.01).Tooltip("Tint on the slot under the pointer")
     .AddColor("textColor", &InventoryUI::textColor).Range(0, 1, 0.01).Tooltip("Text colour")
     .AddColor("countColor", &InventoryUI::countColor).Range(0, 1, 0.01).Tooltip("Count colour")
     .AddColor("numberColor", &InventoryUI::numberColor).Range(0, 1, 0.01).Tooltip("Slot number colour")
     .AddColor("panelColor", &InventoryUI::panelColor).Range(0, 1, 0.01).Tooltip("Panel colour")
     .AddColor("panelBorder", &InventoryUI::panelBorder).Range(0, 1, 0.01).Tooltip("Panel outline colour")
     .AddColor("titleColor", &InventoryUI::titleColor).Range(0, 1, 0.01).Tooltip("Title colour")
     .AddColor("tooltipColor", &InventoryUI::tooltipColor).Range(0, 1, 0.01).Tooltip("Tooltip background")
     .AddColor("tooltipText", &InventoryUI::tooltipText).Range(0, 1, 0.01).Tooltip("Tooltip text colour");
    b.Commit();
}

// GridInventoryUI (record "gridinventoryui"). The rarity rules are a list; the
// drag state and scroll are play state.
void RegisterGridInventoryUI() {
    PropertyBuilder<GridInventoryUI> b("GridInventoryUI", "gridinventoryui", "UI");
    b.Section("Grid")
     .AddFloat("cellSize", &GridInventoryUI::cellSize).Range(1, 1e3, 1).Tooltip("Cell size in pixels")
     .AddFloat("gap", &GridInventoryUI::gap).Range(0, 1e3, 1).Tooltip("Pixels between cells")
     .AddFloat("cornerRadius", &GridInventoryUI::cornerRadius).Range(0, 1e3, 0.5).Tooltip("Corner radius")
     .AddString("iconFolder", &GridInventoryUI::iconFolder).Tooltip("Folder of <item>.png icons")
     .AddBool("darkenWhenOpen", &GridInventoryUI::darkenWhenOpen).Tooltip("Darken the game while it is open")
     .AddBool("showWeight", &GridInventoryUI::showWeight).Tooltip("Show the carried weight")
     .AddBool("showTooltips", &GridInventoryUI::showTooltips).Tooltip("Hovering an item shows its name, size and weight")
     .AddKey("rotateKey", &GridInventoryUI::rotateKey).Tooltip("While dragging, turns the item 90 degrees (0 = off)")
     .AddBool("showItemNames", &GridInventoryUI::showItemNames).Tooltip("Draw the item name on each tile")
     .AddFloat("itemNameScale", &GridInventoryUI::itemNameScale).Range(0.1, 20, 0.05).Tooltip("Item name text size")
     .Section("Containers")
     .AddBool("multiContainer", &GridInventoryUI::multiContainer).Tooltip("Show nearby ground containers in a second column")
     .AddFloat("nearbyRange", &GridInventoryUI::nearbyRange).Range(0, 1e5, 0.1).Tooltip("How far to look for nearby containers")
     .AddString("nearbyTitle", &GridInventoryUI::nearbyTitle).Tooltip("Header above the nearby column")
     .AddFloat("columnGap", &GridInventoryUI::columnGap).Range(0, 1e4, 1).Tooltip("Gap between the columns")
     .AddFloat("panelPad", &GridInventoryUI::panelPad).Range(0, 1e4, 1).Tooltip("Padding inside each column's panel")
     .AddFloat("containerGap", &GridInventoryUI::containerGap).Range(0, 1e4, 1).Tooltip("Gap between stacked containers")
     .AddFloat("headerHeight", &GridInventoryUI::headerHeight).Range(0, 1e4, 1).Tooltip("Height of each container's header")
     .AddBool("showMasterTitle", &GridInventoryUI::showMasterTitle).Tooltip("Show the big title above the bags")
     .AddString("masterTitle", &GridInventoryUI::masterTitle).Tooltip("Big title text")
     .AddFloat("titleScale", &GridInventoryUI::titleScale).Range(0.1, 20, 0.05).Tooltip("Big title text size")
     .Section("Style")
     .AddFloat("backdropDim", &GridInventoryUI::backdropDim).Range(0, 1, 0.01).Tooltip("How dark the backdrop is")
     .AddBool("useGradients", &GridInventoryUI::useGradients).Tooltip("Soft gradients on panels and tiles")
     .AddBool("dropShadows", &GridInventoryUI::dropShadows).Tooltip("Shadows behind panels and the carried item")
     .AddBool("accentBars", &GridInventoryUI::accentBars).Tooltip("Coloured accent stripes on headers and tiles")
     .AddBool("autoAccent", &GridInventoryUI::autoAccent).Tooltip("Pick each accent from the name (off = the accent colour)")
     .Section("Colours")
     .AddColor("panelColor", &GridInventoryUI::panelColor).Range(0, 1, 0.01).Tooltip("Panel colour")
     .AddColor("cellColor", &GridInventoryUI::cellColor).Range(0, 1, 0.01).Tooltip("Cell colour")
     .AddColor("itemColor", &GridInventoryUI::itemColor).Range(0, 1, 0.01).Tooltip("Item tile colour")
     .AddColor("itemBorder", &GridInventoryUI::itemBorder).Range(0, 1, 0.01).Tooltip("Item tile border")
     .AddColor("textColor", &GridInventoryUI::textColor).Range(0, 1, 0.01).Tooltip("Text colour")
     .AddColor("titleBar", &GridInventoryUI::titleBar).Range(0, 1, 0.01).Tooltip("Title strip colour")
     .AddColor("hoverColor", &GridInventoryUI::hoverColor).Range(0, 1, 0.01).Tooltip("Tint over the hovered item")
     .AddColor("dropOk", &GridInventoryUI::dropOk).Range(0, 1, 0.01).Tooltip("Drop target colour when the item fits")
     .AddColor("dropBad", &GridInventoryUI::dropBad).Range(0, 1, 0.01).Tooltip("Drop target colour when it is blocked")
     .AddColor("tooltipColor", &GridInventoryUI::tooltipColor).Range(0, 1, 0.01).Tooltip("Tooltip background")
     .AddColor("tooltipText", &GridInventoryUI::tooltipText).Range(0, 1, 0.01).Tooltip("Tooltip text colour")
     .AddColor("overweightColor", &GridInventoryUI::overweightColor).Range(0, 1, 0.01).Tooltip("Weight text colour when over the limit")
     .AddColor("accentColor", &GridInventoryUI::accentColor).Range(0, 1, 0.01).Tooltip("Fixed accent colour")
     .AddColor("headerColor", &GridInventoryUI::headerColor).Range(0, 1, 0.01).Tooltip("Container header colour")
     .AddColor("weightGood", &GridInventoryUI::weightGood).Range(0, 1, 0.01).Tooltip("Weight bar colour well under the limit")
     .AddColor("weightWarn", &GridInventoryUI::weightWarn).Range(0, 1, 0.01).Tooltip("Weight bar colour near the limit");
    b.Commit();
}

// CraftingStation (record "craftingstation"). The recipes are a list; `open` is play state.
void RegisterCraftingStation() {
    PropertyBuilder<CraftingStation> b("CraftingStation", "craftingstation", "UI");
    b.Section("Station")
     .AddKey("toggleKey", &CraftingStation::toggleKey).Tooltip("Opens and closes the station when in range")
     .AddFloat("range", &CraftingStation::range).Range(0, kBig, 0.1).Tooltip("How close the player must be (0 = any distance)")
     .AddString("playerName", &CraftingStation::playerName).Tooltip("Who counts as the player (empty = the default player)")
     .AddAssetPath("recipeBook", &CraftingStation::recipeBook, "file").Tooltip("Optional .okayrecipes file")
     .AddString("stationTag", &CraftingStation::stationTag).Tooltip("Bench kind (workbench, forge ...): book recipes that need it show here")
     .AddString("title", &CraftingStation::title).Tooltip("Heading of the window")
     .AddBool("closeWhenCrafted", &CraftingStation::closeWhenCrafted).Tooltip("Close after a successful craft")
     .Section("Style")
     .AddFloat("rowHeight", &CraftingStation::rowHeight).Range(1, 1e3, 1).Tooltip("Recipe row height in pixels")
     .AddFloat("width", &CraftingStation::width).Range(1, 1e4, 1).Tooltip("Window width in pixels")
     .AddFloat("marginX", &CraftingStation::marginX).Range(-1e5, 1e5, 1).Tooltip("Sideways nudge in pixels")
     .AddFloat("marginY", &CraftingStation::marginY).Range(-1e5, 1e5, 1).Tooltip("Up and down nudge in pixels")
     .AddFloat("cornerRadius", &CraftingStation::cornerRadius).Range(0, 1e3, 0.5).Tooltip("Corner radius")
     .AddString("iconFolder", &CraftingStation::iconFolder).Tooltip("Folder the item icons are loaded from")
     .AddColor("panelColor", &CraftingStation::panelColor).Range(0, 1, 0.01).Tooltip("Window colour")
     .AddColor("titleBar", &CraftingStation::titleBar).Range(0, 1, 0.01).Tooltip("Title bar colour")
     .AddColor("rowColor", &CraftingStation::rowColor).Range(0, 1, 0.01).Tooltip("Row colour")
     .AddColor("canColor", &CraftingStation::canColor).Range(0, 1, 0.01).Tooltip("Row tint when it can be crafted")
     .AddColor("cantColor", &CraftingStation::cantColor).Range(0, 1, 0.01).Tooltip("Row tint when items are missing")
     .AddColor("textColor", &CraftingStation::textColor).Range(0, 1, 0.01).Tooltip("Text colour")
     .AddColor("hoverColor", &CraftingStation::hoverColor).Range(0, 1, 0.01).Tooltip("Tint on the row under the pointer");
    b.Commit();
}

// UIListView (record "uilist"). The item list is a list; scroll is play state.
void RegisterUIListView() {
    PropertyBuilder<UIListView> b("UIListView", "uilist", "UI");
    b.AddVec2("position", &UIListView::position).Range(-kBig, kBig, 1).Tooltip("Pixels from the anchor")
     .AddVec2("size", &UIListView::size).Range(-kBig, kBig, 1).Tooltip("Width and height in pixels")
     .AddEnum("anchor", &UIListView::anchor, AnchorNames()).Tooltip("The screen point the position is measured from")
     .AddColor("background", &UIListView::background).Range(0, 1, 0.01).Tooltip("List background colour")
     .AddColor("barColor", &UIListView::barColor).Range(0, 1, 0.01).Tooltip("Scroll bar colour")
     .AddFloat("itemHeight", &UIListView::itemHeight).Range(1, 1e4, 1).Tooltip("Row height in pixels")
     .AddFloat("spacing", &UIListView::spacing).Range(0, 1e3, 1).Tooltip("Pixels between rows")
     .AddInt("itemCount", &UIListView::itemCount).Range(0, 1000000, 1).Tooltip("Number of rows when the item list is empty")
     .AddInt("selected", &UIListView::selected).Range(-1, 1000000, 1).Tooltip("Selected row (-1 = none)")
     .AddFloat("wheelSpeed", &UIListView::wheelSpeed).Range(0, 1e4, 1).Tooltip("Pixels scrolled per wheel step");
    b.Commit();
}

// UISlotGrid (record "uislotgrid"). The labels are a list. fontScale,
// numberEmptySlots and textColor are saved in the shared "uix" record.
void RegisterUISlotGrid() {
    PropertyBuilder<UISlotGrid> b("UISlotGrid", "uislotgrid", "UI");
    b.AddVec2("position", &UISlotGrid::position).Range(-kBig, kBig, 1).Tooltip("Pixels from the anchor")
     .AddEnum("anchor", &UISlotGrid::anchor, AnchorNames()).Tooltip("The screen point the position is measured from")
     .AddInt("columns", &UISlotGrid::columns).Range(1, 256, 1).Tooltip("Slots across")
     .AddInt("rows", &UISlotGrid::rows).Range(1, 256, 1).Tooltip("Slots down")
     .AddVec2("cell", &UISlotGrid::cell).Range(0, 1e4, 1).Tooltip("Slot size in pixels")
     .AddFloat("gap", &UISlotGrid::gap).Range(0, 1e3, 1).Tooltip("Pixels between slots")
     .AddInt("value", &UISlotGrid::value).Range(-1, 65535, 1).Tooltip("Selected slot (-1 = none)")
     .AddColor("color", &UISlotGrid::color).Range(0, 1, 0.01).Tooltip("Slot colour")
     .AddColor("selectedColor", &UISlotGrid::selectedColor).Range(0, 1, 0.01).Tooltip("Selected slot colour")
     .AddColor("hoverColor", &UISlotGrid::hoverColor).Range(0, 1, 0.01).Tooltip("Colour of the slot under the pointer")
     .AddFloat("fontScale", &UISlotGrid::fontScale).Range(0.1, 64, 0.05).Tooltip("Slot label text size")
     .AddColor("textColor", &UISlotGrid::textColor).Range(0, 1, 0.01).Tooltip("Slot label colour")
     .AddBool("numberEmptySlots", &UISlotGrid::numberEmptySlots).Tooltip("A slot with no label shows its number (off = blank)");
    b.Commit();
}

// UITabs (record "uitabs"). The tab names, pages, icons, badges and disabled
// flags are lists; the layout cache and scroll are play state.
void RegisterUITabs() {
    PropertyBuilder<UITabs> b("UITabs", "uitabs", "UI");
    b.Section("Layout")
     .AddVec2("position", &UITabs::position).Range(-kBig, kBig, 1).Tooltip("Pixels from the anchor")
     .AddVec2("size", &UITabs::size).Range(-kBig, kBig, 1).Tooltip("Width and height in pixels")
     .AddEnum("anchor", &UITabs::anchor, AnchorNames()).Tooltip("The screen point the position is measured from")
     .AddInt("value", &UITabs::value).Range(0, 65535, 1).Tooltip("Selected tab")
     .AddEnum("style", &UITabs::style, {"Segmented", "Underline", "Folder", "Pills", "Vertical"}).Tooltip("Look of the tab strip")
     .AddEnum("sizing", &UITabs::sizing, {"Equal", "FitText"}).Tooltip("Equal tabs, or each as wide as its text")
     .AddEnum("shape", &UITabs::shape, ShapeNames()).Tooltip("Tab silhouette")
     .AddFloat("cornerRadius", &UITabs::cornerRadius).Range(0, 1e3, 0.5).Tooltip("Corner radius")
     .AddFloat("spacing", &UITabs::spacing).Range(0, 1e3, 1).Tooltip("Gap between tabs in pixels")
     .AddFloat("padding", &UITabs::padding).Range(0, 1e3, 1).Tooltip("Space either side of a tab's content")
     .AddFloat("fontScale", &UITabs::fontScale).Range(0.1, 64, 0.05).Tooltip("Label text size")
     .AddFloat("minTabSize", &UITabs::minTabSize).Range(0, 1e4, 1).Tooltip("Shortest a tab may be (0 = no limit)")
     .AddFloat("indicatorThickness", &UITabs::indicatorThickness).Range(0, 1e3, 0.5).Tooltip("Underline or accent thickness in pixels")
     .AddFloat("iconSize", &UITabs::iconSize).Range(0, 1e3, 1).Tooltip("Icon size in pixels")
     .Section("Colours")
     .AddColor("background", &UITabs::background).Range(0, 1, 0.01).Tooltip("Track and unselected tab colour")
     .AddColor("selected", &UITabs::selected).Range(0, 1, 0.01).Tooltip("Selected tab or indicator colour")
     .AddColor("textColor", &UITabs::textColor).Range(0, 1, 0.01).Tooltip("Text colour")
     .AddColor("selectedTextColor", &UITabs::selectedTextColor).Range(0, 1, 0.01).Tooltip("Selected tab text colour")
     .AddColor("hoverColor", &UITabs::hoverColor).Range(0, 1, 0.01).Tooltip("Tint on the tab under the pointer")
     .AddColor("pressedColor", &UITabs::pressedColor).Range(0, 1, 0.01).Tooltip("Tint while a tab is pressed")
     .AddColor("badgeColor", &UITabs::badgeColor).Range(0, 1, 0.01).Tooltip("Badge colour")
     .AddColor("badgeTextColor", &UITabs::badgeTextColor).Range(0, 1, 0.01).Tooltip("Badge text colour")
     .Section("Behaviour")
     .AddBool("interactable", &UITabs::interactable).Tooltip("Tabs can be clicked")
     .AddBool("closable", &UITabs::closable).Tooltip("Each tab has a close button")
     .AddBool("animate", &UITabs::animate).Tooltip("Slide the indicator to the new tab")
     .AddFloat("animSpeed", &UITabs::animSpeed).Range(0, 1e3, 0.5).Tooltip("Indicator slide speed")
     .AddBool("hotkeys", &UITabs::hotkeys).Tooltip("The previous and next keys change tab")
     .AddString("prevKey", &UITabs::prevKey).Tooltip("Key for the previous tab")
     .AddString("nextKey", &UITabs::nextKey).Tooltip("Key for the next tab")
     .AddBool("wrapAround", &UITabs::wrapAround).Tooltip("Going past the last tab returns to the first")
     .AddBool("focusable", &UITabs::focusable).Tooltip("The tab strip takes keyboard and gamepad focus");
    b.Commit();
}

// UIToast (record "uitoast"). Live toast cards are ordinary children, not properties.
void RegisterUIToast() {
    PropertyBuilder<UIToast> b("UIToast", "uitoast", "UI");
    b.AddEnum("corner", &UIToast::corner, {"TopRight", "TopLeft", "BottomRight", "BottomLeft", "TopCenter"}).Tooltip("Screen corner the toasts stack from")
     .AddFloat("duration", &UIToast::duration).Range(0, 1e6, 0.1).Tooltip("Seconds a toast stays")
     .AddInt("maxStack", &UIToast::maxStack).Range(1, 1000, 1).Tooltip("Most toasts shown at once")
     .AddFloat("margin", &UIToast::margin).Range(0, 1e4, 1).Tooltip("Pixels from the screen edge")
     .AddFloat("gap", &UIToast::gap).Range(0, 1e4, 1).Tooltip("Pixels between toasts")
     .AddFloat("width", &UIToast::width).Range(0, 1e4, 1).Tooltip("Toast width in pixels")
     .AddFloat("height", &UIToast::height).Range(0, 1e4, 1).Tooltip("Toast height in pixels")
     .AddBool("showIcon", &UIToast::showIcon).Tooltip("Show the info / success / warning / error icon");
    b.Commit();
}

// UITooltip (record "uitooltip")
void RegisterUITooltip() {
    PropertyBuilder<UITooltip> b("UITooltip", "uitooltip", "UI");
    b.AddString("text", &UITooltip::text).Tooltip("Text shown while the pointer rests on this widget")
     .AddFloat("delay", &UITooltip::delay).Range(0, 1e4, 0.05).Tooltip("Seconds of hovering before it shows")
     .AddColor("background", &UITooltip::background).Range(0, 1, 0.01).Tooltip("Box colour")
     .AddColor("textColor", &UITooltip::textColor).Range(0, 1, 0.01).Tooltip("Text colour")
     .AddColor("borderColor", &UITooltip::borderColor).Range(0, 1, 0.01).Tooltip("Border colour")
     .AddBool("richText", &UITooltip::richText).Tooltip("Draw <b> and #rrggbb tags in the text")
     .AddVec2("offset", &UITooltip::offset).Range(-1e4, 1e4, 1).Tooltip("Pixels from the pointer to the box");
    b.Commit();
}

// UIModal (record "uimodal")
void RegisterUIModal() {
    PropertyBuilder<UIModal> b("UIModal", "uimodal", "UI");
    b.Section("Behaviour")
     .AddBool("dimBackground", &UIModal::dimBackground).Tooltip("Darken everything behind the dialog")
     .AddColor("dimColor", &UIModal::dimColor).Range(0, 1, 0.01).Tooltip("Colour laid over the background")
     .AddBool("closeOnCancel", &UIModal::closeOnCancel).Tooltip("Escape or gamepad B closes it when it is on top")
     .AddBool("closeOnClickOutside", &UIModal::closeOnClickOutside).Tooltip("A click outside the dialog closes it")
     .Section("Card style")
     .AddBool("styleCard", &UIModal::styleCard).Tooltip("Restyle the Card, Title, Message and button children with the values below")
     .AddFloat("cardRadius", &UIModal::cardRadius).Range(0, 1e3, 0.5).Tooltip("Corner radius of the card")
     .AddColor("cardColor", &UIModal::cardColor).Range(0, 1, 0.01).Tooltip("Card colour")
     .AddColor("titleColor", &UIModal::titleColor).Range(0, 1, 0.01).Tooltip("Title text colour")
     .AddColor("bodyColor", &UIModal::bodyColor).Range(0, 1, 0.01).Tooltip("Message text colour")
     .AddColor("confirmColor", &UIModal::confirmColor).Range(0, 1, 0.01).Tooltip("Confirm button colour")
     .AddColor("cancelColor", &UIModal::cancelColor).Range(0, 1, 0.01).Tooltip("Cancel button colour")
     .AddString("titleText", &UIModal::titleText).Tooltip("Title text (empty = keep the label's own text)")
     .AddString("bodyText", &UIModal::bodyText).Tooltip("Message text (empty = keep the label's own text)")
     .AddString("confirmText", &UIModal::confirmText).Tooltip("Confirm button text (empty = keep the label's own text)")
     .AddString("cancelText", &UIModal::cancelText).Tooltip("Cancel button text (empty = keep the label's own text)");
    b.Commit();
}

// UIContextMenu (record "uicontext"). The item list is a list, not a property.
void RegisterUIContextMenu() {
    PropertyBuilder<UIContextMenu> b("UIContextMenu", "uicontext", "UI");
    b.AddBool("anywhere", &UIContextMenu::anywhere).Tooltip("Open on a right-click anywhere, not only on this widget")
     .AddFloat("itemHeight", &UIContextMenu::itemHeight).Range(1, 1e4, 1).Tooltip("Pixels per item")
     .AddFloat("width", &UIContextMenu::width).Range(1, 1e4, 1).Tooltip("Menu width in pixels")
     .AddFloat("textSize", &UIContextMenu::textSize).Range(0.25, 64, 0.25).Tooltip("Text pixel size")
     .AddColor("background", &UIContextMenu::background).Range(0, 1, 0.01).Tooltip("Menu colour")
     .AddColor("hoverColor", &UIContextMenu::hoverColor).Range(0, 1, 0.01).Tooltip("Colour of the item under the pointer")
     .AddColor("textColor", &UIContextMenu::textColor).Range(0, 1, 0.01).Tooltip("Text colour")
     .AddColor("borderColor", &UIContextMenu::borderColor).Range(0, 1, 0.01).Tooltip("Border colour");
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

void RegisterMoreWidgets() {
    {
        PropertyBuilder<UISearchBar> b("UISearchBar", "uisearch", "UI");
        b.AddVec2("position", &UISearchBar::position).Range(-kBig, kBig, 0.1).Tooltip("Pixels from the anchor")
         .AddVec2("size", &UISearchBar::size).Range(1, kBig, 0.1).Tooltip("Width and height in pixels")
         .AddEnum("anchor", &UISearchBar::anchor, AnchorNames()).Tooltip("Which edge of the screen this sits on")
         .AddString("query", &UISearchBar::query).Tooltip("The text to search for")
         .AddString("placeholder", &UISearchBar::placeholder).Tooltip("Hint shown while the search is empty")
         .AddBool("caseSensitive", &UISearchBar::caseSensitive).Tooltip("Match capital letters exactly")
         .AddInt("value", &UISearchBar::value).Range(-1, 100000, 1).Tooltip("Chosen row in the full list, or -1")
         .AddBool("interactable", &UISearchBar::interactable).Tooltip("When off, the search ignores clicks and typing")
         .AddBool("focusable", &UISearchBar::focusable).Tooltip("A gamepad can move to this")
         .AddColor("background", &UISearchBar::background).Range(0, 1, 0.01).Tooltip("Colour behind the list")
         .AddColor("textColor", &UISearchBar::textColor).Range(0, 1, 0.01).Tooltip("Colour of the text")
         .AddColor("accent", &UISearchBar::accent).Range(0, 1, 0.01).Tooltip("Colour of the chosen row");
        b.Commit();
    }
    {
        PropertyBuilder<UIDateTimePicker> b("UIDateTimePicker", "uidate", "UI");
        b.AddVec2("position", &UIDateTimePicker::position).Range(-kBig, kBig, 0.1).Tooltip("Pixels from the anchor")
         .AddVec2("size", &UIDateTimePicker::size).Range(1, kBig, 0.1).Tooltip("Width and height in pixels")
         .AddEnum("anchor", &UIDateTimePicker::anchor, AnchorNames()).Tooltip("Which edge of the screen this sits on")
         .AddInt("year", &UIDateTimePicker::year).Range(1, 9999, 1).Tooltip("Year")
         .AddInt("month", &UIDateTimePicker::month).Range(1, 12, 1).Tooltip("Month, from 1 to 12")
         .AddInt("day", &UIDateTimePicker::day).Range(1, 31, 1).Tooltip("Day of the month. Days that month does not have are pulled back")
         .AddInt("hour", &UIDateTimePicker::hour).Range(0, 23, 1).Tooltip("Hour, from 0 to 23")
         .AddInt("minute", &UIDateTimePicker::minute).Range(0, 59, 1).Tooltip("Minute, from 0 to 59")
         .AddInt("minYear", &UIDateTimePicker::minYear).Range(1, 9999, 1).Tooltip("Earliest year")
         .AddInt("maxYear", &UIDateTimePicker::maxYear).Range(1, 9999, 1).Tooltip("Latest year")
         .AddBool("showTime", &UIDateTimePicker::showTime).Tooltip("Show the time as well as the date")
         .AddBool("interactable", &UIDateTimePicker::interactable).Tooltip("When off, the arrows do nothing")
         .AddBool("focusable", &UIDateTimePicker::focusable).Tooltip("A gamepad can move to this")
         .AddColor("textColor", &UIDateTimePicker::textColor).Range(0, 1, 0.01).Tooltip("Colour of the numbers");
        b.Commit();
    }
    {
        PropertyBuilder<UIFilePicker> b("UIFilePicker", "uifile", "UI");
        b.AddVec2("position", &UIFilePicker::position).Range(-kBig, kBig, 0.1).Tooltip("Pixels from the anchor")
         .AddVec2("size", &UIFilePicker::size).Range(1, kBig, 0.1).Tooltip("Width and height in pixels")
         .AddEnum("anchor", &UIFilePicker::anchor, AnchorNames()).Tooltip("Which edge of the screen this sits on")
         .AddString("root", &UIFilePicker::root).Tooltip("The only folder this picker may open")
         .AddString("current", &UIFilePicker::current).Tooltip("Folder on screen, inside that folder")
         .AddString("selected", &UIFilePicker::selected).Tooltip("The chosen file, inside that folder")
         .AddBool("showHidden", &UIFilePicker::showHidden).Tooltip("Show files whose names start with a dot")
         .AddBool("interactable", &UIFilePicker::interactable).Tooltip("When off, rows do not open")
         .AddBool("focusable", &UIFilePicker::focusable).Tooltip("A gamepad can move to this")
         .AddColor("textColor", &UIFilePicker::textColor).Range(0, 1, 0.01).Tooltip("Colour of the names");
        b.Commit();
    }
    {
        PropertyBuilder<UIAccordion> b("UIAccordion", "uiaccordion", "UI");
        b.AddVec2("position", &UIAccordion::position).Range(-kBig, kBig, 0.1).Tooltip("Pixels from the anchor")
         .AddVec2("size", &UIAccordion::size).Range(1, kBig, 0.1).Tooltip("Width and height in pixels")
         .AddEnum("anchor", &UIAccordion::anchor, AnchorNames()).Tooltip("Which edge of the screen this sits on")
         .AddBool("allowMultiple", &UIAccordion::allowMultiple).Tooltip("More than one section can be open")
         .AddInt("value", &UIAccordion::value).Range(0, 1000, 1).Tooltip("The section last opened or closed")
         .AddBool("interactable", &UIAccordion::interactable).Tooltip("When off, headings do not open")
         .AddBool("focusable", &UIAccordion::focusable).Tooltip("A gamepad can move to this")
         .AddColor("textColor", &UIAccordion::textColor).Range(0, 1, 0.01).Tooltip("Colour of the headings");
        b.Commit();
    }
    {
        PropertyBuilder<UIBreadcrumbs> b("UIBreadcrumbs", "uibreadcrumb", "UI");
        b.AddVec2("position", &UIBreadcrumbs::position).Range(-kBig, kBig, 0.1).Tooltip("Pixels from the anchor")
         .AddVec2("size", &UIBreadcrumbs::size).Range(1, kBig, 0.1).Tooltip("Width and height in pixels")
         .AddEnum("anchor", &UIBreadcrumbs::anchor, AnchorNames()).Tooltip("Which edge of the screen this sits on")
         .AddString("pathText", &UIBreadcrumbs::pathText).Tooltip("The path. Folders are separated by /")
         .AddInt("value", &UIBreadcrumbs::value).Range(0, 1000, 1).Tooltip("Which crumb is highlighted")
         .AddBool("interactable", &UIBreadcrumbs::interactable).Tooltip("When off, crumbs do not respond")
         .AddBool("focusable", &UIBreadcrumbs::focusable).Tooltip("A gamepad can move to this")
         .AddColor("accent", &UIBreadcrumbs::accent).Range(0, 1, 0.01).Tooltip("Colour of the highlighted crumb");
        b.Commit();
    }
    {
        PropertyBuilder<UIPagination> b("UIPagination", "uipage", "UI");
        b.AddVec2("position", &UIPagination::position).Range(-kBig, kBig, 0.1).Tooltip("Pixels from the anchor")
         .AddVec2("size", &UIPagination::size).Range(1, kBig, 0.1).Tooltip("Width and height in pixels")
         .AddEnum("anchor", &UIPagination::anchor, AnchorNames()).Tooltip("Which edge of the screen this sits on")
         .AddInt("page", &UIPagination::page).Range(1, 100000, 1).Tooltip("The page on screen, starting at 1")
         .AddInt("pageCount", &UIPagination::pageCount).Range(1, 100000, 1).Tooltip("How many pages there are")
         .AddInt("pageSize", &UIPagination::pageSize).Range(1, 100000, 1).Tooltip("How many items are on each page")
         .AddInt("totalItems", &UIPagination::totalItems).Range(0, 10000000, 1).Tooltip("How many items are in the whole list")
         .AddBool("interactable", &UIPagination::interactable).Tooltip("When off, the page does not change")
         .AddBool("focusable", &UIPagination::focusable).Tooltip("A gamepad can move to this")
         .AddColor("accent", &UIPagination::accent).Range(0, 1, 0.01).Tooltip("Colour of the current page");
        b.Commit();
    }
    {
        PropertyBuilder<UISkeleton> b("UISkeleton", "uiskeleton", "UI");
        b.AddVec2("position", &UISkeleton::position).Range(-kBig, kBig, 0.1).Tooltip("Pixels from the anchor")
         .AddVec2("size", &UISkeleton::size).Range(1, kBig, 0.1).Tooltip("Width and height in pixels")
         .AddEnum("anchor", &UISkeleton::anchor, AnchorNames()).Tooltip("Which edge of the screen this sits on")
         .AddBool("loading", &UISkeleton::loading).Tooltip("Show the loading placeholder")
         .AddInt("lines", &UISkeleton::lines).Range(1, 8, 1).Tooltip("How many text bars to show")
         .AddBool("showAvatar", &UISkeleton::showAvatar).Tooltip("Show a round picture placeholder")
         .AddColor("bar", &UISkeleton::bar).Range(0, 1, 0.01).Tooltip("Colour of the placeholder bars")
         .AddColor("highlight", &UISkeleton::highlight).Range(0, 1, 0.01).Tooltip("Colour of the moving shine");
        b.Commit();
    }
    {
        PropertyBuilder<UIBottomSheet> b("UIBottomSheet", "uibsheet", "UI");
        b.AddVec2("position", &UIBottomSheet::position).Range(-kBig, kBig, 0.1).Tooltip("Pixels from the anchor")
         .AddVec2("size", &UIBottomSheet::size).Range(1, kBig, 0.1).Tooltip("Width and height of the open sheet, in pixels")
         .AddEnum("anchor", &UIBottomSheet::anchor, AnchorNames()).Tooltip("Which edge of the screen this sits on")
         .AddBool("open", &UIBottomSheet::open).Tooltip("The sheet is open")
         .AddFloat("duration", &UIBottomSheet::duration).Range(0, 10, 0.01).Tooltip("Seconds to slide open or closed. 0 snaps")
         .AddBool("closeOnBackdrop", &UIBottomSheet::closeOnBackdrop).Tooltip("A click just above the sheet closes it")
         .AddBool("dim", &UIBottomSheet::dim).Tooltip("Darken the screen behind the sheet")
         .AddString("title", &UIBottomSheet::title).Tooltip("Title on the sheet")
         .AddBool("interactable", &UIBottomSheet::interactable).Tooltip("When off, the handle does nothing")
         .AddBool("focusable", &UIBottomSheet::focusable).Tooltip("A gamepad can move to this")
         .AddColor("background", &UIBottomSheet::background).Range(0, 1, 0.01).Tooltip("Colour of the sheet");
        b.Commit();
    }
}

void RegisterUIInventoryBind() {
    PropertyBuilder<UIInventoryBind> b("UIInventoryBind", "uiinvbind", "UI");
    b.AddString("inventoryName", &UIInventoryBind::inventoryName).Tooltip("Object that holds the Inventory (empty = the Player, else the first Inventory in the scene)")
     .AddBool("fitRows", &UIInventoryBind::fitRows).Tooltip("The grid gets as many rows as the bag has room for")
     .AddFloat("slotFontScale", &UIInventoryBind::slotFontScale).Range(0, 16, 0.1).Tooltip("Label size on the slots (0 = leave the grid's own)");
    b.Commit();
}

} // namespace

void RegisterVirtualJoystick() {
    PropertyBuilder<VirtualJoystick> b("VirtualJoystick", "virtualjoystick", "UI");
    b.AddVec2("position", &VirtualJoystick::position).Range(-kBig, kBig, 0.1).Tooltip("Pixels from the anchor")
     .AddVec2("size", &VirtualJoystick::size).Range(-kBig, kBig, 0.1).Tooltip("Width and height of the stick, in pixels")
     .AddEnum("anchor", &VirtualJoystick::anchor, AnchorNames()).Tooltip("The screen point the position is measured from")
     .AddString("action", &VirtualJoystick::action).Tooltip("Direction action this stick drives (Move, by default). A game that already reads that action also hears the stick")
     .AddBool("floating", &VirtualJoystick::floating).Tooltip("Off: the stick stays where you placed it. On: it appears under the finger, inside this rectangle")
     .AddFloat("deadZone", &VirtualJoystick::deadZone).Range(0, 0.9, 0.01).Tooltip("Tilt that still counts as centred")
     .AddBool("onlyOnTouch", &VirtualJoystick::onlyOnTouch).Tooltip("Hide this stick until a touch screen is in use")
     .AddBool("fadeWhenIdle", &VirtualJoystick::fadeWhenIdle).Tooltip("Draw the stick fainter until a finger is on it")
     .AddFloat("knobScale", &VirtualJoystick::knobScale).Range(0.15, 0.9, 0.01).Tooltip("Knob size as a fraction of the stick")
     .AddColor("baseColor", &VirtualJoystick::baseColor).Tooltip("Colour of the base")
     .AddColor("knobColor", &VirtualJoystick::knobColor).Tooltip("Colour of the knob");
    b.Commit();
}
void RegisterTouchDPad() {
    PropertyBuilder<TouchDPad> b("TouchDPad", "touchdpad", "UI");
    b.AddVec2("position", &TouchDPad::position).Range(-kBig, kBig, 0.1).Tooltip("Pixels from the anchor")
     .AddVec2("size", &TouchDPad::size).Range(-kBig, kBig, 0.1).Tooltip("Width and height of the pad, in pixels")
     .AddEnum("anchor", &TouchDPad::anchor, AnchorNames()).Tooltip("The screen point the position is measured from")
     .AddString("action", &TouchDPad::action).Tooltip("Direction action this pad drives (Move, by default)")
     .AddFloat("deadZone", &TouchDPad::deadZone).Range(0, 0.9, 0.01).Tooltip("How far from the centre a press must be before it counts")
     .AddBool("diagonals", &TouchDPad::diagonals).Tooltip("Allow two directions at once (up and right, and so on)")
     .AddBool("onlyOnTouch", &TouchDPad::onlyOnTouch).Tooltip("Hide this pad until a touch screen is in use")
     .AddColor("color", &TouchDPad::color).Tooltip("Colour of the pad")
     .AddColor("pressedColor", &TouchDPad::pressedColor).Tooltip("Colour of a direction while it is held");
    b.Commit();
}
void RegisterTouchButton() {
    PropertyBuilder<TouchButton> b("TouchButton", "touchbutton", "UI");
    b.AddVec2("position", &TouchButton::position).Range(-kBig, kBig, 0.1).Tooltip("Pixels from the anchor")
     .AddVec2("size", &TouchButton::size).Range(-kBig, kBig, 0.1).Tooltip("Width and height of the button, in pixels")
     .AddEnum("anchor", &TouchButton::anchor, AnchorNames()).Tooltip("The screen point the position is measured from")
     .AddString("action", &TouchButton::action).Tooltip("Button action this press drives (Jump, by default). A game bound to that action also hears the button")
     .AddString("label", &TouchButton::label).Tooltip("Words drawn on the button")
     .AddBool("onlyOnTouch", &TouchButton::onlyOnTouch).Tooltip("Hide this button until a touch screen is in use")
     .AddColor("color", &TouchButton::color).Tooltip("Colour of the button")
     .AddColor("pressedColor", &TouchButton::pressedColor).Tooltip("Colour while the finger is down")
     .AddColor("textColor", &TouchButton::textColor).Tooltip("Colour of the label");
    b.Commit();
}

void RegisterUIProperties() {
    RegisterUIButton();
    RegisterVirtualJoystick();
    RegisterTouchDPad();
    RegisterTouchButton();
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
    RegisterUIToast();
    RegisterUITooltip();
    RegisterUIModal();
    RegisterUIContextMenu();
    RegisterWorldSpaceUI();
    RegisterPauseMenu();
    RegisterChestInventory();
    RegisterUIDropTarget();
    RegisterMinimap();
    RegisterUITextBind();
    RegisterUIBarBind();
    RegisterUIAnimation();
    RegisterCraftingMenu();
    RegisterInventoryUI();
    RegisterGridInventoryUI();
    RegisterCraftingStation();
    RegisterUIListView();
    RegisterUISlotGrid();
    RegisterUITabs();
    RegisterUIInventoryBind();
    RegisterMoreWidgets();
}

} // namespace detail
} // namespace okay
