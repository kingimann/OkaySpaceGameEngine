// Game cover art and avatar textures (CoverArtAvatars.hpp): a game's cover image next to its scene, and profile pictures
// downloaded once from a public URL, each cached as a texture per render slot. Moved out of launcher/main.cpp unchanged
// (docs/engine/modularization_plan.md, Part C section 6, batch L3).
#include "LauncherShared.hpp"
#include "CoverArtAvatars.hpp"
#include "ChatText.hpp"   // IconTile, LoadRgbaFile
#include "OkayTheme.hpp"   // the theme palette's colours

namespace okl {

// ---- Optional per-game cover art -----------------------------------------
// A game can ship a cover image next to its .okayscene (cover.png/.jpg, or
// <stem>.png/.jpg). We load it once via okay::DecodeImage, upload an SDL texture, and
// cache it by game path. nullptr (cached) means "no cover — draw the initial".
std::map<std::string, okay::ui2d::Tex> g_coverCaches[2];

okay::ui2d::Tex GameCover(const std::string& gamePath) {
    auto it = g_coverCache.find(gamePath);
    if (it != g_coverCache.end()) return it->second;
    okay::ui2d::Tex tex = nullptr;
    if (g_renderer) {
        fs::path gp(gamePath), dir = gp.parent_path(), stem = gp.stem();
        std::error_code ec;
        const char* names[] = { "cover.png", "cover.jpg", "cover.jpeg" };
        std::vector<fs::path> cands = { dir / (stem.string() + ".png"),
                                        dir / (stem.string() + ".jpg") };
        for (const char* n : names) cands.push_back(dir / n);
        {   // an exported game keeps its scene in Data/ or <Game>_Data/: its cover may sit beside the program instead
            const std::string dn = dir.filename().string();
            if (dn == "Data" || (dn.size() > 5 && dn.compare(dn.size() - 5, 5, "_Data") == 0))
                for (const char* n : names) cands.push_back(dir.parent_path() / n);
        }
        for (const auto& c : cands) {
            if (!fs::exists(c, ec)) continue;
            int w = 0, h = 0;
            unsigned char* px = LoadRgbaFile(c.string(), &w, &h);
            if (!px) continue;
            tex = TextureFromRgba(g_renderer, px, w, h);
            std::free(px);
            break;
        }
    }
    g_coverCache[gamePath] = tex;   // cache even nullptr so we don't re-stat every frame
    return tex;
}

// Draw a game's cover (rounded, cover-fit) if it has one, else the initial tile.
void DrawGameIcon(const std::string& gamePath, const std::string& name, float size) {
    okay::ui2d::Tex tex = GameCover(gamePath);
    if (!tex) { IconTile(name, size); return; }
    ImVec2 p = ImGui::GetCursorScreenPos();
    ImGui::GetWindowDrawList()->AddImageRounded(
        (ImTextureID)(intptr_t)tex, p, ImVec2(p.x + size, p.y + size),
        ImVec2(0, 0), ImVec2(1, 1), IM_COL32_WHITE, size * 0.22f);
    ImGui::Dummy(ImVec2(size, size));
}

// Draw `tex` filling a..b, cropped to keep its shape (the middle of a wider or taller picture). `flags`: which corners
// are rounded (ImDrawFlags_RoundCorners*, 0 = all).
void DrawCoverFill(ImDrawList* dl, okay::ui2d::Tex tex, ImVec2 a, ImVec2 b, float rounding, int flags) {
    if (!tex) return;
    int tw = 0, th = 0;
    okay::pb::QueryTexture(g_renderer, tex, &tw, &th);
    const float bw = std::max(1.0f, b.x - a.x), bh = std::max(1.0f, b.y - a.y);
    const float ta = th > 0 ? (float)tw / (float)th : 1.0f, ba = bw / bh;
    ImVec2 uv0(0, 0), uv1(1, 1);
    if (ta > ba) { uv0.x = (1.0f - ba / ta) * 0.5f; uv1.x = 1.0f - uv0.x; }
    else         { uv0.y = (1.0f - ta / ba) * 0.5f; uv1.y = 1.0f - uv0.y; }
    dl->AddImageRounded((ImTextureID)(intptr_t)tex, a, b, uv0, uv1, IM_COL32_WHITE, rounding, flags);
}

// The art a game without a cover gets: a deep panel in the game's own colour (NameColor, so it never changes), two soft
// orbs and its initials, large. Drawn inside a..b only (the orbs never touch the rounded corners).
void DrawCoverPlaceholder(ImDrawList* dl, const std::string& name, ImVec2 a, ImVec2 b, float rounding, int flags) {
    const ImVec4 hue = NameColor(name.empty() ? std::string("?") : name);
    const float w = b.x - a.x, h = b.y - a.y;
    dl->AddRectFilled(a, b, ImGui::GetColorU32(okaytheme::Mix(hue, okaytheme::kDeep, 0.62f)), rounding, flags);
    const float r1 = std::min(h * 0.40f, w * 0.26f), r2 = std::min(h * 0.28f, w * 0.18f);
    dl->AddCircleFilled(ImVec2(a.x + w * 0.74f, a.y + h * 0.42f), r1, ImGui::GetColorU32(okaytheme::WithAlpha(hue, 0.30f)), 48);
    dl->AddCircleFilled(ImVec2(a.x + w * 0.30f, a.y + h * 0.64f), r2, ImGui::GetColorU32(okaytheme::WithAlpha(okaytheme::Mix(hue, kAccent, 0.5f), 0.22f)), 40);
    const int sf = flags == 0 ? (int)ImDrawFlags_RoundCornersBottom : (flags & (int)ImDrawFlags_RoundCornersBottom);   // the bottom corners it shares
    dl->AddRectFilled(ImVec2(a.x, a.y + h * 0.62f), b, okaytheme::Shade(46), sf ? rounding : 0.0f, sf);
    // Initials: the first letter of the first two words.
    std::string ini;
    bool start = true;
    for (char c : name) {
        if (c == ' ' || c == '_' || c == '-') { start = true; continue; }
        if (start && std::isalnum((unsigned char)c)) { ini += (char)std::toupper((unsigned char)c); if (ini.size() == 2) break; }
        start = false;
    }
    if (ini.empty()) ini = "?";
    const float fs = std::max(12.0f, h * 0.36f);
    ImFont* f = ImGui::GetFont();
    const ImVec2 ts = f->CalcTextSizeA(fs, 1e9f, 0.0f, ini.c_str());
    const ImVec2 tp(a.x + (w - ts.x) * 0.5f, a.y + (h - ts.y) * 0.5f);
    dl->AddText(f, fs, ImVec2(tp.x, tp.y + fs * 0.04f), okaytheme::Shade(90), ini.c_str());
    dl->AddText(f, fs, tp, ImGui::GetColorU32(okaytheme::Mix(okaytheme::kText, hue, 0.15f)), ini.c_str());
}

// A game's cover filling a..b, else its placeholder art.
void DrawGameArt(ImDrawList* dl, const std::string& gamePath, const std::string& name, ImVec2 a, ImVec2 b, float rounding, int flags) {
    if (okay::ui2d::Tex tex = gamePath.empty() ? nullptr : GameCover(gamePath)) DrawCoverFill(dl, tex, a, b, rounding, flags);
    else DrawCoverPlaceholder(dl, name, a, b, rounding, flags);
}

// ---- Profile-picture (avatar) textures, loaded from a public URL ----------
// Downloaded once to a per-URL cache file, decoded via okay::DecodeImage, cached as an
// SDL texture. nullptr (cached) means "no avatar — draw the initial".
std::map<std::string, okay::ui2d::Tex> g_avatarCaches[2];
std::string* g_myAvatarSlot = nullptr;   // points at the Account card's cached avatar URL

// Avatar downloads used to block the render thread (a freeze the first time a
// friends/chat list showed uncached avatars). The network fetch now runs on a
// detached worker; decode + GPU upload stay on the main thread (local, fast).
okay::thr::Mutex*          g_avatarMutex = nullptr;   // guards the two sets below
std::set<std::string> g_avatarPending;         // URLs currently downloading
std::set<std::string> g_avatarFailed;          // URLs that failed (don't retry)
int AvatarDlWorker(void* p) {
    AvatarDl* d = (AvatarDl*)p;
    bool ok = false;
    try { ok = Download(d->url, d->path); } catch (...) {}
    std::error_code ec;
    if (g_avatarMutex) {
        okay::thr::LockMutex(g_avatarMutex);
        g_avatarPending.erase(d->url);
        if (!ok && !fs::exists(d->path, ec)) g_avatarFailed.insert(d->url);
        okay::thr::UnlockMutex(g_avatarMutex);
    }
    delete d;
    return 0;
}

okay::ui2d::Tex AvatarTexture(const std::string& url) {
    if (url.empty() || !g_renderer) return nullptr;
    auto it = g_avatarCache.find(url);
    if (it != g_avatarCache.end()) return it->second;
    std::error_code ec;
    fs::path dir = fs::temp_directory_path(ec) / "okayspace_avatars";
    fs::create_directories(dir, ec);
    char name[32]; std::snprintf(name, sizeof(name), "%zu", std::hash<std::string>{}(url));
    fs::path cache = dir / (std::string(name) + ".img");
    if (fs::exists(cache, ec)) {
        // Already downloaded: decode + upload here (fast), then cache the texture.
        okay::ui2d::Tex tex = nullptr;
        int w = 0, h = 0;
        unsigned char* px = LoadRgbaFile(cache.string(), &w, &h);
        if (px) {
            tex = TextureFromRgba(g_renderer, px, w, h);
            std::free(px);
        }
        if (tex) { g_avatarCache[url] = tex; return tex; }
    }
    // Not on disk yet (or a bad decode): fetch in the background once, and show
    // the coloured-initial fallback until it lands. Never block the UI.
    if (!g_avatarMutex) g_avatarMutex = okay::thr::CreateMutex();
    bool kick = false;
    okay::thr::LockMutex(g_avatarMutex);
    if (g_avatarFailed.count(url)) { okay::thr::UnlockMutex(g_avatarMutex); return nullptr; }
    if (!g_avatarPending.count(url)) { g_avatarPending.insert(url); kick = true; }
    okay::thr::UnlockMutex(g_avatarMutex);
    if (kick) {
        AvatarDl* d = new AvatarDl{url, cache.string()};
        if (okay::thr::Thread* t = okay::thr::CreateThread(AvatarDlWorker, "okay-avdl", d)) okay::thr::DetachThread(t);
        else { okay::thr::LockMutex(g_avatarMutex); g_avatarPending.erase(url); okay::thr::UnlockMutex(g_avatarMutex); delete d; }
    }
    return nullptr;
}

// Draw a circular avatar: the image if the URL loads, else a colored initial.
void DrawAvatar(const std::string& url, const std::string& name, float size) {
    ImVec2 p = ImGui::GetCursorScreenPos();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 ctr(p.x + size * 0.5f, p.y + size * 0.5f);
    okay::ui2d::Tex tex = AvatarTexture(url);
    if (tex) {
        dl->AddImageRounded((ImTextureID)(intptr_t)tex, p, ImVec2(p.x + size, p.y + size),
                            ImVec2(0, 0), ImVec2(1, 1), IM_COL32_WHITE, size * 0.5f);
    } else {
        ImVec4 col = NameColor(name);
        dl->AddCircleFilled(ctr, size * 0.5f, ImGui::GetColorU32(ImVec4(col.x*0.55f, col.y*0.55f, col.z*0.55f, 1)), 32);
        char init[2] = { name.empty() ? '?' : (char)std::toupper((unsigned char)name[0]), 0 };
        ImVec2 ts = ImGui::CalcTextSize(init);
        dl->AddText(ImVec2(ctr.x - ts.x*0.5f, ctr.y - ts.y*0.5f), okaytheme::OnColorU32(), init);
    }
    ImGui::Dummy(ImVec2(size, size));
}

} // namespace okl
