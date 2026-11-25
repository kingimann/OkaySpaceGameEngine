// Browser: the OkayUI view of one tab (see BrowserView.hpp).
#include "BrowserView.hpp"

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <cstdio>

namespace okayuiscreens {

namespace UI = OkayUI;
namespace br = okay::browser;

namespace {

UI::Color32 Pack(const br::Color& c) {
    return (UI::Color32)c.r | ((UI::Color32)c.g << 8) | ((UI::Color32)c.b << 16) | ((UI::Color32)c.a << 24);
}

// The address as shown in the toolbar: cut in the middle when it is too long.
std::string ShortUrl(const std::string& url, std::size_t max) {
    if (url.size() <= max || max < 8) return url;
    const std::size_t keep = (max - 3) / 2;
    return url.substr(0, keep) + "..." + url.substr(url.size() - keep);
}

} // namespace

// OkayUI's text scale 1 is an 8 px line, so a font size of `px` is the scale px / 8 (what DrawList::AddText with a font size
// does). Measuring and drawing both set it, so the width the layout used is the width that is drawn.
struct TextScaleFor {
    float saved;
    explicit TextScaleFor(float px) : saved(UI::Style().textScale) { UI::Style().textScale = px / 8.0f; }
    ~TextScaleFor() { UI::Style().textScale = saved; }
};

float OkayUIMeasure::Width(const std::string& utf8, const br::FontSpec& font) const {
    if (utf8.empty() || !(font.size > 0.0f)) return 0.0f;
    const TextScaleFor scale(font.size);
    float w = UI::CalcTextWidth(utf8.c_str());
    if (font.bold) w += std::max(1.0f, font.size / 24.0f);   // the drawn bold is the text twice, a little apart
    return w;
}

void DrawBrowserPage(const br::LayoutResult& page, float x, float y, float w, float h, float scrollY, const BrowserImageFn& image) {
    const auto& dl = UI::GetWindowDrawList();
    UI::PushClipRect(x, y, w, h);
    dl->AddRectFilled(UI::Vec2(x, y), UI::Vec2(x + w, y + h), Pack(page.canvas));
    const float top = y - scrollY;
    for (const br::DrawCmd& c : page.cmds) {
        const float cx = x + c.x, cy = top + c.y;
        if (cy > y + h || cy + std::max(c.h, 1.0f) < y) continue;   // outside the view
        switch (c.kind) {
        case br::DrawCmd::Fill:
            dl->AddRectFilled(UI::Vec2(cx, cy), UI::Vec2(cx + c.w, cy + c.h), Pack(c.color));
            break;
        case br::DrawCmd::Text: {
            const UI::Color32 col = Pack(c.color);
            {
                const TextScaleFor scale(c.font.size);
                dl->AddText(UI::Vec2(cx, cy), col, c.text.c_str(), c.font.size / 8.0f);
                if (c.font.bold) dl->AddText(UI::Vec2(cx + std::max(1.0f, c.font.size / 24.0f), cy), col, c.text.c_str(), c.font.size / 8.0f);
            }
            const float thick = std::max(1.0f, c.font.size / 16.0f);
            if (c.underline) dl->AddRectFilled(UI::Vec2(cx, cy + c.font.size * 0.92f), UI::Vec2(cx + c.w, cy + c.font.size * 0.92f + thick), col);
            if (c.strike) dl->AddRectFilled(UI::Vec2(cx, cy + c.font.size * 0.55f), UI::Vec2(cx + c.w, cy + c.font.size * 0.55f + thick), col);
            break;
        }
        case br::DrawCmd::Image: {
            SDL_Texture* tex = image ? image(c.text) : nullptr;
            if (tex) dl->AddImage(tex, UI::Vec2(cx, cy), UI::Vec2(cx + c.w, cy + c.h));
            else dl->AddRect(UI::Vec2(cx, cy), UI::Vec2(cx + c.w, cy + c.h), Pack(br::Color{160, 160, 160, 255}), 0.0f, 0, 1.0f);
            break;
        }
        case br::DrawCmd::Embed: {
            // Step 1 runs nothing inside: a placeholder box with its kind (an OkaySpace game will run here in step 2).
            dl->AddRectFilled(UI::Vec2(cx, cy), UI::Vec2(cx + c.w, cy + c.h), Pack(br::Color{24, 28, 38, 255}));
            dl->AddRect(UI::Vec2(cx, cy), UI::Vec2(cx + c.w, cy + c.h), Pack(br::Color{90, 100, 120, 255}), 0.0f, 0, 1.0f);
            const char* label = c.tag == "okay-game" ? "OkaySpace game" : c.tag.c_str();
            const UI::Vec2 ts = UI::CalcTextSize(label);
            dl->AddText(UI::Vec2(cx + (c.w - ts.x) * 0.5f, cy + (c.h - ts.y) * 0.5f), Pack(br::Color{200, 205, 215, 255}), label);
            break;
        }
        case br::DrawCmd::Disc:
            dl->AddCircleFilled(UI::Vec2(cx + c.w * 0.5f, cy + c.h * 0.5f), c.w * 0.5f, Pack(c.color));
            break;
        case br::DrawCmd::Circle:
            dl->AddCircle(UI::Vec2(cx + c.w * 0.5f, cy + c.h * 0.5f), c.w * 0.5f - 0.5f, Pack(c.color), 0, 1.0f);
            break;
        case br::DrawCmd::Square:
            dl->AddRectFilled(UI::Vec2(cx, cy), UI::Vec2(cx + c.w, cy + c.h), Pack(c.color));
            break;
        }
    }
    UI::PopClipRect();
}

BrowserViewResult DrawBrowserView(br::BrowserTab& tab, float x, float y, float w, float h, const BrowserImageFn& image) {
    BrowserViewResult res;
    UI::BareNextWindow();
    if (UI::Begin("Browser##okayui", x, y, w, h)) {
        // ---- the toolbar ----
        UI::BeginDisabled(!tab.CanGoBack());
        if (UI::Button("Back")) res.back = true;
        UI::EndDisabled();
        UI::SameLine();
        UI::BeginDisabled(!tab.CanGoForward());
        if (UI::Button("Forward")) res.forward = true;
        UI::EndDisabled();
        UI::SameLine();
        if (UI::Button(tab.GetState() == br::BrowserTab::State::Loading ? "Loading..." : "Reload")) res.reload = true;
        UI::SameLine();
        if (UI::Button("Open in browser")) res.openInSystemBrowser = true;
        UI::SameLine();
        if (UI::Button("Close")) res.close = true;
        UI::SameLine();
        // the address last, cut in the middle to the room that is left
        const float room = std::max(40.0f, UI::GetContentRegionAvail().x - 8.0f);
        std::size_t chars = tab.Url().size();
        while (chars > 8 && UI::CalcTextWidth(ShortUrl(tab.Url(), chars).c_str()) > room) chars -= chars > 40 ? 8 : 2;
        UI::TextDisabled(ShortUrl(tab.Url(), chars).c_str());
        UI::Spacing(4.0f);

        // ---- the page ----
        const UI::Vec2 avail = UI::GetContentRegionAvail();
        UI::BeginChild("##browserpage", 0.0f, std::max(20.0f, avail.y), false);
        const UI::Vec2 origin = UI::GetCursorScreenPos();   // the page's top-left (already moved up by the scroll)
        const float viewW = std::max(20.0f, UI::GetContentRegionAvail().x);
        const float scroll = UI::GetScrollY();
        const float viewTop = origin.y + scroll;
        const float viewH = std::max(20.0f, avail.y);
        res.page.x = origin.x; res.page.y = viewTop; res.page.w = viewW; res.page.h = viewH;
        res.pageTop = origin.y;
        if (tab.GetState() == br::BrowserTab::State::Ready) {
            static const OkayUIMeasure measure;
            const br::LayoutResult& page = tab.Layout(viewW, measure);
            const float pending = tab.PendingScroll();
            const bool shown = tab.TakeJustShown();
            if (pending >= 0.0f) { UI::SetScrollY(pending); tab.ScrollY() = pending; }
            else if (shown) UI::SetScrollY(tab.ScrollY());   // back where the person was on this page (0 for a new one)
            else tab.ScrollY() = scroll;                       // follow the wheel and the bar
            DrawBrowserPage(page, origin.x, viewTop, viewW, viewH, scroll, image);
            // links: the pointer over a link shows a hand; a click reports it
            const UI::Vec2 m = UI::GetMousePos();
            if (UI::IsMouseHoveringRect(UI::Vec2(origin.x, viewTop), UI::Vec2(origin.x + viewW, viewTop + viewH))) {
                const int link = br::LinkAt(page, m.x - origin.x, m.y - origin.y);
                if (link >= 0 && link < (int)page.links.size()) {
                    UI::SetMouseCursor(UI::MouseCursor_Hand);
                    res.hoverHref = page.links[(std::size_t)link];
                    if (UI::IsMouseClicked(0)) res.linkClicked = link;
                }
            }
            UI::Dummy(viewW, std::max(page.height, 1.0f));
        } else if (tab.GetState() == br::BrowserTab::State::Loading) {
            UI::Spacing(24.0f);
            UI::TextDisabled("Loading the page...");
            UI::Dummy(viewW, 1.0f);
        } else if (tab.GetState() == br::BrowserTab::State::Error) {
            UI::Spacing(24.0f);
            UI::Heading("This page could not be opened");
            UI::TextWrapped(tab.Error().c_str());
            UI::Spacing(8.0f);
            if (!tab.Url().empty() && UI::Button("Try again")) res.reload = true;
        } else {
            UI::TextDisabled("No page.");
        }
        UI::EndChild();
    }
    UI::End();
    if (!res.hoverHref.empty()) {
        // the address under the pointer, at the bottom-left like other browsers
        const auto& fg = UI::GetForegroundDrawList();
        const std::string t = ShortUrl(res.hoverHref, 90);
        const UI::Vec2 ts = UI::CalcTextSize(t.c_str());
        fg->AddRectFilled(UI::Vec2(x, y + h - ts.y - 8.0f), UI::Vec2(x + ts.x + 12.0f, y + h), Pack(br::Color{30, 34, 44, 235}));
        fg->AddText(UI::Vec2(x + 6.0f, y + h - ts.y - 4.0f), Pack(br::Color{220, 225, 235, 255}), t.c_str());
    }
    return res;
}

} // namespace okayuiscreens
