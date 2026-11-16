// The launcher's Friends, Community and Versions pages drawn with OkayUI (see OkayUILauncherMorePages.hpp). Written from what the
// launcher's Dear ImGui versions show and do (launcher/main.cpp, launcher/VersionsUI.cpp): every size, gap and colour below is the
// one that code uses, so the two look the same; the drawing is OkayUI's own widgets and draw list. No code from any other project.
#include "OkayUILauncherMorePages.hpp"
#include "OkayUILauncherLook.hpp"
#include "OkayUILauncherPages.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <deque>
#include <functional>

namespace okayuiscreens {

using namespace look;

// ---- wrapped text ----------------------------------------------------------------------------------------------------------

std::vector<std::string> WrapLines(const std::string& text, float maxW) {
    std::vector<std::string> out;
    std::size_t pos = 0;
    while (pos <= text.size()) {
        const std::size_t nl = text.find('\n', pos);
        const std::string para = text.substr(pos, nl == std::string::npos ? std::string::npos : nl - pos);
        // Greedy: words go on the line while the line up to the end of the word fits; spaces at a break are dropped.
        std::string line;
        std::size_t i = 0;
        while (i < para.size()) {
            std::size_t j = i;
            while (j < para.size() && para[j] == ' ') ++j;
            std::size_t k = j;
            while (k < para.size() && para[k] != ' ') ++k;
            const std::string spaces = para.substr(i, j - i), word = para.substr(j, k - j);
            const std::string trial = line + spaces + word;
            if (line.empty() || UI::CalcTextSize(trial.c_str()).x <= maxW) {
                if (line.empty() && UI::CalcTextSize(word.c_str()).x > maxW && maxW > 0.0f) {
                    // a word longer than the line: as many characters as fit, the rest on the next lines
                    std::string w = word;
                    while (!w.empty() && UI::CalcTextSize(w.c_str()).x > maxW) {
                        std::size_t cut = w.size();
                        while (cut > 1 && UI::CalcTextSize(w.substr(0, cut).c_str()).x > maxW) {
                            --cut;
                            while (cut > 1 && ((unsigned char)w[cut] & 0xC0) == 0x80) --cut;
                        }
                        out.push_back(w.substr(0, cut));
                        w = w.substr(cut);
                    }
                    line = w;
                } else line = line.empty() ? word : trial;
                i = k;
            } else {
                out.push_back(line);
                line.clear();
                i = j;   // the spaces at the break are dropped
            }
        }
        out.push_back(line);
        if (nl == std::string::npos) break;
        pos = nl + 1;
    }
    return out;
}

namespace {

// Text placed the way the launcher's text is placed: its position cut to whole pixels (Dear ImGui truncates; OkayUI's own
// text rounds to the nearest pixel).
void Txt(const UI::DrawList& dl, UI::Vec2 p, UI::Color32 col, const char* s) { dl.AddText(V(Tr(p.x), Tr(p.y)), col, s); }
void CenteredT(const UI::DrawList& dl, float x, float y, float width, const char* s, UI::Color32 col) {
    Txt(dl, V(x + std::max(0.0f, (width - UI::CalcTextSize(s).x) * 0.5f), y), col, s);
}

// A cache of wrapped texts: the pages wrap the same sentences frame after frame.
const std::vector<std::string>& WrapCached(const std::string& text, float maxW) {
    struct Entry { std::string text; float w, fs; std::vector<std::string> lines; };
    static std::deque<Entry> cache;   // a deque: an answer stays valid while later texts are added (until the cache is cleared)
    const float fs = UI::GetTextLineHeight();
    for (const Entry& e : cache) if (e.w == maxW && e.fs == fs && e.text == text) return e.lines;
    if (cache.size() > 128) cache.clear();
    cache.push_back(Entry{text, maxW, fs, WrapLines(text, maxW)});
    return cache.back().lines;
}
// Draw wrapped text from (x, y); returns the height (lines times the line height).
float DrawWrapped(const UI::DrawList& dl, float x, float y, float maxW, const std::string& text, UI::Color32 col) {
    const float lh = UI::GetTextLineHeight();
    const std::vector<std::string>& lines = WrapCached(text, maxW);
    for (std::size_t i = 0; i < lines.size(); ++i) if (!lines[i].empty()) Txt(dl, V(x, y + lh * (float)i), col, lines[i].c_str());
    return lh * (float)lines.size();
}

UI::Color32 CBase() { return Rgb(30, 33, 41); }   // the launcher's page background (Slate kBase)
UI::Color32 CGreen() { return Rgb(76, 200, 110); }
UI::Color32 CGrey() { return Rgb(120, 128, 140); }

// The launcher's avatar without a picture: a disc in the name's colour and the initial in white.
void Avatar(const UI::DrawList& dl, float x, float y, float size, const unsigned char rgb[3], const std::string& name) {
    const UI::Vec2 c = V(x + size * 0.5f, y + size * 0.5f);
    dl.AddCircleFilled(c, size * 0.5f, Rgb(rgb[0], rgb[1], rgb[2]), 32);
    const char init[2] = {name.empty() ? '?' : (char)std::toupper((unsigned char)name[0]), 0};
    const UI::Vec2 ts = UI::CalcTextSize(init);
    Txt(dl, V(c.x - ts.x * 0.5f, c.y - ts.y * 0.5f), Rgb(255, 255, 255), init);
}

// The launcher's five-pointed star with a concave outline (DrawStar in launcher/main.cpp): ten points, filled as five tip
// triangles around a pentagon.
void Star10(const UI::DrawList& dl, UI::Vec2 c, float r, UI::Color32 col) {
    UI::Vec2 p[10];
    for (int i = 0; i < 10; ++i) {
        const float a = -1.5707963f + (float)i * 0.6283185f, rr = (i % 2 == 0) ? r : r * 0.45f;
        p[i] = V(c.x + std::cos(a) * rr, c.y + std::sin(a) * rr);
    }
    const UI::Vec2 inner[5] = {p[1], p[3], p[5], p[7], p[9]};
    dl.AddConvexPolyFilled(inner, 5, col);
    for (int i = 0; i < 5; ++i) dl.AddTriangleFilled(p[(i * 2 + 9) % 10], p[i * 2], p[i * 2 + 1], col);
}

// The launcher's round icon buttons on friend rows (RoundIconButton): a disc, lighter under the mouse, and its glyph.
enum RoundIcon { kRibChat, kRibPlay, kRibCheck, kRibClose, kRibMore };
void RoundIcon(const UI::DrawList& dl, UI::Vec2 p, float d, int icon, bool hov, UI::Color32 bg, UI::Color32 bgHover, UI::Color32 fg) {
    const UI::Vec2 c = V(p.x + d * 0.5f, p.y + d * 0.5f);
    dl.AddCircleFilled(c, d * 0.5f, hov ? bgHover : bg, 28);
    switch (icon) {
    case kRibChat: DrawNavIcon(dl, kIcoChat, c, fg); break;
    case kRibPlay: dl.AddTriangleFilled(V(c.x - 3.5f, c.y - 5.5f), V(c.x - 3.5f, c.y + 5.5f), V(c.x + 5.5f, c.y), fg); break;
    case kRibCheck: {
        const UI::Vec2 pts[3] = {V(c.x - 5.0f, c.y + 0.2f), V(c.x - 1.5f, c.y + 4.0f), V(c.x + 5.5f, c.y - 4.5f)};
        dl.AddPolyline(pts, 3, fg, false, 2.2f);
        break;
    }
    case kRibClose:
        dl.AddLine(V(c.x - 4.5f, c.y - 4.5f), V(c.x + 4.5f, c.y + 4.5f), fg, 2.0f);
        dl.AddLine(V(c.x + 4.5f, c.y - 4.5f), V(c.x - 4.5f, c.y + 4.5f), fg, 2.0f);
        break;
    default:
        for (int k = -1; k <= 1; ++k) dl.AddCircleFilled(V(c.x + (float)k * 5.0f, c.y), 1.8f, fg, 8);
        break;
    }
}

bool In(const UI::Rect& r, UI::Vec2 m) { return m.x >= r.x && m.y >= r.y && m.x < r.x + r.w && m.y < r.y + r.h; }

// The launcher's small-caps section label ("ONLINE  -  3") and the hairline under it; returns where the next item goes.
float SectionLabel(const UI::DrawList& dl, float x, float y, float w, const char* title, int n) {
    y = NextY(y, 6.0f);
    char t[64];
    std::snprintf(t, sizeof(t), "%s  \xE2\x80\x94  %d", title, n);
    PushScale(0.80f);
    const float lh = UI::GetTextLineHeight();
    Txt(dl, V(x + 10.0f, y), Col(UI::Style().textDim), t);
    UI::PopTextScale();
    y = NextY(y, lh);
    dl.AddRectFilled(V(x + 10.0f, y + 2.0f), V(x + w - 10.0f, y + 3.0f), Rgb(255, 255, 255, 14));
    return NextY(y, 6.0f);
}

// The centred empty state with a big icon on a faint disc; returns where the next item goes.
float EmptyState(const UI::DrawList& dl, float x, float y, float w, int icon, const char* title, const char* sub) {
    y = NextY(y, 40.0f);
    const UI::Vec2 c = V(x + w * 0.5f, y + 30.0f);
    dl.AddCircleFilled(c, 30.0f, Rgb(255, 255, 255, 12), 40);
    DrawNavIcon(dl, icon, c, Col(UI::Style().textDim));
    y = NextY(y, 70.0f);
    const float fs = UI::GetTextLineHeight();
    CenteredT(dl, x, y, w, title, Col(UI::Style().text));
    y = NextY(y, fs);
    CenteredT(dl, x, y, w, sub, Col(UI::Style().textDim));
    return NextY(y, fs);
}

// A text field drawn the launcher's way: its frame padding and rounding, no border, the hint while it is empty.
bool Field(const char* id, const char* hint, char* buf, int cap, float x, float y, float w, UI::Vec2 pad, float rounding,
           unsigned flags, UI::Rect& rect) {
    UI::SetCursorScreenPos(V(x, y));
    UI::SetNextItemWidth(w);
    UI::PushStyleVar(UI::StyleVar_FrameRounding, rounding);
    UI::PushStyleVar(UI::StyleVar_FramePadding, pad);
    UI::PushStyleVar(UI::StyleVar_BorderSize, 0.0f);
    const bool r = UI::InputTextWithHint(id, hint, buf, cap, flags);
    rect = UI::GetItemRect();
    UI::PopStyleVar(3);
    return r;
}
// The search pill with its drawn magnifier.
void SearchPill(const UI::DrawList& dl, const char* id, const char* hint, char* buf, int cap, float x, float y, float w, UI::Rect& rect) {
    Field(id, hint, buf, cap, x, y, w, V(30.0f, kFrameY), 16.0f, 0, rect);
    const UI::Vec2 mc = V(x + 15.0f, y + rect.h * 0.5f - 1.0f);
    const UI::Color32 dim = Col(UI::Style().textDim);
    Ring(dl, mc, 4.6f, dim, 16, 1.5f);
    dl.AddLine(V(mc.x + 3.4f, mc.y + 3.4f), V(mc.x + 6.8f, mc.y + 6.8f), dim, 1.8f);
}

// A card the way the launcher's raised children are: its colour, 8 px corners (the style's child rounding), padded.
void Card(const UI::DrawList& dl, float x, float y, float w, float h, float rounding = 8.0f) {
    dl.AddRectFilled(V(x, y), V(x + w, y + h), CRaised(), rounding);
}

} // namespace

// ---- Friends ---------------------------------------------------------------------------------------------------------------

std::string FriendStatusLine(const FriendRow& f, int* colour) {
    const bool playing = f.online && !f.playing.empty();
    const bool accepted = f.kind == 0;
    std::string s = f.kind == 1 ? "Incoming friend request"
                  : f.kind == 2 ? "Request sent"
                  : playing ? f.playing
                  : !f.customStatus.empty() ? f.customStatus
                  : f.online ? "Online"
                  : !f.lastOnline.empty() ? f.lastOnline : "Offline";
    if (colour) *colour = (f.kind == 1 || (accepted && playing)) ? 1 : (accepted && f.online && f.customStatus.empty()) ? 2 : 0;
    return s;
}

std::vector<FriendSection> FriendSections(const FriendsModel& m, int view, const std::string& filter) {
    const std::string q = Lower(filter);
    std::vector<int> reqs, favs, online, offline, pending;
    for (int i = 0; i < (int)m.friends.size(); ++i) {
        const FriendRow& f = m.friends[(std::size_t)i];
        if (!q.empty() && Lower(f.name + " @" + f.username).find(q) == std::string::npos) continue;
        if (f.kind == 1) reqs.push_back(i);
        else if (f.kind == 2) pending.push_back(i);
        else if (f.favorite) favs.push_back(i);
        else if (f.online) online.push_back(i);
        else offline.push_back(i);
    }
    auto byName = [&](int a, int b) {
        const FriendRow& x = m.friends[(std::size_t)a];
        const FriendRow& y = m.friends[(std::size_t)b];
        if (x.online != y.online) return x.online > y.online;
        return Lower(x.name) < Lower(y.name);
    };
    std::sort(favs.begin(), favs.end(), byName);
    std::sort(online.begin(), online.end(), byName);
    std::sort(offline.begin(), offline.end(), [&](int a, int b) {   // most recently online first, then by name
        const long la = m.friends[(std::size_t)a].lastSeen, lb = m.friends[(std::size_t)b].lastSeen;
        return la != lb ? la > lb : byName(a, b);
    });
    std::vector<FriendSection> out;
    auto add = [&](const char* t, int kind, const std::vector<int>& rows) { if (!rows.empty()) out.push_back(FriendSection{t, kind, rows}); };
    if (view == 0) {
        std::vector<int> on;
        for (int i : favs) if (m.friends[(std::size_t)i].online) on.push_back(i);
        on.insert(on.end(), online.begin(), online.end());
        add("ONLINE", 0, on);
    } else if (view == 1) {
        add("FAVORITES", 0, favs); add("ONLINE", 0, online); add("OFFLINE", 0, offline);
    } else if (view == 2) {
        add("INCOMING", 1, reqs); add("SENT", 2, pending);
    }
    return out;
}

FriendsResult DrawFriends(const FriendsModel& m, FriendsState& st, float x, float y, float w, float h) {
    FriendsResult res;
    if (st.view < 0 || st.view > 5) st.view = 0;
    st.filter[sizeof(st.filter) - 1] = st.addBuf[sizeof(st.addBuf) - 1] = st.followBuf[sizeof(st.followBuf) - 1] = '\0';
    int acceptedN = 0, onlineN = 0, reqN = 0, sentN = 0;
    for (const FriendRow& f : m.friends) {
        if (f.kind == 0) { ++acceptedN; if (f.online) ++onlineN; }
        else if (f.kind == 1) ++reqN; else ++sentN;
    }

    const LauncherLook look;
    UI::BareNextWindow();
    if (UI::Begin("Friends##okayui", x, y, w, h)) {
        const UI::Theme& th = UI::Style();
        const UI::Color32 cText = Col(th.text), cDim = Col(th.textDim), accLt = AccentLight();
        const UI::DrawList& wd = UI::GetWindowDrawList();
        const float fs = UI::GetTextLineHeight();
        const UI::Vec2 mouse = UI::GetMousePos();
        float cy = PageHeader(wd, x, y, w, "Friends", nullptr);

        // ---- toolbar: the view chips (left) and my presence (right) ----
        {
            const float ch = fs + 12.0f;
            float cx = x;
            auto chip = [&](int view, const char* label, int count, bool alert) {
                const UI::Vec2 ts = UI::CalcTextSize(label);
                char cnt[12] = "";
                if (count > 0) std::snprintf(cnt, sizeof(cnt), "%d", count);
                const UI::Vec2 cs = UI::CalcTextSize(cnt);
                const float cw = ts.x + 26.0f + (cnt[0] ? cs.x + 14.0f : 0.0f);
                UI::SetCursorScreenPos(V(cx, cy));
                UI::PushID(view);
                if (UI::InvisibleButton("##frchip", cw, ch)) { if (st.view != view) res.viewChanged = true; st.view = view; }
                res.chips.push_back(UI::GetItemRect());
                const bool hov = UI::IsItemHovered();
                UI::PopID();
                const bool on = st.view == view, add = view == 3;
                const UI::Color32 bg = add ? (on ? Rgb(46, 140, 82) : hov ? Rgb(46, 140, 82) : Rgb(38, 120, 70))
                                           : on ? AccentAlpha(0.24f) : Rgb(255, 255, 255, hov ? 22 : 9);
                wd.AddRectFilled(V(cx, cy), V(cx + cw, cy + ch), bg, ch * 0.5f);
                Txt(wd, V(cx + 13.0f, cy + (ch - ts.y) * 0.5f), add ? Rgb(255, 255, 255) : on ? accLt : hov ? cText : cDim, label);
                if (cnt[0]) {
                    const float bx = cx + 13.0f + ts.x + 7.0f, bw = cs.x + 10.0f;
                    wd.AddRectFilled(V(bx, cy + (ch - 16.0f) * 0.5f), V(bx + bw, cy + (ch + 16.0f) * 0.5f), alert ? Rgb(222, 76, 76) : Rgb(255, 255, 255, 28), 8.0f);
                    Txt(wd, V(bx + 5.0f, cy + (ch - cs.y) * 0.5f), Rgb(255, 255, 255), cnt);
                }
                if (hov) UI::SetMouseCursor(UI::MouseCursor_Hand);
                cx += cw + 8.0f;
            };
            chip(0, "Online", onlineN, false);
            chip(1, "All", acceptedN, false);
            chip(2, "Pending", reqN + sentN, reqN > 0);
            chip(4, "Following", (int)m.following.size(), false);
            chip(5, "Followers", (int)m.followers.size(), false);
            chip(3, "Add friend", 0, false);
            // the presence picker: dot, label, chevron; opens a small menu
            static const char* kStat[3] = {"Online", "Away", "Invisible"};
            static const char* kStatDesc[3] = {"Friends see you're around", "Shown as away", "Appear offline to everyone"};
            const UI::Color32 kStatCol[3] = {Rgb(76, 200, 110), Rgb(236, 180, 64), Rgb(140, 146, 156)};
            const int si = std::max(0, std::min(2, m.presence));
            const float pw = UI::CalcTextSize(kStat[si]).x + 52.0f;
            const UI::Vec2 pp = V(x + w - pw, cy);
            UI::SetCursorScreenPos(pp);
            if (UI::InvisibleButton("##presence", pw, ch)) UI::OpenPopup("##presencepop");
            res.presenceButton = UI::GetItemRect();
            const bool ph = UI::IsItemHovered();
            wd.AddRectFilled(pp, V(pp.x + pw, pp.y + ch), Rgb(255, 255, 255, ph ? 22 : 9), ch * 0.5f);
            wd.AddCircleFilled(V(pp.x + 15.0f, pp.y + ch * 0.5f), 4.5f, kStatCol[si], 12);
            Txt(wd, V(pp.x + 26.0f, pp.y + (ch - fs) * 0.5f), cText, kStat[si]);
            const float vx = pp.x + pw - 15.0f, vy = pp.y + ch * 0.5f;
            wd.AddLine(V(vx - 3.5f, vy - 1.5f), V(vx, vy + 2.0f), cDim, 1.6f);
            wd.AddLine(V(vx + 3.5f, vy - 1.5f), V(vx, vy + 2.0f), cDim, 1.6f);
            if (ph) { UI::SetMouseCursor(UI::MouseCursor_Hand); UI::SetTooltip("%s", "Your status"); }
            if (UI::BeginPopup("##presencepop")) {
                for (int k = 0; k < 3; ++k) {
                    UI::PushID(k);
                    const UI::Vec2 rp = UI::GetCursorScreenPos();
                    if (UI::Selectable("##st", m.presence == k, UI::Selectable_None, V(230.0f, 38.0f))) { res.presence = k; UI::CloseCurrentPopup(); }
                    res.presenceItems.push_back(UI::GetItemRect());
                    const UI::DrawList& pd = UI::GetWindowDrawList();
                    pd.AddCircleFilled(V(rp.x + 10.0f, rp.y + 19.0f), 5.0f, kStatCol[k], 12);
                    Txt(pd, V(rp.x + 24.0f, rp.y + 2.0f), cText, kStat[k]);
                    Txt(pd, V(rp.x + 24.0f, rp.y + 19.0f), cDim, kStatDesc[k]);
                    UI::PopID();
                }
                UI::EndPopup();
            }
            cy = NextY(cy + ch + 14.0f, 0.0f);
        }

        // ---- the body: the list (left) and the side panel (right, on a wide page) ----
        const bool side = w >= 760.0f;
        const float listW = side ? w - 280.0f - 18.0f : w;
        const float bodyH = std::max(8.0f, y + h - cy);

        // The friend-code card and the activity card (the side panel, or under the list on a narrow page); return their bottom.
        auto codeCard = [&](const UI::DrawList& dl, float cx0, float cy0, float cw) {
            const float px = cx0 + 16.0f, iw = cw - 32.0f;
            float yy = cy0 + 14.0f;
            static const std::string kHint = "Share it so friends can add you.";
            const std::size_t hintLines = WrapCached(kHint, iw).size();
            PushScale(1.35f);
            const float bigH = UI::GetTextLineHeight();
            UI::PopTextScale();
            const float cardH = 14.0f + (fs + kSpY) + (bigH + kSpY) + (fs + 2.0f * kFrameY + kSpY) + fs * (float)hintLines + 14.0f;
            Card(dl, cx0, cy0, cw, cardH);
            Txt(dl, V(px, yy), cDim, "YOUR FRIEND CODE");
            yy = NextY(yy, fs);
            PushScale(1.35f);
            Txt(dl, V(px, yy), accLt, m.code.empty() ? "\xE2\x80\xA6" : m.code.c_str());
            UI::PopTextScale();
            yy = NextY(yy, bigH);
            if (FlatButton("##frcopy", "Copy code", px, yy, iw - 1.0f, fs + 2.0f * kFrameY, !m.code.empty())) res.copyCode = true;   // width -1
            res.copyCodeButton = UI::GetItemRect();
            yy = NextY(yy, fs + 2.0f * kFrameY);
            DrawWrapped(dl, px, yy, iw, "Share it so friends can add you.", cDim);
            return cy0 + cardH;
        };
        auto activityCard = [&](const UI::DrawList& dl, float cx0, float cy0, float cw) {
            const float px = cx0 + 16.0f, iw = cw - 32.0f;
            // measure first (the card is as tall as what it holds), then draw
            float yy = NextY(NextY(cy0 + 14.0f, fs), 2.0f);
            float bottom = yy;
            const int n = (int)std::min<std::size_t>(8, m.feed.size());
            if (m.feed.empty()) { bottom = yy + fs * (float)WrapCached("It's quiet\xE2\x80\xA6 nothing new from friends yet.", iw).size(); }
            for (int i = 0; i < n; ++i) {
                const FeedRow& e = m.feed[(std::size_t)i];
                const float gw = std::max(1.0f, px + iw - (px + 38.0f));
                const float th = fs * (float)WrapCached(e.text, gw).size();
                const float gh = e.ago.empty() ? th : th + kSpY + fs;
                const float dy = NextY(yy - 1.0f, gh);   // under the group
                bottom = dy + 4.0f;
                yy = NextY(dy, 4.0f);
            }
            const float cardH = bottom + 14.0f - cy0;
            Card(dl, cx0, cy0, cw, cardH);
            yy = cy0 + 14.0f;
            Txt(dl, V(px, yy), cDim, "ACTIVITY");
            yy = NextY(NextY(yy, fs), 2.0f);
            if (m.feed.empty()) DrawWrapped(dl, px, yy, iw, "It's quiet\xE2\x80\xA6 nothing new from friends yet.", cDim);
            for (int i = 0; i < n; ++i) {
                const FeedRow& e = m.feed[(std::size_t)i];
                Avatar(dl, px, yy, 28.0f, e.avatar, e.who);
                const float gx = px + 38.0f, gy = yy - 1.0f, gw = std::max(1.0f, px + iw - gx);
                const float th = DrawWrapped(dl, gx, gy, gw, e.text, cText);
                float gh = th;
                if (!e.ago.empty()) { Txt(dl, V(gx, NextY(gy, th)), cDim, e.ago.c_str()); gh = th + kSpY + fs; }
                yy = NextY(NextY(gy, gh), 4.0f);
            }
            return cy0 + cardH;
        };

        UI::SetCursorScreenPos(V(x, cy));
        BeginPlainChild("##friendlist", listW, bodyH);
        {
            const UI::DrawList& dl = UI::GetWindowDrawList();
            const UI::Vec2 o = UI::GetCursorScreenPos();
            const float lw = UI::GetContentRegionAvail().x;
            float ly = o.y, contentEnd = o.y;
            const bool listHovered = UI::IsWindowHovered();

            if (st.view == 4 || st.view == 5) {   // ---- Following / Followers ----
                const bool ing = st.view == 4;
                if (!m.followSupported) {
                    res.empty = true; res.emptyTitle = "Follows aren't switched on yet";
                    ly = EmptyState(dl, o.x, ly, lw, kIcoFriends, res.emptyTitle.c_str(), "The server needs the follows update (docs/sql/follows.sql).");
                    contentEnd = ly - kSpY;
                } else {
                    if (ing) {   // follow someone by username
                        const UI::Vec2 pad = V(12.0f, 8.0f);
                        const bool sub = Field("##followname", "Follow someone by username", st.followBuf, (int)sizeof(st.followBuf), o.x, ly,
                                               lw - 120.0f, pad, 8.0f, UI::InputText_EnterReturnsTrue, res.followField);
                        const float fh = res.followField.h;
                        const bool go = FlatButton("##follow", "Follow", o.x + lw - 120.0f + 8.0f, ly, 110.0f, fh, st.followBuf[0] && !m.followBusy, true, 8.0f);
                        res.followButton = UI::GetItemRect();
                        if ((go || sub) && st.followBuf[0]) {
                            std::string nm = st.followBuf;
                            if (!nm.empty() && nm[0] == '@') nm.erase(0, 1);
                            res.follow = nm;
                            st.followBuf[0] = '\0';
                        }
                        ly = NextY(ly, fh);
                        Txt(dl, V(o.x, ly), cDim, "Following is one-way: no request, and their activity shows in your feed.");
                        ly = NextY(ly, fs);
                    }
                    const std::vector<FollowRow>& list = ing ? m.following : m.followers;
                    if (list.empty()) {
                        res.empty = true;
                        res.emptyTitle = ing ? "You're not following anyone" : "No followers yet";
                        ly = EmptyState(dl, o.x, ly, lw, kIcoFriends, res.emptyTitle.c_str(),
                                        ing ? "Follow players to see what they're up to." : "When someone follows you, they show up here.");
                        contentEnd = ly - kSpY;
                    } else ly = SectionLabel(dl, o.x, ly, lw, ing ? "FOLLOWING" : "FOLLOWERS", (int)list.size());
                    for (std::size_t i = 0; i < list.size(); ++i) {
                        const FollowRow& e = list[i];
                        bool mutual = false;
                        if (!ing) for (const FollowRow& f : m.following) if (f.id == e.id) { mutual = true; break; }
                        UI::PushID(e.id.c_str());
                        const UI::Vec2 rp = V(o.x, ly);
                        const float rowH = 56.0f, A = 38.0f;
                        UI::SetCursorScreenPos(rp);
                        const bool click = UI::InvisibleButton("##flrow", lw, rowH);
                        res.followRows.push_back(UI::GetItemRect());
                        const bool hov = UI::IsItemHovered();
                        if (hov) dl.AddRectFilled(rp, V(rp.x + lw, rp.y + rowH), Rgb(255, 255, 255, 8), 10.0f);
                        Avatar(dl, rp.x + 10.0f, rp.y + (rowH - A) * 0.5f, A, e.avatar, e.display);
                        const UI::Vec2 pd = V(rp.x + 10.0f + A * 0.85f, rp.y + (rowH + A) * 0.5f - A * 0.15f);
                        dl.AddCircleFilled(pd, 6.5f, CBase(), 16);
                        dl.AddCircleFilled(pd, 4.5f, e.online ? CGreen() : CGrey(), 16);
                        const float tx = rp.x + 10.0f + A + 12.0f;
                        Txt(dl, V(tx, rp.y + 10.0f), cText, e.display.c_str());
                        std::string sub = "@" + e.username + "  \xC2\xB7  " + (e.online ? std::string("Online") : !e.seen.empty() ? e.seen : std::string("Offline"));
                        if (mutual) sub += "  \xC2\xB7  you follow each other";
                        Txt(dl, V(tx, rp.y + 30.0f), cDim, sub.c_str());
                        // the buttons, right to left
                        float bx = rp.x + lw - 10.0f;
                        UI::Rect first, second;
                        auto rowBtn = [&](const char* lbl, bool primary, UI::Rect& r) {
                            const float bw = UI::CalcTextSize(lbl).x + 26.0f;
                            bx -= bw;
                            r = UI::Rect{bx, rp.y + (rowH - 30.0f) * 0.5f, bw, 30.0f};
                            const bool over = hov && In(r, mouse), held = over && UI::IsItemActive(), en = !m.followBusy;
                            const float a = en ? 1.0f : 0.55f;
                            const unsigned char* acc = th.accent;
                            UI::Color32 bg;
                            if (primary) bg = held ? Rgb((int)(acc[0] * 0.73f), (int)(acc[1] * 0.73f), (int)(acc[2] * 0.73f), (int)(255 * a))
                                         : over && en ? Rgb((int)(acc[0] + (255 - acc[0]) * 0.2f), (int)(acc[1] + (255 - acc[1]) * 0.2f), (int)(acc[2] + (255 - acc[2]) * 0.2f), (int)(255 * a))
                                                      : Rgb(acc[0], acc[1], acc[2], (int)(235 * a));
                            else bg = held ? Rgb(acc[0], acc[1], acc[2], (int)(255 * a)) : over && en ? CControlHover(a) : CControl(a);
                            dl.AddRectFilled(V(r.x, r.y), V(r.x + r.w, r.y + r.h), bg, 6.0f);
                            const UI::Vec2 ts = UI::CalcTextSize(lbl);
                            Txt(dl, V(r.x + (r.w - ts.x) * 0.5f, Tr(r.y + (r.h - ts.y) * 0.5f)), primary ? Rgb(255, 255, 255, (int)(255 * a)) : Col(th.text, a), lbl);
                            bx -= 8.0f;
                            return click && over && en;
                        };
                        if (ing) { if (rowBtn("Unfollow", false, first)) res.unfollow = e.id; }
                        else {
                            if (rowBtn("Remove", false, first)) res.removeFollower = e.id;
                            if (hov && In(first, mouse)) UI::SetTooltip("%s", "Stop them following you");
                            if (!mutual && rowBtn("Follow back", true, second)) res.followBack = e.id;
                        }
                        res.followButtons.push_back(first);
                        res.followBackButtons.push_back(second);
                        if (click && !In(first, mouse) && !In(second, mouse)) res.openFollowProfile = (int)i;
                        if (hov && (In(first, mouse) || In(second, mouse))) UI::SetMouseCursor(UI::MouseCursor_Hand);
                        UI::PopID();
                        contentEnd = rp.y + rowH + 4.0f;
                        ly = NextY(rp.y + rowH + 4.0f, 0.0f);
                    }
                }
            } else if (st.view == 3) {   // ---- Add friend ----
                const float px = o.x + 20.0f, iw = lw - 40.0f;
                PushScale(1.15f);
                const float titleH = UI::GetTextLineHeight();
                UI::PopTextScale();
                const float fieldH = fs + 18.0f;
                float yy = o.y + 18.0f;
                float cardH = 18.0f + (titleH + kSpY) + (fs + kSpY) + (6.0f + kSpY) + fieldH;
                if (!m.addError.empty()) cardH += kSpY + fs;
                if (!m.addOk.empty()) cardH += kSpY + fs;
                cardH += 18.0f;
                Card(dl, o.x, o.y, lw, cardH);
                PushScale(1.15f);
                Txt(dl, V(px, yy), cText, "Add a friend");
                UI::PopTextScale();
                yy = NextY(yy, titleH);
                Txt(dl, V(px, yy), cDim, "Add people by their OkaySpace username or friend code.");
                yy = NextY(NextY(yy, fs), 6.0f);
                const bool submit = Field("##addfriend", "Username or friend code", st.addBuf, (int)sizeof(st.addBuf), px, yy, iw - 156.0f,
                                          V(12.0f, 9.0f), 8.0f, UI::InputText_EnterReturnsTrue, res.addField);
                const bool send = FlatButton("##send", "Send request", px + iw - 156.0f + 10.0f, yy, 146.0f, fieldH, st.addBuf[0] != '\0', true, 8.0f);
                res.sendButton = UI::GetItemRect();
                if ((send || submit) && st.addBuf[0]) res.addRequest = st.addBuf;
                yy = NextY(yy, fieldH);
                if (!m.addError.empty()) { Txt(dl, V(px, yy), Rgb(242, 128, 128), m.addError.c_str()); yy = NextY(yy, fs); }
                if (!m.addOk.empty()) { Txt(dl, V(px, yy), Rgb(115, 217, 141), m.addOk.c_str()); yy = NextY(yy, fs); }
                contentEnd = o.y + cardH;
                ly = NextY(o.y, cardH);
                if (!side) { ly = NextY(ly, 10.0f); contentEnd = codeCard(dl, o.x, ly, lw); ly = NextY(contentEnd, 0.0f); }
            } else {   // ---- Online / All / Pending ----
                SearchPill(dl, "##friendsearch", "Search friends", st.filter, (int)sizeof(st.filter), o.x, ly, lw - 1.0f, res.searchField);   // width -1: a pixel short of the edge
                ly = NextY(ly, res.searchField.h);
                contentEnd = ly - kSpY;
                const std::vector<FriendSection> secs = FriendSections(m, st.view, st.filter);
                const float rowH = 62.0f, A = 40.0f, d = 34.0f;
                for (const FriendSection& sec : secs) {
                    ly = SectionLabel(dl, o.x, ly, lw, sec.title, (int)sec.rows.size());
                    for (int idx : sec.rows) {
                        const FriendRow& f = m.friends[(std::size_t)idx];
                        const bool accepted = sec.kind == 0;
                        UI::PushID((int)f.rowId);
                        const UI::Vec2 rp = V(o.x, ly);
                        UI::SetCursorScreenPos(rp);
                        const bool click = UI::InvisibleButton("##frrow", lw, rowH);
                        const UI::Rect rowRect = UI::GetItemRect();
                        const bool rowHov = listHovered && UI::IsMouseHoveringRect(rp, V(rp.x + lw, rp.y + rowH));
                        const bool held = UI::IsItemActive();
                        // the round buttons, right to left
                        const int nBtns = sec.kind == 1 ? 2 : sec.kind == 2 ? 1 : (f.joinable ? 3 : 2);
                        UI::Rect b[3];
                        for (int k = 0; k < nBtns; ++k) b[k] = UI::Rect{rp.x + lw - 12.0f - d - (float)k * (d + 8.0f), rp.y + (rowH - d) * 0.5f, d, d};
                        int over = -1;
                        for (int k = 0; k < nBtns; ++k) if (rowHov && In(b[k], mouse)) over = k;
                        // the right-click menu (and the More button opens it)
                        bool openMenu = false;
                        UI::Rect msgR, joinR, moreR, accR, decR;
                        if (sec.kind == 1) { decR = b[0]; accR = b[1]; }
                        else if (sec.kind == 2) decR = b[0];
                        else { moreR = b[0]; if (f.joinable) { joinR = b[1]; msgR = b[2]; } else msgR = b[1]; }
                        if (click) {
                            if (over < 0) res.openProfile = idx;
                            else if (In(moreR, mouse)) openMenu = true;
                            else if (In(msgR, mouse)) res.message = idx;
                            else if (In(joinR, mouse)) res.join = idx;
                            else if (In(accR, mouse)) res.accept = idx;
                            else if (In(decR, mouse)) res.remove = idx;
                        }
                        if (openMenu) UI::OpenPopup("##frctx");
                        if (UI::BeginPopupContextItem("##frctx") || UI::BeginPopup("##frctx")) {
                            auto item = [&](const char* label) {
                                const bool c = UI::Selectable(label, false);
                                res.menuItems.push_back(UI::GetItemRect());
                                res.menuLabels.push_back(label);
                                if (c) UI::CloseCurrentPopup();
                                return c;
                            };
                            if (accepted) {
                                if (item("Message")) res.message = idx;
                                if (f.joinable && item("Join their game")) res.join = idx;
                                if (m.canInvite && item(m.inviteLabel.c_str())) res.invite = idx;
                                if (item(m.partyLabel.c_str())) res.party = idx;
                                if (item(f.favorite ? "Unfavorite" : "Favorite")) res.toggleFavorite = idx;
                                if (item("Set nickname...")) res.setNickname = idx;
                                if (f.hasNickname && item("Clear nickname")) res.clearNickname = idx;
                                UI::Separator();
                            }
                            if (item("Copy username")) res.copyUsername = idx;
                            if (item("View profile")) res.openProfile = idx;
                            UI::Separator();
                            UI::PushStyleColor(UI::Col_Text, (unsigned char)242, (unsigned char)115, (unsigned char)115);
                            if (item(sec.kind == 1 ? "Decline request" : accepted ? "Remove friend" : "Cancel request")) res.remove = idx;
                            UI::PopStyleColor();
                            UI::EndPopup();
                        }
                        res.shown.push_back(idx);
                        res.rows.push_back(rowRect);
                        res.messageButtons.push_back(msgR); res.joinButtons.push_back(joinR); res.moreButtons.push_back(moreR);
                        res.acceptButtons.push_back(accR); res.declineButtons.push_back(decR);
                        (void)held;
                        if (UI::IsItemVisible() || true) {
                            if (rowHov) dl.AddRectFilled(rp, V(rp.x + lw, rp.y + rowH), Rgb(255, 255, 255, 11), 10.0f);
                            const UI::Vec2 ap = V(rp.x + 10.0f, rp.y + (rowH - A) * 0.5f);
                            Avatar(dl, ap.x, ap.y, A, f.avatar, f.name);
                            if (accepted || sec.kind == 1) {
                                const UI::Vec2 pd = V(ap.x + A * 0.85f, ap.y + A * 0.85f);
                                dl.AddCircleFilled(pd, 7.0f, CBase(), 16);
                                dl.AddCircleFilled(pd, 5.0f, f.online ? CGreen() : CGrey(), 16);
                            }
                            if (f.favorite) Star10(dl, V(ap.x + 3.0f, ap.y + 3.0f), 6.0f, Rgb(250, 200, 70));
                            const float tx = rp.x + 10.0f + A + 12.0f, ty = rp.y + 12.0f;
                            const float right = rp.x + lw - 12.0f - (float)nBtns * (d + 8.0f);
                            const std::string nm = FitTextWidth(f.name, right - tx - 10.0f);
                            Txt(dl, V(tx, ty), cText, nm.c_str());
                            const float nx = tx + UI::CalcTextSize(nm.c_str()).x + 6.0f;
                            const std::string un = "@" + f.username;
                            if (nx + UI::CalcTextSize(un.c_str()).x < right - 6.0f) Txt(dl, V(nx, ty), cDim, un.c_str());
                            int sc = 0;
                            const std::string sub = FriendStatusLine(f, &sc);
                            Txt(dl, V(tx, ty + 20.0f), sc == 1 ? accLt : sc == 2 ? CGreen() : cDim, FitTextWidth(sub, right - tx - 10.0f).c_str());
                            const UI::Color32 bg = Rgb(255, 255, 255, 16), bgh = Rgb(255, 255, 255, 34);
                            static const char* kTips[5] = {"Message", "Join their game", "Accept", "Decline", "More"};
                            auto rib = [&](int k, int icon, UI::Color32 c0, UI::Color32 c1, UI::Color32 fg, const char* tip) {
                                RoundIcon(dl, V(b[k].x, b[k].y), d, icon, over == k, c0, c1, fg);
                                if (over == k) { UI::SetMouseCursor(UI::MouseCursor_Hand); UI::SetTooltip("%s", tip); }
                            };
                            if (sec.kind == 1) {
                                rib(0, kRibClose, bg, Rgb(200, 70, 70), cText, kTips[3]);
                                rib(1, kRibCheck, Rgb(38, 120, 70), Rgb(46, 150, 86), Rgb(255, 255, 255), kTips[2]);
                            } else if (sec.kind == 2) {
                                rib(0, kRibClose, bg, bgh, cText, "Cancel request");
                            } else {
                                rib(0, kRibMore, bg, bgh, cText, kTips[4]);
                                int k = 1;
                                if (f.joinable) rib(k++, kRibPlay, Rgb(38, 120, 70), Rgb(46, 150, 86), Rgb(255, 255, 255), kTips[1]);
                                rib(k, kRibChat, bg, bgh, cText, kTips[0]);
                            }
                        }
                        UI::PopID();
                        contentEnd = rp.y + rowH + 2.0f;
                        ly = NextY(rp.y + rowH + 2.0f, 0.0f);
                    }
                }
                if (secs.empty()) {
                    res.empty = true;
                    const char* sub = "";
                    int icon = kIcoFriends;
                    if (st.filter[0]) { res.emptyTitle = "No matches"; sub = "No friends match your search."; }
                    else if (m.friends.empty()) { res.emptyTitle = "No friends yet"; sub = "Use \"Add friend\" to send your first request."; }
                    else if (st.view == 0) { res.emptyTitle = "No one's online right now"; sub = "Friends who come online will show up here."; }
                    else if (st.view == 2) { res.emptyTitle = "No pending requests"; sub = "Requests you send or receive appear here."; icon = kIcoBell; }
                    else { res.emptyTitle = "No friends yet"; sub = "Use \"Add friend\" to send your first request."; }
                    ly = EmptyState(dl, o.x, ly, lw, icon, res.emptyTitle.c_str(), sub);
                    contentEnd = ly - kSpY;
                }
                if (st.view != 2 && reqN > 0 && !st.filter[0]) {   // the requests nudge
                    ly = NextY(ly, 8.0f);
                    char rq[64];
                    std::snprintf(rq, sizeof(rq), "%d friend request%s waiting  \xC2\xBB", reqN, reqN == 1 ? "" : "s");
                    const UI::Vec2 ts = UI::CalcTextSize(rq);
                    UI::SetCursorScreenPos(V(o.x, ly));
                    if (UI::InvisibleButton("##frnudge", ts.x, fs)) { st.view = 2; res.viewChanged = true; }
                    res.nudge = UI::GetItemRect();
                    if (UI::IsItemHovered()) dl.AddRectFilled(V(o.x - 4.0f, ly - 4.0f), V(o.x + ts.x + 4.0f, ly + fs + 4.0f), Rgb(67, 72, 86), 6.0f);
                    Txt(dl, V(o.x, ly), accLt, rq);
                    contentEnd = ly + fs;
                    ly = NextY(ly, fs);
                }
            }
            if (!side) contentEnd = activityCard(dl, o.x, NextY(ly, 12.0f), lw);
            EndPlainChild(contentEnd);
        }
        if (side) {
            const float sx = x + listW + 18.0f, sw = w - listW - 18.0f;
            UI::SetCursorScreenPos(V(sx, cy));
            BeginPlainChild("##frside", sw, bodyH);
            const UI::DrawList& dl = UI::GetWindowDrawList();
            const UI::Vec2 o = UI::GetCursorScreenPos();
            const float cw = UI::GetContentRegionAvail().x;
            float yy = codeCard(dl, o.x, o.y, cw);
            yy = NextY(NextY(yy, 0.0f), 10.0f);
            EndPlainChild(activityCard(dl, o.x, yy, cw));
        }
    }
    UI::End();
    return res;
}

// ---- Community -------------------------------------------------------------------------------------------------------------

std::vector<int> CommunityFilter(const CommunityModel& m, const std::string& filter) {
    const std::string q = Lower(filter);
    std::vector<int> out;
    for (int i = 0; i < (int)m.items.size(); ++i)
        if (q.empty() || Lower(m.items[(std::size_t)i].file + " " + m.items[(std::size_t)i].title).find(q) != std::string::npos) out.push_back(i);
    return out;
}

std::vector<int> CommunityTemplateFilter(const CommunityModel& m, const std::string& filter) {
    const std::string q = Lower(filter);
    std::vector<int> out;
    for (int i = 0; i < (int)m.templates.size(); ++i)
        if (q.empty() || Lower(m.templates[(std::size_t)i].name + " " + m.templates[(std::size_t)i].desc).find(q) != std::string::npos) out.push_back(i);
    return out;
}

namespace {

// The launcher's initial tile (IconTile): a rounded square in the muted name colour, its outline, the initial.
void IconTile(const UI::DrawList& dl, float x, float y, float size, const unsigned char rgb[3], const std::string& name) {
    const float rnd = size * 0.14f;
    dl.AddRectFilled(V(x, y), V(x + size, y + size), Rgb(rgb[0], rgb[1], rgb[2], 77), rnd);
    dl.AddRect(V(x, y), V(x + size, y + size), Rgb(rgb[0], rgb[1], rgb[2], 140), rnd, 1.0f);
    const char init[2] = {name.empty() ? '?' : (char)std::toupper((unsigned char)name[0]), 0};
    const UI::Vec2 ts = UI::CalcTextSize(init);
    Txt(dl, V(x + (size - ts.x) * 0.5f, y + (size - ts.y) * 0.5f), Rgb(224, 230, 240), init);
}

// A dashed outline along a rounded rectangle (the launcher's drop zone): the corners are quarter circles of as many segments
// as the launcher's renderer gives a circle of that radius (its 0.3 px error target), walked clockwise from the top of the
// left edge, dashes and gaps alternating along the way.
void DashedRoundRect(const UI::DrawList& dl, UI::Vec2 a, UI::Vec2 b, float r, UI::Color32 col, float th, float dash, float gap) {
    r = std::min(r, std::min(b.x - a.x, b.y - a.y) * 0.5f);
    int seg = (int)std::ceil(kPi / std::acos(1.0f - std::min(0.3f, r) / std::max(r, 0.01f)));
    seg = std::max(4, std::min(512, seg + (seg & 1)));
    const int step = std::max(1, std::min(12, 48 / seg)), perQuarter = std::max(1, 12 / step);
    std::vector<UI::Vec2> pts;
    const UI::Vec2 c[4] = {V(a.x + r, a.y + r), V(b.x - r, a.y + r), V(b.x - r, b.y - r), V(a.x + r, b.y - r)};
    for (int q = 0; q < 4; ++q) {
        const float a0 = kPi * (1.0f + 0.5f * (float)q);
        for (int i = 0; i <= perQuarter; ++i) {
            const float t = a0 + (kPi * 0.5f) * (float)i / (float)perQuarter;
            pts.push_back(V(c[q].x + std::cos(t) * r, c[q].y + std::sin(t) * r));
        }
    }
    pts.push_back(pts[0]);
    bool on = true;
    float left = dash;
    for (std::size_t i = 0; i + 1 < pts.size(); ++i) {
        const UI::Vec2 p = pts[i], q = pts[i + 1];
        const float dx = q.x - p.x, dy = q.y - p.y, len = std::sqrt(dx * dx + dy * dy);
        float t = 0.0f;
        while (len > 0.0f && t < len) {
            const float stepLen = std::min(left, len - t);
            if (on) dl.AddLine(V(p.x + dx * t / len, p.y + dy * t / len), V(p.x + dx * (t + stepLen) / len, p.y + dy * (t + stepLen) / len), col, th);
            t += stepLen; left -= stepLen;
            if (left <= 0.001f) { on = !on; left = on ? dash : gap; }
        }
    }
}

// The small-caps label of a Community section ("YOUR LIBRARY  -  3", or just the title); returns its height.
float CommLabel(const UI::DrawList& dl, float x, float y, const char* title, int n) {
    char t[64];
    if (n >= 0) std::snprintf(t, sizeof(t), "%s  \xE2\x80\x94  %d", title, n); else std::snprintf(t, sizeof(t), "%s", title);
    PushScale(0.80f);
    const float lh = UI::GetTextLineHeight();
    Txt(dl, V(x, y), Col(UI::Style().textDim), t);
    UI::PopTextScale();
    return lh;
}

// The folder glyph on a library card's reveal button.
void FolderGlyph(const UI::DrawList& d, UI::Vec2 c, UI::Color32 col) {
    d.PathClear();
    d.PathLineTo(V(c.x - 7.5f, c.y - 5.5f)); d.PathLineTo(V(c.x - 2.5f, c.y - 5.5f));
    d.PathLineTo(V(c.x - 0.5f, c.y - 3.5f)); d.PathLineTo(V(c.x + 7.5f, c.y - 3.5f));
    d.PathLineTo(V(c.x + 7.5f, c.y + 5.5f)); d.PathLineTo(V(c.x - 7.5f, c.y + 5.5f));
    d.PathStroke(col, true, 1.6f);
}

} // namespace

CommunityResult DrawCommunity(const CommunityModel& m, CommunityState& st, float x, float y, float w, float h) {
    CommunityResult res;
    static char noLib[2] = "", noTpl[2] = "";
    char* libBuf = st.libFilter && st.libCap > 0 ? st.libFilter : noLib;
    char* tplBuf = st.tplFilter && st.tplCap > 0 ? st.tplFilter : noTpl;
    const int libCap = st.libFilter && st.libCap > 0 ? st.libCap : 1, tplCap = st.tplFilter && st.tplCap > 0 ? st.tplCap : 1;
    libBuf[libCap - 1] = tplBuf[tplCap - 1] = '\0';

    const LauncherLook look;
    UI::BareNextWindow();
    if (UI::Begin("Community##okayui", x, y, w, h)) {
        const UI::Theme& th = UI::Style();
        const UI::Color32 cText = Col(th.text), cDim = Col(th.textDim), accLt = AccentLight();
        const UI::DrawList& wd = UI::GetWindowDrawList();
        const float fs = UI::GetTextLineHeight();
        const UI::Vec2 mouse = UI::GetMousePos();
        const float cy = PageHeader(wd, x, y, w, "Community", nullptr);

        UI::SetCursorScreenPos(V(x, cy));
        BeginPlainChild("##commbody", w, std::max(8.0f, y + h - cy));
        const UI::DrawList& dl = UI::GetWindowDrawList();
        const UI::Vec2 o = UI::GetCursorScreenPos();
        const float W = UI::GetContentRegionAvail().x;
        const bool bodyHovered = UI::IsWindowHovered();
        float ly = o.y;

        // ---- the hero card: what Community is, and its actions ----
        {
            const float px = o.x + 22.0f, hw = W - 44.0f;
            const bool art = hw >= 560.0f;
            const float textW = art ? hw - 190.0f : hw;
            const float hy = ly + 20.0f;
            PushScale(1.22f);
            const float titleH = UI::GetTextLineHeight();
            UI::PopTextScale();
            const std::string desc = "Games, levels, scripts and models made by other OkaySpace creators. Everything you add "
                                     "lives in your community folder and also shows up in Play.";
            const float descY = NextY(NextY(hy, titleH), 2.0f);
            const float descH = fs * (float)WrapCached(desc, textW).size();
            float by = NextY(NextY(descY, descH), 8.0f);
            // the buttons wrap onto a new line when the card is narrow
            const float bh = 36.0f;
            const float bw[3] = {206.0f, 206.0f, 112.0f};
            float bx[3], byy[3], rowX = 0.0f, rowY = by;
            for (int i = 0; i < 3; ++i) {
                if (i > 0 && rowX + 8.0f + bw[i] <= hw) { bx[i] = px + rowX + 8.0f; rowX += 8.0f + bw[i]; }
                else { if (i > 0) rowY = NextY(rowY, bh); bx[i] = px; rowX = bw[i]; }
                byy[i] = rowY;
            }
            const float cardH = rowY + bh + 20.0f - ly;
            dl.AddRectFilled(V(o.x, ly), V(o.x + W, ly + cardH), CRaised(), 12.0f);
            if (art) {   // a stack of three tiles (play, grid, bag) on the right
                struct Tile { float dx, dy; int icon; float a; };
                const Tile tiles[3] = {{-150.0f, 18.0f, kIcoPlay, 0.16f}, {-104.0f, 2.0f, kIcoGrid, 0.26f}, {-58.0f, 22.0f, kIcoBag, 0.18f}};
                const float S = 58.0f;
                for (const Tile& t : tiles) {
                    const UI::Vec2 a = V(px + hw + t.dx, hy + t.dy);
                    dl.AddRectFilled(a, V(a.x + S, a.y + S), CRaised(), 14.0f);
                    dl.AddRectFilled(a, V(a.x + S, a.y + S), AccentAlpha(t.a), 14.0f);
                    dl.AddRect(a, V(a.x + S, a.y + S), Rgb(255, 255, 255, 26), 14.0f, 1.0f);
                    DrawNavIcon(dl, t.icon, V(a.x + S * 0.5f, a.y + S * 0.5f), accLt);
                }
            }
            PushScale(1.22f);
            Txt(dl, V(px, hy), cText, "Play and share community creations");
            UI::PopTextScale();
            DrawWrapped(dl, px, descY, textW, desc, cDim);
            if (FlatButton("##commpub", "Publish to Marketplace", bx[0], byy[0], bw[0], bh, true, true)) res.publish = true;
            res.publishButton = UI::GetItemRect();
            if (FlatButton("##commfolder", "Open community folder", bx[1], byy[1], bw[1], bh)) res.openFolder = true;
            res.folderButton = UI::GetItemRect();
            if (FlatButton("##commrefresh", "      Refresh", bx[2], byy[2], bw[2], bh)) res.refresh = true;
            res.refreshButton = UI::GetItemRect();
            DrawNavIcon(dl, kIcoRefresh, V(bx[2] + 22.0f, byy[2] + bh * 0.5f), cText);
            if (UI::IsItemHovered()) UI::SetTooltip("%s", "Rescan the community folder");
            ly = NextY(NextY(ly, cardH), 10.0f);
        }

        // ---- the drop zone: a dashed card; a click opens the community folder ----
        {
            const float zh = 88.0f;
            const UI::Vec2 p = V(o.x, ly);
            UI::SetCursorScreenPos(p);
            if (UI::InvisibleButton("##commdrop", W, zh)) res.openFolder = true;
            res.dropZone = UI::GetItemRect();
            const bool hov = UI::IsItemHovered();
            dl.AddRectFilled(p, V(p.x + W, p.y + zh), AccentAlpha(hov ? 0.10f : 0.05f), 12.0f);
            DashedRoundRect(dl, V(p.x + 0.5f, p.y + 0.5f), V(p.x + W - 0.5f, p.y + zh - 0.5f), 12.0f, AccentAlpha(hov ? 0.85f : 0.50f), 1.5f, 7.0f, 5.0f);
            const UI::Vec2 ic = V(p.x + 46.0f, p.y + zh * 0.5f);
            dl.AddCircleFilled(ic, 22.0f, AccentAlpha(0.18f), 32);
            DrawNavIcon(dl, kIcoDownload, ic, accLt);
            const float tx = p.x + 84.0f, tw = W - 84.0f - 18.0f;
            const std::string d1 = "Drag a game folder, scene, script or model onto this window to add it to your library. "
                                   "A dropped .okayscene plays right away.";
            const float dsy = fs * (float)WrapCached(d1, tw).size();
            const float ty = p.y + (zh - (fs + 4.0f + dsy)) * 0.5f;
            Txt(dl, V(tx, ty), cText, "Drop files here to add them to your library");
            DrawWrapped(dl, Tr(tx), Tr(ty + fs + 4.0f), tw, d1, cDim);
            if (hov) { UI::SetMouseCursor(UI::MouseCursor_Hand); UI::SetTooltip("%s", "Open the community folder"); }
            ly = NextY(NextY(p.y, zh), 14.0f);
        }

        // A section header: its label (with the search pill on the right, or under it on a narrow page). Returns where the
        // next item goes.
        auto sectionHeader = [&](float yy, const char* title, int n, const char* id, const char* hint, char* buf, int cap, UI::Rect& rect, bool focus) {
            const bool pillBelow = W < 560.0f;
            const float pillW = pillBelow ? W : std::min(300.0f, W * 0.45f);
            const float labelY = yy + (pillBelow ? 0.0f : 9.0f);
            const float lh = CommLabel(dl, o.x, labelY, title, n);
            float pillY = pillBelow ? NextY(NextY(labelY, lh), 2.0f) : yy;
            if (focus) UI::SetKeyboardFocusHere();
            SearchPill(dl, id, hint, buf, cap, pillBelow ? o.x : o.x + W - pillW, pillY, pillW, rect);
            return NextY(NextY(pillY, rect.h), 6.0f);
        };

        // ---- your library: the header and a grid of cards (or the empty state) ----
        ly = sectionHeader(ly, "YOUR LIBRARY", (int)m.items.size(), "##commFilter", "Search your library  (Ctrl+F)", libBuf, libCap, res.librarySearch, st.focusLibrarySearch);
        st.focusLibrarySearch = false;
        res.shown = CommunityFilter(m, libBuf);
        if (res.shown.empty()) {
            res.libraryEmpty = true;
            ly = NextY(ly, 14.0f);
            const UI::Vec2 c = V(o.x + W * 0.5f, ly + 30.0f);
            dl.AddCircleFilled(c, 30.0f, Rgb(255, 255, 255, 12), 40);
            DrawNavIcon(dl, kIcoGrid, c, cDim);
            ly = NextY(ly, 70.0f);
            std::string sub;
            if (libBuf[0]) { res.emptyTitle = "No matches"; sub = std::string("Nothing in your library matches \"") + libBuf + "\"."; }
            else { res.emptyTitle = "Your library is empty"; sub = "Drop a shared game onto this window, or put it in the community folder and Refresh."; }
            CenteredT(dl, o.x, ly, W, res.emptyTitle.c_str(), cText);
            ly = NextY(ly, fs);
            CenteredT(dl, o.x, ly, W, sub.c_str(), cDim);
            ly = NextY(NextY(ly, fs), 18.0f);
        } else {
            const float gap = 12.0f, cardH = 128.0f;
            const int cols = std::max(1, (int)((W + gap) / (270.0f + gap)));
            const float cardW = std::floor((W - (float)(cols - 1) * gap) / (float)cols);
            const UI::Vec2 g0 = V(o.x, ly);
            int n = 0;
            for (int idx : res.shown) {
                const CommunityItem& it = m.items[(std::size_t)idx];
                UI::PushID(idx + 5000);
                const UI::Vec2 cp = V(g0.x + (float)(n % cols) * (cardW + gap), g0.y + (float)(n / cols) * (cardH + gap));
                ++n;
                const float d = 32.0f, by = cp.y + cardH - 14.0f - d;
                const UI::Rect playR{cp.x + 14.0f, by, cardW - 28.0f - 2.0f * (d + 8.0f), d};
                const UI::Rect revealR{cp.x + cardW - 14.0f - 2.0f * d - 8.0f, by, d, d};
                const UI::Rect moreR{cp.x + cardW - 14.0f - d, by, d, d};
                UI::SetCursorScreenPos(cp);
                const bool click = UI::InvisibleButton("##ccard", cardW, cardH);
                res.cards.push_back(UI::GetItemRect());
                const bool hovCard = UI::IsItemHovered();
                const bool hov = bodyHovered && UI::IsMouseHoveringRect(cp, V(cp.x + cardW, cp.y + cardH));
                const bool overBtn = In(playR, mouse) || In(revealR, mouse) || In(moreR, mouse);
                if (hovCard && !overBtn && m.havePlayer && UI::IsMouseDoubleClicked(0)) res.play = idx;   // a double click plays
                bool openMenu = false;
                if (click && In(moreR, mouse)) openMenu = true;
                if (click && In(revealR, mouse)) res.reveal = idx;
                if (click && In(playR, mouse) && m.havePlayer) res.play = idx;
                if (openMenu) UI::OpenPopup("##commctx");
                if (UI::BeginPopupContextItem("##commctx") || UI::BeginPopup("##commctx")) {
                    auto item = [&](const char* label) {
                        const bool c = UI::Selectable(label, false);
                        res.menuItems.push_back(UI::GetItemRect());
                        res.menuLabels.push_back(label);
                        if (c) UI::CloseCurrentPopup();
                        return c;
                    };
                    if (m.havePlayer && item("Play")) res.play = idx;
                    if (item("Show in Explorer")) res.reveal = idx;
                    if (item("Copy Path")) res.copyPath = idx;
                    UI::Separator();
                    UI::PushStyleColor(UI::Col_Text, (unsigned char)242, (unsigned char)115, (unsigned char)115);
                    if (item("Remove from library...")) res.remove = idx;
                    UI::PopStyleColor();
                    UI::EndPopup();
                }
                res.playButtons.push_back(playR); res.revealButtons.push_back(revealR); res.moreButtons.push_back(moreR);
                dl.AddRectFilled(cp, V(cp.x + cardW, cp.y + cardH), CRaised(), 10.0f);
                if (hov) {
                    dl.AddRectFilled(cp, V(cp.x + cardW, cp.y + cardH), Rgb(255, 255, 255, 8), 10.0f);
                    dl.AddRect(cp, V(cp.x + cardW, cp.y + cardH), Rgb(255, 255, 255, 30), 10.0f, 1.0f);
                }
                const float A = 52.0f;
                IconTile(dl, cp.x + 14.0f, cp.y + 14.0f, A, it.tile, it.title);
                const float tx = cp.x + 14.0f + A + 12.0f, tr = cp.x + cardW - 14.0f;
                Txt(dl, V(tx, cp.y + 16.0f), cText, FitTextWidth(it.title, tr - tx).c_str());
                Txt(dl, V(tx, cp.y + 37.0f), accLt, FitTextWidth(it.meta, tr - tx).c_str());
                const std::string sub = it.file + (it.ago.empty() ? "" : "  \xC2\xB7  " + it.ago);
                Txt(dl, V(tx, cp.y + 56.0f), cDim, FitTextWidth(sub, tr - tx).c_str());
                // Play (the accent button), reveal and more
                {
                    const bool over = hov && In(playR, mouse), held = over && UI::IsItemActive() && m.havePlayer;
                    const float a = m.havePlayer ? 1.0f : 0.55f;
                    const unsigned char* acc = th.accent;
                    const UI::Color32 bg = held ? Rgb((int)(acc[0] * 0.73f), (int)(acc[1] * 0.73f), (int)(acc[2] * 0.73f), (int)(255 * a))
                                         : over && m.havePlayer ? Rgb((int)(acc[0] + (255 - acc[0]) * 0.2f), (int)(acc[1] + (255 - acc[1]) * 0.2f), (int)(acc[2] + (255 - acc[2]) * 0.2f))
                                                                : Rgb(acc[0], acc[1], acc[2], (int)(235 * a));
                    dl.AddRectFilled(V(playR.x, playR.y), V(playR.x + playR.w, playR.y + playR.h), bg, 6.0f);
                    const UI::Vec2 ts = UI::CalcTextSize("Play");
                    Txt(dl, V(playR.x + (playR.w - ts.x) * 0.5f, playR.y + (playR.h - ts.y) * 0.5f), Rgb(255, 255, 255, (int)(255 * a)), "Play");
                    if (over && m.havePlayer) UI::SetMouseCursor(UI::MouseCursor_Hand);
                    if (over && !m.havePlayer) UI::SetTooltip("%s", "Player runtime not found next to the launcher");
                }
                {
                    const bool over = hov && In(revealR, mouse);
                    const UI::Vec2 fc = V(revealR.x + d * 0.5f, revealR.y + d * 0.5f);
                    dl.AddCircleFilled(fc, d * 0.5f, Rgb(255, 255, 255, over ? 34 : 16), 28);
                    FolderGlyph(dl, fc, cText);
                    if (over) { UI::SetMouseCursor(UI::MouseCursor_Hand); UI::SetTooltip("%s", "Show in folder"); }
                }
                {
                    const bool over = hov && In(moreR, mouse);
                    RoundIcon(dl, V(moreR.x, moreR.y), d, kRibMore, over, Rgb(255, 255, 255, 16), Rgb(255, 255, 255, 34), cText);
                    if (over) { UI::SetMouseCursor(UI::MouseCursor_Hand); UI::SetTooltip("%s", "More"); }
                }
                UI::PopID();
            }
            const int rows = (n + cols - 1) / cols;
            ly = NextY(g0.y + (float)rows * (cardH + gap) - gap, 0.0f);
        }
        ly = NextY(ly, 12.0f);

        // ---- share your creation: three numbered steps and two buttons ----
        {
            static const char* kTitle[3] = {"Build it", "Share it", "Publish it"};
            static const char* kDesc[3] = {"In the editor, File > Build Game makes a self-contained game folder.",
                                           "Zip the folder and send it. Friends drop it onto their launcher to play.",
                                           "Or put it on the Marketplace so anyone can find and download it."};
            const float px = o.x + 20.0f, sw = W - 40.0f;
            const bool rowLayout = sw >= 620.0f;
            const float colW = rowLayout ? (sw - 2.0f * 18.0f) / 3.0f : sw;
            float yy = ly + 16.0f;
            PushScale(0.80f);
            const float lblH = UI::GetTextLineHeight();
            UI::PopTextScale();
            const float stepsY = NextY(NextY(yy, lblH), 4.0f);
            // each step is a group: the number disc, the title, the wrapped description; in a row they sit side by side
            float sx = px, sy = stepsY, rowBottom = stepsY;
            float stepX[3], stepY[3];
            for (int k = 0; k < 3; ++k) {
                const float tw = colW - 40.0f;
                const std::string desc = kDesc[k];
                float wmax = UI::CalcTextSize(kTitle[k]).x;
                std::size_t nLines = 0;
                {
                    const std::vector<std::string>& lines = WrapCached(desc, tw);
                    nLines = lines.size();
                    for (const std::string& l : lines) wmax = std::max(wmax, UI::CalcTextSize(l.c_str()).x);
                }
                const float gh = 2.0f + fs + kSpY + fs * (float)nLines;
                if (k > 0 && !rowLayout) sy = NextY(NextY(sy, 0.0f), 6.0f);
                stepX[k] = sx; stepY[k] = sy;
                if (rowLayout) { rowBottom = std::max(rowBottom, sy + gh); sx = sx + 40.0f + wmax + 18.0f; }
                else { sy = NextY(sy, gh) - kSpY; rowBottom = sy; }
                (void)stepY;
            }
            const float btnY = NextY(NextY(rowBottom, 0.0f), 8.0f);
            const bool sameRow = sw >= 196.0f + 8.0f + 200.0f;
            const float btn2Y = sameRow ? btnY : NextY(btnY, 32.0f);
            const float cardH = btn2Y + 32.0f + 16.0f - ly;
            dl.AddRectFilled(V(o.x, ly), V(o.x + W, ly + cardH), CRaised(), 12.0f);
            CommLabel(dl, px, yy, "SHARE YOUR CREATION", -1);
            for (int k = 0; k < 3; ++k) {
                const UI::Vec2 p = V(stepX[k], stepY[k]);
                const UI::Vec2 nc = V(p.x + 14.0f, p.y + 14.0f);
                dl.AddCircleFilled(nc, 14.0f, AccentAlpha(0.22f), 28);
                char num[4]; std::snprintf(num, sizeof(num), "%d", k + 1);
                const UI::Vec2 ns = UI::CalcTextSize(num);
                Txt(dl, V(nc.x - ns.x * 0.5f, nc.y - ns.y * 0.5f), accLt, num);
                Txt(dl, V(p.x + 40.0f, p.y + 2.0f), cText, kTitle[k]);
                DrawWrapped(dl, p.x + 40.0f, NextY(p.y + 2.0f, fs), colW - 40.0f, kDesc[k], cDim);
            }
            if (FlatButton("##commreveal", "Reveal my games folder", px, btnY, 196.0f, 32.0f)) res.revealGames = true;
            res.shareFolderButton = UI::GetItemRect();
            if (FlatButton("##commpub2", "Publish to Marketplace\xE2\x80\xA6", sameRow ? px + 196.0f + 8.0f : px, btn2Y, 200.0f, 32.0f)) res.publish = true;
            res.sharePublishButton = UI::GetItemRect();
            ly = NextY(NextY(ly, cardH), 16.0f);
        }

        // ---- starter templates: the header with its search, a grid of cards ----
        ly = sectionHeader(ly, "STARTER TEMPLATES", -1, "##marketFilter", "Search templates", tplBuf, tplCap, res.templateSearch, false);
        {
            res.templatesShown = CommunityTemplateFilter(m, tplBuf);
            const float gap = 10.0f, cardH = 72.0f;
            const int cols = std::max(1, std::min(4, (int)((W + gap) / (230.0f + gap))));
            const float cardW = std::floor((W - (float)(cols - 1) * gap) / (float)cols);
            const UI::Vec2 g0 = V(o.x, ly);
            int shown = 0;
            for (int idx : res.templatesShown) {
                const CommunityTemplate& t = m.templates[(std::size_t)idx];
                const UI::Vec2 cp = V(g0.x + (float)(shown % cols) * (cardW + gap), g0.y + (float)(shown / cols) * (cardH + gap));
                ++shown;
                UI::PushID(idx + 9000);
                UI::SetCursorScreenPos(cp);
                const bool clicked = UI::InvisibleButton("##tpl", cardW, cardH);
                res.templateCards.push_back(UI::GetItemRect());
                const bool over = UI::IsItemHovered(), hov = over && m.haveEditor;
                dl.AddRectFilled(cp, V(cp.x + cardW, cp.y + cardH), CRaised(), 10.0f);
                if (hov) {
                    dl.AddRectFilled(cp, V(cp.x + cardW, cp.y + cardH), Rgb(255, 255, 255, 10), 10.0f);
                    OutlineRect(dl, cp, V(cp.x + cardW, cp.y + cardH), Col(th.accent), 10.0f, 1.5f);
                    UI::SetMouseCursor(UI::MouseCursor_Hand);
                    UI::SetTooltip("Start a new project from the %s template (you confirm the name first)", t.name.c_str());
                } else if (over) UI::SetTooltip("%s", "Editor not found next to the launcher");
                IconTile(dl, cp.x + 14.0f, cp.y + (cardH - 40.0f) * 0.5f, 40.0f, t.tile, t.name);
                const float tx = cp.x + 14.0f + 40.0f + 12.0f, tr = cp.x + cardW - 12.0f;
                Txt(dl, V(tx, cp.y + cardH * 0.5f - fs - 1.0f), m.haveEditor ? cText : Rgb(255, 255, 255, 110), FitTextWidth(t.name, tr - tx).c_str());
                Txt(dl, V(tx, cp.y + cardH * 0.5f + 2.0f), cDim, FitTextWidth(t.desc, tr - tx).c_str());
                if (clicked && m.haveEditor) res.useTemplate = idx;   // a card only opens New Project; the person confirms name and place
                UI::PopID();
            }
            if (shown > 0) ly = NextY(g0.y + (float)((shown + cols - 1) / cols) * (cardH + gap) - gap, 0.0f);
            else {
                const std::string none = std::string("No templates match \"") + tplBuf + "\".";
                Txt(dl, V(o.x, ly), cDim, none.c_str());
                ly = NextY(ly, fs);
            }
            ly = NextY(ly, 6.0f);
            Txt(dl, V(o.x, ly), cDim, "A template opens New Project on that starting point. Nothing is made until you press Create.");
            ly = NextY(ly, fs);
        }
        EndPlainChild(ly + 6.0f);
    }
    UI::End();
    return res;
}

// ---- Versions --------------------------------------------------------------------------------------------------------------

namespace {

// The Versions module's small rounded label: text at 86% size, padded 7 x 2.5; returns its width.
float Pill(const UI::DrawList& dl, float x, float y, const char* text, UI::Color32 bg, UI::Color32 fg) {
    UI::PushTextScale(0.86f);   // an exact size (14.62 px), as the module draws its pills
    const UI::Vec2 ts = UI::CalcTextSize(text);
    const float w = ts.x + 14.0f, hgt = ts.y + 5.0f;
    dl.AddRectFilled(V(x, y), V(x + w, y + hgt), bg, hgt * 0.5f);
    Txt(dl, V(x + 7.0f, y + 2.5f), fg, text);
    UI::PopTextScale();
    return w;
}
UI::Color32 CRed() { return Rgb(240, 125, 120); }
UI::Color32 CGreenV() { return Rgb(110, 205, 140); }
UI::Color32 CAmber() { return Rgb(240, 200, 100); }

// A combo box the launcher's way (frame colour, arrow button, 6 px corners); returns true when the choice changed.
bool LauncherCombo(const char* id, const std::vector<std::string>& items, int* current, float x, float y, float w, UI::Rect& rect) {
    std::vector<const char*> ptrs;
    for (const std::string& s : items) ptrs.push_back(s.c_str());
    UI::SetCursorScreenPos(V(x, y));
    UI::SetNextItemWidth(w);
    UI::PushStyleVar(UI::StyleVar_FrameRounding, 6.0f);
    UI::PushStyleVar(UI::StyleVar_BorderSize, 0.0f);
    const bool changed = !ptrs.empty() && UI::Combo(id, ptrs.data(), (int)ptrs.size(), current);
    rect = UI::GetItemRect();
    UI::PopStyleVar(2);
    return changed;
}

// The Versions module's danger button: a translucent red, solid red under the mouse.
bool DangerButton(const char* id, const char* label, float x, float y, float w, float h, const UI::Vec2 mouse) {
    UI::SetCursorScreenPos(V(x, y));
    const bool clicked = UI::InvisibleButton(id, w, h);
    const bool hov = UI::IsItemHovered(), held = hov && UI::IsItemActive();
    (void)mouse;
    const unsigned char* acc = UI::Style().accent;
    const UI::DrawList& dl = UI::GetWindowDrawList();
    dl.AddRectFilled(V(x, y), V(x + w, y + h), held ? Rgb(acc[0], acc[1], acc[2]) : hov ? Rgb(191, 66, 66) : Rgb(140, 51, 51, 140), 6.0f);
    const UI::Vec2 ts = UI::CalcTextSize(label);
    Txt(dl, V(x + (w - ts.x) * 0.5f, y + (h - ts.y) * 0.5f), Col(UI::Style().text), label);
    if (hov) UI::SetMouseCursor(UI::MouseCursor_Hand);
    return clicked;
}

} // namespace

VersionsResult DrawVersions(const VersionsModel& m, float x, float y, float w, float viewY0, float viewY1) {
    VersionsResult res;
    res.installedRows.resize(m.installed.size()); res.makeDefaultButtons.resize(m.installed.size()); res.removeButtons.resize(m.installed.size());
    res.availableRows.resize(m.available.size()); res.installButtons.resize(m.available.size()); res.cancelButtons.resize(m.available.size());
    if (!(viewY1 > viewY0)) viewY1 = viewY0 + 1.0f;

    const LauncherLook look;
    UI::BareNextWindow();
    if (UI::Begin("Versions##okayui", x, viewY0, w, viewY1 - viewY0)) {
        const UI::Theme& th = UI::Style();
        const UI::Color32 cText = Col(th.text), cDim = Col(th.textDim);
        const UI::DrawList& dl = UI::GetWindowDrawList();
        const float fs = UI::GetTextLineHeight(), fh = fs + 2.0f * kFrameY;
        float cy = PageHeader(dl, x, y, w, "Versions", m.subtitle.c_str());

        // A card: its small-caps title, then the content `body` lays out from (x, y) in width w, returning where the next item
        // would go. The card is drawn behind (its height known once the body is laid out: it is drawn into a channel first).
        auto card = [&](const char* title, const std::function<float(float, float, float)>& body) {
            const float px = x + 18.0f, iw = w - 36.0f;
            float yy = cy + 14.0f;
            // background first, as tall as last frame's content (stable after the first frame)
            static std::vector<std::pair<std::string, float>> heights;
            float* hh = nullptr;
            for (auto& e : heights) if (e.first == title) hh = &e.second;
            if (!hh) { heights.push_back({title, 0.0f}); hh = &heights.back().second; }
            if (*hh > 0.0f) dl.AddRectFilled(V(x, cy), V(x + w, cy + *hh), CRaised(), 10.0f);
            if (title) {
                PushScale(0.80f);
                const float lh = UI::GetTextLineHeight();
                Txt(dl, V(px, yy), cDim, title);
                UI::PopTextScale();
                yy = NextY(NextY(yy, lh), 4.0f);
            }
            yy = body(px, yy, iw);              // where the next item would go
            const float bottom = yy + 2.0f;   // CardEnd's 2 px spacer where the next item would go
            *hh = bottom + 14.0f - cy;
            cy = NextY(NextY(cy, *hh), 10.0f);
        };

        // ---- a request banner (from a project that needs another version) ----
        if (m.banner) {
            const float px = x + 18.0f, iw = w - 36.0f;
            const std::vector<std::string>& lines = WrapCached(m.bannerText, iw - 90.0f);
            const float hgt = 12.0f + std::max(fs * (float)lines.size(), fs) + 12.0f;
            dl.AddRectFilled(V(x, cy), V(x + w, cy + hgt), m.bannerGreen ? Rgb(110, 204, 140, 36) : Rgb(240, 199, 99, 36), 10.0f);
            DrawWrapped(dl, px, cy + 12.0f, iw - 90.0f, m.bannerText, cText);
            const float bw = UI::CalcTextSize("Dismiss").x + 2.0f * kFrameX;
            if (FlatButton("##verdismiss", "Dismiss", px + iw - 76.0f, cy + 12.0f, bw, fs)) res.dismissBanner = true;
            res.dismissButton = UI::GetItemRect();
            cy = NextY(NextY(cy, hgt), 10.0f);
        }

        // ---- this version ----
        card("THIS VERSION", [&](float px, float yy, float iw) {
            const UI::Vec2 p0 = V(px, yy);
            PushScale(1.18f);
            Txt(dl, V(p0.x, p0.y + 2.0f), cText, m.runningTitle.c_str());
            float tx = p0.x + UI::CalcTextSize(m.runningTitle.c_str()).x + 12.0f;
            UI::PopTextScale();
            if (m.runningLatest) tx += Pill(dl, tx, p0.y + 5.0f, "Latest", AccentAlpha(0.32f), cText) + 6.0f;
            if (m.runningLts) tx += Pill(dl, tx, p0.y + 5.0f, "LTS", Rgb(70, 170, 120, 90), cText) + 6.0f;
            Txt(dl, V(p0.x, p0.y + 30.0f), cDim, FitTextWidth(m.runningSub, iw - 330.0f).c_str());
            if (FlatButton("##vercheck", m.checking ? "Checking\xE2\x80\xA6" : "Check again", p0.x + iw - 306.0f, p0.y + 8.0f, 140.0f, 30.0f, !m.checking)) res.checkAgain = true;
            res.checkButton = UI::GetItemRect();
            if (FlatButton("##verfolder", "Open install folder", p0.x + iw - 306.0f + 148.0f, p0.y + 8.0f, 158.0f, 30.0f)) res.openFolder = true;
            res.folderButton = UI::GetItemRect();
            yy = NextY(p0.y + 54.0f, 0.0f);
            if (m.statusKind == 1 || m.statusKind == 2) { Txt(dl, V(px, yy), m.statusKind == 2 ? CRed() : cDim, m.status.c_str()); yy = NextY(yy, fs); }
            else if (m.statusKind == 3) yy = NextY(yy, DrawWrapped(dl, px, yy, iw, m.status, CAmber()));
            return yy;
        });

        // ---- the update channel ----
        card("UPDATE CHANNEL", [&](float px, float yy, float iw) {
            Txt(dl, V(px, yy + kFrameY), cText, "Follow");
            const float fw = UI::CalcTextSize("Follow").x;
            int ch = m.channel;
            if (LauncherCombo("##updchannel", {"Latest", "Long-term support (LTS)"}, &ch, px + fw + 10.0f, yy, 260.0f, res.channelCombo)) res.channel = ch;
            yy = NextY(NextY(yy, fh), 6.0f);
            yy = NextY(yy, DrawWrapped(dl, px, yy, iw, m.channelText, cText));
            if (!m.channelDim.empty()) yy = NextY(yy, DrawWrapped(dl, px, yy, iw, m.channelDim, cDim));
            if (m.ltsKind >= 0) {
                yy = NextY(yy, 8.0f);
                if (m.ltsKind == 0 || m.ltsKind == 1) {
                    Txt(dl, V(px, yy + kFrameY), cText, m.ltsSentence.c_str());
                    const float sx = px + UI::CalcTextSize(m.ltsSentence.c_str()).x + 12.0f;
                    const float bw = UI::CalcTextSize(m.ltsButton.c_str()).x + 2.0f * kFrameX;
                    if (FlatButton("##verlts", m.ltsButton.c_str(), sx, yy, bw, fh, !m.ltsButtonDisabled, true)) res.ltsAction = true;
                    res.ltsButton = UI::GetItemRect();
                    yy = NextY(yy, fh);
                } else {
                    Txt(dl, V(px, yy + kFrameY), m.ltsKind == 2 ? CAmber() : cDim, m.ltsSentence.c_str());
                    yy = NextY(yy, fh);
                }
                if (m.ltsDefault >= 0) {
                    yy = NextY(yy, 4.0f);
                    if (m.ltsDefault == 0) {
                        Txt(dl, V(px, yy + kFrameY), CGreenV(), m.ltsDefaultText.c_str());
                        const float sx = px + UI::CalcTextSize(m.ltsDefaultText.c_str()).x + 12.0f;
                        const float bw = UI::CalcTextSize(m.ltsDefaultButton.c_str()).x + 2.0f * kFrameX;
                        if (FlatButton("##verltsdef", m.ltsDefaultButton.c_str(), sx, yy, bw, fh)) res.ltsDefaultAction = true;
                    } else {
                        const float bw = UI::CalcTextSize(m.ltsDefaultButton.c_str()).x + 2.0f * kFrameX;
                        if (FlatButton("##verltsdef", m.ltsDefaultButton.c_str(), px, yy, bw, fh, true, true)) res.ltsDefaultAction = true;
                    }
                    res.ltsDefaultButton = UI::GetItemRect();
                    yy = NextY(yy, fh);
                }
            }
            if (m.defaultRow) {
                yy = NextY(yy, 8.0f);
                dl.AddRectFilled(V(px, yy), V(px + iw, yy + 1.0f), Rgb(255, 255, 255, 15));
                yy = NextY(NextY(yy, 1.0f), 6.0f);
                Txt(dl, V(px, yy + kFrameY), cText, "Default editor");
                const float lx = px + UI::CalcTextSize("Default editor").x + 12.0f;
                int sel = m.defaultSelected;
                if (LauncherCombo("##defed", m.defaultOptions, &sel, lx, yy, 300.0f, res.defaultCombo)) res.defaultChoice = sel;
                Txt(dl, V(lx + 300.0f + 12.0f, yy + kFrameY), cDim, "Opens projects that have no version yet.");
                yy = NextY(yy, fh);
            }
            return yy;
        });

        // A version row: the title and its pills, the detail line; returns the x after the pills.
        auto rowTitle = [&](const VersionRow& r, UI::Vec2 p0) {
            UI::PushTextScale(1.14f);   // an exact size (19.38 px), as the module draws it; its glyphs sit a pixel lower than ours do
            Txt(dl, V(p0.x, p0.y + 8.0f), cText, r.title.c_str());
            float tx = p0.x + UI::CalcTextSize(r.title.c_str()).x + 10.0f;
            UI::PopTextScale();
            if (r.latest) tx += Pill(dl, tx, p0.y + 9.0f, "Latest", AccentAlpha(0.32f), cText) + 6.0f;
            if (r.lts) tx += Pill(dl, tx, p0.y + 9.0f, "LTS", Rgb(70, 170, 120, 90), cText) + 6.0f;
            if (r.isDefault) tx += Pill(dl, tx, p0.y + 9.0f, "Default editor", Rgb(255, 255, 255, 30), cText) + 6.0f;
            return tx;
        };
        auto highlight = [&](UI::Vec2 p0, float rw, float rh) {
            dl.AddRectFilled(V(p0.x - 8.0f, p0.y + 1.0f), V(p0.x + rw + 8.0f, p0.y + rh - 2.0f), AccentAlpha(0.10f), 8.0f);
            OutlineRect(dl, V(p0.x - 8.0f, p0.y + 1.0f), V(p0.x + rw + 8.0f, p0.y + rh - 2.0f), AccentAlpha(0.70f), 8.0f, 1.4f);
        };

        // ---- installed next to this version ----
        card("INSTALLED NEXT TO THIS VERSION", [&](float px, float yy, float iw) {
            if (m.installed.empty()) {
                yy = NextY(yy, DrawWrapped(dl, px, yy, iw, m.installedEmpty, cDim));
                yy = NextY(yy, 4.0f);
            }
            for (std::size_t i = 0; i < m.installed.size(); ++i) {
                const VersionRow& r = m.installed[i];
                UI::PushID(r.version.c_str());
                const UI::Vec2 p0 = V(px, yy);
                const float rh = 58.0f;
                res.installedRows[i] = UI::Rect{p0.x, p0.y, iw, rh};
                if (r.highlight) highlight(p0, iw, rh);
                rowTitle(r, p0);
                const float btnW = 120.0f + 8.0f + 96.0f, sizeW = 92.0f;
                Txt(dl, V(p0.x, p0.y + 33.0f), cDim, FitTextWidth(r.sub, iw - btnW - sizeW - 24.0f).c_str());
                Txt(dl, V(p0.x + iw - btnW - 14.0f - UI::CalcTextSize(r.size.c_str()).x, p0.y + 19.0f), cDim, r.size.c_str());
                if (FlatButton("##vermkdef", r.isDefault ? "Default" : "Make default", p0.x + iw - btnW, p0.y + 14.0f, 120.0f, 30.0f, !r.isDefault)) res.makeDefault = (int)i;
                res.makeDefaultButtons[i] = UI::GetItemRect();
                if (DangerButton("##verrm", "Remove", p0.x + iw - 96.0f, p0.y + 14.0f, 96.0f, 30.0f, UI::GetMousePos())) res.remove = (int)i;
                res.removeButtons[i] = UI::GetItemRect();
                yy = NextY(p0.y + rh, 0.0f);
                if (i + 1 < m.installed.size()) dl.AddRectFilled(V(p0.x, yy - 1.0f), V(p0.x + iw, yy), Rgb(255, 255, 255, 14));
                UI::PopID();
            }
            // keep previous versions
            yy = NextY(yy, 6.0f);
            dl.AddRectFilled(V(px, yy), V(px + iw, yy + 1.0f), Rgb(255, 255, 255, 15));
            yy = NextY(NextY(yy, 1.0f), 6.0f);
            Txt(dl, V(px, yy + kFrameY), cText, "Keep previous versions:");
            const float kx = px + UI::CalcTextSize("Keep previous versions:").x + 10.0f;
            int keep = std::max(0, std::min(9, m.keep));
            if (LauncherCombo("##keepprev", {"0", "1", "2", "3", "4", "5", "6", "7", "8", "9"}, &keep, kx, yy, 72.0f, res.keepCombo)) res.keep = keep;
            Txt(dl, V(kx + 72.0f + 14.0f, yy + kFrameY), cDim, m.usage.c_str());
            yy = NextY(yy, fh);
            yy = NextY(yy, DrawWrapped(dl, px, yy, iw, "When the launcher updates it keeps the version it replaces, up to this many. Versions you install "
                                                      "yourself are never removed automatically.", cDim));
            if (!m.prune.empty()) {
                yy = NextY(yy, 2.0f);
                Txt(dl, V(px, yy + kFrameY), CAmber(), m.prune.c_str());
                const float sx = px + UI::CalcTextSize(m.prune.c_str()).x + 12.0f;
                const float bw = UI::CalcTextSize("Remove them now").x + 2.0f * kFrameX;
                if (FlatButton("##verprune", "Remove them now", sx, yy, bw, fh)) res.pruneNow = true;
                res.pruneButton = UI::GetItemRect();
                yy = NextY(yy, fh);
            }
            return yy;
        });

        // ---- available to install ----
        card("AVAILABLE TO INSTALL", [&](float px, float yy, float iw) {
            if (m.availableState == 0) { Txt(dl, V(px, yy), cDim, "Looking up the version list\xE2\x80\xA6"); return NextY(yy, fs); }
            if (m.availableState == 1) {
                yy = NextY(yy, DrawWrapped(dl, px, yy, iw, m.availableError, CRed()));
                if (FlatButton("##verretry", "Try again", px, yy, 120.0f, 30.0f, !m.retryDisabled)) res.retry = true;
                res.retryButton = UI::GetItemRect();
                return NextY(yy, 30.0f);
            }
            if (m.available.empty()) { Txt(dl, V(px, yy), cDim, "Every version in the list is installed or is the one you're running."); yy = NextY(yy, fs); }
            for (std::size_t i = 0; i < m.available.size(); ++i) {
                const VersionRow& r = m.available[i];
                UI::PushID(r.version.c_str());
                const UI::Vec2 p0 = V(px, yy);
                const bool hasNote = !r.note.empty() && !r.downloading;
                const float rh = hasNote ? 84.0f : 58.0f, rightW = r.downloading ? 330.0f : 130.0f;
                res.availableRows[i] = UI::Rect{p0.x, p0.y, iw, rh};
                if (r.highlight) highlight(p0, iw, rh);
                rowTitle(r, p0);
                Txt(dl, V(p0.x, p0.y + 33.0f), cDim, FitTextWidth(r.sub, iw - rightW - 16.0f).c_str());
                if (r.downloading) {
                    const float bx = p0.x + iw - rightW, by = p0.y + 17.0f, bw = rightW - 100.0f, bh = 24.0f;
                    dl.AddRectFilled(V(bx, by), V(bx + bw, by + bh), CField(), 6.0f);
                    const float fill = bw * std::max(0.0f, std::min(1.0f, r.progress == r.progress ? r.progress : 0.0f));
                    if (fill > 0.0f) {
                        dl.PushClipRect(V(bx, by), V(bx + std::max(1.0f, fill), by + bh));
                        dl.AddRectFilled(V(bx, by), V(bx + bw, by + bh), Col(th.accent), 6.0f);
                        dl.PopClipRect();
                    }
                    const UI::Vec2 ts = UI::CalcTextSize(r.overlay.c_str());
                    Txt(dl, V(std::max(bx, std::min(bx + fill + kSpX, bx + bw - ts.x - kInnerX)), by + (bh - ts.y) * 0.5f), cText, r.overlay.c_str());
                    if (FlatButton("##vercancel", "Cancel", bx + rightW - 92.0f, by, 84.0f, 24.0f, !r.cancelDisabled)) res.cancelDownload = true;
                    res.cancelButtons[i] = UI::GetItemRect();
                } else {
                    if (FlatButton("##verinstall", "Install", p0.x + iw - 120.0f, p0.y + 14.0f, 120.0f, 30.0f, !r.installDisabled, true)) res.install = (int)i;
                    res.installButtons[i] = UI::GetItemRect();
                }
                if (hasNote) DrawWrapped(dl, p0.x, p0.y + 56.0f, iw, r.note, r.noteBad ? CRed() : CGreenV());
                yy = NextY(p0.y + rh, 0.0f);
                if (i + 1 < m.available.size()) dl.AddRectFilled(V(p0.x, yy - 1.0f), V(p0.x + iw, yy), Rgb(255, 255, 255, 14));
                UI::PopID();
            }
            if (!m.moreButton.empty()) {
                yy = NextY(yy, 4.0f);
                const float bw = UI::CalcTextSize(m.moreButton.c_str()).x + 2.0f * kFrameX;
                if (FlatButton("##vermore", m.moreButton.c_str(), px, yy, bw, fs)) res.toggleShowAll = true;
                res.moreButton = UI::GetItemRect();
                yy = NextY(yy, fs);
            }
            return yy;
        });
        res.contentHeight = cy - kSpY - y;
    }
    UI::End();
    return res;
}

} // namespace okayuiscreens
