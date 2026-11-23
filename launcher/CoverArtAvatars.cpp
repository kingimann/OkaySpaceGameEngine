// Game cover art and avatar textures (CoverArtAvatars.hpp): a game's cover image next to its scene, and profile pictures
// downloaded once from a public URL, each cached as a texture per render slot. Moved out of launcher/main.cpp unchanged
// (docs/engine/modularization_plan.md, Part C section 6, batch L3).
#include "LauncherShared.hpp"
#include "CoverArtAvatars.hpp"
#include "ChatText.hpp"   // IconTile, LoadRgbaFile

namespace okl {

// ---- Optional per-game cover art -----------------------------------------
// A game can ship a cover image next to its .okayscene (cover.png/.jpg, or
// <stem>.png/.jpg). We load it once via okay::DecodeImage, upload an SDL texture, and
// cache it by game path. nullptr (cached) means "no cover — draw the initial".
std::map<std::string, SDL_Texture*> g_coverCaches[2];

SDL_Texture* GameCover(const std::string& gamePath) {
    auto it = g_coverCache.find(gamePath);
    if (it != g_coverCache.end()) return it->second;
    SDL_Texture* tex = nullptr;
    if (g_renderer) {
        fs::path gp(gamePath), dir = gp.parent_path(), stem = gp.stem();
        std::error_code ec;
        const char* names[] = { "cover.png", "cover.jpg", "cover.jpeg" };
        std::vector<fs::path> cands = { dir / (stem.string() + ".png"),
                                        dir / (stem.string() + ".jpg") };
        for (const char* n : names) cands.push_back(dir / n);
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
    SDL_Texture* tex = GameCover(gamePath);
    if (!tex) { IconTile(name, size); return; }
    ImVec2 p = ImGui::GetCursorScreenPos();
    ImGui::GetWindowDrawList()->AddImageRounded(
        (ImTextureID)(intptr_t)tex, p, ImVec2(p.x + size, p.y + size),
        ImVec2(0, 0), ImVec2(1, 1), IM_COL32_WHITE, size * 0.22f);
    ImGui::Dummy(ImVec2(size, size));
}

// ---- Profile-picture (avatar) textures, loaded from a public URL ----------
// Downloaded once to a per-URL cache file, decoded via okay::DecodeImage, cached as an
// SDL texture. nullptr (cached) means "no avatar — draw the initial".
std::map<std::string, SDL_Texture*> g_avatarCaches[2];
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

SDL_Texture* AvatarTexture(const std::string& url) {
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
        SDL_Texture* tex = nullptr;
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
    SDL_Texture* tex = AvatarTexture(url);
    if (tex) {
        dl->AddImageRounded((ImTextureID)(intptr_t)tex, p, ImVec2(p.x + size, p.y + size),
                            ImVec2(0, 0), ImVec2(1, 1), IM_COL32_WHITE, size * 0.5f);
    } else {
        ImVec4 col = NameColor(name);
        dl->AddCircleFilled(ctr, size * 0.5f, ImGui::GetColorU32(ImVec4(col.x*0.55f, col.y*0.55f, col.z*0.55f, 1)), 32);
        char init[2] = { name.empty() ? '?' : (char)std::toupper((unsigned char)name[0]), 0 };
        ImVec2 ts = ImGui::CalcTextSize(init);
        dl->AddText(ImVec2(ctr.x - ts.x*0.5f, ctr.y - ts.y*0.5f), IM_COL32(255,255,255,255), init);
    }
    ImGui::Dummy(ImVec2(size, size));
}

} // namespace okl
