#pragma once
// Browser (docs/feature_map/17-launcher-platform.md 17.18): the OkayUI view of one browser tab (okay/Browser/Browser.hpp).
// It draws the toolbar (Back, Forward, Reload, the address, Open in browser, Close), the page (the layout's rectangles,
// text, images, list markers and the boxes of embedded games) in a scrolling area, and the loading and error states, and
// says what the person clicked. Like the other screens it is a plain function with no SDL, no Dear ImGui and no network:
// the host fetches (BrowserTab::TakeFetches), makes the image textures, and acts on the result.
#include "okay/Browser/Browser.hpp"
#include "okay/UI/OkayUI.hpp"

#include <functional>
#include <string>

namespace okayuiscreens {

struct BrowserViewResult {
    bool back = false, forward = false, reload = false;
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

/// Draw `tab` inside (x, y, w, h). Call between OkayUI::BeginFrame and EndFrame. A pending scroll (a link to #name) is
/// applied here; the scroll position is kept in the tab (BrowserTab::ScrollY), so Back returns to where the person was.
BrowserViewResult DrawBrowserView(okay::browser::BrowserTab& tab, float x, float y, float w, float h,
                                  const BrowserImageFn& image = BrowserImageFn());

/// Only the page (no toolbar), at (x, y) with the page's own top-left at scroll offset `scrollY`: what DrawBrowserView
/// draws inside its scrolling area, also used by the golden test.
void DrawBrowserPage(const okay::browser::LayoutResult& page, float x, float y, float w, float h, float scrollY,
                     const BrowserImageFn& image = BrowserImageFn());

} // namespace okayuiscreens
