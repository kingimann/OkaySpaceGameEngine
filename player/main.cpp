// OkaySpace Player — the standalone runtime that runs a built game.
//
//   okay-player [scene.okayscene]
//
// With no argument it loads "game.okayscene" next to the executable. The editor's
// "Build Game" writes that file and copies this player beside it, so a shipped
// game is just <Game>.exe + game.okayscene (+ any assets).
#define SDL_MAIN_HANDLED
#include <SDL.h>

#include <Okay.hpp>
#include "okay/Scene/EnsureCamera.hpp"
#include "okay/Net/DedicatedServer.hpp"
#if !defined(_WIN32)
#include <termios.h>
#endif
#include <iostream>
#include <map>
#include "okay/Net/JoinLink.hpp"
#include "okay/Net/Matchmaker.hpp"
#include "okay/Net/SessionPresence.hpp"
#include "okay/Net/ServerBrowser.hpp"
#include "okay/Net/NetworkManager.hpp"
#include "okay/Platform/Account/Account.hpp"
#include "okay/Render/SkyStars.hpp"      // deterministic skybox star field
#include "okay/Render/GLRenderer.hpp"    // optional GPU (OpenGL) 3D renderer
#include "okay/Render/D3D11Renderer.hpp" // optional GPU (Direct3D 11) 3D renderer (Windows)
#include "okay/Render/D3D12Renderer.hpp" // optional GPU (Direct3D 12) 3D renderer (Windows, opt-in)
#include "Splash.hpp"                   // branded boot loading frame (no black screen at launch)
#include "okay/UI/UIRenderer.hpp"       // the in-game UI pass (engine)
#include "okay/UI/UIPainterSDL.hpp"     // ...drawn through this SDL backend
#include "okay/UI/UIBatcher.hpp"        // ...batched into a few geometry calls
#include "AntiCheatChecks.hpp"          // opt-in anti-cheat: integrity manifest + runtime checks
#include "AntiCheatBackend.hpp"         // ...and the one report a run files about what they found
#include "AntiCheatHardware.hpp"        // (with the salted hardware hash)
#ifdef OKAY_HAVE_OKAYUI
#include "okay/UI/OkayUI.hpp"
#include "OkayScriptUIBridge.hpp"
#include "OkayTestPanel.hpp"
#endif

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#endif

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <string>
#include <unordered_map>
#include <vector>

#if defined(_WIN32)
// Prefer the dedicated GPU on hybrid-graphics laptops (NVIDIA Optimus / AMD
// switchable): exporting these tells the drivers to run the game on the discrete
// NVIDIA/AMD GPU instead of the slower integrated one.
extern "C" {
    __declspec(dllexport) unsigned long NvOptimusEnablement = 1;
    __declspec(dllexport) int AmdPowerXpressRequestHighPerformance = 1;
}
#endif

using namespace okay;

static SDL_Point W2S(const Vec3& p, const Vec3& camPos, float scale, int w, int h) {
    return SDL_Point{(int)(w * 0.5f + (p.x - camPos.x) * scale),
                     (int)(h * 0.5f - (p.y - camPos.y) * scale)};
}

// UI drawing helpers. The implementations live in the engine (okay::ui2d in
// UIRenderer.hpp, shared with the in-game UI pass); these keep the player's own
// overlays (inventory, crafting, loading screen, effects) on their SDL_Renderer.
static void FillUIShape(SDL_Renderer* ren, const SDL_Rect& r, UIShape shape, float radius,
                        const Color& top, const Color& bottom, bool gradient, bool horizontal,
                        float op, int diag = 0, int cornerMask = UICornerAll) {
    ui2d::SDLPainter p(ren);
    ui2d::FillUIShape(p, ui2d::FromSDL(r), shape, radius, top, bottom, gradient, horizontal, op, diag, cornerMask);
}

// A stable, distinct color for each non-zero tile id (no palette is stored).
static SDL_Color TileColor(int id) {
    unsigned h = (unsigned)id * 2654435761u;
    return SDL_Color{(Uint8)(80 + (h & 0x7F)), (Uint8)(80 + ((h >> 8) & 0x7F)),
                     (Uint8)(80 + ((h >> 16) & 0x7F)), 255};
}

// Draw a world-space axis-aligned quad (used for tiles and particles).
static void FillWorldQuad(SDL_Renderer* r, const Vec3& center, float wWorld, float hWorld,
                          const Vec3& camPos, float scale, int w, int h, SDL_Color col) {
    SDL_Point c = W2S(center, camPos, scale, w, h);
    int hw = (int)(wWorld * 0.5f * scale), hh = (int)(hWorld * 0.5f * scale);
    SDL_Rect rect{c.x - hw, c.y - hh, hw * 2 > 0 ? hw * 2 : 1, hh * 2 > 0 ? hh * 2 : 1};
    SDL_SetRenderDrawColor(r, col.r, col.g, col.b, col.a);
    SDL_RenderFillRect(r, &rect);
}

static void MMFillCircle(SDL_Renderer* r, int cx, int cy, int rad, SDL_Color col) {
    ui2d::SDLPainter p(r);
    ui2d::MMFillCircle(p, cx, cy, rad, ui2d::FromSDL(col));
}
static void MMThickLine(SDL_Renderer* r, float x0, float y0, float x1, float y1, float width, SDL_Color col) {
    ui2d::SDLPainter p(r);
    ui2d::MMThickLine(p, x0, y0, x1, y1, width, ui2d::FromSDL(col));
}
// Width of `text` in screen pixels at font-pixel size `px` (the scene's UI font while
// one is set, else the 8x8 bitmap font).
static float UITextW(const std::string& text, float px) { return ui2d::UITextW(text, px); }
static void DrawText(SDL_Renderer* r, const std::string& text, float ox, float oy,
                     float px, SDL_Color col, float letterSp = 0.0f, float lineSp = 0.0f,
                     bool italic = false, bool gradient = false, SDL_Color col2 = SDL_Color{0, 0, 0, 0},
                     okay::TtfFont* ttf = nullptr) {
    ui2d::SDLPainter p(r);
    ui2d::DrawUIText(p, text, ox, oy, px, ui2d::FromSDL(col), letterSp, lineSp, italic, gradient,
                   ui2d::FromSDL(col2), ttf);
}
// Load (and cache) a sprite texture; nullptr if the image can't be read (callers fall
// back to a flat colored quad).
static SDL_Texture* GetTexture(SDL_Renderer* r, const std::string& path,
                               const std::string& baseDir,
                               std::unordered_map<std::string, SDL_Texture*>& cache) {
    return ui2d::LoadSDLTexture(r, path, baseDir, cache);
}

// An item's icon (okay::ItemIconPath), or null when it has none (the name is drawn).
static SDL_Texture* IconTexture(SDL_Renderer* r, const std::string& path, const std::string& baseDir,
                                std::unordered_map<std::string, SDL_Texture*>& cache) {
    return path.empty() ? nullptr : GetTexture(r, path, baseDir, cache);
}


// Screen-space vignette (RenderSettings.vignette): darken the frame's edges toward
// the corners. A cheap post overlay of stepped 1px border frames fading inward, so
// it looks the same regardless of which 3D backend rendered the scene beneath it.
static void DrawVignette(SDL_Renderer* r, float strength) {
    if (strength <= 0.0f) return;
    if (strength > 1.0f) strength = 1.0f;
    int w = 0, h = 0;
    SDL_GetRendererOutputSize(r, &w, &h);
    if (w <= 2 || h <= 2) return;
    SDL_BlendMode prev; SDL_GetRenderDrawBlendMode(r, &prev);
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
    const int steps = 56;
    const float maxInset = 0.42f * (float)std::min(w, h);   // how far the fade reaches
    for (int i = 0; i < steps; ++i) {
        float t = (float)i / (float)(steps - 1);            // 0 at edge .. 1 inward
        int inset = (int)(t * maxInset);
        if (w - 2 * inset <= 0 || h - 2 * inset <= 0) break;
        Uint8 a = (Uint8)(strength * 160.0f * (1.0f - t) * (1.0f - t));   // strongest at edge
        if (a == 0) continue;
        SDL_SetRenderDrawColor(r, 0, 0, 0, a);
        SDL_Rect top{inset, inset, w - 2 * inset, 1};
        SDL_Rect bot{inset, h - inset - 1, w - 2 * inset, 1};
        SDL_Rect lft{inset, inset, 1, h - 2 * inset};
        SDL_Rect rgt{w - inset - 1, inset, 1, h - 2 * inset};
        SDL_RenderFillRect(r, &top); SDL_RenderFillRect(r, &bot);
        SDL_RenderFillRect(r, &lft); SDL_RenderFillRect(r, &rgt);
    }
    SDL_SetRenderDrawBlendMode(r, prev);
}

// Full-screen loading-screen overlay: draws the layout BuildLoadingScreen() makes
// (the editor's Play view draws the same list, so both look the same).
static void DrawLoadingScreen(SDL_Renderer* r, okay::LoadingScreen& ls, const std::string& baseDir,
                              std::unordered_map<std::string, SDL_Texture*>& cache) {
    int W = 0, H = 0; SDL_GetRendererOutputSize(r, &W, &H);
    if (W <= 0 || H <= 0) return;
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
    auto sc = [](const Color& c, float a) {
        return SDL_Color{(Uint8)(c.r * 255), (Uint8)(c.g * 255), (Uint8)(c.b * 255),
                         (Uint8)(std::clamp(a * c.a, 0.0f, 1.0f) * 255)};
    };
    using K = okay::LoadingDrawCmd::Kind;
    for (const okay::LoadingDrawCmd& d : okay::BuildLoadingScreen(ls, (float)W, (float)H)) {
        SDL_Rect box{(int)std::lround(d.x), (int)std::lround(d.y), (int)std::lround(d.w), (int)std::lround(d.h)};
        switch (d.kind) {
        case K::Rect: {
            UIShape shp = d.radius > 0.5f ? UIShape::Rounded : UIShape::Rectangle;
            FillUIShape(r, box, shp, d.radius, d.color, d.color, false, false, d.alpha);
            break;
        }
        case K::Gradient:
            FillUIShape(r, box, UIShape::Rectangle, 0.0f, d.color, d.color2, true, false, d.alpha);
            break;
        case K::Outline: {
            SDL_Color c = sc(d.color, d.alpha);
            SDL_SetRenderDrawColor(r, c.r, c.g, c.b, c.a);
            SDL_RenderDrawRect(r, &box);
            break;
        }
        case K::Image: {
            SDL_Texture* tex = GetTexture(r, d.text, baseDir, cache);
            if (!tex) break;
            int tw = 0, th = 0; SDL_QueryTexture(tex, nullptr, nullptr, &tw, &th);
            SDL_Rect src{(int)(d.u0 * tw), (int)(d.v0 * th), (int)((d.u1 - d.u0) * tw), (int)((d.v1 - d.v0) * th)};
            SDL_SetTextureAlphaMod(tex, (Uint8)(std::clamp(d.alpha, 0.0f, 1.0f) * 255));
            SDL_RenderCopy(r, tex, &src, &box);
            break;
        }
        case K::Text: {
            const float px = std::max(1.0f, d.h / (float)Font8x8::Height);
            const float tw = UITextW(d.text, px);
            const float x = d.align == 1 ? d.x - tw * 0.5f : d.align == 2 ? d.x - tw : d.x;
            DrawText(r, d.text, x, d.y, px, sc(d.color, d.alpha));
            break;
        }
        case K::Circle:
            MMFillCircle(r, (int)std::lround(d.x), (int)std::lround(d.y), std::max(1, (int)std::lround(d.w)), sc(d.color, d.alpha));
            break;
        case K::Line:
            MMThickLine(r, d.x, d.y, d.w, d.h, d.radius, sc(d.color, d.alpha));
            break;
        case K::Arc: {   // exact pixel spans of the ring, so a see-through track has no seams
            SDL_Color c = sc(d.color, d.alpha);
            SDL_SetRenderDrawColor(r, c.r, c.g, c.b, c.a);
            const float ro = d.w + d.radius * 0.5f, ri = std::max(0.0f, d.w - d.radius * 0.5f);
            const float span = d.u1 - d.u0;
            const bool full = std::fabs(span) >= 6.2831853f - 1e-3f;
            const float lo = std::min(d.u0, d.u1);
            auto inArc = [&](float px, float py) {
                float ang = std::atan2(py - d.y, px - d.x) - lo;
                ang = std::fmod(ang, 6.2831853f); if (ang < 0) ang += 6.2831853f;
                return ang <= std::fabs(span);
            };
            for (int yy = (int)std::floor(d.y - ro); yy <= (int)std::ceil(d.y + ro); ++yy) {
                const float dy = yy + 0.5f - d.y;
                if (std::fabs(dy) > ro) continue;
                const float xo = std::sqrt(ro * ro - dy * dy);
                const float xi = std::fabs(dy) < ri ? std::sqrt(ri * ri - dy * dy) : 0.0f;
                const int spans[2][2] = {{(int)std::lround(d.x - xo), (int)std::lround(d.x - xi)},
                                         {(int)std::lround(d.x + xi), (int)std::lround(d.x + xo)}};
                for (int k = 0; k < (xi > 0.0f ? 2 : 1); ++k) {
                    int s0 = spans[k][0], s1 = xi > 0.0f ? spans[k][1] : spans[1][1];
                    if (full) { if (s1 > s0) SDL_RenderDrawLine(r, s0, yy, s1 - 1, yy); continue; }
                    int run = -1;   // draw the runs of pixels inside the arc's angle range
                    for (int xx = s0; xx <= s1; ++xx) {
                        const bool in = xx < s1 && inArc(xx + 0.5f, yy + 0.5f);
                        if (in && run < 0) run = xx;
                        else if (!in && run >= 0) { SDL_RenderDrawLine(r, run, yy, xx - 1, yy); run = -1; }
                    }
                }
            }
            break;
        }
        }
    }
}

// "okay://match?mode=duel&players=2&min=2&region=EU" — the launcher's "Find match"
// (via --find-match or a running game's join file). False if it isn't one.
static bool ParseMatchLink(const std::string& link, okay::MatchRequest& out) {
    const std::string pre = "okay://match";
    if (link.compare(0, pre.size(), pre) != 0) return false;
    out = okay::MatchRequest{};
    std::size_t q = link.find('?');
    while (q != std::string::npos && q + 1 < link.size()) {
        const std::size_t st = q + 1, amp = link.find('&', st);
        const std::string kv = link.substr(st, amp == std::string::npos ? std::string::npos : amp - st);
        const std::size_t eq = kv.find('=');
        std::string k = kv.substr(0, eq), v, raw = eq == std::string::npos ? std::string() : kv.substr(eq + 1);
        for (std::size_t i = 0; i < raw.size(); ++i) {
            if (raw[i] == '%' && i + 2 < raw.size()) { v += (char)std::strtol(raw.substr(i + 1, 2).c_str(), nullptr, 16); i += 2; }
            else v += raw[i] == '+' ? ' ' : raw[i];
        }
        if (k == "mode") out.mode = v;
        else if (k == "players") out.players = std::max(2, std::atoi(v.c_str()));
        else if (k == "min") out.minPlayers = std::atoi(v.c_str());
        else if (k == "region") out.region = v;
        else if (k == "game") out.game = v;
        q = amp;
    }
    return true;
}

// A Minecraft-style hotbar (+ openable backpack) drawn from an Inventory.
#ifdef OKAY_HAVE_OKAYUI
// The built-in in-game server browser ("Find a server"): public servers for this
// game plus the LAN, with ping, players, filters, a password prompt and Join. A
// game opens it with net_browser_open() (or the Open Server Browser action).
static void DrawServerBrowserWindow(okay::Scene& scene, SDL_Window* window) {
    okay::ServerBrowser& sb = okay::ServerBrowser::Shared();
    static bool wasOpen = false;
    static int tab = 0, shownTab = -1, sel = -1;
    static char search[64] = "", pass[64] = "", direct[128] = "";
    static bool notFull = false, noPassword = false;
    static std::string status;
    static okay::NetworkManager* joining = nullptr;
    static Uint32 lastClick = 0; static int lastClickRow = -1;
    static bool cursorWasShown = true;
    if (!sb.windowOpen) {
        if (wasOpen) { wasOpen = false; SDL_ShowCursor(cursorWasShown ? SDL_ENABLE : SDL_DISABLE); }
        return;
    }
    if (!wasOpen) {   // just opened: fresh search, free the mouse (FPS games capture it)
        wasOpen = true; shownTab = -1; sel = -1; status.clear(); joining = nullptr;
        cursorWasShown = SDL_ShowCursor(SDL_QUERY) == SDL_ENABLE;
        okay::Cursor::Capture(false);
        SDL_ShowCursor(SDL_ENABLE);
    }
    const int sources = tab == 1 ? okay::ServerBrowser::kInternet : tab == 2 ? okay::ServerBrowser::kLan : okay::ServerBrowser::kAll;
    if (shownTab != tab && !sb.Busy()) { shownTab = tab; sb.Refresh(sources); sel = -1; }

    auto joinLink = [&](const okay::JoinLink& l) {
        okay::NetworkManager* nm = scene.FindObjectOfType<okay::NetworkManager>();
        if (nm) { nm->JoinByLink(l.Format()); joining = nm; status = "Joining " + (l.name.empty() ? l.addr : l.name) + "\xE2\x80\xA6"; }
        else { okay::NetworkManager::SetLaunchJoin(l.Format()); sb.windowOpen = false; }
    };
    if (joining) {   // close once we're in; say why if refused
        if (joining->Joined()) { sb.windowOpen = false; joining = nullptr; return; }
        if (joining->JoinRejected()) { status = "Couldn't join: the server is full or the password is wrong."; joining = nullptr; }
    }

    int ww = 960, wh = 600; SDL_GetWindowSize(window, &ww, &wh);
    const float W = (float)std::min(780, ww - 40), H = (float)std::min(620, wh - 40);
    bool open = true;
    if (OkayUI::Begin("Find a server", (ww - W) * 0.5f, (wh - H) * 0.5f, W, H, &open)) {
        const char* tabs[] = {"All", "Internet", "LAN"};
        OkayUI::TabBar(tabs, 3, &tab);
        OkayUI::SetNextItemWidth(200.0f);
        OkayUI::InputText("Search", search, sizeof(search));
        OkayUI::SameLine(); OkayUI::Checkbox("Not full", &notFull);
        OkayUI::SameLine(); OkayUI::Checkbox("No password", &noPassword);
        OkayUI::SameLine();
        if (OkayUI::Button(sb.Busy() ? "Searching..." : "Refresh") && !sb.Busy()) { sb.Refresh(sources); sel = -1; }

        const auto all = sb.Results();
        std::string q = search; for (auto& c : q) c = (char)std::tolower((unsigned char)c);
        std::vector<int> rows;
        for (int i = 0; i < (int)all.size(); ++i) {
            const auto& e = all[i];
            if (tab == 1 && !e.listed) continue;
            if (tab == 2 && e.listed) continue;
            if (notFull && e.info.maxPlayers > 0 && e.info.players >= e.info.maxPlayers) continue;
            if (noPassword && e.info.password) continue;
            if (!q.empty()) {
                std::string hay = e.info.name + " " + e.info.map; for (auto& c : hay) c = (char)std::tolower((unsigned char)c);
                if (hay.find(q) == std::string::npos) continue;
            }
            rows.push_back(i);
        }
        if (OkayUI::BeginChild("##servers", 0.0f, H - 300.0f, true)) {
            const char* hdr[] = {"Server", "Players", "Map", "Ping"};
            const float weights[] = {3.2f, 1.1f, 1.4f, 1.0f};
            const float nameW = (W - 60.0f) * weights[0] / 6.7f - 12.0f;
            auto fit = [](std::string t, float maxW) {   // trim with "..." to fit the column
                if (OkayUI::CalcTextWidth(t.c_str()) <= maxW) return t;
                while (!t.empty() && OkayUI::CalcTextWidth((t + "...").c_str()) > maxW) t.pop_back();
                return t + "...";
            };
            if (OkayUI::BeginTable("##srvtable", 4, hdr, weights)) {
                for (int i : rows) {
                    const auto& e = all[i];
                    OkayUI::TableNextRow();
                    OkayUI::TableNextColumn();
                    OkayUI::PushID(i);
                    const std::string label = fit(std::string(e.info.password ? "(locked) " : "") + (e.info.name.empty() ? e.info.Address() : e.info.name), nameW);
                    if (OkayUI::Selectable(label.c_str(), sel == i)) {
                        const Uint32 now = SDL_GetTicks();
                        if (lastClickRow == i && now - lastClick < 400 && !e.info.password) joinLink(e.Link());   // double-click joins
                        lastClick = now; lastClickRow = i; sel = i;
                    }
                    OkayUI::PopID();
                    OkayUI::TableNextColumn();
                    char pl[32];
                    if (e.info.maxPlayers > 0) std::snprintf(pl, sizeof(pl), "%d / %d", e.info.players, e.info.maxPlayers); else std::snprintf(pl, sizeof(pl), "-");
                    OkayUI::Text(pl);
                    OkayUI::TableNextColumn();
                    OkayUI::Text(e.info.map.empty() ? "-" : e.info.map.c_str());
                    OkayUI::TableNextColumn();
                    if (e.info.pingMs < 0) OkayUI::TextDisabled("-");
                    else {
                        char pg[16]; std::snprintf(pg, sizeof(pg), "%d ms", e.info.pingMs);
                        if (e.info.pingMs < 60) OkayUI::TextColored(115, 209, 143, pg);
                        else if (e.info.pingMs < 130) OkayUI::TextColored(235, 199, 97, pg);
                        else OkayUI::TextColored(240, 128, 115, pg);
                    }
                }
                OkayUI::EndTable();
            }
            if (rows.empty())
                OkayUI::TextDisabled(sb.Busy() ? "Looking for servers..." : all.empty() ? "No servers found. Try Refresh, or connect by address below." : "No servers match your filters.");
        }
        OkayUI::EndChild();

        // Selected server: join (with a password if it's locked).
        if (sel >= 0 && sel < (int)all.size()) {
            const auto& e = all[sel];
            const float lineW = W - 40.0f;
            auto fitLine = [&](std::string t) {
                if (OkayUI::CalcTextWidth(t.c_str()) <= lineW) return t;
                while (!t.empty() && OkayUI::CalcTextWidth((t + "...").c_str()) > lineW) t.pop_back();
                return t + "...";
            };
            OkayUI::Text(fitLine(e.info.name.empty() ? e.info.Address() : e.info.name).c_str());
            std::string line = e.info.Address();
            if (e.info.dedicated) line += "  dedicated";
            if (e.info.auth) line += "  sign-in";
            if (e.info.password) line += "  password";
            OkayUI::TextDisabled(fitLine(line).c_str());
            if (e.info.password) { OkayUI::SetNextItemWidth(200.0f); OkayUI::InputText("Password", pass, sizeof(pass)); OkayUI::SameLine(); }
            if (OkayUI::PrimaryButton("Join")) {
                okay::JoinLink l = e.Link();
                if (e.info.password) l.password = pass;
                if (e.info.password && !pass[0]) status = "This server needs a password.";
                else joinLink(l);
            }
        } else OkayUI::TextDisabled("Pick a server (double-click to join).");
        OkayUI::Separator();
        {   // Matchmaking: get put in a match with other players (Steam's "Find match").
            static int players = 2;
            okay::Matchmaker& mm = okay::Matchmaker::Shared();
            const auto st = mm.GetState();
            const bool busy = st == okay::Matchmaker::State::Searching || st == okay::Matchmaker::State::Waiting;
            OkayUI::SetNextItemWidth(160.0f);
            OkayUI::SliderInt("Players", &players, 2, 16);
            OkayUI::SameLine();
            char plText[16]; std::snprintf(plText, sizeof(plText), "%d", players);
            OkayUI::Badge(plText);
            OkayUI::SameLine();
            if (busy) { if (OkayUI::Button("Cancel search")) mm.Cancel(); }
            else if (OkayUI::PrimaryButton("Find match")) {
                okay::MatchRequest r; r.players = players; r.minPlayers = players > 2 ? 2 : 0;
                mm.Start(r);
                sb.windowOpen = false;   // the "Finding a match" banner takes over
            }
        }
        OkayUI::Separator();
        // Direct connect by address (or a pasted okay://join link).
        OkayUI::SetNextItemWidth(260.0f);
        OkayUI::InputText("Address", direct, sizeof(direct));
        OkayUI::SameLine();
        if (OkayUI::Button("Connect") && direct[0]) {
            okay::JoinLink l;
            if (!okay::JoinLink::Parse(direct, l)) {
                std::string h; std::uint16_t port = 0;
                if (okay::SplitHostPort(direct, h, port)) { l = okay::JoinLink{}; l.game = sb.Game(); l.addr = h + ":" + std::to_string(port); }
            }
            if (!l.addr.empty() || l.Relayed()) { if (pass[0] && l.password.empty()) l.password = pass; joinLink(l); }
            else status = "That isn't an address (try host:port).";
        }
        if (!status.empty()) OkayUI::TextColored(235, 199, 97, status.c_str());
    }
    OkayUI::End();
    if (!open) sb.windowOpen = false;
}

// "Finding a match 0:23 · 6 searching [Cancel]" while matchmaking runs (top of the
// screen), "Match found" as it connects, and why it stopped if it failed.
static void DrawMatchmakingBanner(SDL_Window* window) {
    okay::Matchmaker& mm = okay::Matchmaker::Shared();
    const auto st = mm.GetState();
    static Uint32 foundAt = 0, failedAt = 0;
    if (st != okay::Matchmaker::State::Found) foundAt = 0;
    if (st != okay::Matchmaker::State::Failed) failedAt = 0;
    if (st == okay::Matchmaker::State::Idle) return;
    const Uint32 now = SDL_GetTicks();
    if (st == okay::Matchmaker::State::Found) { if (!foundAt) foundAt = now; if (now - foundAt > 4000) return; }
    if (st == okay::Matchmaker::State::Failed) { if (!failedAt) failedAt = now; if (now - failedAt > 8000) { mm.Cancel(); return; } }
    int ww = 960, wh = 600; SDL_GetWindowSize(window, &ww, &wh);
    const float W = std::min(440.0f, (float)ww - 20.0f), H = 184.0f;
    bool open = true;
    if (OkayUI::Begin("Matchmaking", (ww - W) * 0.5f, 14.0f, W, H, &open)) {
        const int secs = (int)mm.Elapsed();
        char line[160];
        const okay::MatchRequest rq = mm.Request();
        if (st == okay::Matchmaker::State::Searching || st == okay::Matchmaker::State::Waiting) {
            const bool waiting = st == okay::Matchmaker::State::Waiting;
            OkayUI::Spinner(); OkayUI::SameLine();
            std::snprintf(line, sizeof(line), "%s   %d:%02d", waiting ? "Match found - starting" : "Finding a match", secs / 60, secs % 60);
            OkayUI::Text(line);
            if (waiting) std::snprintf(line, sizeof(line), "Waiting for the host to start the match...");
            else std::snprintf(line, sizeof(line), "%s%s%d players  -  %d in the queue", rq.mode.c_str(), rq.mode.empty() ? "" : "  -  ", rq.players, mm.Searching());
            OkayUI::TextDisabled(line);
            if (OkayUI::Button("Cancel")) mm.Cancel();
        } else if (st == okay::Matchmaker::State::Found) {
            const okay::MatchAssignment a = mm.Assignment();
            OkayUI::Caption("MATCH FOUND");
            std::snprintf(line, sizeof(line), "%s - %d players", a.isHost ? "You're hosting" : !a.serverName.empty() ? a.serverName.c_str() : "Joining", a.players);
            OkayUI::Text(line);
        } else {
            OkayUI::Caption("COULDN'T FIND A MATCH", 240, 128, 115);
            const std::string e = mm.Error();
            OkayUI::TextWrapped(e.empty() ? "Something went wrong." : e.c_str());
            if (OkayUI::Button("OK")) mm.Cancel();
        }
    }
    OkayUI::End();
    if (!open) mm.Cancel();
}
#endif

static void DrawInventoryUI(SDL_Renderer* r, okay::InventoryUI& ui, const std::string& baseDir,
                            std::unordered_map<std::string, SDL_Texture*>& cache) {
    okay::Inventory* inv = ui.Inv();
    int W = 0, H = 0; SDL_GetRendererOutputSize(r, &W, &H);
    if (W <= 0 || H <= 0) return;
    // Layout + input are shared with the editor Play view (okay/Inventory/InventoryInput).
    const okay::InventoryInput::HotbarLayout L = okay::InventoryInput::LayoutHotbar(ui, (float)W, (float)H);
    const int n = L.hotbar, total = L.total, bpRows = L.rows;
    const float uiScale = L.scale, sz = L.sz, gap = L.gap, panelPad = ui.panelPad * uiScale;
    auto setc = [&](const Color& c, int a = -1) {
        SDL_SetRenderDrawColor(r, (Uint8)(c.r * 255), (Uint8)(c.g * 255), (Uint8)(c.b * 255),
                               (Uint8)(a < 0 ? c.a * 255 : a));
    };
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
    const float cr = ui.cornerRadius;
    const UIShape shp = cr > 0.5f ? UIShape::Rounded : UIShape::Rectangle;
    // Draw one slot at (x,y) for inventory stack index `idx`, highlighted if selected.
    auto slot = [&](float x, float y, int idx, bool sel) {
        // Selected hotbar slot "pop": grow it about its centre by selectedScale.
        float S = sz, X = x, Y = y;
        if (sel && ui.selectedScale != 1.0f && ui.selectedScale > 0.01f) {
            S = sz * ui.selectedScale; X = x - (S - sz) * 0.5f; Y = y - (S - sz) * 0.5f;
        }
        float b = (ui.borderWidth < 0 ? 0 : ui.borderWidth) + (sel ? 1.0f : 0.0f);
        SDL_Rect outer{(int)X, (int)Y, (int)S, (int)S};
        Color bc = sel ? ui.selectedColor : ui.slotBorder;
        bool empty = !(inv && idx >= 0 && idx < (int)inv->slots.size() && !inv->slots[idx].item.empty());
        if (!sel && !empty)
            if (const Color* rc = ui.RarityOf(inv->slots[idx].item)) bc = *rc;   // rarity tint
        FillUIShape(r, outer, shp, cr, bc, bc, false, false, 1.0f);
        SDL_Rect inner{(int)(X + b), (int)(Y + b), (int)(S - 2 * b), (int)(S - 2 * b)};
        float ir = cr > 0.5f ? (cr - b < 0 ? 0 : cr - b) : 0.0f;
        Color fillc = empty ? ui.emptySlotColor : (idx < n ? ui.hotbarColor : ui.slotColor);
        FillUIShape(r, inner, ir > 0.5f ? UIShape::Rounded : UIShape::Rectangle, ir, fillc, fillc, false, false, 1.0f);
        // Count shown in the slot: the source of a split-drag still shows its remainder
        // (a whole-stack drag lifts everything, so the source draws empty).
        int shown = (inv && idx >= 0 && idx < (int)inv->slots.size()) ? inv->slots[idx].count : 0;
        if (idx == ui.dragIndex) shown -= (ui.dragAmount > 0 ? ui.dragAmount : shown);
        if (shown > 0 && inv && idx >= 0 && idx < (int)inv->slots.size() && !inv->slots[idx].item.empty()) {
            const auto& it = inv->slots[idx];
            SDL_Texture* icon = IconTexture(r, okay::ItemIconPath(ui.iconFolder, it.item), baseDir, cache);
            if (icon) { int ins = (int)(ui.iconInset * uiScale); SDL_Rect d{inner.x + ins, inner.y + ins, inner.w - 2 * ins, inner.h - 2 * ins}; SDL_RenderCopy(r, icon, nullptr, &d); }
            else if (ui.showNames) {
                std::string nm = it.item.substr(0, ui.nameChars < 1 ? 1 : (std::size_t)ui.nameChars);
                float px = (S - 8.0f) / ((float)nm.size() * (Font8x8::Width + 1)) * ui.labelScale;
                if (px < 1.0f) px = 1.0f; if (px > S * 0.05f) px = S * 0.05f;   // fit the slot
                DrawText(r, nm, X + 4, Y + 5, px,
                         SDL_Color{(Uint8)(ui.textColor.r*255),(Uint8)(ui.textColor.g*255),(Uint8)(ui.textColor.b*255),255});
            }
            if (ui.showCounts && shown > 1) {
                std::string cs = std::to_string(shown);
                float px = S * 0.038f * ui.labelScale; if (px < 1.0f) px = 1.0f;
                float tw = UITextW(cs, px);
                DrawText(r, cs, X + S - tw - 3, Y + S - (Font8x8::Height + 1) * px - 3, px,
                         SDL_Color{(Uint8)(ui.countColor.r*255),(Uint8)(ui.countColor.g*255),(Uint8)(ui.countColor.b*255),255});
            }
        }
    };
    // Hotbar docked to the bottom (or top); the backpack grows away from that edge.
    const float rowW = L.rowW, hx = L.hx, hy = L.hy;
    auto bpY = [&](int row) { return L.RowY(row); };
    auto panel = [&](float x, float y, float w, float h) {
        SDL_Rect rc{(int)x, (int)y, (int)w, (int)h};
        UIShape psh = cr > 0.5f ? UIShape::Rounded : UIShape::Rectangle;
        FillUIShape(r, rc, psh, cr + 2.0f, ui.panelColor, ui.panelColor, false, false, 1.0f);
        // Optional outline (alpha 0 = off): a few inset frames approximate a border.
        if (ui.panelBorder.a > 0.001f && ui.panelBorderWidth > 0.0f) {
            int bw = (int)(ui.panelBorderWidth * uiScale); if (bw < 1) bw = 1;
            setc(ui.panelBorder);
            for (int k = 0; k < bw; ++k) {
                SDL_Rect fr{rc.x + k, rc.y + k, rc.w - 2 * k, rc.h - 2 * k};
                if (fr.w > 0 && fr.h > 0) SDL_RenderDrawRect(r, &fr);
            }
        }
    };
    // Backpack (when open): darken, then the grid.
    if (ui.open && bpRows > 0) {
        if (ui.darkenWhenOpen) { setc(Color::FromBytes(0, 0, 0, 150)); SDL_Rect full{0, 0, W, H}; SDL_RenderFillRect(r, &full); }
        if (ui.showPanel) {
            float t = bpY(0), b = bpY(bpRows - 1);
            float top = t < b ? t : b, bot = (t > b ? t : b) + sz, pad = panelPad;
            panel(hx - pad, top - pad, rowW + 2 * pad, (bot - top) + 2 * pad);
        }
        // Backpack header title above the grid.
        if (ui.showTitle && !ui.backpackTitle.empty()) {
            float t = bpY(0), b = bpY(bpRows - 1);
            float top = (t < b ? t : b);
            float px = 2.0f * ui.labelScale * uiScale; if (px < 1.0f) px = 1.0f;
            float tw = UITextW(ui.backpackTitle, px);
            DrawText(r, ui.backpackTitle, hx + rowW * 0.5f - tw * 0.5f,
                     top - panelPad - (Font8x8::Height + 1) * px - 2.0f, px,
                     SDL_Color{(Uint8)(ui.titleColor.r*255),(Uint8)(ui.titleColor.g*255),(Uint8)(ui.titleColor.b*255),255});
        }
        for (int row = 0; row < bpRows; ++row)
            for (int c = 0; c < n && n + row * n + c < total; ++c)
                slot(hx + c * (sz + gap), bpY(row), n + row * n + c, false);
    }
    if (ui.showPanel) panel(hx - panelPad, hy - panelPad, rowW + 2 * panelPad, sz + 2 * panelPad);
    for (int i = 0; i < n && i < total; ++i) slot(hx + i * (sz + gap), hy, i, i == ui.selected);
    // Equipment column (worn items) beside the open backpack.
    auto equipIcon = [&](const std::string& item, float x, float y) {
        if (SDL_Texture* icon = IconTexture(r, okay::ItemIconPath(ui.iconFolder, item), baseDir, cache)) {
            int ins = (int)(ui.iconInset * uiScale); SDL_Rect d{(int)x + ins, (int)y + ins, (int)sz - 2 * ins, (int)sz - 2 * ins};
            SDL_RenderCopy(r, icon, nullptr, &d);
        } else {
            std::string nm = item.substr(0, ui.nameChars < 1 ? 1 : (std::size_t)ui.nameChars);
            float px = (sz - 8.0f) / ((float)nm.size() * (Font8x8::Width + 1)) * ui.labelScale;
            if (px < 1.0f) px = 1.0f; if (px > sz * 0.05f) px = sz * 0.05f;
            DrawText(r, nm, x + 4, y + 5, px, SDL_Color{(Uint8)(ui.textColor.r*255),(Uint8)(ui.textColor.g*255),(Uint8)(ui.textColor.b*255),255});
        }
    };
    if (L.equip && L.equipN > 0) {
        float x0, y0, x1, y1; L.EquipXY(0, x0, y0); L.EquipXY(L.equipN - 1, x1, y1);
        if (ui.showPanel) panel(x0 - panelPad, y0 - panelPad, sz + 2 * panelPad, (y1 - y0) + sz + 2 * panelPad);
        for (int e = 0; e < L.equipN; ++e) {
            float x, y; L.EquipXY(e, x, y);
            const std::string& it = L.equip->ItemIn(e);
            SDL_Rect outer{(int)x, (int)y, (int)sz, (int)sz};
            FillUIShape(r, outer, shp, cr, ui.slotBorder, ui.slotBorder, false, false, 1.0f);
            float b = ui.borderWidth < 0 ? 0 : ui.borderWidth;
            SDL_Rect inner{(int)(x + b), (int)(y + b), (int)(sz - 2 * b), (int)(sz - 2 * b)};
            const Color fc = it.empty() ? ui.emptySlotColor : ui.slotColor;
            FillUIShape(r, inner, shp, cr, fc, fc, false, false, 1.0f);
            if (!it.empty() && e != ui.equipDrag) equipIcon(it, x, y);
            else if (it.empty()) {   // the slot's name, small, so you know what goes there
                const std::string& nm = L.equip->slots[e].name;
                float px = (sz - 6.0f) / ((float)(nm.size() < 4 ? 4 : nm.size()) * (Font8x8::Width + 1));
                if (px < 1.0f) px = 1.0f;
                DrawText(r, nm, x + 3, y + sz * 0.5f - Font8x8::Height * px * 0.5f, px,
                         SDL_Color{(Uint8)(ui.textColor.r*255),(Uint8)(ui.textColor.g*255),(Uint8)(ui.textColor.b*255),110});
            }
        }
    }
    // Hotbar slot numbers (1–9) in the top-left corner.
    if (ui.slotNumbers) {
        float px = sz * 0.030f * ui.labelScale; if (px < 1.0f) px = 1.0f;
        for (int i = 0; i < n && i < 9; ++i)
            DrawText(r, std::to_string(i + 1), hx + i * (sz + gap) + 3, hy + 2, px,
                     SDL_Color{(Uint8)(ui.numberColor.r*255),(Uint8)(ui.numberColor.g*255),(Uint8)(ui.numberColor.b*255),255});
    }
    // Tooltip helper: a small panel near the cursor with the item's name + count.
    okay::Vec2 mpos = okay::Input::MousePosition();
    auto drawTip = [&](const std::string& label) {
        if (!ui.showTooltips || label.empty()) return;
        float px = sz * 0.030f * ui.labelScale; if (px < 1.0f) px = 1.0f;
        float tw = UITextW(label, px), th = Font8x8::Height * px, pad = 6.0f;
        SDL_Rect bg{(int)(mpos.x + 14), (int)(mpos.y + 14), (int)(tw + pad * 2), (int)(th + pad * 2)};
        FillUIShape(r, bg, UIShape::Rounded, 4.0f, ui.tooltipColor, ui.tooltipColor, false, false, 1.0f);
        DrawText(r, label, bg.x + pad, bg.y + pad, px,
                 SDL_Color{(Uint8)(ui.tooltipText.r*255),(Uint8)(ui.tooltipText.g*255),(Uint8)(ui.tooltipText.b*255),255});
    };
    auto slotLabel = [&](const Inventory::Slot& it) {
        return it.item + (it.count > 1 ? " x" + std::to_string(it.count) : std::string());
    };
    // Hotbar tooltip on hover (shown even when the backpack is closed).
    if (inv && ui.showTooltips && !ui.open && ui.dragIndex < 0)
        for (int i = 0; i < n && i < (int)inv->slots.size(); ++i) {
            float sx = hx + i * (sz + gap);
            if (mpos.x >= sx && mpos.x < sx + sz && mpos.y >= hy && mpos.y < hy + sz &&
                !inv->slots[i].item.empty()) { drawTip(slotLabel(inv->slots[i])); break; }
        }
    // Minecraft-style held-item name above (or below) the hotbar.
    if (ui.showSelectedName) {
        std::string nm = ui.SelectedItem();
        if (!nm.empty()) {
            float px = 2.0f * ui.labelScale * uiScale; if (px < 1.0f) px = 1.0f;
            float tw = UITextW(nm, px);
            float ty = ui.anchorTop ? hy + sz + panelPad + 4.0f : hy - (Font8x8::Height + 1) * px - panelPad - 4.0f;
            DrawText(r, nm, hx + rowW * 0.5f - tw * 0.5f, ty, px,
                     SDL_Color{(Uint8)(ui.textColor.r*255),(Uint8)(ui.textColor.g*255),(Uint8)(ui.textColor.b*255),255});
        }
    }

    // Drag-and-drop (only while the backpack is open): pick, split, quick-move, drop.
    const okay::Vec2 mp = mpos;
    const okay::InventoryInput::HotbarFrame fr = okay::InventoryInput::HandleHotbar(ui, L, mp);
    if (!ui.open || !ui.dragItems || !inv) return;
    auto hilite = [&](int idx, const Color& c) {
        if (idx < 0) return; float sx, sy; L.SlotXY(idx, sx, sy);
        SDL_Rect hr{(int)sx, (int)sy, (int)sz, (int)sz};
        FillUIShape(r, hr, cr > 0.5f ? UIShape::Rounded : UIShape::Rectangle, cr, c, c, false, false, 1.0f);
    };
    if (L.equip && fr.equipHover >= 0) {   // equipment slot under the cursor: highlight + what's worn
        float ex, ey; L.EquipXY(fr.equipHover, ex, ey);
        SDL_Rect hr{(int)ex, (int)ey, (int)sz, (int)sz};
        FillUIShape(r, hr, cr > 0.5f ? UIShape::Rounded : UIShape::Rectangle, cr, ui.hoverColor, ui.hoverColor, false, false, 1.0f);
        const std::string& it = L.equip->ItemIn(fr.equipHover);
        const okay::ItemDefinition* d = it.empty() ? nullptr : okay::ItemDatabase::Get().Find(it);
        drawTip(L.equip->slots[fr.equipHover].name + (it.empty() ? std::string() : ": " + (d ? d->DisplayName() : it)));
    }
    if (L.equip && ui.equipDrag >= 0 && ui.equipDrag < L.equipN) {   // a worn item following the cursor
        const float x = mp.x - sz * 0.5f, y = mp.y - sz * 0.5f;
        slot(x, y, -2, false);
        equipIcon(L.equip->ItemIn(ui.equipDrag), x, y);
    }
    // Highlight the slot under the cursor while hovering, or where a held stack will land.
    if (fr.hover >= 0) {
        hilite(fr.hover, ui.hoverColor);
        if (fr.hover < (int)inv->slots.size() && !inv->slots[fr.hover].item.empty()) drawTip(slotLabel(inv->slots[fr.hover]));
    }
    hilite(fr.target, Color{ui.selectedColor.r, ui.selectedColor.g, ui.selectedColor.b, 0.40f});
    if (ui.dragIndex >= 0 && ui.dragIndex < (int)inv->slots.size()) {
        const auto& it = inv->slots[ui.dragIndex];
        float x = mp.x - sz * 0.5f, y = mp.y - sz * 0.5f;
        slot(x, y, -2, false);   // an empty slot frame floating under the cursor
        SDL_Texture* icon = IconTexture(r, okay::ItemIconPath(ui.iconFolder, it.item), baseDir, cache);
        if (icon) { SDL_Rect d{(int)x + 4, (int)y + 4, (int)sz - 8, (int)sz - 8}; SDL_RenderCopy(r, icon, nullptr, &d); }
        else {
            std::string nm = it.item.substr(0, ui.nameChars < 1 ? 1 : (std::size_t)ui.nameChars);
            float px = (sz - 8.0f) / ((float)nm.size() * (Font8x8::Width + 1)) * ui.labelScale;
            if (px < 1.0f) px = 1.0f; if (px > sz * 0.05f) px = sz * 0.05f;
            DrawText(r, nm, x + 4, y + 5, px,
                     SDL_Color{(Uint8)(ui.textColor.r*255),(Uint8)(ui.textColor.g*255),(Uint8)(ui.textColor.b*255),255});
        }
        // The carried count (the split amount, or the whole stack).
        int carried = ui.dragAmount > 0 ? ui.dragAmount : it.count;
        if (ui.showCounts && carried > 1) {
            std::string ccs = std::to_string(carried);
            float px = sz * 0.038f * ui.labelScale; if (px < 1.0f) px = 1.0f;
            float tw = UITextW(ccs, px);
            DrawText(r, ccs, x + sz - tw - 3, y + sz - (Font8x8::Height + 1) * px - 3, px,
                     SDL_Color{(Uint8)(ui.countColor.r*255),(Uint8)(ui.countColor.g*255),(Uint8)(ui.countColor.b*255),255});
        }
    }
}

// A DayZ/Unturned grid inventory: multi-cell items, drag-and-drop within the grid.
// Unturned-style multi-container inventory: the player's own grid + every worn
// clothing/bag grid stacked in a left column, and nearby ground containers in a
// right column. Drag-and-drop works across all of them (cross-bag / loot moves).
static void DrawGridInventory(SDL_Renderer* r, okay::GridInventoryUI& ui, const std::string& baseDir,
                              std::unordered_map<std::string, SDL_Texture*>& cache) {
    okay::GridInventory* primary = ui.Inv();
    if (!primary || !primary->open) { ui.CancelDrag(); return; }
    int W = 0, H = 0; SDL_GetRendererOutputSize(r, &W, &H);
    if (W <= 0 || H <= 0) return;
    // Layout + input are shared with the editor Play view (okay/Inventory/InventoryInput).
    const okay::InventoryInput::GridLayout GL = okay::InventoryInput::LayoutGrid(ui, *primary, (float)W, (float)H);
    const float cs = GL.cs, gp = GL.gp;
    const float cr = ui.cornerRadius < 0.0f ? 0.0f : ui.cornerRadius;
    auto setc = [&](const Color& c, int a = -1) {
        SDL_SetRenderDrawColor(r, (Uint8)(c.r * 255), (Uint8)(c.g * 255), (Uint8)(c.b * 255), (Uint8)(a < 0 ? c.a * 255 : a));
    };
    auto sc = [&](const Color& c) { return SDL_Color{(Uint8)(c.r * 255), (Uint8)(c.g * 255), (Uint8)(c.b * 255), 255}; };
    auto fill = [&](float x, float y, float w, float h, const Color& c, float rad) {
        SDL_Rect rc{(int)x, (int)y, (int)w, (int)h};
        FillUIShape(r, rc, rad > 0.5f ? UIShape::Rounded : UIShape::Rectangle, rad, c, c, false, false, 1.0f);
    };
    auto fillG = [&](float x, float y, float w, float h, const Color& a, const Color& b, float rad) {
        SDL_Rect rc{(int)x, (int)y, (int)w, (int)h};
        if (ui.useGradients) FillUIShape(r, rc, rad > 0.5f ? UIShape::Rounded : UIShape::Rectangle, rad, a, b, true, false, 1.0f);
        else                 FillUIShape(r, rc, rad > 0.5f ? UIShape::Rounded : UIShape::Rectangle, rad, a, a, false, false, 1.0f);
    };
    auto lighten = [&](Color c, float f) { return Color{c.r + (1 - c.r) * f, c.g + (1 - c.g) * f, c.b + (1 - c.b) * f, c.a}; };
    auto darken  = [&](Color c, float f) { return Color{c.r * (1 - f), c.g * (1 - f), c.b * (1 - f), c.a}; };
    auto shadow  = [&](float x, float y, float w, float h, float rad) { if (ui.dropShadows) fill(x + 3, y + 5, w, h, Color{0, 0, 0, 0.35f}, rad); };
    auto stroke  = [&](float x, float y, float w, float h, const Color& c, int a) {
        setc(c, a); SDL_Rect bx{(int)x, (int)y, (int)w, (int)h}; SDL_RenderDrawRect(r, &bx);
    };

    const std::vector<okay::GridInventory*>& equipped = GL.equipped;
    const std::vector<okay::InventoryInput::GridPanel>& panels = GL.panels;
    const float labelH = GL.labelH, colGap = GL.colGap, pad = GL.pad, headH = GL.headH, headGap = GL.headGap;
    const float lw = GL.lw, lh = GL.lh, rw = GL.rw, rh = GL.rh, startX = GL.startX, startY = GL.startY;
    const bool hasNear = GL.hasNear;

    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
    if (ui.darkenWhenOpen) { setc(Color{0, 0, 0, ui.backdropDim}); SDL_Rect full{0, 0, W, H}; SDL_RenderFillRect(r, &full); }
    // Backing panels (drop shadow + soft vertical gradient + hairline rim) per column.
    auto backing = [&](float x, float y, float w, float h) {
        shadow(x, y, w, h, cr + 3);
        fillG(x, y, w, h, lighten(ui.panelColor, 0.05f), darken(ui.panelColor, 0.14f), cr + 3);
        stroke(x, y, w, h, lighten(ui.panelColor, 0.16f), 110);
    };
    backing(startX - pad, startY - pad, lw + pad*2, lh + pad*2);
    if (hasNear) backing(startX + lw + colGap - pad, startY - pad, rw + pad*2, rh + pad*2);

    // One slim centred title over the equipped column (the per-bag headers below
    // carry the container names), plus an aggregate weight bar.
    if (ui.showMasterTitle) {
        float hbX = startX - pad, hbW = lw + pad*2, hbY = startY - pad - headH - headGap;
        if (hbW < 200.0f) hbW = 200.0f;
        const std::string& mt = ui.masterTitle; float px = ui.titleScale;
        float tw = UITextW(mt, px);
        DrawText(r, mt, hbX + (hbW - tw) * 0.5f, hbY, px, sc(ui.textColor));
        float totW = 0, totL = 0; for (auto* g : equipped) { totW += g->TotalWeight(); totL += g->weightLimit; }
        if (ui.showWeight && totL > 0.0f) {
            bool over = totW > totL; float frac = totW / totL; if (frac > 1.0f) frac = 1.0f; if (frac < 0.0f) frac = 0.0f;
            float bx = hbX + 4, bw = hbW - 8, by = hbY + 20.0f;
            fill(bx, by, bw, 5, darken(ui.panelColor, 0.4f), 2);
            Color bc = over ? ui.overweightColor : (frac > 0.85f ? ui.weightWarn : ui.weightGood);
            fill(bx, by, bw * frac, 5, bc, 2);
        }
    }
    if (hasNear) {
        float nx = startX + lw + colGap;
        fill(nx - 4, startY - pad - headH - headGap + 2, 4, 14, Color::FromBytes(200, 170, 110, 255), 0);
        DrawText(r, ui.nearbyTitle, nx + 6, startY - pad - headH - headGap, 1.6f, sc(ui.textColor));
    }

    okay::Vec2 mp = okay::Input::MousePosition();
    const okay::InventoryInput::GridFrame hov = okay::InventoryInput::HoverGrid(GL, mp);   // panel + cell under the cursor
    const int hovPanel = hov.panel, hovCx = hov.cx, hovCy = hov.cy;

    auto drawItem = [&](okay::GridInventoryUI& u, const okay::GridItem& it, float px0, float py0, bool ghost, bool hover) {
        float w = it.w * cs + (it.w - 1) * gp, h = it.h * cs + (it.h - 1) * gp;
        const Color* rar = u.RarityOf(it.name);
        Color accent = rar ? *rar : u.Accent(it.name);
        Color top = lighten(u.itemColor, hover ? 0.16f : 0.08f), bot = darken(u.itemColor, 0.16f);
        if (ghost) { top.a *= 0.72f; bot.a *= 0.72f; }
        fillG(px0, py0, w, h, top, bot, cr);
        if (u.useGradients) fill(px0 + 2, py0 + 2, w - 4, 2, Color{1, 1, 1, ghost ? 0.10f : 0.18f}, 0);   // top sheen
        if (u.accentBars) fill(px0, py0, 3, h, accent, 0);                            // rarity/category bar
        SDL_Rect box{(int)px0, (int)py0, (int)w, (int)h};
        stroke(px0, py0, w, h, rar ? *rar : u.itemBorder, ghost ? 170 : 235);
        if (hover) { fill(px0, py0, w, h, u.hoverColor, cr); stroke(px0, py0, w, h, lighten(u.itemBorder, 0.35f), 255); }
        SDL_Texture* icon = IconTexture(r, okay::ItemIconPath(u.iconFolder, it.name), baseDir, cache);
        if (icon) { SDL_SetTextureAlphaMod(icon, ghost ? 160 : 255); SDL_Rect d{box.x + 6, box.y + 6, box.w - 12, box.h - 12}; SDL_RenderCopy(r, icon, nullptr, &d); }
        else if (u.showItemNames) {   // clip the name to the tile width so it never overflows the cell
            float px = u.itemNameScale; int maxc = (int)((w - 12) / ((Font8x8::Width + 1) * px)); if (maxc < 1) maxc = 1;
            std::string nm = (int)it.name.size() > maxc ? it.name.substr(0, maxc) : it.name;
            DrawText(r, nm, box.x + 6, box.y + 7, px, sc(u.textColor));
        }
        if (it.count > 1) {
            std::string cstr = std::to_string(it.count); float px = 1.3f;
            float tw = UITextW(cstr, px), ph = Font8x8::Height * px;
            float pwid = tw + 7, phgt = ph + 5;
            fill(box.x + box.w - pwid - 2, box.y + box.h - phgt - 2, pwid, phgt, Color::FromBytes(10, 12, 18, 225), 3);
            DrawText(r, cstr, box.x + box.w - tw - 5, box.y + box.h - ph - 4, px, SDL_Color{255, 255, 255, 255});
        }
    };

    // Draw every container: accent header, inset cells, drop highlight, items.
    for (int pi = 0; pi < (int)panels.size(); ++pi) {
        const okay::InventoryInput::GridPanel& P = panels[pi]; okay::GridInventory* inv = P.inv;
        Color acc = ui.Accent(P.label);
        float hy = P.oy - labelH - 2;
        fillG(P.ox - 6, hy, P.gw + 12, labelH, lighten(ui.headerColor, 0.08f), darken(ui.headerColor, 0.05f), cr);
        if (ui.accentBars) fill(P.ox - 6, hy, 3, labelH, acc, 0);    // category accent stripe
        DrawText(r, P.label, P.ox + (ui.accentBars ? 2.0f : 6.0f), hy + 5, 1.4f, sc(ui.textColor));
        if (ui.showWeight && inv->weightLimit > 0.0f) {
            char wb[48]; std::snprintf(wb, sizeof(wb), "%.1f/%.0f", inv->TotalWeight(), inv->weightLimit);
            float px = 1.2f, tw = UITextW(wb, px);
            DrawText(r, wb, P.ox + P.gw - tw, hy + 6, px, sc(inv->OverWeight() ? ui.overweightColor : ui.textColor));
        }
        for (int y = 0; y < inv->rows; ++y)
            for (int x = 0; x < inv->cols; ++x) {
                float cxp = P.ox + x * (cs + gp), cyp = P.oy + y * (cs + gp);
                fillG(cxp, cyp, cs, cs, darken(ui.cellColor, 0.04f), darken(ui.cellColor, 0.18f), cr);
                stroke(cxp, cyp, cs, cs, darken(ui.cellColor, 0.32f), 130);
            }
        if (ui.dragInv && hovPanel == pi) {
            const okay::GridItem it = ui.HeldItem();
            int tx = hovCx - ui.grabX, ty = hovCy - ui.grabY;
            bool ok = ui.CanDrop(inv, hovCx, hovCy);
            for (int yy = 0; yy < it.h; ++yy)
                for (int xx = 0; xx < it.w; ++xx) {
                    int gxc = tx + xx, gyc = ty + yy;
                    if (gxc < 0 || gyc < 0 || gxc >= inv->cols || gyc >= inv->rows) continue;
                    fill(P.ox + gxc * (cs + gp), P.oy + gyc * (cs + gp), cs, cs, ok ? ui.dropOk : ui.dropBad, cr);
                }
        }
        int hovered = (!ui.dragInv && hovPanel == pi) ? inv->ItemAtCell(hovCx, hovCy) : -1;
        for (int i = 0; i < (int)inv->items.size(); ++i) {
            if (inv == ui.dragInv && i == ui.dragIndex) continue;
            const auto& it = inv->items[i];
            drawItem(ui, it, P.ox + it.x * (cs + gp), P.oy + it.y * (cs + gp), false, i == hovered);
        }
    }

    // Hover tooltip.
    if (ui.showTooltips && !ui.dragInv && hovPanel >= 0) {
        okay::GridInventory* inv = panels[hovPanel].inv;
        int hov = inv->ItemAtCell(hovCx, hovCy);
        if (hov >= 0) {
            const auto& it = inv->items[hov];
            char tip[96];
            std::snprintf(tip, sizeof(tip), "%s  %dx%d%s  %.1fkg", it.name.c_str(), it.w, it.h,
                          it.count > 1 ? ("  x" + std::to_string(it.count)).c_str() : "", it.weight * it.count);
            float px = 1.4f, tw = UITextW(tip, px), tpad = 6.0f;
            fill(mp.x + 14, mp.y + 14, tw + tpad * 2, Font8x8::Height * px + tpad * 2, ui.tooltipColor, 4.0f);
            DrawText(r, tip, mp.x + 14 + tpad, mp.y + 14 + tpad, px, sc(ui.tooltipText));
        }
    }
    // Rotate (applied only on a good drop), pick up, right-click split, drop / transfer / merge.
    okay::InventoryInput::HandleGrid(ui, GL, hov);
    // The lifted item floats under the cursor (with a soft drop shadow).
    if (ui.dragInv) {
        const okay::GridItem it = ui.HeldItem();
        float gx = mp.x - ui.grabX * (cs + gp) - cs * 0.5f, gy = mp.y - ui.grabY * (cs + gp) - cs * 0.5f;
        float gw = it.w * cs + (it.w - 1) * gp, gh = it.h * cs + (it.h - 1) * gp;
        shadow(gx, gy, gw, gh, cr);
        drawItem(ui, it, gx, gy, true, false);
    }
}

// Crafting bench panel: lists recipes, tints by affordability, crafts on click.
static void DrawCrafting(SDL_Renderer* r, okay::CraftingStation& cs) {
    if (!cs.open) return;
    okay::Inventory* inv = cs.Inv();
    int W = 0, H = 0; SDL_GetRendererOutputSize(r, &W, &H);
    if (W <= 0 || H <= 0) return;
    const float cr = cs.cornerRadius;
    auto sc = [&](const Color& c) { return SDL_Color{(Uint8)(c.r*255),(Uint8)(c.g*255),(Uint8)(c.b*255),255}; };
    auto fill = [&](float x, float y, float w, float h, const Color& c, float rad) {
        SDL_Rect rc2{(int)x, (int)y, (int)w, (int)h};
        FillUIShape(r, rc2, rad > 0.5f ? UIShape::Rounded : UIShape::Rectangle, rad, c, c, false, false, 1.0f);
    };
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
    // Layout + input are shared with the editor Play view (okay/Inventory/InventoryInput).
    const okay::InventoryInput::CraftingLayout L = okay::InventoryInput::LayoutCrafting(cs, (float)W, (float)H);
    const float px = L.px, py = L.py, pw = L.pw, ph = L.ph;
    const auto recipes = cs.Recipes();
    const bool busy = !cs.Queue().Jobs().empty();
    fill(px, py - 26, pw, ph + 26 + (busy ? 16.0f : 0.0f), cs.panelColor, cr + 2);
    fill(px, py - 26, pw, 24, cs.titleBar, cr + 2);
    DrawText(r, cs.title, px + 8, py - 20, 1.6f, sc(cs.textColor));
    auto label = [](const std::string& item) {
        const okay::ItemDefinition* d = okay::ItemDatabase::Get().Find(item);
        return d ? d->DisplayName() : item;
    };
    const int hover = okay::InventoryInput::HandleCrafting(cs, L, okay::Input::MousePosition());   // click: craft one, shift: max
    for (int i = 0; i < L.rows && i < (int)recipes.size(); ++i) {
        const auto& rec = recipes[i];
        const float ry = L.RowY(i);
        const bool can = inv && cs.CanCraft(rec, *inv);
        fill(px + 4, ry, pw - 8, cs.rowHeight - 2, can ? cs.canColor : cs.cantColor, cr);
        if (i == hover) fill(px + 4, ry, pw - 8, cs.rowHeight - 2, cs.hoverColor, cr);
        std::string out = label(rec.output) + (rec.outputCount > 1 ? " x" + std::to_string(rec.outputCount) : std::string());
        DrawText(r, out, px + 10, ry + 4, 1.4f, sc(cs.textColor));
        std::string ins;
        for (std::size_t k = 0; k < rec.inputs.size(); ++k) { if (k) ins += ", "; ins += std::to_string(rec.inputs[k].count) + " " + label(rec.inputs[k].item); }
        float tw = UITextW(ins, 1.0f);
        DrawText(r, ins, px + pw - 10 - tw, ry + 6, 1.0f, sc(cs.textColor));
    }
    if (busy) {   // the queue: progress of the item being made, and how many are waiting
        const float by = py + ph + 2.0f;
        fill(px + 6, by, pw - 12, 8, cs.cantColor, 3);
        fill(px + 6, by, (pw - 12) * cs.Queue().Progress(), 8, cs.canColor, 3);
        const int waiting = (int)cs.Queue().Jobs().size();
        if (waiting > 1) DrawText(r, "x" + std::to_string(waiting), px + pw - 30, by - 1, 1.0f, sc(cs.textColor));
    }
}

// A lootable chest: chest contents on top, your inventory below. Click a stack to
// move the whole thing to the other side (take loot / stash items).
static void DrawChest(SDL_Renderer* r, okay::ChestInventory& ch, const std::string& baseDir,
                      std::unordered_map<std::string, SDL_Texture*>& cache) {
    if (!ch.open) return;
    int W = 0, H = 0; SDL_GetRendererOutputSize(r, &W, &H); if (W <= 0 || H <= 0) return;
    // Layout + input are shared with the editor Play view (okay/Inventory/InventoryInput).
    const okay::InventoryInput::ChestLayout L = okay::InventoryInput::LayoutChest(ch, (float)W, (float)H);
    if (!L.chest) return;
    const float cr = ch.cornerRadius;
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
    if (ch.darkenWhenOpen) { SDL_SetRenderDrawColor(r, 0, 0, 0, 150); SDL_Rect f{0, 0, W, H}; SDL_RenderFillRect(r, &f); }
    auto sc = [&](const Color& c) { return SDL_Color{(Uint8)(c.r*255),(Uint8)(c.g*255),(Uint8)(c.b*255),255}; };
    auto fill = [&](float x, float y, float w, float h, const Color& c, float rad) {
        SDL_Rect rc2{(int)x, (int)y, (int)w, (int)h};
        FillUIShape(r, rc2, rad > 0.5f ? UIShape::Rounded : UIShape::Rectangle, rad, c, c, false, false, 1.0f);
    };
    okay::Vec2 mp = okay::Input::MousePosition();
    fill(L.panelX - 12, L.topY - 34, L.panelW + 24, L.totalH + 46, ch.panelColor, cr + 2);
    fill(L.panelX - 12, L.topY - 34, L.panelW + 24, 26, ch.titleBar, cr + 2);
    DrawText(r, ch.title, L.panelX, L.topY - 28, 1.6f, sc(ch.textColor));
    int hovSide = -1, hovIdx = -1;   // which grid + slot the cursor is over
    L.Pick(mp.x, mp.y, hovSide, hovIdx);
    auto stack = [&](const okay::Inventory::Slot& it, float x, float y, float sz) {
        SDL_Texture* icon = IconTexture(r, okay::ItemIconPath(ch.iconFolder, it.item), baseDir, cache);
        if (icon) { SDL_Rect d{(int)x+4, (int)y+4, (int)sz-8, (int)sz-8}; SDL_RenderCopy(r, icon, nullptr, &d); }
        else DrawText(r, it.item.substr(0, 5), x + 4, y + 5, 1.0f, sc(ch.textColor));
        if (it.count > 1) { std::string c = std::to_string(it.count); float px = 1.2f;
            float tw = UITextW(c, px);
            DrawText(r, c, x + sz - tw - 3, y + sz - (Font8x8::Height + 1) * px - 2, px, SDL_Color{255,255,255,255}); }
    };
    auto grid = [&](int side) {
        okay::Inventory* inv = L.Inv(side);
        const float sz = L.Size(side);
        for (int i = 0; i < L.Count(side); ++i) {
            float x, y; L.SlotXY(side, i, x, y);
            fill(x, y, sz, sz, ch.slotBorder, cr);
            fill(x + 2, y + 2, sz - 4, sz - 4, ch.slotColor, cr > 2 ? cr - 2 : 0);
            if (side == hovSide && i == hovIdx) fill(x, y, sz, sz, ch.hoverColor, cr);
            bool dragged = (side == ch.dragSide && i == ch.dragIndex);   // hide the lifted item
            if (!dragged && inv && i < (int)inv->slots.size() && !inv->slots[i].item.empty()) stack(inv->slots[i], x, y, sz);
        }
    };
    grid(0);
    DrawText(r, "Your inventory", L.playOx, L.topY + L.chestH + 6, 1.2f, sc(ch.textColor));
    if (L.player) grid(1);
    okay::InventoryInput::HandleChest(ch, L, mp);   // lift / drop (move, swap, merge)
    // The lifted item floats under the cursor, at its source grid's slot size.
    if (ch.dragSide >= 0) {
        const auto& it = L.Inv(ch.dragSide)->slots[ch.dragIndex];
        const float sz = L.Size(ch.dragSide), x = mp.x - sz * 0.5f, y = mp.y - sz * 0.5f;
        fill(x, y, sz, sz, ch.slotBorder, cr);
        fill(x + 2, y + 2, sz - 4, sz - 4, ch.slotColor, cr > 2 ? cr - 2 : 0);
        stack(it, x, y, sz);
    }
}

int main(int argc, char** argv) {
    SDL_SetMainReady();
    // Windows 11 / high-DPI: per-monitor-DPI-aware so the game renders at native
    // resolution instead of being bitmap-stretched (blurry) on scaled displays.
#ifdef SDL_HINT_WINDOWS_DPI_AWARENESS   // SDL >= 2.24; the Linux server build uses an older SDL
    SDL_SetHint(SDL_HINT_WINDOWS_DPI_AWARENESS, "permonitorv2");
#endif
    // `--server`: run headless as the game's dedicated server (no window, GPU or
    // audio). See okay/Net/DedicatedServer.hpp for the flags.
    for (int i = 1; i < argc; ++i)
        if (std::string(argv[i]) == "--help" || std::string(argv[i]) == "-h") {
            std::printf("Usage: %s [scene.okayscene] [--join LINK | --connect HOST:PORT [--password P] [--room R]]\n"
                        "       %s --server [options]\n\n"
                        "Joining (what the OkaySpace launcher passes):\n"
                        "  --join LINK          join the session in an okay://join link\n"
                        "  --connect HOST:PORT  join a server directly\n"
                        "  --password P         its password\n"
                        "  --room R             its lobby room\n"
                        "  --find-match [MODE]  matchmaking: get put in a match (--match-players N, --match-min N, --region R)\n\n"
                        "Account (for headless servers):\n"
                        "  --login NAME         sign this machine in (password from OKAY_LOGIN_PASSWORD or typed)\n"
                        "  --logout             sign out\n\n%s", argv[0], argv[0], okay::DedicatedServerUsage());
            return 0;
        }
    // `--login NAME` / `--logout`: sign this machine in to OkaySpace without the
    // launcher (a headless server needs it to list itself with --advertise).
    // The password comes from OKAY_LOGIN_PASSWORD or is typed (not echoed).
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        if (a == "--logout") { okay::Account::Logout(); std::printf("Signed out.\n"); return 0; }
        if (a != "--login") continue;
        if (i + 1 >= argc) { std::fprintf(stderr, "--login needs your OkaySpace username\n"); return 1; }
        const std::string user = argv[i + 1];
        std::string pass;
        if (const char* e = std::getenv("OKAY_LOGIN_PASSWORD")) pass = e;
        else {
            std::printf("Password for %s: ", user.c_str()); std::fflush(stdout);
#if defined(_WIN32)
            HANDLE hin = GetStdHandle(STD_INPUT_HANDLE); DWORD mode = 0;
            const bool tty = GetConsoleMode(hin, &mode) != 0;
            if (tty) SetConsoleMode(hin, mode & ~ENABLE_ECHO_INPUT);
#else
            termios old{}; const bool tty = tcgetattr(0, &old) == 0;
            if (tty) { termios t = old; t.c_lflag &= ~ECHO; tcsetattr(0, TCSANOW, &t); }
#endif
            std::getline(std::cin, pass);
#if defined(_WIN32)
            if (tty) SetConsoleMode(hin, mode);
#else
            if (tty) tcsetattr(0, TCSANOW, &old);
#endif
            std::printf("\n");
        }
        const okay::account::Result r = okay::Account::Login(user, pass);
        if (!r.ok) { std::fprintf(stderr, "Sign-in failed: %s\n", r.error.c_str()); return 1; }
        std::printf("Signed in as %s. Servers on this machine can now use --advertise.\n", okay::Account::Username().c_str());
        return 0;
    }
    okay::DedicatedServerOptions serverOpts;
    std::string serverArgErr;
    const bool dedicatedServer = okay::ParseDedicatedServerArgs(argc, argv, serverOpts, &serverArgErr);
    if (!serverArgErr.empty()) { std::fprintf(stderr, "%s\n\n%s", serverArgErr.c_str(), okay::DedicatedServerUsage()); return 1; }
    // --log FILE: send stdout + stderr (the engine log, [server] lines, script
    // prints) to a file. The Windows player has no console, so without this a
    // server's output is lost. Unbuffered so a crash still leaves the last lines.
    if (dedicatedServer && !serverOpts.logPath.empty()) {
        // Start it empty, then open both streams for appending, so each line lands
        // at the end whichever stream wrote it.
        if (std::FILE* f = std::fopen(serverOpts.logPath.c_str(), "w")) std::fclose(f);
        if (!std::freopen(serverOpts.logPath.c_str(), "a", stdout) ||
            !std::freopen(serverOpts.logPath.c_str(), "a", stderr)) {
            std::fprintf(stderr, "Couldn't open the log file %s\n", serverOpts.logPath.c_str());
            return 1;
        }
        std::setvbuf(stdout, nullptr, _IONBF, 0);
        std::setvbuf(stderr, nullptr, _IONBF, 0);
    }
    // `--join LINK` / `--connect HOST:PORT`: the launcher started us to join a
    // friend, an invite or a server from the browser (Steam's "+connect").
    std::string launchJoin;
    std::string launchFindMatch;   // --find-match [MODE] [--match-players N]: start matchmaking once the game is up
    if (!dedicatedServer) {
        okay::JoinLink cl; bool direct = false;
        for (int i = 1; i + 1 < argc; ++i) {
            const std::string a = argv[i];
            if (a == "--join") launchJoin = argv[++i];
            else if (a == "--connect") { cl.addr = argv[++i]; direct = true; }
            else if (a == "--password") cl.password = argv[++i];
            else if (a == "--room") cl.room = argv[++i];
        }
        std::string fmMode, fmPlayers = "2", fmMin, fmRegion; bool findMatch = false;
        for (int i = 1; i < argc; ++i) {
            const std::string a = argv[i];
            if (a == "--find-match") { findMatch = true; if (i + 1 < argc && argv[i + 1][0] != '-') fmMode = argv[++i]; }
            else if (a == "--match-players" && i + 1 < argc) fmPlayers = argv[++i];
            else if (a == "--match-min" && i + 1 < argc) fmMin = argv[++i];
            else if (a == "--region" && i + 1 < argc) fmRegion = argv[++i];
        }
        if (findMatch) launchFindMatch = "okay://match?mode=" + fmMode + "&players=" + fmPlayers + "&min=" + fmMin + "&region=" + fmRegion;
        okay::JoinLink jl;
        if (!launchJoin.empty() && okay::JoinLink::Parse(launchJoin, jl)) {
            if (!cl.password.empty()) { jl.password = cl.password; launchJoin = jl.Format(); }
        } else if (direct) { cl.game = "game"; launchJoin = cl.Format(); }
        else launchJoin.clear();
    }
    if (dedicatedServer) {
        if (SDL_Init(SDL_INIT_TIMER) != 0) { std::fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError()); return 1; }
    } else
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER | SDL_INIT_AUDIO | SDL_INIT_GAMECONTROLLER) != 0) {
        SDL_Log("SDL_Init failed: %s", SDL_GetError());
        return 1;
    }

    // Resolve where the game files live. New builds use a clean layout
    // (exe + DLLs at the root, scenes/config/assets in a Data/ subfolder); prefer
    // that when present, else fall back to the flat layout (everything beside exe).
    std::string baseDir;
    { char* base = SDL_GetBasePath(); baseDir = base ? base : ""; if (base) SDL_free(base); }
    {
        std::ifstream probe(baseDir + "Data/game.okayconfig");
        if (probe.good()) baseDir += "Data/";
    }

    // Build settings (written by the editor's Build Settings) live in an optional
    // game.okayconfig beside the exe: window size/title/flags, the scene list (so
    // load_scene_index works) and which scene starts. Sensible defaults if absent.
    struct GameConfig {
        std::string title = "OkaySpace Game";
        int  width = 960, height = 600;
        bool fullscreen = false, borderless = false, resizable = true, vsync = true;
        bool smoothDt = true;   // even frame steps (Time::SmoothDelta) for movement and cameras
        bool showCursor = true, quitOnEscape = true, showFps = false;
        int  fpsCap = 0;
        float volume = 1.0f;
        bool lockCursor = false, perPixel = false, shadows = true, bloom = false, ssao = false, fxaa = true;
        float shadowDistance = 80.0f, shadowSoftness = 2.5f;
        int  shadowCascades = 3, shadowResolution = 2048;
        int  antialias = 1;
        bool gpu = true;   // try the GPU (D3D11/OpenGL) 3D renderer; fall back to software
        bool d3d12 = false; // opt-in: prefer the Direct3D 12 backend (Windows) over D3D11
        std::string startup;
        std::vector<std::string> scenes;
        // Presentation.
        std::string icon;          // window icon PNG (in the Data folder)
        bool runInBackground = true;
        bool fullscreenToggle = true;    // Alt+Enter / F11 switches fullscreen
        bool muteOnFocusLoss = false;
        bool startMaximized = false;
        int  minWidth = 0, minHeight = 0;
        std::string company = "OkaySpace";
        bool saveToUserDir = true; // saves -> per-user app folder (persistentDataPath)
        std::string splash;        // startup logo PNG (in the Data folder)
        float splashTime = 2.0f;   // seconds the splash shows
        int   splashBg[3] = {12, 12, 16};  // splash background colour
        // Boot splash — the project's branded loading frame, shown immediately on
        // launch (before the renderer/scene are up) so the game never flashes a
        // black window and matches the editor/launcher loading screen.
        std::string bootLogo;      // optional PNG (in the Data folder)
        float bootBgTop[3] = {0.094f, 0.094f, 0.106f};
        float bootBgBot[3] = {0.051f, 0.051f, 0.059f};
        bool  bootAccentOn = false;
        float bootAccent[3] = {0.235f, 0.475f, 0.800f};
        bool  bootBar = true;
        std::string antiCheat;     // "" / off, report, enforce (docs/anticheat.md)
    } cfg;
    {
        // Read the whole config and transparently decode it if it was obfuscated at
        // build time (DataPack), so encrypted builds load their settings too.
        std::ifstream cfRaw(baseDir + "game.okayconfig", std::ios::binary);
        std::stringstream cfBuf; cfBuf << cfRaw.rdbuf();
        std::istringstream cf(okay::DataPack::Unpack(cfBuf.str()));
        std::string line;
        while (std::getline(cf, line)) {
            if (!line.empty() && line.back() == '\r') line.pop_back();
            std::size_t eq = line.find('=');
            if (eq == std::string::npos) continue;
            std::string k = line.substr(0, eq), v = line.substr(eq + 1);
            if      (k == "title")      cfg.title = v;
            else if (k == "width")      cfg.width = std::atoi(v.c_str());
            else if (k == "height")     cfg.height = std::atoi(v.c_str());
            else if (k == "fullscreen") cfg.fullscreen = std::atoi(v.c_str()) != 0;
            else if (k == "borderless") cfg.borderless = std::atoi(v.c_str()) != 0;
            else if (k == "resizable")  cfg.resizable = std::atoi(v.c_str()) != 0;
            else if (k == "vsync")      cfg.vsync = std::atoi(v.c_str()) != 0;
            else if (k == "smooth_dt")  cfg.smoothDt = std::atoi(v.c_str()) != 0;
            else if (k == "cursor")     cfg.showCursor = std::atoi(v.c_str()) != 0;
            else if (k == "fps_cap")    cfg.fpsCap = std::atoi(v.c_str());
            else if (k == "quit_on_escape") cfg.quitOnEscape = std::atoi(v.c_str()) != 0;
            else if (k == "volume")     cfg.volume = (float)std::atof(v.c_str());
            else if (k == "show_fps")   cfg.showFps = std::atoi(v.c_str()) != 0;
            else if (k == "lock_cursor") cfg.lockCursor = std::atoi(v.c_str()) != 0;
            else if (k == "perpixel")   cfg.perPixel = std::atoi(v.c_str()) != 0;
            else if (k == "shadows")    cfg.shadows = std::atoi(v.c_str()) != 0;
            else if (k == "shadow_distance")   cfg.shadowDistance = (float)std::atof(v.c_str());
            else if (k == "shadow_softness")   cfg.shadowSoftness = (float)std::atof(v.c_str());
            else if (k == "shadow_cascades")   cfg.shadowCascades = std::atoi(v.c_str());
            else if (k == "shadow_resolution") cfg.shadowResolution = std::atoi(v.c_str());
            else if (k == "bloom")      cfg.bloom = std::atoi(v.c_str()) != 0;
            else if (k == "ssao")       cfg.ssao = std::atoi(v.c_str()) != 0;
            else if (k == "fxaa")       cfg.fxaa = std::atoi(v.c_str()) != 0;
            else if (k == "antialias")  cfg.antialias = std::atoi(v.c_str());
            else if (k == "gpu")        cfg.gpu = std::atoi(v.c_str()) != 0;
            else if (k == "d3d12")      cfg.d3d12 = std::atoi(v.c_str()) != 0;
            else if (k == "startup")    cfg.startup = v;
            else if (k == "scene")      cfg.scenes.push_back(v);
            else if (k == "icon")           cfg.icon = v;
            else if (k == "run_background") cfg.runInBackground = std::atoi(v.c_str()) != 0;
            else if (k == "fullscreen_toggle") cfg.fullscreenToggle = std::atoi(v.c_str()) != 0;
            else if (k == "mute_unfocused") cfg.muteOnFocusLoss = std::atoi(v.c_str()) != 0;
            else if (k == "start_maximized")cfg.startMaximized = std::atoi(v.c_str()) != 0;
            else if (k == "min_width")      cfg.minWidth = std::atoi(v.c_str());
            else if (k == "min_height")     cfg.minHeight = std::atoi(v.c_str());
            else if (k == "company")        cfg.company = v;
            else if (k == "save_userdir")   cfg.saveToUserDir = std::atoi(v.c_str()) != 0;
            else if (k == "splash")         cfg.splash = v;
            else if (k == "splash_time")    cfg.splashTime = (float)std::atof(v.c_str());
            else if (k == "splash_bg")      std::sscanf(v.c_str(), "%d,%d,%d", &cfg.splashBg[0], &cfg.splashBg[1], &cfg.splashBg[2]);
            else if (k == "boot_logo")      cfg.bootLogo = v;
            else if (k == "boot_bg_top")    std::sscanf(v.c_str(), "%f %f %f", &cfg.bootBgTop[0], &cfg.bootBgTop[1], &cfg.bootBgTop[2]);
            else if (k == "boot_bg_bot")    std::sscanf(v.c_str(), "%f %f %f", &cfg.bootBgBot[0], &cfg.bootBgBot[1], &cfg.bootBgBot[2]);
            else if (k == "boot_accent")  { std::sscanf(v.c_str(), "%f %f %f", &cfg.bootAccent[0], &cfg.bootAccent[1], &cfg.bootAccent[2]); cfg.bootAccentOn = true; }
            else if (k == "boot_bar")       cfg.bootBar = std::atoi(v.c_str()) != 0;
            else if (k == "anticheat")      cfg.antiCheat = v;
        }
    }
    // Register the build's scenes so scripts can load_scene_index / load_next.
    SceneManager::ClearScenes();
    for (const std::string& s : cfg.scenes) SceneManager::AddScene(baseDir + s);

    // Resolve the scene path: CLI arg > config startup > game.okayscene.
    std::string scenePath;
    if (dedicatedServer && !serverOpts.scenePath.empty()) {   // --scene: as given, else beside the game
        scenePath = serverOpts.scenePath;
        if (!std::ifstream(scenePath).good()) scenePath = baseDir + serverOpts.scenePath;
    }
    else if (!dedicatedServer && argc > 1 && argv[1][0] != '-') scenePath = argv[1];
    else if (!cfg.startup.empty()) scenePath = baseDir + cfg.startup;
    else scenePath = baseDir + "game.okayscene";

    // Where the game WRITES saves: a per-user, writable
    // app folder when enabled — so saving works even from a read-only install (e.g.
    // Program Files) and two games never clash. Falls back to beside-the-exe.
    std::string saveDir = baseDir;
    if (cfg.saveToUserDir) {
        const char* org = cfg.company.empty() ? "OkaySpace" : cfg.company.c_str();
        const char* app = cfg.title.empty() ? "Game" : cfg.title.c_str();
        if (char* pref = SDL_GetPrefPath(org, app)) { saveDir = pref; SDL_free(pref); }
    }
    okay::Save::SetBaseDir(saveDir);  // relative SaveData files (save.okaysave, ...) land here

    // Persistent prefs (high scores, settings) live in the save folder.
    std::string prefsPath = saveDir + "game.okayprefs";
    Prefs::Load(prefsPath);
    DataAsset::SetBaseDir(baseDir);   // read-only .okaydata assets stay beside the game
    Character::SetAssetBaseDir(baseDir);   // a Character's relative clips file, too
    ItemDatabase::Get().Load(baseDir + "items.okayitems");   // the project's item definitions (shipped by Build Game)

    // Anti-cheat (okayac::DecideStart). Without trusted keys built in: off unless
    // the build (anticheat=) or OKAY_ANTICHEAT turns it on (either only makes it
    // stricter); then the files are checked against Data/manifest.json before
    // anything loads; report logs, enforce refuses. With trusted keys built in
    // (-DOKAY_AC_TRUSTED_KEYS): the game needs a valid trusted manifest to start
    // at all, and its signed mode can't be lowered by editing game.okayconfig.
    // The runtime checks run in the frame loop. (Dedicated servers: not checked.)
    std::string acRoot;
    { char* b = SDL_GetBasePath(); acRoot = b ? b : ""; if (b) SDL_free(b); }
    auto acLog = [&saveDir](const std::string& line) {
        SDL_Log("%s", line.c_str());
        std::ofstream(saveDir + "anticheat.log", std::ios::app) << line << "\n";
    };
    okayac::StartDecision acStart;
    // What anti-cheat found goes to the backend (docs/sql/anticheat.sql) as one
    // client_check report per run, when the player is signed in.
    bool acReported = false;
    auto acFileReport = [&](const std::string& check, const std::string& detail) {
        if (acReported || (acStart.mode == okayac::Mode::Off && !acStart.result.ran) || !okay::Account::IsLoggedIn()) return;
        acReported = true;
        okayac::backend::Request q;
        if (okayac::backend::FileClientCheck(cfg.title, check, detail, okayac::MachineHash(), q))
            okay::Account::Api(q.path, q.method, q.body, q.header);
    };
    if (!dedicatedServer) {
        const char* acEnv = std::getenv("OKAY_ANTICHEAT");
        const std::string acScene = argc > 1 && argv[1][0] != '-' ? std::string(argv[1]) : std::string();
        const okayac::TrustedKeySet acTrusted = okayac::TrustedKeys();
        acStart = okayac::DecideStart(std::filesystem::u8path(acRoot), okayac::ParseMode(cfg.antiCheat),
                                      okayac::ParseMode(acEnv ? acEnv : ""), acTrusted, acScene);
        if (acStart.result.ran) {
            acLog("[anti-cheat] " + std::string(okayac::ModeName(acStart.mode)) + ": " + acStart.result.Summary());
            for (const std::string& n : acStart.result.notes) acLog("[anti-cheat] note: " + n);
            for (const std::string& p : acStart.result.problems) acLog("[anti-cheat] problem: " + p);
            if (!acStart.result.ok) {
                std::string all;
                for (const std::string& p : acStart.result.problems) all += p + "\n";
                acFileReport("integrity", all);
            }
        }
        if (!acStart.allow) {
            SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, cfg.title.c_str(), acStart.message.c_str(), nullptr);
            SDL_Quit();
            return 1;
        }
    }
    const okayac::Mode acMode = acStart.mode;
    okayac::Integrity& acManifest = acStart.manifest;
    const bool acHaveManifest = !acManifest.files.empty();

    Scene scene("Game");
    std::string err;
    if (!SceneSerializer::LoadFromFile(scene, scenePath, &err)) {
        SDL_Log("Could not load %s: %s", scenePath.c_str(), err.c_str());
        // Keep running with an empty scene rather than failing outright.
    }
    if (dedicatedServer) {   // headless: run the scene as the server until stopped
        std::printf("[server] scene %s\n", scenePath.c_str());
        if (serverOpts.game.empty())   // what the server browser shows as the game
            serverOpts.game = !cfg.title.empty() && cfg.title != "OkaySpace Game" ? cfg.title : scene.Name();
        const int rc = okay::RunDedicatedServer(scene, serverOpts);
        Prefs::Save(prefsPath);
        SDL_Quit();
        return rc;
    }
    {   // A scene without a camera would render nothing: add one.
        bool made = false;
        okay::EnsureSceneCamera(scene, okay::SceneLooks2D(scene), &made);
        if (made) SDL_Log("Scene has no camera; added a Main Camera.");
    }
    // Multiplayer: a signed-in OkaySpace player presents their account when joining
    // (servers started with --auth only let signed-in accounts in).
    for (const auto& up : scene.Objects())
        if (auto* nm = up ? up->GetComponent<okay::NetworkManager>() : nullptr)
            if (nm->AuthToken().empty() && okay::Account::IsLoggedIn()) nm->SetAuthToken(okay::Account::Token());
    if (okay::Account::IsLoggedIn())   // also for networks scripts create later
        okay::NetworkManager::SetDefaultIdentity(okay::Account::Token(), okay::Account::Username());
    if (!launchJoin.empty()) {
        SDL_Log("Joining %s", launchJoin.c_str());
        okay::NetworkManager::SetLaunchJoin(launchJoin);
    }

    Uint32 winFlags = SDL_WINDOW_ALLOW_HIGHDPI;
    if (cfg.resizable)  winFlags |= SDL_WINDOW_RESIZABLE;
    if (cfg.fullscreen) winFlags |= SDL_WINDOW_FULLSCREEN_DESKTOP;
    if (cfg.borderless) winFlags |= SDL_WINDOW_BORDERLESS;
    if (cfg.startMaximized && cfg.resizable && !cfg.fullscreen) winFlags |= SDL_WINDOW_MAXIMIZED;
#if !defined(__EMSCRIPTEN__)
    // Create hidden so the game never flashes a black window during renderer /
    // GPU setup; we draw the boot splash then reveal it (see below).
    if (!cfg.fullscreen) winFlags |= SDL_WINDOW_HIDDEN;
#endif
    if (!cfg.showCursor) SDL_ShowCursor(SDL_DISABLE);
    std::string title = !cfg.title.empty() ? cfg.title : scene.Name();
    SDL_Window* window = SDL_CreateWindow(
        title.c_str(), SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        cfg.width, cfg.height, winFlags);
    if (window && cfg.minWidth > 0 && cfg.minHeight > 0)
        SDL_SetWindowMinimumSize(window, cfg.minWidth, cfg.minHeight);
    // Window/taskbar icon: load the PNG and apply it.
    if (window && !cfg.icon.empty()) {
        okay::Image ico;
        if (ico.Load(cfg.icon) || ico.Load(baseDir + cfg.icon)) {
            SDL_Surface* surf = SDL_CreateRGBSurfaceWithFormatFrom(
                (void*)ico.Data(), ico.Width(), ico.Height(), 32, ico.Width() * 4, SDL_PIXELFORMAT_ABGR8888);
            if (surf) { SDL_SetWindowIcon(window, surf); SDL_FreeSurface(surf); }
        }
    }
    Uint32 renFlags = SDL_RENDERER_ACCELERATED | (cfg.vsync ? SDL_RENDERER_PRESENTVSYNC : 0);
    SDL_Renderer* renderer = SDL_CreateRenderer(window, -1, renFlags);
    if (!renderer) renderer = SDL_CreateRenderer(window, -1, 0);

    // ---- Boot splash: the project's branded loading frame, shown immediately so
    // the game doesn't flash a black window while the renderer / GPU / scene set
    // up. Matches the editor/launcher loading screen. Reveal the window on it.
    SDL_Texture* bootTex = nullptr;
    if (renderer && window) {
        okay::SplashStyle st;
        if (cfg.bootAccentOn) { st.ar = cfg.bootAccent[0]; st.ag = cfg.bootAccent[1]; st.ab = cfg.bootAccent[2]; }
        auto C = [](const float* c) -> SDL_Color {
            auto u = [](float v){ return (Uint8)((v < 0 ? 0 : v > 1 ? 1 : v) * 255.0f + 0.5f); };
            return SDL_Color{u(c[0]), u(c[1]), u(c[2]), 255}; };
        st.bgTop = C(cfg.bootBgTop); st.bgBottom = C(cfg.bootBgBot); st.showProgress = cfg.bootBar;
        if (!cfg.bootLogo.empty()) {
            okay::Image img;
            if (img.Load(cfg.bootLogo) || img.Load(baseDir + cfg.bootLogo)) {
                if (SDL_Surface* s = SDL_CreateRGBSurfaceWithFormatFrom(
                        (void*)img.Data(), img.Width(), img.Height(), 32, img.Width() * 4, SDL_PIXELFORMAT_ABGR8888)) {
                    bootTex = SDL_CreateTextureFromSurface(renderer, s);
                    SDL_FreeSurface(s);
                }
            }
        }
        st.logo = bootTex;
        int bw = cfg.width, bh = cfg.height; SDL_GetRendererOutputSize(renderer, &bw, &bh);
        okay::DrawSplashStyled(renderer, bw, bh, st);
#if !defined(__EMSCRIPTEN__)
        SDL_ShowWindow(window); SDL_RaiseWindow(window);
        okay::DrawSplashStyled(renderer, bw, bh, st);
#endif
    }

    // Apply the build's graphics/quality settings to the renderer + cursor.
    PerPixelLighting() = cfg.perPixel;
    ShadowsEnabled()   = cfg.shadows;
    ShadowDistance()   = cfg.shadowDistance;
    ShadowSoftness()   = cfg.shadowSoftness;
    ShadowCascades()   = cfg.shadowCascades;
    ShadowMapResolution() = cfg.shadowResolution;
    BloomEnabled()     = cfg.bloom;
    SSAOEnabled()      = cfg.ssao;
    FXAAEnabled()      = cfg.fxaa;
    if (cfg.lockCursor) Cursor::Capture(true);
    SDL_StartTextInput();   // deliver SDL_TEXTINPUT events for UI input fields

    // Optional GPU 3D renderer — the same hardware path the editor's Scene view uses,
    // now shipped with the game. Picks the best backend for this machine (Direct3D 11
    // on Windows, OpenGL elsewhere); each renders the scene to an offscreen target and
    // reads back RGBA8, a drop-in for the software rasterizer. If a GPU backend can't
    // be created (or a frame fails repeatedly), the player silently uses software, so
    // a build never fails to display 3D. Toggle in Build Game > GPU renderer.
    okay::GLRenderer*    glRenderer = nullptr;   // OpenGL backend (any platform)
#if defined(_WIN32)
    okay::D3D11Renderer* d3dRenderer = nullptr;  // Direct3D 11 backend (Windows)
    okay::D3D12Renderer* d3d12Renderer = nullptr;// Direct3D 12 backend (Windows, opt-in)
#endif
    SDL_Window*          glWindow = nullptr;      // hidden context window for the GL path
    SDL_GLContext        glCtx = nullptr;
    bool                 glReady = false, d3dReady = false, d3d12Ready = false;
#ifndef __EMSCRIPTEN__
    if (cfg.gpu) {
#if defined(_WIN32)
        // Opt-in Direct3D 12 backend, preferred when enabled; otherwise D3D11.
        if (cfg.d3d12) {
            d3d12Renderer = new okay::D3D12Renderer();
            if (d3d12Renderer->Init()) d3d12Ready = true;
            else { delete d3d12Renderer; d3d12Renderer = nullptr; }
        }
        d3dRenderer = new okay::D3D11Renderer();
        if (d3dRenderer->Init()) d3dReady = true;
        else { delete d3dRenderer; d3dRenderer = nullptr; }
#endif
        // A 3.2 compatibility context: core FBOs/MSAA + our #version 120 shaders.
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 2);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_COMPATIBILITY);
        SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
        glWindow = SDL_CreateWindow("okay-gl", 0, 0, 16, 16, SDL_WINDOW_OPENGL | SDL_WINDOW_HIDDEN);
        if (glWindow) {
            glCtx = SDL_GL_CreateContext(glWindow);
            if (glCtx) {
                SDL_GL_MakeCurrent(glWindow, glCtx);
                if (okay::GLRenderer::LoadGL((okay::GLRenderer::GLGetProc)SDL_GL_GetProcAddress)) {
                    glRenderer = new okay::GLRenderer();
                    glReady = true;
                }
                SDL_GL_MakeCurrent(nullptr, nullptr);
            }
        }
    }
#endif
    // Reused scratch buffer for GPU downscale/readback (mirrors the editor's path).
    std::vector<std::uint32_t> mesh3DGpu;
    int gpuFails = 0;   // self-heal: after repeated GPU failures, fall back for good

    SDL_AudioSpec want{}, have{};
    want.freq = 44100; want.format = AUDIO_F32SYS; want.channels = 1; want.samples = 1024;
    SDL_AudioDeviceID audioDev = SDL_OpenAudioDevice(nullptr, 0, &want, &have, 0);
    if (audioDev) SDL_PauseAudioDevice(audioDev, 0);
    AudioMixer::masterVolume = cfg.volume;   // global mix scale from build settings

    // baseDir (resolved above) is where the game files live, for relative paths.
    std::unordered_map<std::string, SDL_Texture*> textureCache;
    // The UI pass draws through one batcher for the whole run: it keeps the UI atlas
    // (fonts + images) and its vertex buffers between frames.
    ui2d::SDLPainter uiPainter(renderer, &textureCache, baseDir);
    ui2d::BatchPainter uiBatch(uiPainter, [&baseDir](const std::string& path, Image& out) {
        return out.Load(path) || out.Load(baseDir + path);
    });
    // Z-buffered 3D: meshes are rasterized into this texture each frame so
    // overlapping faces occlude correctly, then blitted under the 2D/UI layers.
    Raster mesh3D;
    SDL_Texture* mesh3DTex = nullptr;
    int mesh3DW = 0, mesh3DH = 0;

    // Load any WAV clips referenced by AudioSources, resampled to the mix rate.
    for (const auto& up : scene.Objects()) {
        auto* au = up->GetComponent<AudioSource>();
        if (!au || au->clipPath.empty()) continue;
        AudioClip wav;
        if (wav.LoadWAV(au->clipPath) || wav.LoadWAV(baseDir + au->clipPath))
            au->clip = wav.Resampled(44100);
    }

    // Load any .OBJ models referenced by MeshRenderers (resolve next to the exe).
    for (const auto& up : scene.Objects()) {
        auto* mr = up->GetComponent<MeshRenderer>();
        if (!mr || mr->meshPath.empty()) continue;
        bool ok = false;
        Mesh m = Mesh::LoadOBJ(mr->meshPath, &ok);
        if (!ok || m.vertices.empty()) m = Mesh::LoadOBJ(baseDir + mr->meshPath, &ok);
        if (ok && !m.vertices.empty()) mr->mesh = m;
    }

    okay::Game::Reset();   // clear any stale pause/quit state from a prior scene
    scene.Start();

    // Open the first connected game controller, if any.
    SDL_GameController* pad = nullptr;
    for (int i = 0; i < SDL_NumJoysticks(); ++i)
        if (SDL_IsGameController(i)) { pad = SDL_GameControllerOpen(i); break; }

    bool running = true;
#ifdef OKAY_HAVE_OKAYUI
    bool g_testUI = false;            // F1 toggles the OkayUI "Test UI" overlay
    if (std::getenv("OKAY_TESTUI")) g_testUI = true;   // dev screenshot hook
    // Safe-area insets (fractions of the screen: "left,top,right,bottom") for UI Safe
    // Area containers, from the platform / a launcher, or to test a phone layout.
    if (const char* si = std::getenv("OKAY_SAFE_INSETS")) {
        auto& f = okay::UISafeInsets();
        std::sscanf(si, "%f,%f,%f,%f", &f.left, &f.top, &f.right, &f.bottom);
    }
    bool g_statsUI = false;           // F3 toggles the OkayUI performance HUD
    char g_uiText[32] = {0}; bool g_uiBack = false;
    float g_uiWheel = 0.0f;           // mouse-wheel delta routed to OkayUI this frame
    // Let game scripts draw OkayUI via the ui_* builtins.
    static okay::OkayUIScriptBridge g_uiBridge;
    okay::SetScriptUI(&g_uiBridge);
#endif
    // Rich presence + the launcher link (okay/Platform/GameIpc.hpp): once a second,
    // tell the launcher what we're doing and how friends can join; pick up joins
    // it asks for (an invite accepted while we run). Shift+Tab asks it to open the
    // invite-friends window, like the Steam overlay.
#if !defined(__EMSCRIPTEN__)
    const std::string lanAddr = okay::LocalNetworkAddress();
    const std::string gameTitle = !cfg.title.empty() && cfg.title != "OkaySpace Game" ? cfg.title : scene.Name();
    okay::ServerBrowser::Shared().SetGame(gameTitle);   // the server browser lists this game's servers
    if (std::getenv("OKAY_BROWSER_DEMO")) okay::ServerBrowser::Shared().windowOpen = true;   // dev screenshot hook
    if (std::getenv("OKAY_MM_DEMO")) {   // dev screenshot hook: a queue that keeps searching
        struct DemoQueue : okay::Matchmaker::Backend {
            bool Join(const okay::MatchRequest&, std::string& t, std::string&) override { t = "demo"; return true; }
            bool Poll(const std::string&, okay::Matchmaker::PollResult& o, std::string&) override { o.status = "searching"; o.searching = 7; return true; }
            void Leave(const std::string&) override {}
            bool HostReady(const std::string&, const std::string&) override { return true; }
        };
        okay::Matchmaker::Shared().SetBackend(std::make_shared<DemoQueue>());
        okay::MatchRequest r; r.mode = "ranked"; r.players = 4;
        okay::Matchmaker::Shared().Start(r);
    }
    Uint32 presenceAt = 0, presenceWritten = 0;
    std::string presenceLast;
    std::map<std::string, Uint32> playedWith;   // who we've logged this run (key -> when)
    auto pumpPresence = [&]() {
        const Uint32 now = SDL_GetTicks();
        if (now - presenceAt < 1000) return;
        presenceAt = now;
        okay::NetworkManager* nm = scene.FindObjectOfType<okay::NetworkManager>();
        if (nm) {
            if (nm->gameId.empty()) nm->gameId = gameTitle;
            if (nm->mapName.empty()) nm->mapName = scene.Name();
        }
        std::string link;
        if (okay::GameIpc::TakeJoin(link)) {   // the launcher: "join this now" (or "find me a match")
            okay::MatchRequest mr;
            if (ParseMatchLink(link, mr)) { SDL_Log("Launcher asked to find a match (%d players)", mr.players); okay::Matchmaker::Shared().Start(mr); }
            else {
                SDL_Log("Launcher asked to join %s", link.c_str());
                if (nm) nm->JoinByLink(link); else okay::NetworkManager::SetLaunchJoin(link);
            }
        }
        if (!launchFindMatch.empty() && now > 1500) {   // --find-match: start once the game is up
            okay::MatchRequest mr;
            if (ParseMatchLink(launchFindMatch, mr)) okay::Matchmaker::Shared().Start(mr);
            launchFindMatch.clear();
        }
        // A launch join no NetworkManager picked up on Start (one a script made later).
        if (nm && now > 1500) {
            const std::string pending = okay::NetworkManager::TakeLaunchJoin();
            if (!pending.empty()) nm->JoinByLink(pending);
        }
        // "Recently played with": everyone who's been in my session for 30s+.
        if (nm && (nm->IsServer() || nm->Joined())) {
            const std::uint32_t me = nm->IsServer() ? 0u : nm->LocalId();
            std::vector<okay::PlayedWith> met;
            for (const auto& e : nm->Roster()) {
                if (e.id == me || e.seconds < 30.0f || (e.userId.empty() && e.name.empty())) continue;
                okay::PlayedWith p; p.userId = e.userId; p.name = e.name; p.game = gameTitle;
                const std::string k = p.Key();
                if (playedWith.count(k) && now - playedWith[k] < 300000) continue;   // refresh every 5 min
                playedWith[k] = now;
                met.push_back(p);
            }
            if (!met.empty()) okay::GameIpc::RecordPlayedWith(met);
        }
        okay::GamePresence g = okay::DescribeSession(nm, gameTitle, lanAddr);
        const std::string key = g.status + "|" + g.join + "|" + std::to_string(g.players);
        if (key != presenceLast || now - presenceWritten > 10000) {
            okay::GameIpc::WritePresence(g);
            presenceLast = key; presenceWritten = now;
        }
    };
#endif
    Uint64 last = SDL_GetPerformanceCounter();
    bool windowFocused = true;   // Run In Background / mute-on-focus-loss
    bool cursorReleased = false; // Esc / focus loss freed a game-locked cursor; a click re-locks it
    // Debugger / injected-module / speedhack checks (Windows; idle elsewhere and when off).
    okayac::RuntimeGuard acGuard(acMode, okayac::SystemGuardProbes(acRoot, acHaveManifest ? &acManifest : nullptr), acLog);
    auto frame = [&]() {
        Uint64 fStart = SDL_GetPerformanceCounter();
        Input::ClearTypedText();                 // collect this frame's typed chars
#ifdef OKAY_HAVE_OKAYUI
        g_uiText[0] = '\0'; g_uiBack = false; g_uiWheel = 0.0f;
#endif
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_QUIT) running = false;
#ifdef OKAY_HAVE_OKAYUI
            if (e.type == SDL_KEYDOWN && e.key.keysym.scancode == SDL_SCANCODE_F1) g_testUI = !g_testUI;
            if (e.type == SDL_KEYDOWN && e.key.keysym.scancode == SDL_SCANCODE_F3) g_statsUI = !g_statsUI;
            if (e.type == SDL_MOUSEWHEEL) g_uiWheel += (float)e.wheel.y;   // for OkayUI scrolling
            if (g_testUI || okay::ServerBrowser::Shared().windowOpen) {   // OkayUI text fields need the typing
                if (e.type == SDL_TEXTINPUT) SDL_strlcat(g_uiText, e.text.text, sizeof(g_uiText));
                if (e.type == SDL_KEYDOWN && e.key.keysym.scancode == SDL_SCANCODE_BACKSPACE) g_uiBack = true;
            }
#endif
#if !defined(__EMSCRIPTEN__)
            if (e.type == SDL_KEYDOWN && !e.key.repeat && e.key.keysym.sym == SDLK_TAB &&
                (e.key.keysym.mod & KMOD_SHIFT)) {
                okay::GameIpc::RequestInvite();   // the launcher pops its invite-friends window
                SDL_Log("Asked the OkaySpace launcher to open Invite friends");
            }
#endif
            if (e.type == SDL_WINDOWEVENT) {
                if (e.window.event == SDL_WINDOWEVENT_FOCUS_GAINED) windowFocused = true;
                else if (e.window.event == SDL_WINDOWEVENT_FOCUS_LOST) { windowFocused = false; cursorReleased = true; }
            }
            // A click in the window gives a released cursor back to the game.
            if (e.type == SDL_MOUSEBUTTONDOWN && cursorReleased) cursorReleased = false;
            if (e.type == SDL_CONTROLLERDEVICEADDED && !pad)
                pad = SDL_GameControllerOpen(e.cdevice.which);
            if (e.type == SDL_TEXTINPUT) Input::FeedText(e.text.text);   // real characters
            if (e.type == SDL_MOUSEWHEEL) Input::FeedMouseWheel((float)e.wheel.y);
            if (e.type == SDL_FINGERDOWN || e.type == SDL_FINGERMOTION || e.type == SDL_FINGERUP) {
                // Finger coordinates are 0..1 of the window; the UI works in renderer pixels.
                int ow = 0, oh = 0;
                SDL_GetRendererOutputSize(renderer, &ow, &oh);
                const int state = e.type == SDL_FINGERDOWN ? 0 : e.type == SDL_FINGERMOTION ? 1 : 2;
                Input::FeedTouch((long long)e.tfinger.fingerId, state, Vec2{e.tfinger.x * ow, e.tfinger.y * oh});
            }
            // Alt+Enter / F11: switch between fullscreen and windowed (when enabled in Build Settings).
            if (cfg.fullscreenToggle && e.type == SDL_KEYDOWN && !e.key.repeat &&
                (e.key.keysym.sym == SDLK_F11 ||
                 ((e.key.keysym.sym == SDLK_RETURN || e.key.keysym.sym == SDLK_KP_ENTER) && (e.key.keysym.mod & KMOD_ALT)))) {
                const bool fs = (SDL_GetWindowFlags(window) & SDL_WINDOW_FULLSCREEN) != 0;
                SDL_SetWindowFullscreen(window, fs ? 0 : SDL_WINDOW_FULLSCREEN_DESKTOP);
                continue;
            }
            // Esc quits only when no input field is focused (otherwise it cancels it).
            if (e.type == SDL_KEYDOWN && e.key.keysym.sym == SDLK_ESCAPE) {
                bool typing = false;
                for (const auto& up : scene.Objects())
                    if (auto* f = up->GetComponent<UIInputField>()) if (f->focused) typing = true;
                // Shift+Esc is a GUARANTEED quit: it always closes the game regardless of
                // pause menus, fullscreen or a hidden cursor — so a player can never get
                // trapped in a game with no visible way out.
                bool shift = (SDL_GetModState() & KMOD_SHIFT) != 0;
                if (shift && !typing) { running = false; }
                else {
                    // A PauseMenu owns a plain Escape (it pauses instead of quitting); only
                    // hard-quit on Escape when the scene has no pause menu.
                    bool hasPause = scene.FindObjectOfType<okay::PauseMenu>() != nullptr;
                    if (!typing && cfg.quitOnEscape && !hasPause) running = false;
                    // No pause menu to free it: Esc shows a locked cursor again (FPS/TPS
                    // controllers re-lock it every frame); a click captures it again.
                    else if (!hasPause && Cursor::IsLocked()) cursorReleased = true;
                }
            }
        }

#if !defined(__EMSCRIPTEN__)
        pumpPresence();
        okay::Matchmaker::Shared().Pump(scene.FindObjectOfType<okay::NetworkManager>());   // a found match: join / host it
#endif
        // Feed keyboard into the engine Input.
        const Uint8* ks = SDL_GetKeyboardState(nullptr);
        std::vector<char> down;
        for (char c = 'a'; c <= 'z'; ++c)
            if (ks[SDL_GetScancodeFromKey(c)]) down.push_back(c);
        for (char c = '0'; c <= '9'; ++c)
            if (ks[SDL_GetScancodeFromKey(c)]) down.push_back(c);
        if (ks[SDL_SCANCODE_SPACE]) down.push_back(' ');
        if (ks[SDL_SCANCODE_LSHIFT] || ks[SDL_SCANCODE_RSHIFT]) down.push_back(Input::KeyShift); // sprint
        if (ks[SDL_SCANCODE_LCTRL]  || ks[SDL_SCANCODE_RCTRL])  down.push_back(Input::KeyCtrl);  // crouch
        // Editing keys for text fields (held-state; edge-detected by the field).
        if (ks[SDL_SCANCODE_BACKSPACE]) down.push_back((char)8);
        if (ks[SDL_SCANCODE_RETURN] || ks[SDL_SCANCODE_KP_ENTER]) down.push_back('\r');
        if (ks[SDL_SCANCODE_ESCAPE]) down.push_back((char)27);
        // Arrows count as themselves and as WASD, so axis_x/axis_y and key("w") agree.
        if (ks[SDL_SCANCODE_UP])    { down.push_back('w'); down.push_back(Input::KeyUp); }
        if (ks[SDL_SCANCODE_LEFT])  { down.push_back('a'); down.push_back(Input::KeyLeft); }
        if (ks[SDL_SCANCODE_DOWN])  { down.push_back('s'); down.push_back(Input::KeyDown); }
        if (ks[SDL_SCANCODE_RIGHT]) { down.push_back('d'); down.push_back(Input::KeyRight); }

        // Read the gamepad (first connected controller).
        Vec2 padAxis; unsigned padMask = 0;
        if (pad) {
            float lx = SDL_GameControllerGetAxis(pad, SDL_CONTROLLER_AXIS_LEFTX) / 32767.0f;
            float ly = -SDL_GameControllerGetAxis(pad, SDL_CONTROLLER_AXIS_LEFTY) / 32767.0f;
            if (lx > -0.18f && lx < 0.18f) lx = 0.0f; // stick deadzone
            if (ly > -0.18f && ly < 0.18f) ly = 0.0f;
            padAxis = {lx, ly};
            auto bit = [&](SDL_GameControllerButton b, int id) {
                if (SDL_GameControllerGetButton(pad, b)) padMask |= 1u << id;
            };
            bit(SDL_CONTROLLER_BUTTON_A, 0); bit(SDL_CONTROLLER_BUTTON_B, 1);
            bit(SDL_CONTROLLER_BUTTON_X, 2); bit(SDL_CONTROLLER_BUTTON_Y, 3);
            bit(SDL_CONTROLLER_BUTTON_BACK, 4); bit(SDL_CONTROLLER_BUTTON_START, 5);
            bit(SDL_CONTROLLER_BUTTON_LEFTSHOULDER, 6); bit(SDL_CONTROLLER_BUTTON_RIGHTSHOULDER, 7);
            bit(SDL_CONTROLLER_BUTTON_DPAD_UP, 8); bit(SDL_CONTROLLER_BUTTON_DPAD_DOWN, 9);
            bit(SDL_CONTROLLER_BUTTON_DPAD_LEFT, 10); bit(SDL_CONTROLLER_BUTTON_DPAD_RIGHT, 11);
            // Fold the stick / d-pad into WASD so movement scripts work on a pad.
            if (lx > 0.4f || (padMask & (1u << 11))) down.push_back('d');
            if (lx < -0.4f || (padMask & (1u << 10))) down.push_back('a');
            if (ly > 0.4f || (padMask & (1u << 8)))  down.push_back('w');
            if (ly < -0.4f || (padMask & (1u << 9)))  down.push_back('s');
            if (padMask & (1u << 0)) down.push_back(' '); // A -> space (jump/confirm)
        }
        Input::FeedKeys(down);
        Input::FeedGamepad(padAxis, padMask);

        // An open inventory is modal: free the cursor (even in FPS/locked mode) so you
        // can point at and drag items, and show it — just like Minecraft/DayZ.
        bool invModal = false, itemDragging = false;
        for (const auto& up : scene.Objects()) {
            if (!up) continue;
            if (auto* iu = up->GetComponent<InventoryUI>()) {
                if (iu->open && iu->dragItems) invModal = true;
                if (iu->dragIndex >= 0) itemDragging = true;
            }
            if (auto* gi = up->GetComponent<GridInventory>()) { if (gi->open) invModal = true; }
            if (auto* gu = up->GetComponent<GridInventoryUI>()) { if (gu->dragIndex >= 0) itemDragging = true; }
            if (auto* cs = up->GetComponent<CraftingStation>()) { if (cs->open) invModal = true; }
            if (auto* ce = up->GetComponent<ChestInventory>()) { if (ce->open) invModal = true; }
            // The fullscreen pause-map is modal too: free + show the cursor so it can be
            // clicked to drop a waypoint (GTA-style), and pause look/move.
            if (auto* mm = up->GetComponent<Minimap>()) { if (mm->fullscreen) invModal = true; }
        }
        Input::SetUICaptured(invModal);   // controllers pause look/move while a bag is open

        // Apply the game's requested cursor state.
        static Vec2 s_virtualMouse{0, 0};
        bool locked = Cursor::IsLocked() && !invModal && !cursorReleased;
        // hand-off: when a bag opens (mouselook was locked) free the pointer
        // AT SCREEN CENTRE, and when it closes warp back to centre before re-locking, so
        // the cursor always appears in the middle and the look anchor resets cleanly.
        static bool s_prevInvModal = false;
        if (invModal != s_prevInvModal && Cursor::IsLocked()) {
            int ww = 0, wh = 0; SDL_GetWindowSize(window, &ww, &wh);
            SDL_WarpMouseInWindow(window, ww / 2, wh / 2);
        }
        s_prevInvModal = invModal;
        SDL_SetRelativeMouseMode(locked ? SDL_TRUE : SDL_FALSE);
        // While actively dragging a stack, hide the OS arrow: the dragged icon (drawn
        // centred on the mouse) becomes the pointer. The arrow's hotspot sits at its
        // tip — ~half a slot above where it looks like it's pointing — which biased the
        // nearest-slot drop to the slot above. Hiding it makes the icon centre the only
        // reference, so "drop where the item is" lines up with the slot under it.
        bool showCursor = (Cursor::visible || invModal || cursorReleased) && !itemDragging;
        SDL_ShowCursor(showCursor ? SDL_ENABLE : SDL_DISABLE);

        // Feed the mouse (position in pixels + left/right/middle button state). When
        // locked we accumulate relative motion into a virtual position so the
        // controllers' look deltas keep working with a hidden, centred cursor.
        int mx, my; Uint32 mb;
        if (locked) {
            int rx, ry; mb = SDL_GetRelativeMouseState(&rx, &ry);
            s_virtualMouse.x += (float)rx; s_virtualMouse.y += (float)ry;
        } else {
            int ax, ay; mb = SDL_GetMouseState(&ax, &ay);
            // SDL_GetMouseState is in window POINTS, but the UI is drawn in renderer
            // PIXELS (the window is High-DPI). Scale so UI hit-testing — inventory
            // slots, buttons — lines up with what's on screen.
            int ww = 0, wh = 0, ow = 0, oh = 0;
            SDL_GetWindowSize(window, &ww, &wh);
            SDL_GetRendererOutputSize(renderer, &ow, &oh);
            float sx = ww > 0 ? (float)ow / ww : 1.0f, sy = wh > 0 ? (float)oh / wh : 1.0f;
            s_virtualMouse = Vec2{ax * sx, ay * sy};
        }
        unsigned mask = 0;
        if (mb & SDL_BUTTON(SDL_BUTTON_LEFT))   mask |= 1u << 0;
        if (mb & SDL_BUTTON(SDL_BUTTON_RIGHT))  mask |= 1u << 1;
        if (mb & SDL_BUTTON(SDL_BUTTON_MIDDLE)) mask |= 1u << 2;
        Input::FeedMouse(s_virtualMouse, mask);

        Uint64 now = SDL_GetPerformanceCounter();
        float dt = (float)((now - last) / (double)SDL_GetPerformanceFrequency());
        last = now;
        if (dt > 0.1f) dt = 0.1f;
        // The game steps by an evened-out frame time (vsync: whole refresh
        // periods); the FPS readouts below keep the measured `dt`. The refresh
        // rate is re-read now and then (the window can move to another display).
        static float s_refreshHz = 0.0f;
        static int s_refreshAge = 0;
        if (s_refreshAge-- <= 0) {
            SDL_DisplayMode dm;
            s_refreshHz = (window && SDL_GetWindowDisplayMode(window, &dm) == 0 && dm.refresh_rate > 0) ? (float)dm.refresh_rate : 0.0f;
            s_refreshAge = 120;
        }
        const float stepDt = cfg.smoothDt ? Time::SmoothDelta(dt, cfg.vsync ? s_refreshHz : 0.0f) : dt;

        // Publish the render-target size so anchored UI widgets and their
        // hit-tests (run inside scene.Update) agree on where things sit.
        { int cw, ch; SDL_GetRendererOutputSize(renderer, &cw, &ch);
          UICanvas::Set((float)cw, (float)ch);
          // Make the world-space UI projector live during scene.Update too, so a
          // world-space button's Contains() hit-tests where it actually appears
          // on screen (the draw pass re-installs the same projector each frame).
          UIWorld().active = false;
          // Widgets hit-test against where they're really drawn (inside their UI
          // parent, at the Canvas scale): always, camera or not.
          UIWorld().rectOf = [cw, ch](GameObject* go, Vec2& o, Vec2& sz) {
              if (UIHidden(go)) return false;   // hidden canvas / inactive parent: not clickable
              return GetUIScreenRect(go, (float)cw, (float)ch, o, sz);
          };
          UIWorld().clipContains = [cw, ch](GameObject* go, const Vec2& p) { return UIClipHitTest(go, p, (float)cw, (float)ch); };
          if (Camera* wcam = scene.mainCamera) {
              UIWorld().active = true;
              UIWorld().screenW = (float)cw; UIWorld().screenH = (float)ch;
              if (wcam->gameObject && wcam->gameObject->transform)
                  UIWorld().right = wcam->gameObject->transform->Right();
              UIWorld().project = [wcam, cw, ch](const Vec3& p, Vec2& out, float& depth) {
                  return wcam->WorldToScreen(p, (float)cw, (float)ch, out, &depth);
              };
          } }

        // Lay out the UI before updating (scroll extents from their children, flex
        // boxes, list rows), so the wheel and scrollbars use the real content extent.
        UpdateUILayouts(scene);

#ifdef OKAY_HAVE_OKAYUI
        // Start the OkayUI frame BEFORE the scene updates, so scripts' ui_* builtins
        // (via the installed ScriptUIBridge) draw into this frame. Flushed after the
        // game is rendered (see EndFrame near the present), so UI sits on top.
        {
            int mx, my; Uint32 mb = SDL_GetMouseState(&mx, &my);
            OkayUI::Input ui;
            ui.mouseX = (float)mx; ui.mouseY = (float)my;
            ui.mouseDown = (mb & SDL_BUTTON(SDL_BUTTON_LEFT)) != 0;
            ui.rightDown = (mb & SDL_BUTTON(SDL_BUTTON_RIGHT)) != 0;
            ui.text = g_uiText[0] ? g_uiText : nullptr;
            ui.backspace = g_uiBack;
            ui.wheel = g_uiWheel;
            static bool lookChosen = false;   // game UI looks like the OkaySpace launcher (OKAY_CLASSIC_UI=1: the old look)
            if (!lookChosen) { lookChosen = true; if (!std::getenv("OKAY_CLASSIC_UI")) OkayUI::UseLauncherLook(true); }
            OkayUI::BeginFrame(ui);
        }
#endif
        // Focus behaviour: optionally pause the sim and/or mute audio
        // when the window isn't focused.
        if (cfg.muteOnFocusLoss) AudioMixer::masterVolume = windowFocused ? cfg.volume : 0.0f;
        const bool tick = windowFocused || cfg.runInBackground;

        // --- Built-in Builder Mode (an engine feature, not a component to add): F2
        // toggles in-game modeling on the main camera's player. Left-click places the
        // current brush, right-click removes, R rotates, +/- scales, G grabs, P saves
        // the model as a prefab, O saves the whole map — so any game lets players
        // build and save with zero setup. ---
        {
            static bool s_bmPrev = false, s_bmOn = false;
            static BuilderMode* s_bm = nullptr;
            const Uint8* kb = SDL_GetKeyboardState(nullptr);
            bool f2 = kb && kb[SDL_SCANCODE_F2] != 0;
            if (f2 && !s_bmPrev) {
                s_bmOn = !s_bmOn;
                if (s_bmOn && !s_bm && scene.mainCamera && scene.mainCamera->gameObject) {
                    Transform* t = scene.mainCamera->gameObject->transform;
                    while (t->Parent()) t = t->Parent();          // attach to the player root
                    GameObject* host = t->gameObject ? t->gameObject : scene.mainCamera->gameObject;
                    s_bm = host->AddComponent<BuilderMode>();
                }
                if (s_bm) s_bm->enabled = s_bmOn;
            }
            s_bmPrev = f2;
        }

        // Drive global Time so ElapsedTime()/DeltaTime()/timeScale work, then
        // advance the scene by the scaled delta (timeScale 0 = paused).
        Time::Step(tick ? stepDt : 0.0f);
        // Characters skin in the vertex shader when Direct3D 11 or OpenGL draws the
        // frame: bone matrices per frame instead of every vertex on the CPU plus a
        // full mesh re-upload. (D3D12 and the software fallback skin on the CPU.)
        {
            bool gpuSkin = false; int maxJoints = okay::SkinnedMesh::kMaxGpuJoints;
            if (cfg.gpu && gpuFails < 3) {
#if defined(_WIN32)
                if (d3d12Ready) gpuSkin = false;
                else if (d3dReady && d3dRenderer) gpuSkin = d3dRenderer->SupportsGpuSkinning();
                else
#endif
                if (glReady && glRenderer && glRenderer->SupportsGpuSkinning()) {
                    gpuSkin = true; maxJoints = glRenderer->GpuSkinJoints();
                }
            }
            okay::SkinnedMesh::GpuSkinning() = gpuSkin;
            okay::SkinnedMesh::GpuMaxJoints() = maxJoints;
        }
        if (tick) scene.Update(Time::DeltaTime());
        if (okay::Game::QuitRequested()) running = false;   // a PauseMenu / script asked to quit
        if (acGuard.Tick()) {   // only ever true in enforce mode
            running = false;
            SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, cfg.title.c_str(),
                ("The game was closed by its anti-cheat: " + acGuard.Findings().back().detail + ".").c_str(), window);
        }

        // Keyboard / gamepad menu navigation (arrows/WASD + Enter/Space/A).
        NavigateUI(scene);

        if (audioDev) {
            int n = (int)(dt * 44100.0f); if (n > 8192) n = 8192;
            if (n > 0) {
                if (scene.mainCamera)   // listener for 3D/spatial audio sources
                    AudioMixer::SetListener(scene.mainCamera->gameObject->transform->Position());
                std::vector<float> ab(n);
                AudioMixer::Render(scene, ab.data(), n);
                SDL_QueueAudio(audioDev, ab.data(), (Uint32)(n * sizeof(float)));
            }
        }

        int w, h; SDL_GetRendererOutputSize(renderer, &w, &h);
        Camera* cam = scene.mainCamera;
        Color bg = cam ? cam->backgroundColor : Color::Black;
        SDL_SetRenderDrawColor(renderer, (Uint8)(bg.r * 255), (Uint8)(bg.g * 255),
                               (Uint8)(bg.b * 255), 255);
        SDL_RenderClear(renderer);

        bool perspective = cam && cam->projection == Camera::Projection::Perspective;
        // A camera set to "Solid Color" clear flags suppresses the skybox (the
        // background color from the clear above shows through) — matches the editor.
        bool solidClear = cam && cam->clearFlags == Camera::ClearFlags::SolidColor;

        // Skybox: the same vertical sky gradient the editor previews, baked into
        // the scene's render settings so a built game looks identical. Drawn first
        // (behind everything) for 3D/perspective scenes. Painted as solid-color
        // horizontal strips (SDL_RenderFillRect) rather than SDL_RenderGeometry so
        // it renders on every driver — RenderGeometry can silently no-op on some
        // GPUs/backends, which left the game showing a black sky.
        if (perspective && scene.renderSettings.skybox && !solidClear && w > 0 && h > 0) {
            const auto& rs = scene.renderSettings;
            auto lerp = [](const Color& a, const Color& b, float t) {
                return Color{a.r + (b.r - a.r) * t, a.g + (b.g - a.g) * t,
                             a.b + (b.b - a.b) * t, 1.0f};
            };
            SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_NONE);
            float hp = rs.skyHorizonPos < 0.05f ? 0.05f : (rs.skyHorizonPos > 0.95f ? 0.95f : rs.skyHorizonPos);
            const int strips = h < 128 ? h : 128;     // smooth enough, cheap
            for (int s = 0; s < strips; ++s) {
                float t = (float)s / (float)(strips - 1);   // 0 (top) .. 1 (bottom)
                // Two-stop gradient with the horizon band at `hp` (top->horizon above
                // it, horizon->bottom below it).
                Color c = (t < hp) ? lerp(rs.skyTop, rs.skyHorizon, t / hp)
                                   : lerp(rs.skyHorizon, rs.skyBottom, (t - hp) / (1.0f - hp));
                // Atmospheric horizon haze: pale/warm brightening centered on the
                // horizon line (matches the editor preview).
                float hz = 1.0f - std::fabs(t - hp) / 0.10f;
                if (hz > 0.0f) {
                    Color haze{std::fmin(rs.skyHorizon.r * 1.15f + 0.12f, 1.0f),
                               std::fmin(rs.skyHorizon.g * 1.15f + 0.12f, 1.0f),
                               std::fmin(rs.skyHorizon.b * 1.10f + 0.10f, 1.0f), 1.0f};
                    c = lerp(c, haze, hz * 0.5f);
                }
                SDL_SetRenderDrawColor(renderer, (Uint8)(c.r * 255), (Uint8)(c.g * 255),
                                       (Uint8)(c.b * 255), 255);
                int y0 = (int)((float)s / strips * h);
                int y1 = (int)((float)(s + 1) / strips * h);
                SDL_Rect rrect{0, y0, w, (y1 > y0 ? y1 - y0 : 1)};
                SDL_RenderFillRect(renderer, &rrect);
            }
            // Optional star field in the upper sky (above the horizon band).
            if (rs.skyStars) {
                int horizonPx = (int)(hp * h);
                SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
                for (const auto& st : okay::SkyStars((float)w, (float)h, (float)horizonPx,
                                                     rs.skyStarDensity, rs.skyStarBright)) {
                    SDL_SetRenderDrawColor(renderer, 255, 255, 255, st.a);
                    int r = (int)(st.r + 0.5f); if (r < 1) r = 1;
                    SDL_Rect sq{(int)st.x - r / 2, (int)st.y - r / 2, r, r};
                    SDL_RenderFillRect(renderer, &sq);
                }
                SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_NONE);
            }
            // Optional sun disc: a filled circle + soft glow at a screen-space position.
            if (rs.skySun) {
                int cx = (int)(rs.skySunX * w), cy = (int)(rs.skySunY * h);
                int rad = (int)((rs.skySunSize < 0.005f ? 0.005f : rs.skySunSize) * h);
                const Color& sk = rs.skySunColor;
                Uint8 sr = (Uint8)(sk.r * 255), sg = (Uint8)(sk.g * 255), sb = (Uint8)(sk.b * 255);
                SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
                auto fillDisc = [&](int r, Uint8 a) {
                    SDL_SetRenderDrawColor(renderer, sr, sg, sb, a);
                    for (int dy = -r; dy <= r; ++dy) {
                        int dx = (int)std::sqrt((double)(r * r - dy * dy));
                        SDL_Rect row{cx - dx, cy + dy, 2 * dx + 1, 1};
                        SDL_RenderFillRect(renderer, &row);
                    }
                };
                // Broad sun-scattering glow: faint warm discs brightening the sky
                // around the sun (largest first so the centre accumulates).
                int glowR = (int)(h * 0.45f);
                for (int i = 12; i >= 1; --i) {
                    float fr = (float)i / 12.0f;
                    int a = (int)(34.0f * (1.0f - fr) * (1.0f - fr));
                    if (a > 0) fillDisc((int)(glowR * fr), (Uint8)a);
                }
                for (int g = 4; g >= 1; --g) fillDisc((int)(rad * (1.0f + g * 0.7f)), (Uint8)(40 - g * 6));
                fillDisc(rad, 255);
                SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_NONE);
            }
        }

        if (perspective) {
            // Z-buffered software render so overlapping faces occlude correctly,
            // then blit it under the 2D/UI layers (transparent where no geometry).
            Vec3 camPos = (cam && cam->transform) ? cam->RenderPosition() : Vec3::Zero;
            // The camera's Viewport Rect (X/Y/W/H, 0..1, origin bottom-left): split
            // screen, picture-in-picture. The 3D image is drawn into that part of
            // the window (it used to always fill the window in built games).
            int vx = 0, vy = 0, vw = w, vh = h;
            if (cam) {
                const float rw = Mathf::Clamp(cam->rectW, 0.01f, 1.0f), rh = Mathf::Clamp(cam->rectH, 0.01f, 1.0f);
                const float rx = Mathf::Clamp(cam->rectX, 0.0f, 1.0f - rw), ry = Mathf::Clamp(cam->rectY, 0.0f, 1.0f - rh);
                vw = std::max(1, (int)(rw * w + 0.5f)); vh = std::max(1, (int)(rh * h + 0.5f));
                vx = (int)(rx * w + 0.5f); vy = std::max(0, h - vh - (int)(ry * h + 0.5f));
            }
            const bool fullView = vx == 0 && vy == 0 && vw == w && vh == h;
            Mat4 vp = cam->ProjectionMatrix(vh > 0 ? (float)vw / vh : 1.0f) * cam->ViewMatrix();
            if (w > 0 && h > 0) {
                ApplySceneLight(scene);                 // a Light object aims the shading
                const GameObject* ignore = cam ? cam->ignoreObject : nullptr;
                okay::RenderCullingMask() = cam ? cam->cullingMask : ~0;   // honor the camera's layer mask (software + GPU)
                const std::uint32_t* px = nullptr;
                // GPU path first: D3D11 (Windows) then OpenGL, each rendering to an
                // offscreen target and reading back RGBA8. A self-heal counter disables
                // the GPU path after repeated failures so the game is never stuck.
                if (cfg.gpu && (d3d12Ready || d3dReady || glReady) && gpuFails < 3) {
#if defined(_WIN32)
                    if (!px && d3d12Ready && d3d12Renderer)
                        px = d3d12Renderer->RenderToPixels(scene, vp, camPos, vw, vh, 4,
                                                           0.0f, 0.0f, 0.0f, 0.0f, ignore);
                    if (!px && d3dReady && d3dRenderer)
                        px = d3dRenderer->RenderToPixels(scene, vp, camPos, vw, vh, 4,
                                                         0.0f, 0.0f, 0.0f, 0.0f, ignore);
#endif
                    if (!px && glReady && glRenderer && glWindow && glCtx) {
                        if (SDL_GL_MakeCurrent(glWindow, glCtx) == 0) {
                            px = glRenderer->RenderToPixels(scene, vp, camPos, vw, vh, 4,
                                                            0.0f, 0.0f, 0.0f, 0.0f, ignore);
                            SDL_GL_MakeCurrent(nullptr, nullptr);
                        }
                    }
                    if (!px) ++gpuFails; else gpuFails = 0;
                    (void)mesh3DGpu;
                }
                // Software fallback: native-resolution z-buffered raster (FXAA handles
                // edge AA). 2x supersampling is 4x the pixels — off by default.
                static std::vector<std::uint32_t> mesh3DDown;
                // GPU renderers don't read back depth, so particle occlusion is off for
                // them; the software path (next line) fills + validates the depth buffer.
                SceneOcclusionDepth().valid = false;
                if (!px)
                    px = RenderMeshesSS(mesh3D, mesh3DDown, scene, vp, camPos, vw, vh,
                                        cfg.antialias < 1 ? 1 : cfg.antialias, ignore);
                if (px) {
                    if (!mesh3DTex || mesh3DW != vw || mesh3DH != vh) {
                        if (mesh3DTex) SDL_DestroyTexture(mesh3DTex);
                        mesh3DTex = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ABGR8888,
                                                      SDL_TEXTUREACCESS_STREAMING, vw, vh);
                        SDL_SetTextureBlendMode(mesh3DTex, SDL_BLENDMODE_BLEND);
                        mesh3DW = vw; mesh3DH = vh;
                    }
                    SDL_UpdateTexture(mesh3DTex, nullptr, px, vw * 4);
                    if (fullView) SDL_RenderCopy(renderer, mesh3DTex, nullptr, nullptr);
                    else {   // outside the camera's rect stays black, like an empty screen area
                        SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
                        const SDL_Rect bars[4] = {{0, 0, w, vy}, {0, vy + vh, w, h - vy - vh},
                                                  {0, vy, vx, vh}, {vx + vw, vy, w - vx - vw, vh}};
                        for (const SDL_Rect& b : bars) if (b.w > 0 && b.h > 0) SDL_RenderFillRect(renderer, &b);
                        const SDL_Rect dst{vx, vy, vw, vh};
                        SDL_RenderCopy(renderer, mesh3DTex, nullptr, &dst);
                    }
                }
                // Depth occlusion on the GPU color path: the GPU renderers don't read
                // their depth back, so when one of them drew the frame, run a cheap
                // HALF-resolution software depth pre-pass to fill the occlusion buffer so
                // particles still hide behind geometry. Only pay it when particles exist.
                if (!SceneOcclusionDepth().valid) {
                    bool anyParticles = false;
                    for (const auto& up : scene.Objects())
                        if (up && up->active && up->GetComponent<ParticleSystem>()) { anyParticles = true; break; }
                    if (anyParticles) {
                        static Raster occWork; static std::vector<std::uint32_t> occDown;
                        int hw = w > 1 ? w / 2 : 1, hh = h > 1 ? h / 2 : 1;
                        RenderMeshesSS(occWork, occDown, scene, vp, camPos, hw, hh, 1, ignore);
                    }
                }
                // Particles in 3D: camera-facing billboards projected with the camera
                // (the 2D quad path below only runs for orthographic scenes, so 3D games
                // showed no particles at all). Collected, depth-sorted back-to-front, and
                // drawn as soft volumetric puffs so the 3D particle motion reads as depth.
                {
                    // len/wid = half-extents along/across the billboard; ang = long-axis angle (deg).
                    struct PP { float depth, sx, sy, len, wid, ang; SDL_Color col; SDL_Texture* tex; bool stretch; };
                    std::vector<PP> pp;
                    for (const auto& up : scene.Objects()) {
                        auto* ps = up->GetComponent<ParticleSystem>();
                        if (!ps || !up->active || UIHidden(up.get())) continue;
                        SDL_Texture* ptex = ps->texture.empty() ? nullptr
                                            : GetTexture(renderer, ps->texture, baseDir, textureCache);
                        const bool stretchMode = ps->renderMode == ParticleSystem::RenderMode::Stretch;
                        for (const auto& p : ps->Particles()) {
                            if (!p.alive) continue;
                            Vec4 c = vp * Vec4{p.position, 1.0f};
                            if (c.w <= 0.05f) continue;
                            float sx = w * 0.5f + (c.x / c.w) * w * 0.5f;
                            float sy = h * 0.5f - (c.y / c.w) * h * 0.5f;
                            if (ParticleOccluded(sx / w, sy / h, c.w)) continue;  // hidden behind geometry
                            float rad = p.size * 0.5f / c.w * w * 0.5f;
                            if (rad < 1.0f) rad = 1.0f; if (rad > 400.0f) rad = 400.0f;
                            SDL_Color col{(Uint8)(p.color.r*255), (Uint8)(p.color.g*255),
                                          (Uint8)(p.color.b*255), (Uint8)(p.color.a*255)};
                            float len = rad, ang = p.rotation; bool stretched = false;
                            float speed = std::sqrt(p.velocity.x*p.velocity.x + p.velocity.y*p.velocity.y + p.velocity.z*p.velocity.z);
                            if (stretchMode && speed > 1e-3f) {
                                Vec3 vn = p.velocity * (1.0f / speed);
                                Vec4 c2 = vp * Vec4{p.position + vn, 1.0f};
                                if (c2.w > 0.05f) {
                                    float s2x = w * 0.5f + (c2.x / c2.w) * w * 0.5f;
                                    float s2y = h * 0.5f - (c2.y / c2.w) * h * 0.5f;
                                    float dx = s2x - sx, dy = s2y - sy, dl2 = std::sqrt(dx*dx + dy*dy);
                                    if (dl2 > 1e-3f) {
                                        ang = std::atan2(dy, dx) * 57.29578f;
                                        len = rad * (1.0f + ps->stretchScale * speed);
                                        stretched = true;
                                    }
                                }
                            }
                            pp.push_back({c.w, sx, sy, len, rad, ang, col, ptex, stretched});
                        }
                    }
                    std::sort(pp.begin(), pp.end(), [](const PP& a, const PP& b){ return a.depth > b.depth; });
                    for (const PP& q : pp) {
                        if (q.tex) {
                            // Rotated, tinted sprite — elongated along velocity in stretch mode.
                            SDL_SetTextureColorMod(q.tex, q.col.r, q.col.g, q.col.b);
                            SDL_SetTextureAlphaMod(q.tex, q.col.a);
                            SDL_SetTextureBlendMode(q.tex, SDL_BLENDMODE_BLEND);
                            SDL_FRect dst{q.sx - q.len, q.sy - q.wid, q.len * 2.0f, q.wid * 2.0f};
                            SDL_RenderCopyExF(renderer, q.tex, nullptr, &dst, q.ang, nullptr, SDL_FLIP_NONE);
                        } else if (q.stretch) {
                            // Untextured streak: a thick line along the velocity.
                            float a = q.ang * 0.01745329f, cx = std::cos(a) * q.len, cy = std::sin(a) * q.len;
                            MMThickLine(renderer, q.sx - cx, q.sy - cy, q.sx + cx, q.sy + cy, q.wid * 2.0f, q.col);
                        } else {
                            SDL_Color halo = q.col; halo.a = (Uint8)(q.col.a / 3);   // soft outer glow
                            MMFillCircle(renderer, (int)q.sx, (int)q.sy, (int)(q.wid * 1.6f), halo);
                            MMFillCircle(renderer, (int)q.sx, (int)q.sy, (int)q.wid, q.col);  // bright core
                        }
                    }
                }
            }
        } else {
            float ortho = cam ? cam->EffectiveOrthoSize() : 5.0f;
            Vec3 camPos = (cam && cam->transform) ? cam->RenderPosition() : Vec3::Zero;
            float scale = h / (2.0f * ortho);
            // Gather active sprites and draw back-to-front by sort key — sorting
            // layer first, then order-in-layer (stable, so same-key sprites keep
            // scene order). Enables layered 2D scenes.
            std::vector<GameObject*> sprites;
            for (const auto& up : scene.Objects())
                if (up->active && up->GetComponent<SpriteRenderer>()) sprites.push_back(up.get());
            std::stable_sort(sprites.begin(), sprites.end(), [](GameObject* a, GameObject* b) {
                return a->GetComponent<SpriteRenderer>()->SortKey() <
                       b->GetComponent<SpriteRenderer>()->SortKey();
            });
            for (GameObject* obj : sprites) {
                auto* sr = obj->GetComponent<SpriteRenderer>();
                // Rotate/scale the sprite quad through the full transform so 2D
                // games can spin and skew sprites, not just place axis-aligned ones.
                Mat4 model = obj->transform->LocalToWorldMatrix();
                float hx = sr->size.x * 0.5f, hy = sr->size.y * 0.5f;
                Vec3 corners[4] = {{-hx, -hy, 0}, {hx, -hy, 0}, {hx, hy, 0}, {-hx, hy, 0}};
                SDL_Color col{(Uint8)(sr->color.r * 255), (Uint8)(sr->color.g * 255),
                              (Uint8)(sr->color.b * 255), (Uint8)(sr->color.a * 255)};
                // Texture coords map the image upright onto the quad corners
                // (corner 3 = top-left in world = texture uvMin). Honors the
                // sprite's uv sub-region so sprite sheets / atlases work.
                SDL_Texture* tex = GetTexture(renderer, sr->texture, baseDir, textureCache);
                if (tex) SDL_SetTextureScaleMode(tex,
                    sr->texFilter == SpriteRenderer::TexFilter::Pixel ? SDL_ScaleModeNearest : SDL_ScaleModeLinear);
                float u0 = sr->uvMin.x, v0 = sr->uvMin.y, u1 = sr->uvMax.x, v1 = sr->uvMax.y;
                if (sr->flipX) std::swap(u0, u1);
                if (sr->flipY) std::swap(v0, v1);
                const SDL_FPoint uv[4] = {{u0, v1}, {u1, v1}, {u1, v0}, {u0, v0}};
                SDL_Vertex vtx[4];
                for (int k = 0; k < 4; ++k) {
                    Vec3 wpos = model.MultiplyPoint(corners[k]);
                    SDL_Point s = W2S(wpos, camPos, scale, w, h);
                    vtx[k] = SDL_Vertex{{(float)s.x, (float)s.y}, col, uv[k]};
                }
                const int idx[6] = {0, 1, 2, 0, 2, 3};
                SDL_RenderGeometry(renderer, tex, vtx, 4, idx, 6);
            }

            // Drop-zone highlight: tint a zone while a valid item hovers it.
            SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
            for (const auto& up : scene.Objects()) {
                auto* dz = up->GetComponent<DropZone>();
                auto* sr = up->GetComponent<SpriteRenderer>();
                if (!dz || !sr || !up->active || !dz->IsHovered()) continue;
                Vec3 ls = up->transform->LossyScale();
                FillWorldQuad(renderer, up->transform->Position(),
                              sr->size.x * ls.x, sr->size.y * ls.y,
                              camPos, scale, w, h, SDL_Color{255, 255, 255, 70});
            }

            // Tilemaps: draw each non-empty cell as a colored quad.
            SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
            for (const auto& up : scene.Objects()) {
                auto* tm = up->GetComponent<Tilemap>();
                if (!tm || !up->active || UIHidden(up.get())) continue;
                for (int y = 0; y < tm->Height(); ++y)
                    for (int x = 0; x < tm->Width(); ++x) {
                        int id = tm->GetTile(x, y);
                        if (id == 0) continue;
                        FillWorldQuad(renderer, tm->CellToWorld(x, y), tm->tileSize,
                                      tm->tileSize, camPos, scale, w, h, TileColor(id));
                    }
            }

            // Particle systems: draw each live particle as a small fading quad.
            for (const auto& up : scene.Objects()) {
                auto* ps = up->GetComponent<ParticleSystem>();
                if (!ps || !up->active || UIHidden(up.get())) continue;
                for (const auto& p : ps->Particles()) {
                    if (!p.alive) continue;
                    SDL_Color col{(Uint8)(p.color.r * 255), (Uint8)(p.color.g * 255),
                                  (Uint8)(p.color.b * 255), (Uint8)(p.color.a * 255)};
                    FillWorldQuad(renderer, p.position, p.size, p.size,
                                  camPos, scale, w, h, col);
                }
            }
        }

        // World-space text only (sits with the 2D scene). Screen-space HUD text is
        // drawn LATER (after the UI widgets) so labels sit ON TOP of panels instead
        // of being hidden behind them — see the screen-space text pass below.
        {
            float ortho = cam ? cam->EffectiveOrthoSize() : 5.0f;
            Vec3 camPos = (cam && cam->transform) ? cam->RenderPosition() : Vec3::Zero;
            float scale = h / (2.0f * ortho);
            for (const auto& up : scene.Objects()) {
                auto* tr = up->GetComponent<TextRenderer>();
                if (!tr || !up->active || tr->screenSpace || UIHidden(up.get())) continue;
                float op = UIOpacity(up.get());   // canvas master fade
                SDL_Color col{(Uint8)(tr->color.r * 255), (Uint8)(tr->color.g * 255),
                              (Uint8)(tr->color.b * 255), (Uint8)(tr->color.a * 255 * op)};
                SDL_Color sh{(Uint8)(tr->shadowColor.r * 255), (Uint8)(tr->shadowColor.g * 255),
                             (Uint8)(tr->shadowColor.b * 255), (Uint8)(tr->shadowColor.a * 255 * op)};
                SDL_Color ol{(Uint8)(tr->outlineColor.r * 255), (Uint8)(tr->outlineColor.g * 255),
                             (Uint8)(tr->outlineColor.b * 255), (Uint8)(tr->outlineColor.a * 255 * op)};
                SDL_Point o = W2S(up->transform->Position(), camPos, scale, w, h);
                float px = tr->EffectivePixelSize() * scale;
                okay::TtfFont* fnt = tr->Font();
                if (tr->shadow)
                    DrawText(renderer, tr->text, o.x + tr->shadowOffset.x * px,
                             o.y + tr->shadowOffset.y * px, px, sh, 0, 0, false, false, SDL_Color{0,0,0,0}, fnt);
                const float fo = fnt ? 1.0f : px;   // TTF: ~1 screen px, not a whole font pixel
                if (tr->outline) {
                    DrawText(renderer, tr->text, o.x - fo, o.y, px, ol, 0, 0, false, false, SDL_Color{0,0,0,0}, fnt);
                    DrawText(renderer, tr->text, o.x + fo, o.y, px, ol, 0, 0, false, false, SDL_Color{0,0,0,0}, fnt);
                    DrawText(renderer, tr->text, o.x, o.y - fo, px, ol, 0, 0, false, false, SDL_Color{0,0,0,0}, fnt);
                    DrawText(renderer, tr->text, o.x, o.y + fo, px, ol, 0, 0, false, false, SDL_Color{0,0,0,0}, fnt);
                }
                DrawText(renderer, tr->text, (float)o.x, (float)o.y, px, col, 0, 0, false, false, SDL_Color{0,0,0,0}, fnt);
                if (tr->bold) DrawText(renderer, tr->text, (float)o.x + fo, (float)o.y, px, col, 0, 0, false, false, SDL_Color{0,0,0,0}, fnt);
            }
        }

        // In-game UI (screen space), drawn on top of everything: one sorted pass over
        // every widget (Canvas sortOrder, uiDrawOrder / type layer, hierarchy order),
        // done by the engine's UI renderer (okay::ui2d::RenderSceneUI) on this
        // SDL_Renderer.
        SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
        // World-space Canvas projection: hand the shared UI layout helpers a camera
        // projector for this frame so any widget under a world-space Canvas renders
        // (and hit-tests) in 3D. Cleared after the pass; screen-space UI is untouched.
        UIWorld().active = false;
        if (Camera* wcam = scene.mainCamera) {
            int ww = w, hh = h;
            UIWorld().active = true;
            UIWorld().screenW = (float)ww; UIWorld().screenH = (float)hh;
            if (wcam->gameObject && wcam->gameObject->transform)
                UIWorld().right = wcam->gameObject->transform->Right();
            UIWorld().project = [wcam, ww, hh](const Vec3& p, Vec2& out, float& depth) {
                return wcam->WorldToScreen(p, (float)ww, (float)hh, out, &depth);
            };
            UIWorld().rectOf = [ww, hh](GameObject* go, Vec2& o, Vec2& sz) {
                if (UIHidden(go)) return false;   // hidden canvas / inactive parent: not clickable
                return GetUIScreenRect(go, (float)ww, (float)hh, o, sz);
            };
            UIWorld().clipContains = [ww, hh](GameObject* go, const Vec2& p) { return UIClipHitTest(go, p, (float)ww, (float)hh); };
        }
        ui2d::RenderSceneUI(scene, uiBatch, w, h);
        uiBatch.Flush();   // leaves no clip, scale 1

        // Optional FPS overlay (top-left) when enabled in build settings.
        if (cfg.showFps) {
            static float fpsSmooth = 0.0f;
            float inst = dt > 0.0001f ? 1.0f / dt : 0.0f;
            fpsSmooth = fpsSmooth <= 0.0f ? inst : fpsSmooth * 0.9f + inst * 0.1f;
            char buf[32]; std::snprintf(buf, sizeof(buf), "FPS %d", (int)(fpsSmooth + 0.5f));
            float px = 2.0f;
            float tw = UITextW(buf, px);
            SDL_Rect bg{4, 4, (int)(tw + 8), (int)(Font8x8::Height * px + 8)};
            SDL_SetRenderDrawColor(renderer, 0, 0, 0, 160);
            SDL_RenderFillRect(renderer, &bg);
            DrawText(renderer, buf, 8, 8, px, SDL_Color{80, 255, 120, 255});
        }
        UIWorld().active = false;   // end of the frame's world-space UI projection
#ifdef OKAY_HAVE_OKAYUI
        // Scripts drew their UI during scene.Update (via the bridge); add the optional
        // F1 "Test UI" panel on top, then flush the whole OkayUI frame here so it
        // composites above the game.
        if (g_testUI) { okay_testui::Panel(24.0f, 24.0f); okay_testui::Showcase(390.0f, 24.0f); }
        static bool toastDemo = false;   // dev screenshot hook
        if (!toastDemo && std::getenv("OKAY_TESTUI")) { toastDemo = true; OkayUI::Toast("Match found - joining EU 1", 1, 30.0f); OkayUI::Toast("Maya invited you to play", 0, 30.0f); }
        DrawServerBrowserWindow(scene, window);   // net_browser_open(): the in-game "Find a server" window
        DrawMatchmakingBanner(window);            // net_queue_join() / Find match: progress + Cancel
        // F3 performance HUD — a real in-engine use of OkayUI (FPS graph, frame time,
        // and a scrollable list of scene objects). Showcases PlotLines/LabelText/child.
        if (g_statsUI) {
            static float s_fps = 0.0f;
            float inst = dt > 0.0001f ? 1.0f / dt : 0.0f;
            s_fps = s_fps <= 0.0f ? inst : s_fps * 0.9f + inst * 0.1f;   // smoothed FPS
            static float s_fpsHist[90] = {0};
            static int   s_fpsIdx = 0;
            s_fpsHist[s_fpsIdx] = s_fps;
            s_fpsIdx = (s_fpsIdx + 1) % 90;
            float plot[90];
            for (int i = 0; i < 90; ++i) plot[i] = s_fpsHist[(s_fpsIdx + i) % 90];  // chronological

            OkayUI::Begin("Performance  (F3)", 24.0f, 24.0f, 320.0f, 320.0f);
            char line[64];
            std::snprintf(line, sizeof(line), "%d", (int)(s_fps + 0.5f));
            OkayUI::LabelText("FPS", line);
            std::snprintf(line, sizeof(line), "%.2f ms", dt * 1000.0f);
            OkayUI::LabelText("Frame time", line);
            std::snprintf(line, sizeof(line), "%d", (int)scene.Objects().size());
            OkayUI::LabelText("Objects", line);
            OkayUI::SeparatorText("FPS (last 90 frames)");
            OkayUI::PlotLines("##fps", plot, 90, 0.0f, 0.0f, 56.0f);
            OkayUI::SeparatorText("Scene objects");
            if (OkayUI::BeginChild("##objs", 0.0f, 130.0f, true)) {
                for (const auto& up : scene.Objects())
                    if (up) OkayUI::BulletText(up->name.c_str());
            }
            OkayUI::EndChild();
            OkayUI::End();
        }
        OkayUI::EndFrame(renderer);
        SDL_StartTextInput();   // keep text flowing for OkayUI text fields
#endif
        // Minecraft-style inventory hotbar / backpack (under the loading screen).
        for (const auto& up : scene.Objects()) {
            auto* ui = up ? up->GetComponent<InventoryUI>() : nullptr;
            if (!ui) continue;
            DrawInventoryUI(renderer, *ui, baseDir, textureCache);
            break;
        }
        for (const auto& up : scene.Objects()) {
            auto* gui = up ? up->GetComponent<GridInventoryUI>() : nullptr;
            if (!gui) continue;
            DrawGridInventory(renderer, *gui, baseDir, textureCache);
            break;
        }
        for (const auto& up : scene.Objects()) {
            auto* cs = up ? up->GetComponent<CraftingStation>() : nullptr;
            if (!cs || !cs->open) continue;   // the open one (only the nearest opens), not just the first
            DrawCrafting(renderer, *cs);
            break;
        }
        for (const auto& up : scene.Objects()) {
            auto* ce = up ? up->GetComponent<ChestInventory>() : nullptr;
            if (!ce || !ce->open) continue;
            DrawChest(renderer, *ce, baseDir, textureCache);
            break;
        }
        // Loading-screen overlay: drawn on top of everything (and the UI) when active.
        for (const auto& up : scene.Objects()) {
            auto* ls = up ? up->GetComponent<LoadingScreen>() : nullptr;
            if (!ls || !ls->Active()) continue;
            ui2d::SetUIDefaultFont(okay::SceneUIFont(scene.uiFont));   // same text as the game's UI
            DrawLoadingScreen(renderer, *ls, baseDir, textureCache);
            ui2d::SetUIDefaultFont(nullptr);
            break;
        }
        // Post: screen-space vignette (on top of the scene + UI, under nothing).
        DrawVignette(renderer, scene.renderSettings.vignette);
        SDL_RenderPresent(renderer);

        // Optional frame-rate cap: sleep the remainder of the frame budget.
        if (cfg.fpsCap > 0) {
            double budget = 1.0 / cfg.fpsCap;
            double freq = (double)SDL_GetPerformanceFrequency();
            double elapsed = (SDL_GetPerformanceCounter() - fStart) / freq;
            double remain = budget - elapsed;
            if (remain > 0.0015) SDL_Delay((Uint32)((remain - 0.0005) * 1000.0));
        }
    };

    // Drive the frame loop: the browser owns the loop on web (a blocking while
    // would freeze the tab), so register the frame with Emscripten there.
#ifdef __EMSCRIPTEN__
    (void)running;
    static auto* s_frame = &frame;
    emscripten_set_main_loop([]() { (*s_frame)(); }, 0, 1);
#else
    // Startup splash: show a logo over a solid colour, fading in and
    // out, before the game proper. Skippable with a key/click; closing quits.
    if (running && !cfg.splash.empty()) {
        if (SDL_Texture* stex = GetTexture(renderer, cfg.splash, baseDir, textureCache)) {
            Uint64 t0 = SDL_GetPerformanceCounter();
            const double dur = cfg.splashTime > 0.1f ? cfg.splashTime : 2.0;
            const double fade = std::min(0.5, dur * 0.3);
            bool done = false;
            while (running && !done) {
                double el = (double)(SDL_GetPerformanceCounter() - t0) / (double)SDL_GetPerformanceFrequency();
                if (el >= dur) break;
                SDL_Event e;
                while (SDL_PollEvent(&e)) {
                    if (e.type == SDL_QUIT) { running = false; }
                    else if (e.type == SDL_KEYDOWN || e.type == SDL_MOUSEBUTTONDOWN) done = true;  // skip
                }
                double a = 1.0;
                if (el < fade) a = el / fade;
                else if (el > dur - fade) a = (dur - el) / fade;
                if (a < 0) a = 0; if (a > 1) a = 1;
                int w = 0, h = 0; SDL_GetRendererOutputSize(renderer, &w, &h);
                SDL_SetRenderDrawColor(renderer, (Uint8)cfg.splashBg[0], (Uint8)cfg.splashBg[1], (Uint8)cfg.splashBg[2], 255);
                SDL_RenderClear(renderer);
                int iw = 0, ih = 0; SDL_QueryTexture(stex, nullptr, nullptr, &iw, &ih);
                if (iw > 0 && ih > 0) {
                    double fit = std::min((double)w / iw, (double)h / ih) * 0.55;  // 55% of the window
                    int dw = (int)(iw * fit), dh = (int)(ih * fit);
                    SDL_Rect dst{(w - dw) / 2, (h - dh) / 2, dw, dh};
                    SDL_SetTextureBlendMode(stex, SDL_BLENDMODE_BLEND);
                    SDL_SetTextureAlphaMod(stex, (Uint8)(a * 255));
                    SDL_RenderCopy(renderer, stex, nullptr, &dst);
                }
                SDL_RenderPresent(renderer);
                SDL_Delay(8);
            }
        }
    }

    while (running) frame();
#endif
#if !defined(__EMSCRIPTEN__)
    okay::GameIpc::ClearPresence();   // friends stop seeing a game that has closed
#endif
    if (!acGuard.Findings().empty()) {   // the runtime checks' findings, sent once at the end
        std::string all;
        for (const okayac::Finding& f : acGuard.Findings()) all += f.check + ": " + f.detail + "\n";
        acFileReport(acGuard.Findings().front().check, all);
    }

    Prefs::Save(prefsPath); // persist any prefs the game set this session

    for (auto& kv : textureCache)
        if (kv.second) SDL_DestroyTexture(kv.second);
    if (mesh3DTex) SDL_DestroyTexture(mesh3DTex);
    if (audioDev) SDL_CloseAudioDevice(audioDev);
#if defined(_WIN32)
    if (d3d12Renderer) { d3d12Renderer->Destroy(); delete d3d12Renderer; }
    if (d3dRenderer) { d3dRenderer->Destroy(); delete d3dRenderer; }
#endif
    if (glRenderer) {
        if (glWindow && glCtx) SDL_GL_MakeCurrent(glWindow, glCtx);
        glRenderer->Destroy(); delete glRenderer;
        SDL_GL_MakeCurrent(nullptr, nullptr);
    }
    if (glCtx) SDL_GL_DeleteContext(glCtx);
    if (glWindow) SDL_DestroyWindow(glWindow);
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}
