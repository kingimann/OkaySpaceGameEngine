#pragma once
// Browser (docs/feature_map/17-launcher-platform.md 17.18): the OkayUI view of one browser tab (okay/Browser/Browser.hpp).
// It draws the toolbar (Back, Forward, Reload, the address, Open in browser, Close), the page (the layout's rectangles,
// text, images, list markers and the boxes of embedded games) in a scrolling area, and the loading and error states, and
// says what the person clicked. Like the other screens it is a plain function with no SDL, no Dear ImGui and no network:
// the host fetches (BrowserTab::TakeFetches), makes the image textures, and acts on the result.
// Step 2 (docs/platform/browser_plan.md): the browser program (browser/) asks for an address field instead of the address
// as text and for no "Open in browser" / "Close", and an embedded OkaySpace game's box gets a Play button and the game's
// state (downloading, running, an error) from the host (BrowserViewOptions::embedState); the host starts the game.
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
};

/// Web fonts (@font-face) the host loaded: a TrueType face made by OkayUI::AddFontTtf, under its family name (lower-case), weight
/// and style. Text whose font-family names a registered family is measured and drawn with it (the closest weight and style;
/// a bold face when there is one, else the default bold look). The default font otherwise.
void RegisterWebFont(const std::string& family, int weight, bool italic, const OkayUI::Font* font);
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
};

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
};

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
