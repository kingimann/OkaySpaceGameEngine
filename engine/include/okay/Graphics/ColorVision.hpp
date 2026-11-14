#pragma once
// Colour vision: what a colour looks like with a colour vision deficiency, how to shift a
// colour so it is easier to tell apart, a palette that stays readable for everyone, and
// contrast helpers (docs/accessibility.md).
//
// Everything is a pure function of its arguments. Colours are sRGB in [0, 1] like Color; the
// matrices act on linear light. Alpha is never changed.
#include "okay/Graphics/Image.hpp"
#include "okay/Render/Color.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace okay {

enum class ColorblindMode {
    None,
    Protanopia,      ///< no red cones: reds dark, red and green confused
    Deuteranopia,    ///< no green cones: red and green confused
    Tritanopia,      ///< no blue cones: blue and green, yellow and pink confused
    Achromatopsia,   ///< no colour vision: only brightness
};
const char* ColorblindName(ColorblindMode mode);   ///< "none", "protanopia", "deuteranopia", "tritanopia", "achromatopsia"
/// Parse a name: the above, "protan", "deutan", "tritan", "mono", "grayscale", "normal", "off".
/// False (mode untouched) when it isn't one.
bool ColorblindFromName(const std::string& name, ColorblindMode& mode);

// ---- Transfer function -------------------------------------------------------------------------
float SrgbToLinear(float c);
float LinearToSrgb(float c);

// ---- Simulation ------------------------------------------------------------------------------------
/// A 3x3 matrix, row major, applied to linear RGB as a column vector.
using ColorMatrix3 = std::array<float, 9>;
/// The matrix that shows what someone with the deficiency sees (Machado, Oliveira and Fernandes
/// 2009 for the three dichromacies; luminance for achromatopsia). `severity` 0..1 blends from the
/// identity (normal vision) to the full deficiency; the partial matrices are a linear blend, an
/// approximation of the paper's own. Rows of every matrix add to 1, so white stays white.
ColorMatrix3 ColorblindMatrix(ColorblindMode mode, float severity = 1.0f);
/// `c` as seen with the deficiency.
Color SimulateColorblind(const Color& c, ColorblindMode mode, float severity = 1.0f);
/// `c` shifted so what a person with the deficiency loses is moved into channels they still
/// see ("daltonization"): the difference between the colour and its simulation, redirected.
/// `strength` 0..1 scales the shift. Protanopia and deuteranopia move it into green and blue,
/// tritanopia into red and green; achromatopsia has no hue to recover, so it returns `c`.
Color DaltonizeColor(const Color& c, ColorblindMode mode, float strength = 1.0f);

/// The same over every pixel of an image, in place (alpha kept).
void SimulateColorblind(Image& img, ColorblindMode mode, float severity = 1.0f);
void DaltonizeImage(Image& img, ColorblindMode mode, float strength = 1.0f);
/// The daltonize shift over `pixels` RGBA8 pixels in place (4 bytes each, alpha kept); what the
/// player applies to the whole 3D frame. Does nothing for None and Achromatopsia.
void DaltonizeRgba8(std::uint8_t* rgba, std::size_t pixels, ColorblindMode mode, float strength = 1.0f);
/// Copies.
Image SimulatedColorblind(const Image& img, ColorblindMode mode, float severity = 1.0f);
Image DaltonizedImage(const Image& img, ColorblindMode mode, float strength = 1.0f);

// ---- Telling colours apart ----------------------------------------------------------------------------
struct LabColor { float L = 0, a = 0, b = 0; };
/// CIELAB (D65) of an sRGB colour.
LabColor ToLab(const Color& c);
/// How different two colours look: CIE76 delta E (about 2.3 is just noticeable, 10+ reads as
/// different colours at a glance, 20+ as clearly different).
float ColorDistance(const Color& a, const Color& b);
/// The smallest delta E between any two of `colors` as seen with `mode` (None = normal vision).
/// 0 for fewer than two colours.
float MinColorSeparation(const std::vector<Color>& colors, ColorblindMode mode = ColorblindMode::None);

/// 12 colours chosen to stay apart for normal vision and under protanopia, deuteranopia and
/// tritanopia (the smallest delta E between any two is about 20), light enough to read on dark
/// backgrounds (at least 3.5:1 against black). Ordered so the first n are also well separated:
/// use the first few for few teams, markers or speakers. With no colour vision (achromatopsia)
/// only brightness is left and twelve colours can't all differ in it: pair them with a shape,
/// pattern or label.
const std::vector<Color>& SafePalette();
/// SafePalette()[index], wrapping around past the end (negative indices count from the end).
Color SafePaletteColor(int index);

// ---- Contrast --------------------------------------------------------------------------------------------------
/// WCAG relative luminance, 0 (black) .. 1 (white).
float RelativeLuminance(const Color& c);
/// WCAG contrast ratio between two colours, 1 (same) .. 21 (black on white).
float ContrastRatio(const Color& a, const Color& b);
/// `fg` moved towards white or black (whichever reaches it with less change) until its contrast
/// with `bg` is at least `minRatio` (WCAG AA for text is 4.5, large text 3). Returned unchanged
/// when it already has it; black or white when even those fall short.
Color EnsureContrast(const Color& fg, const Color& bg, float minRatio = 4.5f);

} // namespace okay
