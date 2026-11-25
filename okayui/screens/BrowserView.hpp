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

namespace okayuiscreens {

struct BrowserViewResult {
    bool back = false, forward = false, reload = false;
    std::string navigate;               // the address field: the address the person typed and confirmed (Enter or Go)
    int playEmbed = -1;                 // the Play button of an embedded game: its index into the layout's cmds
    bool openInSystemBrowser = false;   // "Open in browser": the host opens tab.Url() with the system browser
    bool close = false;
    int linkClicked = -1;               // index into the layout's links (pass the href to BrowserTab::Activate)
    std::string hoverHref;              // the link under the pointer, as written ("" when none)
    OkayUI::Rect page;                  // where the page area is on the screen
    float pageTop = 0.0f;               // the screen y of the page's own top (the page area's top minus the scroll)
};

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

struct BrowserViewOptions {
    AddressField* address = nullptr;   ///< set: an address field and a Go button; null: the address as text (the launcher)
    bool hostButtons = true;           ///< "Open in browser" and "Close" (the launcher's page has them; the program does not)
    EmbedStateFn embedState;           ///< set: embedded boxes ask it for a Play button and a state line
};

/// Where the Play button of an embedded box `c` is, relative to the page's top-left (c's own coordinates).
OkayUI::Rect EmbedPlayButton(const okay::browser::DrawCmd& c);

/// Draw `tab` inside (x, y, w, h). Call between OkayUI::BeginFrame and EndFrame. A pending scroll (a link to #name) is
/// applied here; the scroll position is kept in the tab (BrowserTab::ScrollY), so Back returns to where the person was.
BrowserViewResult DrawBrowserView(okay::browser::BrowserTab& tab, float x, float y, float w, float h,
                                  const BrowserImageFn& image = BrowserImageFn(), const BrowserViewOptions& options = BrowserViewOptions());

/// Only the page (no toolbar), at (x, y) with the page's own top-left at scroll offset `scrollY`: what DrawBrowserView
/// draws inside its scrolling area, also used by the golden test.
void DrawBrowserPage(const okay::browser::LayoutResult& page, float x, float y, float w, float h, float scrollY,
                     const BrowserImageFn& image = BrowserImageFn(), const EmbedStateFn& embedState = EmbedStateFn(),
                     int hoveredPlay = -1);

} // namespace okayuiscreens
