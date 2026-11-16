// The launcher's Downloads page, Notifications panel and Achievements page drawn with OkayUI (see OkayUILauncherPages.hpp).
// Written from what the launcher's Dear ImGui versions show and do (launcher/main.cpp); the drawing is OkayUI's own widgets and
// draw list. The icons are simple shapes drawn here (a bell, a person, a speech bubble, a circular arrow, a trophy, a star).
#include "OkayUILauncherPages.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace UI = OkayUI;

namespace okayuiscreens {

namespace {

constexpr float kPi = 3.14159265f;

UI::Color32 Col(const unsigned char c[4], float alpha = 1.0f) {
    return UI::RGBA(c[0], c[1], c[2], (unsigned)(c[3] * std::max(0.0f, std::min(1.0f, alpha)) + 0.5f));
}
UI::Color32 Rgb(int r, int g, int b, int a = 255) { return UI::RGBA((unsigned)r, (unsigned)g, (unsigned)b, (unsigned)a); }
// The accent mixed toward white (the launcher's "accent light" for text on accent-tinted fills).
UI::Color32 AccentLight() {
    const unsigned char* a = UI::Style().accent;
    return Rgb((int)(a[0] * 0.55f + 115), (int)(a[1] * 0.55f + 115), (int)(a[2] * 0.55f + 115));
}
UI::Color32 AccentAlpha(float alpha) { const unsigned char* a = UI::Style().accent; return Rgb(a[0], a[1], a[2], (int)(alpha * 255.0f + 0.5f)); }
UI::Vec2 V(float x, float y) { return UI::Vec2{x, y}; }

std::string Lower(std::string s) { for (char& c : s) c = (char)std::tolower((unsigned char)c); return s; }

// One line of text centred in `width` from the current cursor.
void Centered(const char* s, float width, bool dim) {
    const float tw = UI::CalcTextSize(s).x;
    UI::SetCursorPosX(UI::GetCursorPosX() + std::max(0.0f, (width - tw) * 0.5f));
    if (dim) UI::TextDisabled(s); else UI::Text(s);
}

// ---- icons ----
void DrawBell(const UI::DrawList& dl, UI::Vec2 c, float s, UI::Color32 col) {
    dl.PathClear();
    dl.PathArcTo(V(c.x, c.y - 1.5f * s), 5.0f * s, kPi, 2.0f * kPi, 12);   // the dome, left to right over the top
    dl.PathLineTo(V(c.x + 6.5f * s, c.y + 4.5f * s));
    dl.PathLineTo(V(c.x - 6.5f * s, c.y + 4.5f * s));
    dl.PathFillConvex(col);
    dl.AddRectFilled(V(c.x - 5.0f * s, c.y - 1.6f * s), V(c.x + 5.0f * s, c.y + 4.5f * s), col);
    dl.AddCircleFilled(V(c.x, c.y + 6.3f * s), 1.7f * s, col, 10);
}
void DrawPerson(const UI::DrawList& dl, UI::Vec2 c, float s, UI::Color32 col) {
    dl.AddCircleFilled(V(c.x, c.y - 3.0f * s), 3.4f * s, col, 14);
    dl.PathClear();
    dl.PathArcTo(V(c.x, c.y + 7.0f * s), 6.2f * s, kPi, 2.0f * kPi, 14);
    dl.PathFillConvex(col);
}
void DrawBubble(const UI::DrawList& dl, UI::Vec2 c, float s, UI::Color32 col) {
    dl.AddRectFilled(V(c.x - 7.0f * s, c.y - 5.5f * s), V(c.x + 7.0f * s, c.y + 3.5f * s), col, 3.0f * s);
    dl.AddTriangleFilled(V(c.x - 4.0f * s, c.y + 3.0f * s), V(c.x + 0.5f * s, c.y + 3.0f * s), V(c.x - 4.5f * s, c.y + 7.0f * s), col);
}
void DrawCycle(const UI::DrawList& dl, UI::Vec2 c, float s, UI::Color32 col) {
    dl.PathClear();
    dl.PathArcTo(c, 5.5f * s, -0.35f * kPi, 1.25f * kPi, 18);
    dl.PathStroke(col, false, 1.8f * s);
    const float a = -0.35f * kPi;
    const UI::Vec2 tip = V(c.x + std::cos(a) * 5.5f * s, c.y + std::sin(a) * 5.5f * s);
    dl.AddTriangleFilled(V(tip.x - 3.2f * s, tip.y - 2.2f * s), V(tip.x + 2.6f * s, tip.y - 3.0f * s), V(tip.x + 0.8f * s, tip.y + 2.8f * s), col);
}
void DrawTrophy(const UI::DrawList& dl, UI::Vec2 c, float s, UI::Color32 col) {
    dl.PathClear();
    dl.PathLineTo(V(c.x - 5.6f * s, c.y - 7.5f * s));
    dl.PathLineTo(V(c.x + 5.6f * s, c.y - 7.5f * s));
    dl.PathArcTo(V(c.x, c.y - 2.6f * s), 5.6f * s, 0.0f, kPi, 14);   // right, down round the bottom, to the left
    dl.PathFillConvex(col);
    dl.PathClear(); dl.PathArcTo(V(c.x - 5.6f * s, c.y - 4.6f * s), 2.6f * s, kPi * 0.5f, kPi * 1.5f, 8); dl.PathStroke(col, false, 1.5f * s);
    dl.PathClear(); dl.PathArcTo(V(c.x + 5.6f * s, c.y - 4.6f * s), 2.6f * s, -kPi * 0.5f, kPi * 0.5f, 8); dl.PathStroke(col, false, 1.5f * s);
    dl.AddRectFilled(V(c.x - 1.2f * s, c.y + 2.6f * s), V(c.x + 1.2f * s, c.y + 6.0f * s), col);
    dl.AddRectFilled(V(c.x - 4.8f * s, c.y + 5.8f * s), V(c.x + 4.8f * s, c.y + 8.4f * s), col, 1.2f * s);
}
// A five-pointed star: the centre pentagon and five tip triangles (the fill is convex piece by piece).
void DrawStar(const UI::DrawList& dl, UI::Vec2 c, float r, UI::Color32 col) {
    UI::Vec2 outer[5], inner[5];
    for (int i = 0; i < 5; ++i) {
        const float a = -kPi * 0.5f + i * (2.0f * kPi / 5.0f), b = a + kPi / 5.0f;
        outer[i] = V(c.x + std::cos(a) * r, c.y + std::sin(a) * r);
        inner[i] = V(c.x + std::cos(b) * r * 0.45f, c.y + std::sin(b) * r * 0.45f);
    }
    dl.AddConvexPolyFilled(inner, 5, col);
    for (int i = 0; i < 5; ++i) dl.AddTriangleFilled(outer[i], inner[i], inner[(i + 4) % 5], col);
}
// The achievement medal: a soft halo, a hexagon in the id's colour with an outline, a trophy on it.
void DrawMedal(const UI::DrawList& dl, UI::Vec2 c, float r, const unsigned char rgb[3], float alpha) {
    const float s = r / 15.0f;
    dl.AddCircleFilled(c, r + 3.0f, Rgb(rgb[0], rgb[1], rgb[2], (int)(0.14f * alpha * 255.0f)), 32);
    UI::Vec2 hex[6];
    for (int i = 0; i < 6; ++i) hex[i] = V(c.x + std::cos(i * kPi / 3.0f) * r, c.y + std::sin(i * kPi / 3.0f) * r);
    const int a = (int)(alpha * 255.0f + 0.5f);
    dl.AddConvexPolyFilled(hex, 6, Rgb((int)(rgb[0] * 0.46f), (int)(rgb[1] * 0.46f), (int)(rgb[2] * 0.46f), a));
    dl.AddPolyline(hex, 6, Rgb(rgb[0], rgb[1], rgb[2], a), true, 1.8f);
    DrawTrophy(dl, c, s, Rgb((int)(rgb[0] * 0.30f + 178.5f), (int)(rgb[1] * 0.30f + 178.5f), (int)(rgb[2] * 0.30f + 178.5f), a));
}
// A round [x]: a disc that brightens on hover and two strokes.
void DrawCross(const UI::DrawList& dl, UI::Vec2 c, float r, float arm, bool hover, UI::Color32 col, int baseAlpha, int hoverAlpha) {
    dl.AddCircleFilled(c, r, Rgb(255, 255, 255, hover ? hoverAlpha : baseAlpha), 20);
    dl.AddLine(V(c.x - arm, c.y - arm), V(c.x + arm, c.y + arm), col, 1.6f);
    dl.AddLine(V(c.x + arm, c.y - arm), V(c.x - arm, c.y + arm), col, 1.6f);
}

} // namespace

// ---- text helpers ----------------------------------------------------------------------------------------------------------

std::string FitTextWidth(const std::string& s, float maxW) {
    if (!(maxW > 0.0f)) return std::string();
    if (UI::CalcTextSize(s.c_str()).x <= maxW) return s;
    static const char* kEll = "\xE2\x80\xA6";
    const float ew = UI::CalcTextSize(kEll).x;
    // Binary search over character boundaries: the most characters whose prefix still fits with the ellipsis.
    std::vector<std::size_t> starts;   // byte offset where each character begins: a prefix of k characters is starts[k] bytes
    starts.reserve(s.size());
    for (std::size_t i = 0; i < s.size(); ++i) if (((unsigned char)s[i] & 0xC0) != 0x80) starts.push_back(i);
    std::size_t lo = 0, hi = starts.empty() ? 0 : starts.size() - 1;   // the whole text does not fit, so fewer than all
    while (lo < hi) {
        const std::size_t mid = (lo + hi + 1) / 2;
        if (UI::CalcTextSize(s.substr(0, starts[mid]).c_str()).x + ew <= maxW) lo = mid; else hi = mid - 1;
    }
    return s.substr(0, lo < starts.size() ? starts[lo] : 0) + kEll;
}

void WrapTwoLines(const std::string& s, float maxW, std::string& l1, std::string& l2) {
    l1.clear(); l2.clear();
    if (UI::CalcTextSize(s.c_str()).x <= maxW) { l1 = s; return; }
    std::size_t cut = 0;
    for (std::size_t i = 0; i < s.size(); ++i)
        if (s[i] == ' ') { if (UI::CalcTextSize(s.substr(0, i).c_str()).x <= maxW) cut = i; else break; }
    if (cut == 0) { l1 = FitTextWidth(s, maxW); return; }
    l1 = s.substr(0, cut);
    l2 = FitTextWidth(s.substr(cut + 1), maxW);
}

// ---- Downloads -------------------------------------------------------------------------------------------------------------

std::string HumanBytes(long long b) {
    char out[32];
    if (b <= 0) std::snprintf(out, sizeof(out), "--");
    else if (b < 1024) std::snprintf(out, sizeof(out), "%lld B", b);
    else if (b < 1024 * 1024) std::snprintf(out, sizeof(out), "%.1f KB", b / 1024.0);
    else if (b < 1024LL * 1024 * 1024) std::snprintf(out, sizeof(out), "%.1f MB", b / (1024.0 * 1024));
    else std::snprintf(out, sizeof(out), "%.2f GB", b / (1024.0 * 1024 * 1024));
    return out;
}

const char* DownloadStatusText(int s) {
    return s == 0 ? "Queued" : s == 1 ? "Downloading" : s == 2 ? "Completed" : s == 3 ? "Failed" : "Canceled";
}

bool DownloadIsZip(const std::string& dest) {
    if (dest.size() <= 4) return false;
    const std::string ext = dest.substr(dest.size() - 4);
    return ext == ".zip" || ext == ".ZIP";
}

float DownloadFraction(const DownloadEntry& d) {
    float f = d.total > 0 ? (float)((double)d.received / (double)d.total) : (d.status == 2 ? 1.0f : 0.0f);
    if (!(f > 0.0f)) f = 0.0f;
    return std::min(f, 1.0f);
}

DownloadsResult DrawDownloads(const DownloadsModel& m, float x, float y, float w, float h) {
    DownloadsResult res;
    const std::size_t n = m.items.size();
    res.rows.resize(n); res.cancelButtons.resize(n); res.openButtons.resize(n); res.installButtons.resize(n); res.removeButtons.resize(n);
    for (const DownloadEntry& d : m.items) if (d.status >= 2) { res.clearEnabled = true; break; }
    res.empty = m.items.empty();

    UI::BareNextWindow();
    if (UI::Begin("Downloads##okayui", x, y, w, h)) {
        UI::Heading("Downloads");
        UI::TextDisabled("Games you download from the marketplace land here.");
        UI::Spacing(4.0f);
        if (UI::Button("Open downloads folder", UI::Vec2{200.0f, 0.0f})) res.openFolder = true;
        res.openFolderButton = UI::GetItemRect();
        UI::SameLine();
        UI::BeginDisabled(!res.clearEnabled);
        if (UI::Button("Clear finished", UI::Vec2{130.0f, 0.0f})) res.clearFinished = true;
        res.clearFinishedButton = UI::GetItemRect();
        UI::EndDisabled();
        UI::Spacing(6.0f);

        UI::BeginChild("##dllist", 0.0f, UI::GetContentRegionAvail().y, false);
        const UI::Theme& th = UI::Style();
        if (res.empty) {
            const float cw = UI::GetContentRegionAvail().x;
            UI::Dummy(0.0f, 34.0f);
            const UI::Vec2 ip = UI::GetCursorScreenPos();
            const float icx = ip.x + cw * 0.5f, icy = ip.y + 12.0f;
            const UI::DrawList& dl = UI::GetWindowDrawList();
            const UI::Color32 ic = Col(th.textDim);   // an arrow down into a tray
            dl.AddLine(V(icx, icy - 16), V(icx, icy + 6), ic, 2.6f);
            dl.AddLine(V(icx - 9, icy - 3), V(icx, icy + 6), ic, 2.6f);
            dl.AddLine(V(icx + 9, icy - 3), V(icx, icy + 6), ic, 2.6f);
            dl.AddLine(V(icx - 15, icy + 15), V(icx + 15, icy + 15), ic, 2.6f);
            UI::Dummy(0.0f, 40.0f);
            Centered("No downloads yet", cw, false);
            Centered("Games you get from the Marketplace land here.", cw, true);
            UI::Spacing(8.0f);
            UI::SetCursorPosX(UI::GetCursorPosX() + std::max(0.0f, (cw - 210.0f) * 0.5f));
            if (UI::PrimaryButton("Browse Marketplace", UI::Vec2{210.0f, 38.0f})) res.browseMarketplace = true;
            res.browseButton = UI::GetItemRect();
        }
        for (std::size_t i = 0; i < n; ++i) {
            const DownloadEntry& d = m.items[i];
            UI::PushID(d.id);
            UI::BeginCard("##dlrow");
            UI::Text(d.title.c_str());
            char overlay[64];
            if (d.total > 0) std::snprintf(overlay, sizeof(overlay), "%s / %s", HumanBytes(d.received).c_str(), HumanBytes(d.total).c_str());
            else std::snprintf(overlay, sizeof(overlay), "%s", HumanBytes(d.received).c_str());
            const float barW = std::max(40.0f, UI::GetContentRegionAvail().x - 220.0f);
            UI::ProgressBar(DownloadFraction(d), UI::Vec2{barW, 16.0f}, overlay);
            UI::SameLine();
            const char* st = DownloadStatusText(d.status);
            if (d.status == 2) UI::TextColored(140, 230, 153, st);
            else if (d.status == 3) UI::TextColored(230, 115, 115, st);
            else if (d.status == 4) UI::TextColored(191, 179, 128, st);
            else UI::TextDisabled(st);
            if (d.status == 0 || d.status == 1) {
                if (UI::SmallButton("Cancel")) res.cancelId = d.id;
                res.cancelButtons[i] = UI::GetItemRect();
            } else {
                if (d.status == 2) {
                    if (UI::SmallButton("Open folder")) res.openItemFolderId = d.id;
                    res.openButtons[i] = UI::GetItemRect();
                    if (DownloadIsZip(d.dest)) {
                        UI::SameLine();
                        if (UI::SmallButton("Install to Community")) res.installId = d.id;
                        res.installButtons[i] = UI::GetItemRect();
                    }
                    UI::SameLine();
                }
                if (UI::SmallButton("Remove")) res.removeId = d.id;
                res.removeButtons[i] = UI::GetItemRect();
            }
            UI::EndCard();
            res.rows[i] = UI::GetItemRect();
            UI::PopID();
        }
        UI::EndChild();
    }
    UI::End();
    return res;
}

// ---- Notifications ---------------------------------------------------------------------------------------------------------

namespace {
int NotifKind(const NotifEntry& n) { return (n.kind >= 1 && n.kind <= 3) ? n.kind : 0; }
const char* kChipLabel[6] = {"All", "Unread", "Friends", "Messages", "Updates", "System"};
const int kFilterKind[6] = {-1, -1, 1, 2, 3, 0};
} // namespace

bool NotifPassesFilter(const NotifEntry& n, int filter) {
    if (filter == 1) return n.unread;
    if (filter >= 2 && filter <= 5) return NotifKind(n) == kFilterKind[filter];
    return true;
}

std::string NotifAgeText(unsigned a) {
    char rt[16];
    if (a < 60) std::snprintf(rt, sizeof(rt), "now");
    else if (a < 3600) std::snprintf(rt, sizeof(rt), "%um", a / 60);
    else if (a < 86400) std::snprintf(rt, sizeof(rt), "%uh", a / 3600);
    else std::snprintf(rt, sizeof(rt), "%ud", a / 86400);
    return rt;
}

std::vector<int> NotifOrder(const NotificationsModel& m, int filter) {
    std::vector<int> order;
    for (int i = 0; i < (int)m.items.size(); ++i) if (NotifPassesFilter(m.items[(std::size_t)i], filter)) order.push_back(i);
    std::stable_sort(order.begin(), order.end(), [&](int a, int b) { return m.items[(std::size_t)a].ageSeconds < m.items[(std::size_t)b].ageSeconds; });
    return order;
}

void NotifEmptyText(int total, int filter, std::string& title, std::string& hint) {
    const bool caughtUp = total == 0 || filter == 1;
    title = caughtUp ? "You're all caught up" : "Nothing here";
    hint = total == 0 ? "Friend requests, messages and updates show up here."
         : filter == 1 ? "No unread notifications."
         : filter == 2 ? "No friend notifications."
         : filter == 3 ? "No message notifications."
         : filter == 4 ? "No update notifications."
                       : "No system notifications.";
}

NotificationsResult DrawNotifications(const NotificationsModel& m, NotificationsState& st, float x, float y, float w, float h) {
    NotificationsResult res;
    if (st.filter < 0 || st.filter > 5) st.filter = 0;
    res.total = (int)m.items.size();
    for (const NotifEntry& n : m.items) res.unread += n.unread ? 1 : 0;

    UI::SetNextWindowPos(V(x, y));
    UI::SetNextWindowSize(V(w, h));
    if (UI::Begin("Notifications##okayui", nullptr, UI::Window_NoTitleBar | UI::Window_NoResize | UI::Window_NoMove | UI::Window_NoCollapse | UI::Window_NoScrollbar)) {
        const UI::Theme& th = UI::Style();
        const UI::Color32 cText = Col(th.text), cDim = Col(th.textDim), accLt = AccentLight();
        const UI::DrawList& dl = UI::GetWindowDrawList();

        // ---- header: title and "N new", then Mark all read, Clear and the close button at the right ----
        const UI::Vec2 hp = UI::GetCursorScreenPos();
        const float cw = UI::GetContentRegionAvail().x, hh = 28.0f;
        UI::PushTextScale(1.22f);
        const UI::Vec2 tsz = UI::CalcTextSize("Notifications");
        dl.AddText(V(hp.x, hp.y + (hh - tsz.y) * 0.5f), cText, "Notifications");
        UI::PopTextScale();
        if (res.unread > 0) {
            char nb[24]; std::snprintf(nb, sizeof(nb), "%d new", res.unread);
            const UI::Vec2 bs = UI::CalcTextSize(nb);
            const UI::Vec2 b0 = V(hp.x + tsz.x + 10.0f, hp.y + hh * 0.5f - 10.0f);
            dl.AddRectFilled(b0, V(b0.x + bs.x + 14.0f, b0.y + 20.0f), AccentAlpha(0.24f), 10.0f);
            dl.AddText(V(b0.x + 7.0f, b0.y + (20.0f - bs.y) * 0.5f), accLt, nb);
        }
        auto textButton = [&](const char* id, const char* label, float bx, bool enabled, bool accent, UI::Rect& rect) {
            const UI::Vec2 ts = UI::CalcTextSize(label);
            const UI::Vec2 p = V(bx, hp.y + (hh - 26.0f) * 0.5f), sz = V(ts.x + 16.0f, 26.0f);
            UI::SetCursorScreenPos(p);
            const bool c = UI::InvisibleButton(id, sz.x, sz.y) && enabled;
            rect = UI::GetItemRect();
            const bool hov = enabled && UI::IsItemHovered();
            if (hov) { dl.AddRectFilled(p, V(p.x + sz.x, p.y + sz.y), Rgb(255, 255, 255, 16), 7.0f); UI::SetMouseCursor(UI::MouseCursor_Hand); }
            const UI::Color32 col = !enabled ? Col(th.textDim, 0.55f) : hov ? cText : accent ? accLt : cDim;
            dl.AddText(V(p.x + 8.0f, p.y + (sz.y - ts.y) * 0.5f), col, label);
            return c;
        };
        {
            const UI::Vec2 xp = V(hp.x + cw - 26.0f, hp.y + (hh - 26.0f) * 0.5f);
            UI::SetCursorScreenPos(xp);
            if (UI::InvisibleButton("##ntclose", 26.0f, 26.0f)) res.close = true;
            res.closeButton = UI::GetItemRect();
            const bool xh = UI::IsItemHovered();
            if (xh) UI::SetTooltip("%s", "Close  (Esc)");
            DrawCross(dl, V(xp.x + 13.0f, xp.y + 13.0f), 13.0f, 4.5f, xh, xh ? cText : cDim, 0, 20);
            const float clearW = UI::CalcTextSize("Clear").x + 16.0f, markW = UI::CalcTextSize("Mark all read").x + 16.0f;
            const float clearX = xp.x - 6.0f - clearW, markX = clearX - 2.0f - markW;
            if (textButton("##ntmark", "Mark all read", markX, res.unread > 0, true, res.markAllButton)) res.markAllRead = true;
            if (textButton("##ntclear", "Clear", clearX, res.total > 0, false, res.clearButton)) res.clearAll = true;
        }
        UI::SetCursorScreenPos(V(hp.x, hp.y + hh + 10.0f));
        UI::Dummy(0.0f, 0.0f);

        // ---- filter chips (wrapping onto a second row on a narrow panel) ----
        {
            const UI::Vec2 cp = UI::GetCursorScreenPos();
            const float ch = UI::GetTextLineHeight() + 10.0f;
            float cx = cp.x, cy = cp.y;
            for (int k = 0; k < 6; ++k) {
                const UI::Vec2 ts = UI::CalcTextSize(kChipLabel[k]);
                char cnt[8] = "";
                if (k == 1 && res.unread > 0) std::snprintf(cnt, sizeof(cnt), "%d", res.unread);
                const UI::Vec2 cs = UI::CalcTextSize(cnt);
                const float cwid = ts.x + 18.0f + (cnt[0] ? cs.x + 16.0f : 0.0f);
                if (cx > cp.x && cx + cwid > cp.x + cw) { cx = cp.x; cy += ch + 6.0f; }
                UI::SetCursorScreenPos(V(cx, cy));
                UI::PushID(k);
                if (UI::InvisibleButton("##ntchip", cwid, ch)) st.filter = k;
                res.chips.push_back(UI::GetItemRect());
                const bool hov = UI::IsItemHovered();
                UI::PopID();
                const bool on = st.filter == k;
                dl.AddRectFilled(V(cx, cy), V(cx + cwid, cy + ch), on ? AccentAlpha(0.24f) : Rgb(255, 255, 255, hov ? 22 : 9), ch * 0.5f);
                dl.AddText(V(cx + 9.0f, cy + (ch - ts.y) * 0.5f), on ? accLt : (hov ? cText : cDim), kChipLabel[k]);
                if (cnt[0]) {
                    const float bx = cx + 9.0f + ts.x + 6.0f, bw = cs.x + 10.0f;
                    dl.AddRectFilled(V(bx, cy + (ch - 16.0f) * 0.5f), V(bx + bw, cy + (ch + 16.0f) * 0.5f), Col(th.accent), 8.0f);
                    dl.AddText(V(bx + 5.0f, cy + (ch - cs.y) * 0.5f), Rgb(255, 255, 255), cnt);
                }
                if (hov) UI::SetMouseCursor(UI::MouseCursor_Hand);
                cx += cwid + 6.0f;
            }
            UI::SetCursorScreenPos(V(cp.x, cy + ch + 12.0f));
            const UI::Vec2 lp = UI::GetCursorScreenPos();
            dl.AddLine(V(lp.x - 10.0f, lp.y), V(lp.x + cw + 10.0f, lp.y), Rgb(255, 255, 255, 16), 1.0f);
            UI::Dummy(0.0f, 2.0f);
        }

        // ---- the feed, newest first, grouped Today / Yesterday / Earlier ----
        res.shown = NotifOrder(m, st.filter);
        UI::BeginChild("##ntlist", 0.0f, UI::GetContentRegionAvail().y, false);
        res.list = UI::GetItemRect();
        {
            const UI::DrawList& ld = UI::GetWindowDrawList();
            const float tl = UI::GetTextLineHeight();
            int lastGroup = -1;
            const UI::Input& in = UI::GetFrameInput();
            for (int idx : res.shown) {
                const NotifEntry& n = m.items[(std::size_t)idx];
                const int kind = NotifKind(n);
                const int grp = n.day < 0 ? 0 : n.day > 2 ? 2 : n.day;
                if (grp != lastGroup) {
                    if (lastGroup != -1) UI::Spacing(2.0f);
                    UI::SetCursorPosX(UI::GetCursorPosX() + 4.0f);
                    UI::PushTextScale(0.80f);
                    UI::TextDisabled(grp == 0 ? "TODAY" : grp == 1 ? "YESTERDAY" : "EARLIER");
                    UI::PopTextScale();
                    lastGroup = grp;
                }
                UI::PushID(idx);
                const UI::Vec2 rp = UI::GetCursorScreenPos();
                const float rw = UI::GetContentRegionAvail().x;
                const float icD = 36.0f, tx = rp.x + 10.0f + icD + 12.0f;
                const float textW = rw - (tx - rp.x) - 46.0f;
                std::string b1, b2;
                WrapTwoLines(n.body, textW, b1, b2);
                const int bodyLines = n.body.empty() ? 0 : (b2.empty() ? 1 : 2);
                const float rh = std::max(58.0f, 11.0f + tl + 3.0f + bodyLines * (tl + 1.0f) + 10.0f);
                const float rx = rp.x + rw - 12.0f;
                const UI::Rect xr{rx - 22.0f, rp.y + 8.0f, 22.0f, 22.0f};   // the dismiss button, shown while the row is hovered
                const bool click = UI::InvisibleButton("##ntrow", rw, rh);
                const UI::Rect rowRect = UI::GetItemRect();
                res.rows.push_back(rowRect);
                const bool hov = UI::IsItemHovered();
                const bool visible = UI::IsItemVisible();
                const bool overX = hov && in.mouseX >= xr.x && in.mouseX < xr.x + xr.w && in.mouseY >= xr.y && in.mouseY < xr.y + xr.h;
                if (hov) { res.dismissButton = xr; UI::SetMouseCursor(UI::MouseCursor_Hand); }
                if (click) { if (overX) res.dismissed = idx; else res.clicked = idx; }
                if (visible) {
                    ld.AddRectFilled(rp, V(rp.x + rw, rp.y + rh), hov ? Rgb(255, 255, 255, 15) : n.unread ? AccentAlpha(0.07f) : Rgb(0, 0, 0, 0), 10.0f);
                    // the kind's icon on a tinted disc
                    const unsigned char kc[3][3] = {{242, 184, 77}, {97, 173, 250}, {97, 204, 140}};
                    const unsigned char upd[3] = {168, 138, 250};
                    const unsigned char* c = kind == 1 ? kc[1] : kind == 2 ? kc[2] : kind == 3 ? upd : kc[0];
                    const UI::Vec2 ic = V(rp.x + 10.0f + icD * 0.5f, rp.y + 11.0f + icD * 0.5f - 2.0f);
                    ld.AddCircleFilled(ic, icD * 0.5f, Rgb(c[0], c[1], c[2], 43), 32);
                    const UI::Color32 icol = Rgb((int)(c[0] * 0.8f + 51), (int)(c[1] * 0.8f + 51), (int)(c[2] * 0.8f + 51));
                    if (kind == 1) DrawPerson(ld, ic, 1.0f, icol);
                    else if (kind == 2) DrawBubble(ld, ic, 1.0f, icol);
                    else if (kind == 3) DrawCycle(ld, ic, 1.0f, icol);
                    else DrawBell(ld, ic, 1.0f, icol);
                    // the title (doubled for weight when unread), then up to two body lines
                    const std::string title = FitTextWidth(n.title.empty() ? std::string("Notification") : n.title, textW);
                    const UI::Color32 tcol = n.unread ? cText : Col(th.text, 0.80f);
                    ld.AddText(V(tx, rp.y + 11.0f), tcol, title.c_str());
                    if (n.unread) ld.AddText(V(tx + 0.7f, rp.y + 11.0f), tcol, title.c_str());
                    float by = rp.y + 11.0f + tl + 3.0f;
                    if (!b1.empty()) { ld.AddText(V(tx, by), cDim, b1.c_str()); by += tl + 1.0f; }
                    if (!b2.empty()) ld.AddText(V(tx, by), cDim, b2.c_str());
                    // right column: the age, or the dismiss button while hovered; the unread dot
                    if (hov) {
                        DrawCross(ld, V(xr.x + 11.0f, xr.y + 11.0f), 11.0f, 3.5f, overX, overX ? cText : cDim, 14, 34);
                        if (overX) UI::SetTooltip("%s", "Dismiss");
                    } else {
                        const std::string age = NotifAgeText(n.ageSeconds);
                        const UI::Vec2 ts = UI::CalcTextSize(age.c_str());
                        ld.AddText(V(rx - ts.x, rp.y + 11.0f), n.unread ? accLt : cDim, age.c_str());
                    }
                    if (n.unread) ld.AddCircleFilled(V(rx - 5.0f, rp.y + 11.0f + tl + 3.0f + tl * 0.5f + 1.0f), 4.5f, Col(th.accent), 14);
                }
                UI::PopID();
            }
            if (res.shown.empty()) {   // ---- the empty state ----
                res.empty = true;
                NotifEmptyText(res.total, st.filter, res.emptyTitle, res.emptyHint);
                const float aw = UI::GetContentRegionAvail().x, ah = UI::GetContentRegionAvail().y;
                UI::Dummy(0.0f, std::max(16.0f, ah * 0.20f));
                const UI::Vec2 c = V(UI::GetCursorScreenPos().x + aw * 0.5f, UI::GetCursorScreenPos().y + 30.0f);
                ld.AddCircleFilled(c, 30.0f, Rgb(255, 255, 255, 12), 40);
                DrawBell(ld, c, 1.3f, cDim);
                if (res.total == 0 || st.filter == 1) {   // a small green check badge
                    const UI::Vec2 bc = V(c.x + 20.0f, c.y + 20.0f);
                    ld.AddCircleFilled(bc, 10.0f, Col(th.panel), 20);
                    ld.AddCircleFilled(bc, 8.0f, Rgb(64, 186, 104), 20);
                    const UI::Vec2 pts[3] = {V(bc.x - 3.6f, bc.y + 0.2f), V(bc.x - 1.0f, bc.y + 2.8f), V(bc.x + 3.8f, bc.y - 2.8f)};
                    ld.AddPolyline(pts, 3, Rgb(255, 255, 255), false, 1.8f);
                }
                UI::Dummy(0.0f, 70.0f);
                UI::PushTextScale(1.08f);
                Centered(res.emptyTitle.c_str(), aw, false);
                UI::PopTextScale();
                Centered(res.emptyHint.c_str(), aw, true);
            }
        }
        UI::EndChild();
    }
    UI::End();
    return res;
}

// ---- Achievements ----------------------------------------------------------------------------------------------------------

std::string AchPrettyId(const std::string& id) {
    std::string o; bool up = true;
    for (char c : id) {
        if (c == '_' || c == '-') { o += ' '; up = true; continue; }
        o += up ? (char)std::toupper((unsigned char)c) : c;
        up = false;
    }
    return o;
}

int AchMonthCount(const AchievementsModel& m) {
    int k = 0;
    for (const AchEntry& a : m.items) if (a.when > m.now - 30L * 86400) ++k;
    return k;
}

std::vector<int> AchList(const AchievementsModel& m, int view, int sort, const std::string& filter) {
    const std::string q = Lower(filter);
    std::vector<int> list;
    for (int i = 0; i < (int)m.items.size(); ++i) {
        const AchEntry& a = m.items[(std::size_t)i];
        if (view == 1 && a.when <= m.now - 30L * 86400) continue;
        if (view == 2 && !a.showcased) continue;
        if (!q.empty() && Lower(a.label + " " + a.id).find(q) == std::string::npos) continue;
        list.push_back(i);
    }
    std::vector<std::string> lower;
    if (sort == 2) for (const AchEntry& a : m.items) lower.push_back(Lower(a.label));
    std::sort(list.begin(), list.end(), [&](int x, int y) {
        if (sort == 2) return lower[(std::size_t)x] < lower[(std::size_t)y];
        const long wx = m.items[(std::size_t)x].when, wy = m.items[(std::size_t)y].when;
        return sort == 1 ? wx < wy : wx > wy;
    });
    return list;
}

AchievementsResult DrawAchievements(const AchievementsModel& m, AchievementsState& st, float x, float y, float w, float h) {
    AchievementsResult res;
    if (st.view < 0 || st.view > 2) st.view = 0;
    if (st.sort < 0 || st.sort > 2) st.sort = 0;
    st.filter[sizeof(st.filter) - 1] = '\0';
    res.monthCount = AchMonthCount(m);
    const UI::Color32 gold = Rgb(250, 200, 70);

    UI::BareNextWindow();
    if (UI::Begin("Achievements##okayui", x, y, w, h)) {
        const UI::Theme& th = UI::Style();
        const UI::Color32 cText = Col(th.text), cDim = Col(th.textDim), accLt = AccentLight();
        UI::Heading("Achievements");
        UI::Spacing(4.0f);

        // ---- header card: the three newest medals, the count, the latest unlock, level, points, this month, profile ----
        int latest = -1;
        for (int i = 0; i < (int)m.items.size(); ++i) if (latest < 0 || m.items[(std::size_t)i].when > m.items[(std::size_t)latest].when) latest = i;
        UI::BeginCard("##achhead");
        {
            const UI::DrawList& dl = UI::GetWindowDrawList();
            const UI::Vec2 p = UI::GetCursorScreenPos();
            const float avail = UI::GetContentRegionAvail().x;
            std::vector<int> top((std::size_t)m.items.size());
            for (int i = 0; i < (int)top.size(); ++i) top[(std::size_t)i] = i;
            std::stable_sort(top.begin(), top.end(), [&](int a, int b) { return m.items[(std::size_t)a].when > m.items[(std::size_t)b].when; });
            const int heroN = std::min<int>(3, (int)top.size());
            for (int k = heroN - 1; k >= 0; --k)
                DrawMedal(dl, V(p.x + 30.0f + k * 26.0f, p.y + 32.0f), k == 0 ? 26.0f : 22.0f, m.items[(std::size_t)top[(std::size_t)k]].color, k == 0 ? 1.0f : 0.75f);
            if (heroN == 0) {
                UI::Vec2 hex[6];
                for (int i = 0; i < 6; ++i) hex[i] = V(p.x + 30.0f + std::cos(i * kPi / 3.0f) * 24.0f, p.y + 32.0f + std::sin(i * kPi / 3.0f) * 24.0f);
                dl.AddPolyline(hex, 6, cDim, true, 1.8f);
                DrawTrophy(dl, V(p.x + 30.0f, p.y + 32.0f), 1.0f, cDim);
            }
            const float tx = p.x + 30.0f + std::max(1, heroN) * 26.0f + 32.0f;
            UI::SetCursorScreenPos(V(tx, p.y + 2.0f));
            char cnt[24]; std::snprintf(cnt, sizeof(cnt), "%d", (int)m.items.size());
            UI::PushTextScale(1.45f);
            UI::Text(cnt);
            const float countBottom = UI::GetItemRect().y + UI::GetItemRect().h;
            UI::PopTextScale();
            UI::SameLine(8.0f);
            UI::Text(m.items.size() == 1 ? "achievement unlocked" : "achievements unlocked");
            UI::SetCursorScreenPos(V(tx, countBottom + 4.0f));   // the second line starts under the count, not at the card's edge
            if (latest >= 0) {
                const AchEntry& a = m.items[(std::size_t)latest];
                const std::string line = "Latest: " + (a.label.empty() ? AchPrettyId(a.id) : a.label) + "  \xC2\xB7  " + a.rel;
                UI::TextDisabled(line.c_str());
            } else UI::TextDisabled("Play games to start earning achievements.");
            const float bw = 150.0f, rx = p.x + avail;
            auto stat = [&](float sx, const char* big, const char* small) {
                UI::PushTextScale(1.2f);
                const UI::Vec2 bs = UI::CalcTextSize(big);
                dl.AddText(V(sx, p.y + 6.0f), cText, big);
                UI::PopTextScale();
                UI::PushTextScale(0.8f);
                dl.AddText(V(sx, p.y + 10.0f + bs.y), cDim, small);
                UI::PopTextScale();
            };
            if (avail > 640.0f) {
                const UI::Vec2 lc = V(rx - bw - 300.0f, p.y + 30.0f);
                const float prog = std::max(0.0f, std::min(1.0f, m.levelProgress == m.levelProgress ? m.levelProgress : 0.0f));
                dl.AddCircle(lc, 22.0f, Rgb(255, 255, 255, 30), 40, 4.0f);
                if (prog > 0.0f) {
                    dl.PathClear();
                    dl.PathArcTo(lc, 20.0f, -kPi * 0.5f, -kPi * 0.5f + prog * 2.0f * kPi, 40);
                    dl.PathStroke(gold, false, 4.0f);
                }
                char ls[12]; std::snprintf(ls, sizeof(ls), "%d", m.level);
                UI::PushTextScale(1.2f);
                const UI::Vec2 lsz = UI::CalcTextSize(ls);
                dl.AddText(V(lc.x - lsz.x * 0.5f, lc.y - lsz.y * 0.5f), cText, ls);
                UI::PopTextScale();
                UI::SetCursorScreenPos(V(lc.x - 24.0f, lc.y - 24.0f));
                UI::InvisibleButton("##lvl", 48.0f, 48.0f);
                res.levelBadge = UI::GetItemRect();
                if (UI::IsItemHovered())
                    UI::SetTooltip("Level %d  \xC2\xB7  %d%% to level %d\nEvery achievement earns points; games can make hard ones worth more.",
                                   m.level, (int)(prog * 100.0f), m.level + 1);
                char ps[24]; std::snprintf(ps, sizeof(ps), "%d", m.points);
                char ms[16]; std::snprintf(ms, sizeof(ms), "%d", res.monthCount);
                stat(rx - bw - 230.0f, ps, "POINTS");
                stat(rx - bw - 120.0f, ms, "THIS MONTH");
            }
            UI::SetCursorScreenPos(V(rx - bw, p.y + 12.0f));
            if (UI::Button("View my profile", UI::Vec2{bw, 34.0f})) res.viewProfile = true;
            res.profileButton = UI::GetItemRect();
            UI::SetCursorScreenPos(V(p.x, p.y + 66.0f));
            UI::Dummy(0.0f, 0.0f);
        }
        UI::EndCard();
        UI::Spacing(4.0f);

        // ---- chips, sort and search ----
        {
            const UI::DrawList& dl = UI::GetWindowDrawList();
            const UI::Vec2 cp = UI::GetCursorScreenPos();
            const float ch = UI::GetTextLineHeight() + 12.0f, right = cp.x + UI::GetContentRegionAvail().x;
            float cx = cp.x;
            const char* labels[3] = {"All", "This month", "Showcased"};
            const int counts[3] = {(int)m.items.size(), res.monthCount, m.showcaseCount};
            for (int k = 0; k < 3; ++k) {
                char cnt[12]; std::snprintf(cnt, sizeof(cnt), "%d", counts[k]);
                const UI::Vec2 ts = UI::CalcTextSize(labels[k]), cs = UI::CalcTextSize(cnt);
                const float cw = ts.x + cs.x + 40.0f;
                UI::SetCursorScreenPos(V(cx, cp.y));
                UI::PushID(k);
                if (UI::InvisibleButton("##achchip", cw, ch)) st.view = k;
                res.chips.push_back(UI::GetItemRect());
                const bool hov = UI::IsItemHovered();
                UI::PopID();
                const bool on = st.view == k;
                dl.AddRectFilled(V(cx, cp.y), V(cx + cw, cp.y + ch), on ? AccentAlpha(0.24f) : Rgb(255, 255, 255, hov ? 22 : 9), ch * 0.5f);
                dl.AddText(V(cx + 13.0f, cp.y + (ch - ts.y) * 0.5f), on ? accLt : (hov ? cText : cDim), labels[k]);
                const float bx = cx + 13.0f + ts.x + 7.0f;
                dl.AddRectFilled(V(bx, cp.y + (ch - 16.0f) * 0.5f), V(bx + cs.x + 10.0f, cp.y + (ch + 16.0f) * 0.5f), Rgb(255, 255, 255, 28), 8.0f);
                dl.AddText(V(bx + 5.0f, cp.y + (ch - cs.y) * 0.5f), Rgb(255, 255, 255), cnt);
                if (hov) UI::SetMouseCursor(UI::MouseCursor_Hand);
                cx += cw + 8.0f;
            }
            const float sortW = 132.0f;
            static const char* kSorts[3] = {"Newest first", "Oldest first", "A to Z"};
            UI::SetCursorScreenPos(V(right - sortW, cp.y));
            UI::SetNextItemWidth(sortW);
            UI::Combo("##achsort", kSorts, 3, &st.sort);
            res.sortCombo = UI::GetItemRect();
            const float sw = std::min(280.0f, right - sortW - 10.0f - (cx + 10.0f));
            if (sw > 120.0f) {
                UI::SetCursorScreenPos(V(right - sortW - 10.0f - sw, cp.y));
                UI::SetNextItemWidth(sw);
                UI::InputTextWithHint("##achsearch", "Search achievements", st.filter, (int)sizeof(st.filter));
                res.searchField = UI::GetItemRect();
            }
            UI::SetCursorScreenPos(V(cp.x, cp.y + std::max(ch, res.sortCombo.h) + 12.0f));
            UI::Dummy(0.0f, 0.0f);
        }

        // ---- the grid of medal tiles ----
        res.shown = AchList(m, st.view, st.sort, st.filter);
        UI::BeginChild("##achlist", 0.0f, UI::GetContentRegionAvail().y, false);
        const UI::DrawList& dl = UI::GetWindowDrawList();
        if (res.shown.empty()) {
            res.empty = true;
            const float aw = UI::GetContentRegionAvail().x;
            UI::Dummy(0.0f, 30.0f);
            const UI::Vec2 c = V(UI::GetCursorScreenPos().x + aw * 0.5f, UI::GetCursorScreenPos().y + 30.0f);
            dl.AddCircleFilled(c, 30.0f, Rgb(255, 255, 255, 12), 40);
            DrawTrophy(dl, c, 1.3f, cDim);
            UI::Dummy(0.0f, 70.0f);
            if (m.items.empty()) {
                res.emptyTitle = "No achievements yet";
                Centered(res.emptyTitle.c_str(), aw, false);
                Centered("Games award them with the \"Unlock Achievement\" action. Go play something!", aw, true);
                UI::Spacing(6.0f);
                UI::SetCursorPosX(UI::GetCursorPosX() + std::max(0.0f, (aw - 150.0f) * 0.5f));
                if (UI::PrimaryButton("Browse games", UI::Vec2{150.0f, 34.0f})) res.browseGames = true;
                res.browseButton = UI::GetItemRect();
            } else if (st.view == 2) {
                res.emptyTitle = "Nothing showcased yet";
                Centered(res.emptyTitle.c_str(), aw, false);
                Centered("Pick up to 6 achievements to show off on your profile (right-click a tile).", aw, true);
            } else if (st.view == 1) {
                res.emptyTitle = "No new achievements this month";
                Centered(res.emptyTitle.c_str(), aw, false);
                Centered("Your latest unlocks will show up here.", aw, true);
            } else {
                res.emptyTitle = "No achievements match your search";
                Centered(res.emptyTitle.c_str(), aw, false);
            }
        } else {
            const float gap = 12.0f, tileH = 78.0f;
            const float aw = UI::GetContentRegionAvail().x;
            res.columns = std::max(1, std::min(4, (int)((aw + gap) / (260.0f + gap))));
            const float tileW = (aw - gap * (res.columns - 1)) / res.columns;
            const UI::Vec2 o = UI::GetCursorScreenPos();
            const unsigned char raised[3] = {38, 42, 51};
            for (std::size_t i = 0; i < res.shown.size(); ++i) {
                const AchEntry& a = m.items[(std::size_t)res.shown[i]];
                const int r = (int)i / res.columns, c = (int)i % res.columns;
                const UI::Vec2 p = V(o.x + c * (tileW + gap), o.y + r * (tileH + gap));
                UI::SetCursorScreenPos(p);
                UI::PushID(a.id.c_str());
                UI::InvisibleButton("##achtile", tileW, tileH);
                res.tiles.push_back(UI::GetItemRect());
                const bool hov = UI::IsItemHovered();
                const bool visible = UI::IsItemVisible();
                if (UI::BeginPopupContextItem("##achctx")) {
                    if (a.showcased) {
                        if (UI::Selectable("Remove from showcase", false)) { res.removeShowcase = a.id; UI::CloseCurrentPopup(); }
                        res.menuShowcase = UI::GetItemRect();
                    } else {
                        const bool room = m.showcaseCount < m.showcaseMax;   // six at most, as the profile shows
                        UI::BeginDisabled(!room);
                        if (UI::Selectable("Add to showcase", false) && room) { res.addShowcase = a.id; UI::CloseCurrentPopup(); }
                        res.menuShowcase = UI::GetItemRect();
                        UI::EndDisabled();
                    }
                    if (UI::Selectable("Copy id", false)) { res.copyId = a.id; UI::CloseCurrentPopup(); }
                    res.menuCopyId = UI::GetItemRect();
                    UI::EndPopup();
                }
                UI::PopID();
                if (!visible) continue;   // scrolled out of the list: laid out (for the scroll range) but not drawn
                const bool fresh = a.when > m.now - 2L * 86400;
                const int lift = hov ? 9 : 0;   // a hovered tile is a shade lighter
                dl.AddRectFilled(p, V(p.x + tileW, p.y + tileH), Rgb(raised[0] + lift, raised[1] + lift, raised[2] + lift), 12.0f);
                if (a.showcased) dl.AddRect(p, V(p.x + tileW, p.y + tileH), Rgb(255, 204, 77, 115), 12.0f, 1.5f);
                DrawMedal(dl, V(p.x + 38.0f, p.y + tileH * 0.5f), 24.0f, a.color, 1.0f);
                const float tx = p.x + 74.0f, maxW = tileW - 74.0f - 14.0f - (a.showcased ? 16.0f : 0.0f);
                dl.AddText(V(tx, p.y + 18.0f), cText, FitTextWidth(a.label.empty() ? AchPrettyId(a.id) : a.label, maxW).c_str());
                char ptxt[16]; std::snprintf(ptxt, sizeof(ptxt), "+%d", a.points);
                const UI::Vec2 pts = UI::CalcTextSize(ptxt);
                dl.AddRectFilled(V(tx, p.y + 40.0f), V(tx + pts.x + 12.0f, p.y + 40.0f + pts.y + 2.0f), Rgb(250, 200, 70, a.points >= 50 ? 60 : 32), 8.0f);
                dl.AddText(V(tx + 6.0f, p.y + 41.0f), gold, ptxt);
                dl.AddText(V(tx + pts.x + 20.0f, p.y + 40.0f), cDim, FitTextWidth(a.date.empty() ? std::string("Unlocked") : a.date, maxW - pts.x - 20.0f).c_str());
                if (a.showcased) DrawStar(dl, V(p.x + tileW - 18.0f, p.y + 18.0f), 6.0f, gold);
                if (fresh) {   // "NEW" on the last two days' unlocks
                    UI::PushTextScale(0.72f);
                    const UI::Vec2 ns = UI::CalcTextSize("NEW");
                    const UI::Vec2 np = V(p.x + tileW - ns.x - 22.0f - (a.showcased ? 18.0f : 0.0f), p.y + 11.0f);
                    dl.AddRectFilled(V(np.x - 6.0f, np.y - 2.0f), V(np.x + ns.x + 6.0f, np.y + ns.y + 2.0f), Col(th.accent), 8.0f);
                    dl.AddText(np, Rgb(255, 255, 255), "NEW");
                    UI::PopTextScale();
                }
                if (hov) {
                    UI::SetMouseCursor(UI::MouseCursor_Hand);
                    UI::SetTooltip("%s\n%s  \xC2\xB7  %s\nRight-click to %s your showcase", a.label.c_str(), a.id.c_str(), a.rel.c_str(),
                                   a.showcased ? "remove it from" : "add it to");
                }
            }
            const int rows = ((int)res.shown.size() + res.columns - 1) / res.columns;
            UI::SetCursorScreenPos(V(o.x, o.y + rows * (tileH + gap)));
            UI::Dummy(0.0f, 0.0f);
        }
        UI::EndChild();
    }
    UI::End();
    return res;
}

} // namespace okayuiscreens
