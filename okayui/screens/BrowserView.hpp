#pragma once
// Browser (docs/feature_map/17-launcher-platform.md 17.18): the OkayUI view of one browser tab (okay/Browser/Browser.hpp).
// It draws the toolbar (Back, Forward, Reload, the address, Open in browser, Close), the page (the layout's rectangles,
// text, images, list markers and the boxes of embedded games) in a scrolling area, and the loading and error states, and
// says what the person clicked. Like the other screens it is a plain function with no SDL, no Dear ImGui and no network:
// the host fetches (BrowserTab::TakeFetches), makes the image textures, and acts on the result.
// Step 2 (docs/platform/browser_plan.md): the browser program (browser/) asks for an address field instead of the address
// as text and for no "Open in browser" / "Close", and an embedded OkaySpace game's box gets a Play button and the game's
// state (downloading, running, an error) from the host (BrowserViewOptions::embedState); the host starts the game.
// Round 2 of the look (docs/platform/browser_plan.md): the program's toolbar has round icon buttons drawn by our own code, a rounded
// address bar with the site's badge and the host standing out, a friendly error page, a status bubble, key scrolling, Tab focus
// with a ring, and text selection (PageKeys).
#include "okay/Browser/Browser.hpp"
#include "okay/UI/OkayUI.hpp"

#include <functional>
#include <string>
#include <vector>

namespace okayuiscreens {

struct BrowserViewResult {
    bool back = false, forward = false, reload = false;
    bool stop = false, home = false, bookmark = false, menu = false;   // the program's buttons (BrowserViewOptions::programButtons)
    OkayUI::Rect menuButton;            // where the menu button is (the program opens its menu under it)
    std::string navigate;               // the address field: the address the person typed and confirmed (Enter or Go)
    int playEmbed = -1;                 // the Play button of an embedded game: its index into the layout's cmds
    bool openInSystemBrowser = false;   // "Open in browser": the host opens tab.Url() with the system browser
    bool close = false;
    int linkClicked = -1;               // index into the layout's links (pass the href to BrowserTab::Activate)
    bool linkInNewTab = false;          // the click was a middle click or a Ctrl+click (open the link in a new tab)
    bool contextMenu = false;           // a right click on the page (the program shows its context menu)
    int contextLink = -1;               // ... on this link (index into links), or -1
    std::string contextText;            // ... the text run under the pointer ("" when none)
    OkayUI::Vec2 contextAt;             // ... where (screen)
    bool formSent = false;              // a form was sent (the tab is loading its answer)
    std::string formNote;               // what the form submission said (why it was not sent, or what was left out)
    std::string hoverHref;              // the link under the pointer, as written ("" when none)
    OkayUI::Rect page;                  // where the page area is on the screen
    float pageTop = 0.0f;               // the screen y of the page's own top (the page area's top minus the scroll)
    // ---- round 2 of the look (the browser program's toolbar) ----
    OkayUI::Rect address;               // the rounded address bar (the program draws its suggestions under it)
    bool addressFocused = false;        // the address field has the keyboard
    bool tabFocused = false;            // Tab / Shift+Tab moved the keyboard focus on the page this frame
    // ---- the host's round 3 ----
    int historyMenu = 0;                // a right click on Back (-1) or Forward (+1), or a press held there: show the tab's history list
    OkayUI::Vec2 historyAt;             // ... under this point (screen)
    bool keyCancelled = false;          // the page cancelled this frame's key (preventDefault on keydown / keypress): it was not used
    bool mouseDownCancelled = false;    // the page cancelled this frame's mousedown: no text selection, no field took the keyboard
};

/// Web fonts (@font-face) the host loaded: a TrueType face made by OkayUI::AddFontTtf, under its family name (lower-case), weight
/// and style. Text whose font-family names a registered family is measured and drawn with it (the closest weight and style;
/// a bold face when there is one, else the default bold look). The default font otherwise.
/// A font's vertical size in em (CSS sizes text by the em; OkayUI sizes a font by its line, ascent - descent): the line and the
/// ascent per em. The default (1, 0.8) is what the layout assumes, so text drawn with it is as before.
struct FontEm { float linePerEm = 1.0f; float ascentPerEm = 0.8f; };
/// The em of a TrueType / OpenType file (its head unitsPerEm and hhea ascent / descent); false (and `out` unchanged) when the
/// bytes are not a font we read or its numbers are not believable.
bool FontEmOf(const std::uint8_t* data, std::size_t size, FontEm& out);
void RegisterWebFont(const std::string& family, int weight, bool italic, const OkayUI::Font* font, const FontEm& em = FontEm());
void ClearWebFonts();

/// Text measured with OkayUI's current font, for the layout.
class OkayUIMeasure : public okay::browser::TextMeasure {
public:
    float Width(const std::string& utf8, const okay::browser::FontSpec& font) const override;
};

/// The texture for an image of the page (`src` as written), or null while there is none.
using BrowserImageFn = std::function<SDL_Texture*(const std::string& src)>;

/// What the host says about an embedded box (DrawCmd::Embed): whether it can run an OkaySpace game, and the game's state.
struct EmbedState {
    bool playable = false;   ///< draw a Play button in the box
    bool busy = false;       ///< the Play button is disabled (downloading, unpacking, running)
    std::string status;      ///< one line under the button ("Downloading 40%", "Running in its own window", an error)
    std::string title;       ///< the game's name, drawn above the button ("" = "OkaySpace game")
};
using EmbedStateFn = std::function<EmbedState(const okay::browser::DrawCmd& embed)>;

/// The address field of the browser program (the host keeps one per tab, so typing survives a frame).
struct AddressField {
    char text[2048] = {0};
    std::string shownFor;   ///< the address `text` was last filled from (refilled when the tab's address changes)
    bool focused = false;   ///< it had the keyboard last frame (it then shows the whole address; else the host stands out)
};

/// The page's keyboard state for one tab (the browser program keeps one per tab): where a key scroll is heading (it moves
/// there smoothly over a few frames) and the link or field that Tab / Shift+Tab moved the focus to (drawn with a ring; Enter
/// follows the link). Keys work while no text field has the keyboard: Space / Shift+Space and Page Up / Page Down scroll by
/// most of a screen, the arrow keys by a line, Home / End to the top and the bottom.
struct PageKeys {
    float scrollTarget = -1.0f;      ///< the view's scroll it is heading to (screen px), < 0 = none
    int focusLink = -1;              ///< the focused link (index into the layout's links), or -1
    const void* focusControl = nullptr;   ///< ... or the focused form control (an element of the page)
    const void* forDoc = nullptr;    ///< the page the focus is on (it is dropped when another page is shown)
    int glideFrames = 0;             ///< frames the current glide has taken (it lands after at most 40)
    // text selection with the mouse (drag over the text; Ctrl+A selects all of it, Ctrl+C copies it): its two ends as a text
    // run (index into the layout's cmds) and a byte offset in that run; from == to is no selection
    int selFromCmd = -1, selFromChar = 0, selToCmd = -1, selToChar = 0;
    bool selecting = false;          ///< the mouse button is held after a press on the page's text
    bool HasSelection() const { return selFromCmd >= 0 && selToCmd >= 0 && (selFromCmd != selToCmd || selFromChar != selToChar); }
    void ClearSelection() { selFromCmd = selToCmd = -1; selFromChar = selToChar = 0; selecting = false; }
};

/// The text between the selection's ends, runs on one line joined as they are, a new line between lines ("" when none).
std::string SelectedPageText(const okay::browser::LayoutResult& page, const PageKeys& keys);

/// The browser program's form editing state for one tab (the host keeps one per tab): the field being typed in.
struct ControlEdit {
    const void* node = nullptr;     ///< the focused field (an element of the tab's page)
    std::vector<char> buf;          ///< its text while it is typed
    bool focusNow = false;          ///< give the keyboard to it this frame
    const void* selectOpen = nullptr;   ///< a <select> whose list is open
};

struct BrowserViewOptions {
    AddressField* address = nullptr;   ///< set: an address field and a Go button; null: the address as text (the launcher)
    bool hostButtons = true;           ///< "Open in browser" and "Close" (the launcher's page has them; the program does not)
    EmbedStateFn embedState;           ///< set: embedded boxes ask it for a Play button and a state line
    // ---- the browser program (round 1 of "a regular browser") ----
    bool programButtons = false;       ///< Reload becomes Stop while loading; Home, a bookmark star and a menu button
    bool bookmarked = false;           ///< the star shows the page is bookmarked
    std::string placeholder;           ///< shown in an empty address field ("Search the web or type an address")
    bool focusAddress = false;         ///< put the keyboard in the address field and select it (Ctrl+L, a new tab)
    bool blockInput = false;           ///< a menu or dialog of the program is open over the page: the page takes no clicks
    float zoom = 1.0f;                 ///< the site's zoom: the page is laid out at width / zoom and drawn zoom times larger
    const std::vector<okay::browser::FindHit>* findHits = nullptr;   ///< find on the page: highlight these
    int findCurrent = -1;              ///< ... and this one more strongly
    float scrollToPageY = -1.0f;       ///< scroll so this page y (unzoomed) is in view (the current find hit); < 0 = no
    ControlEdit* edit = nullptr;       ///< set: the page's form controls take clicks and typing (the tab's FormState), forms are sent
    /// A row the program draws between the toolbar and the page (the bookmarks bar): called inside the view's window with the
    /// row's screen position and width; `belowToolbarH` px are kept for it.
    std::function<void(float x, float y, float w)> belowToolbar;
    float belowToolbarH = 0.0f;
    PageKeys* keys = nullptr;          ///< set: the page scrolls and moves its focus with the keyboard (see PageKeys)
    bool smoothScroll = true;          ///< key and wheel scrolls glide over a few frames (off: they jump; tests)
};

/// The toolbar's look in the browser program (round 2): shared by the tab strip, the toolbar and the bookmarks bar, so the
/// active tab and the toolbar under it are one surface, as in common browsers.
namespace browserlook {
inline constexpr OkayUI::Color32 Rgb(int r, int g, int b, int a = 255) {
    return (OkayUI::Color32)r | ((OkayUI::Color32)g << 8) | ((OkayUI::Color32)b << 16) | ((OkayUI::Color32)a << 24);
}
inline OkayUI::Color32 kStrip = Rgb(24, 27, 34);        ///< behind the tabs
inline OkayUI::Color32 kToolbar = Rgb(44, 48, 58);      ///< the active tab, the toolbar and the bookmarks bar
inline OkayUI::Color32 kTabHover = Rgb(35, 39, 48);     ///< an inactive tab under the pointer
inline OkayUI::Color32 kField = Rgb(29, 32, 40);        ///< the address bar
inline OkayUI::Color32 kFieldHover = Rgb(35, 38, 47);
inline OkayUI::Color32 kHover = Rgb(62, 67, 80);        ///< a toolbar button under the pointer
inline OkayUI::Color32 kPressed = Rgb(74, 80, 96);      ///< ... held down
inline OkayUI::Color32 kText = Rgb(232, 236, 243);
inline OkayUI::Color32 kTextDim = Rgb(150, 158, 175);
inline OkayUI::Color32 kIconOff = Rgb(96, 102, 116);    ///< a button that cannot be used now
inline OkayUI::Color32 kAccent = Rgb(66, 150, 250);     ///< focus rings, the bookmarked star, the loading bar
inline OkayUI::Color32 kWarn = Rgb(240, 120, 100);      ///< "Not secure"
inline OkayUI::Color32 kDivider = Rgb(62, 67, 80);
/// The chrome follows the design in force (okay/UI/OkayUIDesign.hpp): called at the start of each browser frame.
inline void FollowDesign() {
    const OkayUI::Theme& t = OkayUI::Style();
    auto c = [](const unsigned char* v) { return Rgb(v[0], v[1], v[2]); };
    auto mix = [](const unsigned char* a, const unsigned char* b, float k) {
        return Rgb((int)(a[0] + (b[0] - a[0]) * k + 0.5f), (int)(a[1] + (b[1] - a[1]) * k + 0.5f), (int)(a[2] + (b[2] - a[2]) * k + 0.5f)); };
    kStrip = c(t.track); kToolbar = c(t.raised); kTabHover = mix(t.track, t.raised, 0.5f); kField = c(t.field); kFieldHover = c(t.bgHover);
    kHover = c(t.bgHover); kPressed = c(t.bgDown); kText = c(t.text); kTextDim = c(t.textDim); kIconOff = c(t.textFaint);
    kAccent = c(t.accent); kWarn = c(t.danger); kDivider = mix(t.raised, t.text, 0.16f);
}
}

/// Toolbar icons, drawn with lines (no picture files): centred on (cx, cy), about `size` px across, into the current window
/// (`onTop`: over everything, the foreground list).
enum class BrowserIcon { Back, Forward, Reload, Stop, Home, Menu, Star, StarFilled, Lock, Info, Warning, Close, Plus, Globe, Search, Clock, Tab };
void DrawBrowserIcon(BrowserIcon icon, float cx, float cy, float size, OkayUI::Color32 col, bool onTop = false);

/// A round toolbar button with an icon: a circle while the pointer is over it, darker while held; `tip` is shown after the
/// pointer rests on it. Returns true when it was clicked (never while `enabled` is false). `rightClicked` (when set) is true
/// after a right click on it. `held` (when set) is true once when the left button has been held on it for kHoldSeconds; that
/// press is then not a click when it is let go (Back and Forward show their history list that way).
bool BrowserIconButton(const char* id, BrowserIcon icon, float x, float y, float size, bool enabled, const char* tip,
                       bool* rightClicked = nullptr, bool* held = nullptr);
inline constexpr double kHoldSeconds = 0.5;

/// The page's key event for an OkayUI key (UI Events KeyboardEvent: key, code, keyCode and the modifiers held now); false for a
/// key the page is not told about.
bool PageKeyInfo(OkayUI::KeyId key, okay::browser::KeyInfo& out);
/// A tooltip that shows once the pointer has rested on the last item for half a second (`id` names what it is for).
void BrowserDelayedTooltip(unsigned id, const std::string& text);

/// Where the Play button of an embedded box `c` is, relative to the page's top-left (c's own coordinates).
OkayUI::Rect EmbedPlayButton(const okay::browser::DrawCmd& c);

/// Draw `tab` inside (x, y, w, h). Call between OkayUI::BeginFrame and EndFrame. A pending scroll (a link to #name) is
/// applied here; the scroll position is kept in the tab (BrowserTab::ScrollY), so Back returns to where the person was.
BrowserViewResult DrawBrowserView(okay::browser::BrowserTab& tab, float x, float y, float w, float h,
                                  const BrowserImageFn& image = BrowserImageFn(), const BrowserViewOptions& options = BrowserViewOptions());

/// Only the page (no toolbar), at (x, y) with the page's own top-left at scroll offset `scrollY`: what DrawBrowserView
/// draws inside its scrolling area, also used by the golden test.
/// `zoom` scales the page (its coordinates and text); find hits are drawn over the text.
void DrawBrowserPage(const okay::browser::LayoutResult& page, float x, float y, float w, float h, float scrollY,
                     const BrowserImageFn& image = BrowserImageFn(), const EmbedStateFn& embedState = EmbedStateFn(),
                     int hoveredPlay = -1, float zoom = 1.0f, const std::vector<okay::browser::FindHit>* findHits = nullptr,
                     int findCurrent = -1, const okay::browser::ScrollOffsets* boxScroll = nullptr, const void* editing = nullptr);

} // namespace okayuiscreens
