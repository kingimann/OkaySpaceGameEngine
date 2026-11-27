#pragma once
// OkayUI designs: the ONE theme source for every program that draws UI (OkayUI screens, and the launcher's and the editor's
// Dear ImGui style through shared/OkayTheme.hpp). A design is data: a colour palette for dark and/or light, and the shape
// choices (corner radii, borders or none, shadow depth, tab shape, focus ring, density, motion). The person's choices on top
// of a design (accent, light or dark, density, roundness, text size, UI scale, shadows, reduced motion) are Options, saved in
// one small JSON file in the per-user config folder (appearance.json) and applied live. See docs/ui/okayui_design.md.
//
// Plain C++ with no Dear ImGui and no STL in this header (OkayUI's rule), so every program and test can include it.
// Written by us from general published design principles (WCAG 2.x contrast ratios, a 4-pixel spacing grid); the colour
// values and shapes are our own. No code, theme or palette was copied from any other project.
#include "okay/UI/OkayUI.hpp"

namespace OkayUI {
namespace design {

struct Rgba { unsigned char r = 0, g = 0, b = 0, a = 255; };

/// How a tab bar draws its tabs.
enum class TabShape : int { Underline = 0, Pill = 1, Segmented = 2 };
/// How much room is between and inside controls.
enum class Density : int { Compact = 0, Comfortable = 1, Spacious = 2 };
/// Light or dark: the design's own first choice, or one of the two (when the design has it).
enum class Scheme : int { Auto = 0, Dark = 1, Light = 2 };

/// Every colour a design defines, for one scheme (dark or light).
struct Palette {
    // Surfaces, from the bottom layer up: empty space and title rails; side rails, tool bars and grooves; the page / window
    // body; cards and raised panels; popovers, menus and dialogs; text fields; buttons and other controls.
    Rgba deep, sunken, base, raised, overlay, field, control;
    Rgba text, textDim, textFaint;          // body text; secondary text (4.5:1 on every surface); hints and placeholders
    Rgba hairline;                          // borders and separators (see-through over any surface)
    Rgba accent;                            // the design's own accent (Options can replace it)
    Rgba success, warning, danger, info;    // status colours, readable as text on the page
    Rgba onAccent;                          // text and glyphs on a filled accent
    Rgba ink;                               // the tint for hovers and soft fills: light on dark schemes, dark on light ones
    Rgba tooltipBg, tooltipText;            // tooltips are small cards of their own colour
    bool light = false;
};

/// The shape choices of a design (pixels at 1x, Comfortable density).
struct Shape {
    float radius      = 6.0f;    // buttons, fields, combo boxes
    float radiusSmall = 4.0f;    // check boxes, tags, small chips
    float radiusLarge = 10.0f;   // windows, popovers, cards
    float borderPx    = 1.0f;    // hairline width
    bool  controlBorders = false;   // buttons and fields have an outline (else they stand out by their fill alone)
    int   shadow      = 2;       // 0 none, 1 soft, 2 medium, 3 deep (windows, popovers, toasts)
    TabShape tabs     = TabShape::Underline;
    float focusRingPx = 2.0f;    // the ring drawn around the control that has the keyboard
    float spacing     = 1.0f;    // the design's own density (multiplied by the person's choice)
    float motionMs    = 120.0f;  // hover, press and open transitions (0: none)
    bool  boldHeaders = false;   // section headers use the accent bar on their left
};

struct Design {
    const char* name = "";
    const char* blurb = "";      // one line for the picker
    Palette dark, light;
    bool hasDark = true, hasLight = false;
    bool lightFirst = false;     // Scheme::Auto picks light
    Shape shape;
};

/// The person's choices on top of a design (what appearance.json holds).
struct Options {
    char    design[32] = "Graphite";
    Scheme  scheme = Scheme::Auto;
    bool    customAccent = false;   // false: the design's accent
    Rgba    accent;
    Density density = Density::Comfortable;
    float   roundness = 1.0f;       // 0 (square) .. 2 (twice the design's radii)
    float   uiScale = 1.0f;         // 0.75 .. 2: the size of everything (hosts apply it to their whole UI)
    float   textSize = 1.0f;        // 0.85 .. 1.3: text only, on top of the UI scale
    bool    shadows = true;
    bool    reduceMotion = false;
};

/// A design with the Options applied: what widgets and hosts draw from.
struct Tokens {
    const char* designName = "";
    Palette c;                      // accent replaced when Options asks
    Shape   shape;                  // radii times roundness, shadow 0 when shadows are off, motion 0 when reduced
    float   spacing = 1.0f;         // design spacing times the density choice
    float   uiScale = 1.0f;
    float   textSize = 1.0f;
    // Derived control states (one formula for every program, so a hover looks the same in OkayUI and in Dear ImGui).
    Rgba controlHover, controlPressed, accentHover, accentPressed, selection, focusRing, shadowCol;
};

/// The built-in designs, in picker order. Names are stable (they are saved in appearance.json).
int DesignCount();
const Design& DesignAt(int index);
/// Index of the design with this name (case is ignored), or -1.
int FindDesign(const char* name);
/// The design's index the Options name, or 0 (the default) when the name is unknown.
int DesignIndexOf(const Options& o);

/// Clamp every field of `o` to its range (unknown design -> the default). Load and Set call it.
void Sanitize(Options& o);
/// The tokens for a set of Options (pure: no global state).
Tokens Resolve(const Options& o);
/// Whether a scheme is available for the design, and which one Auto means.
bool SchemeIsLight(const Options& o);

/// OkayUI's Theme from tokens. `trueType`: the metrics that go with TrueType text (the default in every program); false: the
/// 8x8 bitmap font's metrics (the last fallback, and the headless tests' text).
Theme ThemeFor(const Tokens& t, bool trueType);
/// The default design's Theme with bitmap metrics (what a fresh OkayUI and ResetState start from).
Theme DefaultTheme();

// ---- The live choice of this program -----------------------------------------------------------------------------------------
/// The Options in force (the defaults until Set or Load).
const Options& Current();
Tokens CurrentTokens();
/// Make `o` the live choice: OkayUI's Style() follows at once (custom colours pushed with PushStyleColor are kept), and
/// Revision() moves on so a host restyles its Dear ImGui (shared/OkayTheme.hpp ApplyDesign) on its next frame.
void Set(const Options& o);
/// Changes every time Set changes something (starts at 1).
unsigned Revision();

// ---- appearance.json -----------------------------------------------------------------------------------------------------------
/// The per-user file: $OKAY_APPEARANCE_FILE when set, else <per-user config folder>/appearance.json (Windows:
/// %APPDATA%/OkaySpace/config/appearance.json). Written into `out` (NUL-terminated); false when no folder is known.
bool DefaultPath(char* out, int cap);
/// Read Options from JSON text (unknown keys are ignored, missing ones keep `out`'s values). False: not a JSON object.
bool ParseJson(const char* text, Options* out);
/// The Options as JSON (NUL-terminated); the length written, or -1 when `cap` is too small.
int WriteJson(const Options& o, char* out, int cap);
/// Load the file into `out`. False when it is missing or unreadable (`out` untouched).
bool Load(const char* path, Options* out);
/// Write the file (makes its folder). False on failure.
bool Save(const char* path, const Options& o);
/// For the programs (launcher, editor, Script Editor, browser, Visual Script): from now on the live choice follows the
/// per-user file. Loads it now (Set), and Poll() re-reads it when another program changed it. Tests never call this, so
/// they always draw the default design. True when the file was there and read (false: a first start, the defaults stay).
bool FollowUserFile();
/// Whether FollowUserFile was called (the program saves the person's changes).
bool FollowingUserFile();
/// Save the live choice to the per-user file (after the person changed something). No-op unless FollowUserFile was called.
bool SaveUserFile();
/// Called by hosts about once a frame: re-reads the per-user file when its time stamp changed (at most twice a second,
/// time in seconds from the host's clock). True when the choice changed.
bool Poll(double nowSeconds);

// ---- Colour helpers (shared by OkayUI and the Dear ImGui style) ---------------------------------------------------------------
Rgba Mix(Rgba a, Rgba b, float t);
Rgba WithAlpha(Rgba c, unsigned char a);
/// WCAG 2.x contrast ratio of two opaque colours (1..21).
float Contrast(Rgba a, Rgba b);

} // namespace design
} // namespace OkayUI
