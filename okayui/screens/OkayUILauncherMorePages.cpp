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
    static std::vector<Entry> cache;
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
            const std::vector<std::string>& hint = WrapCached("Share it so friends can add you.", iw);
            PushScale(1.35f);
            const float bigH = UI::GetTextLineHeight();
            UI::PopTextScale();
            const float cardH = 14.0f + (fs + kSpY) + (bigH + kSpY) + (fs + 2.0f * kFrameY + kSpY) + fs * (float)hint.size() + 14.0f;
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
                        auto rowBtn = [&](const char* id, const char* lbl, bool primary, UI::Rect& r) {
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
                        if (ing) { if (rowBtn("##unf", "Unfollow", false, first)) res.unfollow = e.id; }
                        else {
                            if (rowBtn("##rm", "Remove", false, first)) res.removeFollower = e.id;
                            if (hov && In(first, mouse)) UI::SetTooltip("%s", "Stop them following you");
                            if (!mutual && rowBtn("##fb", "Follow back", true, second)) res.followBack = e.id;
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

} // namespace okayuiscreens
