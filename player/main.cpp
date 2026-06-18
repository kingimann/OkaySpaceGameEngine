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

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#endif

#include <algorithm>
#include <string>
#include <unordered_map>
#include <vector>

using namespace okay;

static SDL_Point W2S(const Vec3& p, const Vec3& camPos, float scale, int w, int h) {
    return SDL_Point{(int)(w * 0.5f + (p.x - camPos.x) * scale),
                     (int)(h * 0.5f - (p.y - camPos.y) * scale)};
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

// Draw a string with the built-in 8x8 font as filled rects, top-left at (ox, oy)
// in screen pixels, each font pixel `px` screen pixels wide.
static void DrawText(SDL_Renderer* r, const std::string& text, float ox, float oy,
                     float px, SDL_Color col) {
    if (px < 1.0f) px = 1.0f;
    SDL_SetRenderDrawColor(r, col.r, col.g, col.b, col.a);
    float cx = ox;
    for (char ch : text) {
        if (ch == '\n') { oy += (Font8x8::Height + 1) * px; cx = ox; continue; }
        for (int y = 0; y < Font8x8::Height; ++y)
            for (int x = 0; x < Font8x8::Width; ++x)
                if (Font8x8::Pixel(ch, x, y)) {
                    SDL_Rect cell{(int)(cx + x * px), (int)(oy + y * px),
                                  (int)px + 1, (int)px + 1};
                    SDL_RenderFillRect(r, &cell);
                }
        cx += (Font8x8::Width + 1) * px; // 1px inter-glyph gap
    }
}

// Load (and cache) a sprite texture. Returns nullptr if the image can't be read,
// in which case the caller falls back to a flat colored quad. A null cache entry
// is stored for misses so we don't retry decoding a bad path every frame.
static SDL_Texture* GetTexture(SDL_Renderer* r, const std::string& path,
                               const std::string& baseDir,
                               std::unordered_map<std::string, SDL_Texture*>& cache) {
    if (path.empty()) return nullptr;
    auto it = cache.find(path);
    if (it != cache.end()) return it->second;

    Image img;
    if (!img.Load(path) && !img.Load(baseDir + path)) {
        cache[path] = nullptr; // remember the miss
        return nullptr;
    }
    SDL_Texture* tex = SDL_CreateTexture(r, SDL_PIXELFORMAT_ABGR8888,
                                         SDL_TEXTUREACCESS_STATIC, img.Width(), img.Height());
    if (tex) {
        SDL_UpdateTexture(tex, nullptr, img.Data(), img.Width() * 4);
        SDL_SetTextureBlendMode(tex, SDL_BLENDMODE_BLEND);
    }
    cache[path] = tex;
    return tex;
}

int main(int argc, char** argv) {
    SDL_SetMainReady();
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER | SDL_INIT_AUDIO | SDL_INIT_GAMECONTROLLER) != 0) {
        SDL_Log("SDL_Init failed: %s", SDL_GetError());
        return 1;
    }

    // Resolve the scene path.
    std::string scenePath;
    if (argc > 1) scenePath = argv[1];
    else {
        char* base = SDL_GetBasePath();
        scenePath = (base ? std::string(base) : "") + "game.okayscene";
        if (base) SDL_free(base);
    }

    // Persistent prefs (high scores, settings) live beside the scene file.
    std::string prefsPath;
    {
        char* base = SDL_GetBasePath();
        prefsPath = (base ? std::string(base) : "") + "game.okayprefs";
        if (base) SDL_free(base);
    }
    Prefs::Load(prefsPath);

    Scene scene("Game");
    std::string err;
    if (!SceneSerializer::LoadFromFile(scene, scenePath, &err)) {
        SDL_Log("Could not load %s: %s", scenePath.c_str(), err.c_str());
        // Keep running with an empty scene rather than failing outright.
    }

    SDL_Window* window = SDL_CreateWindow(
        scene.Name().c_str(), SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        960, 600, SDL_WINDOW_RESIZABLE);
    SDL_Renderer* renderer = SDL_CreateRenderer(
        window, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!renderer) renderer = SDL_CreateRenderer(window, -1, 0);

    SDL_AudioSpec want{}, have{};
    want.freq = 44100; want.format = AUDIO_F32SYS; want.channels = 1; want.samples = 1024;
    SDL_AudioDeviceID audioDev = SDL_OpenAudioDevice(nullptr, 0, &want, &have, 0);
    if (audioDev) SDL_PauseAudioDevice(audioDev, 0);

    // Resolve the directory the game files live in, for relative texture paths.
    std::string baseDir;
    {
        char* base = SDL_GetBasePath();
        baseDir = base ? std::string(base) : "";
        if (base) SDL_free(base);
    }
    std::unordered_map<std::string, SDL_Texture*> textureCache;
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

    scene.Start();

    // Open the first connected game controller, if any.
    SDL_GameController* pad = nullptr;
    for (int i = 0; i < SDL_NumJoysticks(); ++i)
        if (SDL_IsGameController(i)) { pad = SDL_GameControllerOpen(i); break; }

    bool running = true;
    Uint64 last = SDL_GetPerformanceCounter();
    auto frame = [&]() {
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_QUIT) running = false;
            if (e.type == SDL_CONTROLLERDEVICEADDED && !pad)
                pad = SDL_GameControllerOpen(e.cdevice.which);
            if (e.type == SDL_KEYDOWN && e.key.keysym.sym == SDLK_ESCAPE) running = false;
        }

        // Feed keyboard into the engine Input.
        const Uint8* ks = SDL_GetKeyboardState(nullptr);
        std::vector<char> down;
        for (char c = 'a'; c <= 'z'; ++c)
            if (ks[SDL_GetScancodeFromKey(c)]) down.push_back(c);
        for (char c = '0'; c <= '9'; ++c)
            if (ks[SDL_GetScancodeFromKey(c)]) down.push_back(c);
        if (ks[SDL_SCANCODE_SPACE]) down.push_back(' ');
        // Map arrow keys onto WASD so arrow-key movement just works.
        if (ks[SDL_SCANCODE_UP])    down.push_back('w');
        if (ks[SDL_SCANCODE_LEFT])  down.push_back('a');
        if (ks[SDL_SCANCODE_DOWN])  down.push_back('s');
        if (ks[SDL_SCANCODE_RIGHT]) down.push_back('d');

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

        // Feed the mouse (position in pixels + left/right/middle button state).
        int mx, my; Uint32 mb = SDL_GetMouseState(&mx, &my);
        unsigned mask = 0;
        if (mb & SDL_BUTTON(SDL_BUTTON_LEFT))   mask |= 1u << 0;
        if (mb & SDL_BUTTON(SDL_BUTTON_RIGHT))  mask |= 1u << 1;
        if (mb & SDL_BUTTON(SDL_BUTTON_MIDDLE)) mask |= 1u << 2;
        Input::FeedMouse(Vec2{(float)mx, (float)my}, mask);

        Uint64 now = SDL_GetPerformanceCounter();
        float dt = (float)((now - last) / (double)SDL_GetPerformanceFrequency());
        last = now;
        if (dt > 0.1f) dt = 0.1f;

        // Publish the render-target size so anchored UI widgets and their
        // hit-tests (run inside scene.Update) agree on where things sit.
        { int cw, ch; SDL_GetRendererOutputSize(renderer, &cw, &ch);
          UICanvas::Set((float)cw, (float)ch); }

        // Drive global Time so ElapsedTime()/DeltaTime()/timeScale work, then
        // advance the scene by the scaled delta (timeScale 0 = paused).
        Time::Step(dt);
        scene.Update(Time::DeltaTime());

        // Keyboard / gamepad menu navigation (arrows/WASD + Enter/Space/A).
        NavigateUI(scene);

        if (audioDev) {
            int n = (int)(dt * 44100.0f); if (n > 8192) n = 8192;
            if (n > 0) {
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
        if (perspective) {
            // Z-buffered software render so overlapping faces occlude correctly,
            // then blit it under the 2D/UI layers (transparent where no geometry).
            Vec3 camPos = (cam && cam->transform) ? cam->transform->Position() : Vec3::Zero;
            Mat4 vp = cam->ProjectionMatrix(h > 0 ? (float)w / h : 1.0f) * cam->ViewMatrix();
            if (w > 0 && h > 0) {
                mesh3D.Resize(w, h);
                mesh3D.Clear(0u);                       // transparent
                RenderMeshes(mesh3D, scene, vp, camPos);
                if (!mesh3DTex || mesh3DW != w || mesh3DH != h) {
                    if (mesh3DTex) SDL_DestroyTexture(mesh3DTex);
                    mesh3DTex = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ABGR8888,
                                                  SDL_TEXTUREACCESS_STREAMING, w, h);
                    SDL_SetTextureBlendMode(mesh3DTex, SDL_BLENDMODE_BLEND);
                    mesh3DW = w; mesh3DH = h;
                }
                SDL_UpdateTexture(mesh3DTex, nullptr, mesh3D.color.data(), w * 4);
                SDL_RenderCopy(renderer, mesh3DTex, nullptr, nullptr);
            }
        } else {
            float ortho = cam ? cam->orthographicSize : 5.0f;
            Vec3 camPos = (cam && cam->transform) ? cam->transform->Position() : Vec3::Zero;
            float scale = h / (2.0f * ortho);
            // Gather active sprites and draw back-to-front by sortOrder (stable,
            // so same-order sprites keep scene order). Enables layered 2D scenes.
            std::vector<GameObject*> sprites;
            for (const auto& up : scene.Objects())
                if (up->active && up->GetComponent<SpriteRenderer>()) sprites.push_back(up.get());
            std::stable_sort(sprites.begin(), sprites.end(), [](GameObject* a, GameObject* b) {
                return a->GetComponent<SpriteRenderer>()->sortOrder <
                       b->GetComponent<SpriteRenderer>()->sortOrder;
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

            // Tilemaps: draw each non-empty cell as a colored quad.
            SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
            for (const auto& up : scene.Objects()) {
                auto* tm = up->GetComponent<Tilemap>();
                if (!tm || !up->active) continue;
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
                if (!ps || !up->active) continue;
                for (const auto& p : ps->Particles()) {
                    if (!p.alive) continue;
                    SDL_Color col{(Uint8)(p.color.r * 255), (Uint8)(p.color.g * 255),
                                  (Uint8)(p.color.b * 255), (Uint8)(p.color.a * 255)};
                    FillWorldQuad(renderer, p.position, p.size, p.size,
                                  camPos, scale, w, h, col);
                }
            }
        }

        // Text (HUD / labels) — drawn last so it sits on top, in 2D or 3D scenes.
        {
            float ortho = cam ? cam->orthographicSize : 5.0f;
            Vec3 camPos = (cam && cam->transform) ? cam->transform->Position() : Vec3::Zero;
            float scale = h / (2.0f * ortho);
            for (const auto& up : scene.Objects()) {
                auto* tr = up->GetComponent<TextRenderer>();
                if (!tr || !up->active) continue;
                SDL_Color col{(Uint8)(tr->color.r * 255), (Uint8)(tr->color.g * 255),
                              (Uint8)(tr->color.b * 255), (Uint8)(tr->color.a * 255)};
                SDL_Color sh{(Uint8)(tr->shadowColor.r * 255), (Uint8)(tr->shadowColor.g * 255),
                             (Uint8)(tr->shadowColor.b * 255), (Uint8)(tr->shadowColor.a * 255)};
                if (tr->screenSpace) {
                    Vec2 o = tr->ResolvedScreenPos((float)w, (float)h);
                    if (tr->shadow)
                        DrawText(renderer, tr->text, o.x + tr->shadowOffset.x * tr->pixelSize,
                                 o.y + tr->shadowOffset.y * tr->pixelSize, tr->pixelSize, sh);
                    DrawText(renderer, tr->text, o.x, o.y, tr->pixelSize, col);
                } else {
                    SDL_Point o = W2S(up->transform->Position(), camPos, scale, w, h);
                    float px = tr->pixelSize * scale;
                    if (tr->shadow)
                        DrawText(renderer, tr->text, o.x + tr->shadowOffset.x * px,
                                 o.y + tr->shadowOffset.y * px, px, sh);
                    DrawText(renderer, tr->text, (float)o.x, (float)o.y, px, col);
                }
            }
        }

        // In-game UI (screen space), drawn on top of everything.
        SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
        for (const auto& up : scene.Objects()) {           // images (logos/icons) first
            auto* im = up->GetComponent<UIImage>();
            if (!im || !up->active) continue;
            Vec2 o = ResolveAnchor(im->anchor, im->position, im->size, (float)w, (float)h);
            SDL_Rect r{(int)o.x, (int)o.y, (int)im->size.x, (int)im->size.y};
            SDL_Texture* tex = GetTexture(renderer, im->texture, baseDir, textureCache);
            if (tex) {
                SDL_SetTextureColorMod(tex, (Uint8)(im->color.r * 255), (Uint8)(im->color.g * 255),
                                       (Uint8)(im->color.b * 255));
                SDL_SetTextureAlphaMod(tex, (Uint8)(im->color.a * 255));
                if (im->nineSlice && im->border > 0.0f) {
                    int tw = 0, th = 0; SDL_QueryTexture(tex, nullptr, nullptr, &tw, &th);
                    int sb = (int)im->border;                       // source border
                    int dbx = (int)im->border, dby = (int)im->border;  // dest border (clamped)
                    if (dbx * 2 > r.w) dbx = r.w / 2;
                    if (dby * 2 > r.h) dby = r.h / 2;
                    // Column x's (src and dst) and row y's: left | middle | right.
                    int sx[4] = {0, sb, tw - sb, tw};
                    int sy[4] = {0, sb, th - sb, th};
                    int dx[4] = {r.x, r.x + dbx, r.x + r.w - dbx, r.x + r.w};
                    int dy[4] = {r.y, r.y + dby, r.y + r.h - dby, r.y + r.h};
                    for (int cy = 0; cy < 3; ++cy)
                        for (int cx = 0; cx < 3; ++cx) {
                            SDL_Rect s{sx[cx], sy[cy], sx[cx + 1] - sx[cx], sy[cy + 1] - sy[cy]};
                            SDL_Rect d{dx[cx], dy[cy], dx[cx + 1] - dx[cx], dy[cy + 1] - dy[cy]};
                            if (s.w > 0 && s.h > 0 && d.w > 0 && d.h > 0)
                                SDL_RenderCopy(renderer, tex, &s, &d);
                        }
                } else {
                    SDL_RenderCopy(renderer, tex, nullptr, &r);
                }
            } else {                                        // no image -> colored fill
                SDL_SetRenderDrawColor(renderer, (Uint8)(im->color.r * 255), (Uint8)(im->color.g * 255),
                                       (Uint8)(im->color.b * 255), (Uint8)(im->color.a * 255));
                SDL_RenderFillRect(renderer, &r);
            }
        }
        for (const auto& up : scene.Objects()) {           // panels (backgrounds) first
            auto* pn = up->GetComponent<UIPanel>();
            if (!pn || !up->active) continue;
            Vec2 o = ResolveAnchor(pn->anchor, pn->position, pn->size, (float)w, (float)h);
            SDL_Rect r{(int)o.x, (int)o.y, (int)pn->size.x, (int)pn->size.y};
            SDL_SetRenderDrawColor(renderer, (Uint8)(pn->color.r * 255), (Uint8)(pn->color.g * 255),
                                   (Uint8)(pn->color.b * 255), (Uint8)(pn->color.a * 255));
            SDL_RenderFillRect(renderer, &r);
        }
        for (const auto& up : scene.Objects()) {           // progress bars
            auto* pb = up->GetComponent<UIProgressBar>();
            if (!pb || !up->active) continue;
            Vec2 o = ResolveAnchor(pb->anchor, pb->position, pb->size, (float)w, (float)h);
            SDL_Rect bg{(int)o.x, (int)o.y, (int)pb->size.x, (int)pb->size.y};
            SDL_SetRenderDrawColor(renderer, (Uint8)(pb->background.r * 255), (Uint8)(pb->background.g * 255),
                                   (Uint8)(pb->background.b * 255), (Uint8)(pb->background.a * 255));
            SDL_RenderFillRect(renderer, &bg);
            SDL_Rect fl{(int)o.x, (int)o.y,
                        (int)(pb->size.x * pb->Fraction()), (int)pb->size.y};
            SDL_SetRenderDrawColor(renderer, (Uint8)(pb->fill.r * 255), (Uint8)(pb->fill.g * 255),
                                   (Uint8)(pb->fill.b * 255), (Uint8)(pb->fill.a * 255));
            SDL_RenderFillRect(renderer, &fl);
        }
        for (const auto& up : scene.Objects()) {           // sliders
            auto* sl = up->GetComponent<UISlider>();
            if (!sl || !up->active) continue;
            Vec2 o = ResolveAnchor(sl->anchor, sl->position, sl->size, (float)w, (float)h);
            SDL_Rect bg{(int)o.x, (int)o.y, (int)sl->size.x, (int)sl->size.y};
            SDL_SetRenderDrawColor(renderer, (Uint8)(sl->background.r * 255), (Uint8)(sl->background.g * 255),
                                   (Uint8)(sl->background.b * 255), (Uint8)(sl->background.a * 255));
            SDL_RenderFillRect(renderer, &bg);
            SDL_Rect fl{(int)o.x, (int)o.y,
                        (int)(sl->size.x * sl->Fraction()), (int)sl->size.y};
            SDL_SetRenderDrawColor(renderer, (Uint8)(sl->fill.r * 255), (Uint8)(sl->fill.g * 255),
                                   (Uint8)(sl->fill.b * 255), (Uint8)(sl->fill.a * 255));
            SDL_RenderFillRect(renderer, &fl);
            int kw = (int)(sl->size.y * 0.6f);
            SDL_Rect kn{(int)(o.x + sl->size.x * sl->Fraction()) - kw / 2,
                        (int)o.y - 2, kw, (int)sl->size.y + 4};
            SDL_SetRenderDrawColor(renderer, (Uint8)(sl->knob.r * 255), (Uint8)(sl->knob.g * 255),
                                   (Uint8)(sl->knob.b * 255), (Uint8)(sl->knob.a * 255));
            SDL_RenderFillRect(renderer, &kn);
        }
        for (const auto& up : scene.Objects()) {           // toggles (checkboxes)
            auto* tg = up->GetComponent<UIToggle>();
            if (!tg || !up->active) continue;
            Vec2 o = ResolveAnchor(tg->anchor, tg->position, tg->size, (float)w, (float)h);
            SDL_Rect box{(int)o.x, (int)o.y, (int)tg->size.x, (int)tg->size.y};
            SDL_SetRenderDrawColor(renderer, (Uint8)(tg->boxColor.r * 255), (Uint8)(tg->boxColor.g * 255),
                                   (Uint8)(tg->boxColor.b * 255), (Uint8)(tg->boxColor.a * 255));
            SDL_RenderFillRect(renderer, &box);
            if (tg->on) {                                  // inset check fill
                int pad = (int)(tg->size.x * 0.22f);
                SDL_Rect chk{box.x + pad, box.y + pad, box.w - 2 * pad, box.h - 2 * pad};
                SDL_SetRenderDrawColor(renderer, (Uint8)(tg->checkColor.r * 255), (Uint8)(tg->checkColor.g * 255),
                                       (Uint8)(tg->checkColor.b * 255), (Uint8)(tg->checkColor.a * 255));
                SDL_RenderFillRect(renderer, &chk);
            }
            float px = 2.0f;
            float tx = o.x + tg->size.x + 8.0f;
            float ty = o.y + (tg->size.y - Font8x8::Height * px) * 0.5f;
            SDL_Color tc{(Uint8)(tg->textColor.r * 255), (Uint8)(tg->textColor.g * 255),
                         (Uint8)(tg->textColor.b * 255), (Uint8)(tg->textColor.a * 255)};
            DrawText(renderer, tg->label, tx, ty, px, tc);
        }
        for (const auto& up : scene.Objects()) {
            auto* btn = up->GetComponent<UIButton>();
            if (!btn || !up->active) continue;
            Color bg = btn->CurrentColor();
            Vec2 o = ResolveAnchor(btn->anchor, btn->position, btn->size, (float)w, (float)h);
            SDL_Rect r{(int)o.x, (int)o.y, (int)btn->size.x, (int)btn->size.y};
            SDL_SetRenderDrawColor(renderer, (Uint8)(bg.r * 255), (Uint8)(bg.g * 255),
                                   (Uint8)(bg.b * 255), (Uint8)(bg.a * 255));
            SDL_RenderFillRect(renderer, &r);
            // Center the label (8px glyphs, ~1px gap) at pixel size 2.
            float px = 2.0f;
            float tw = btn->label.size() * (Font8x8::Width + 1) * px;
            float tx = o.x + (btn->size.x - tw) * 0.5f;
            float ty = o.y + (btn->size.y - Font8x8::Height * px) * 0.5f;
            SDL_Color tc{(Uint8)(btn->textColor.r * 255), (Uint8)(btn->textColor.g * 255),
                         (Uint8)(btn->textColor.b * 255), (Uint8)(btn->textColor.a * 255)};
            DrawText(renderer, btn->label, tx, ty, px, tc);
        }
        SDL_RenderPresent(renderer);
    };

    // Drive the frame loop: the browser owns the loop on web (a blocking while
    // would freeze the tab), so register the frame with Emscripten there.
#ifdef __EMSCRIPTEN__
    (void)running;
    static auto* s_frame = &frame;
    emscripten_set_main_loop([]() { (*s_frame)(); }, 0, 1);
#else
    while (running) frame();
#endif

    Prefs::Save(prefsPath); // persist any prefs the game set this session

    for (auto& kv : textureCache)
        if (kv.second) SDL_DestroyTexture(kv.second);
    if (mesh3DTex) SDL_DestroyTexture(mesh3DTex);
    if (audioDev) SDL_CloseAudioDevice(audioDev);
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}
