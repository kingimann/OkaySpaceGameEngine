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
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
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

    // Resolve where the game files live (beside the executable).
    std::string baseDir;
    { char* base = SDL_GetBasePath(); baseDir = base ? base : ""; if (base) SDL_free(base); }

    // Build settings (written by the editor's Build Settings) live in an optional
    // game.okayconfig beside the exe: window size/title/flags, the scene list (so
    // load_scene_index works) and which scene starts. Sensible defaults if absent.
    struct GameConfig {
        std::string title = "OkaySpace Game";
        int  width = 960, height = 600;
        bool fullscreen = false, resizable = true, vsync = true;
        std::string startup;
        std::vector<std::string> scenes;
    } cfg;
    {
        std::ifstream cf(baseDir + "game.okayconfig");
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
            else if (k == "resizable")  cfg.resizable = std::atoi(v.c_str()) != 0;
            else if (k == "vsync")      cfg.vsync = std::atoi(v.c_str()) != 0;
            else if (k == "startup")    cfg.startup = v;
            else if (k == "scene")      cfg.scenes.push_back(v);
        }
    }
    // Register the build's scenes so scripts can load_scene_index / load_next.
    SceneManager::ClearScenes();
    for (const std::string& s : cfg.scenes) SceneManager::AddScene(baseDir + s);

    // Resolve the scene path: CLI arg > config startup > game.okayscene.
    std::string scenePath;
    if (argc > 1) scenePath = argv[1];
    else if (!cfg.startup.empty()) scenePath = baseDir + cfg.startup;
    else scenePath = baseDir + "game.okayscene";

    // Persistent prefs (high scores, settings) live beside the scene file.
    std::string prefsPath = baseDir + "game.okayprefs";
    Prefs::Load(prefsPath);

    Scene scene("Game");
    std::string err;
    if (!SceneSerializer::LoadFromFile(scene, scenePath, &err)) {
        SDL_Log("Could not load %s: %s", scenePath.c_str(), err.c_str());
        // Keep running with an empty scene rather than failing outright.
    }

    Uint32 winFlags = SDL_WINDOW_ALLOW_HIGHDPI;
    if (cfg.resizable)  winFlags |= SDL_WINDOW_RESIZABLE;
    if (cfg.fullscreen) winFlags |= SDL_WINDOW_FULLSCREEN_DESKTOP;
    std::string title = !cfg.title.empty() ? cfg.title : scene.Name();
    SDL_Window* window = SDL_CreateWindow(
        title.c_str(), SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        cfg.width, cfg.height, winFlags);
    Uint32 renFlags = SDL_RENDERER_ACCELERATED | (cfg.vsync ? SDL_RENDERER_PRESENTVSYNC : 0);
    SDL_Renderer* renderer = SDL_CreateRenderer(window, -1, renFlags);
    if (!renderer) renderer = SDL_CreateRenderer(window, -1, 0);
    SDL_StartTextInput();   // deliver SDL_TEXTINPUT events for UI input fields

    SDL_AudioSpec want{}, have{};
    want.freq = 44100; want.format = AUDIO_F32SYS; want.channels = 1; want.samples = 1024;
    SDL_AudioDeviceID audioDev = SDL_OpenAudioDevice(nullptr, 0, &want, &have, 0);
    if (audioDev) SDL_PauseAudioDevice(audioDev, 0);

    // baseDir (resolved above) is where the game files live, for relative paths.
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
        Input::ClearTypedText();                 // collect this frame's typed chars
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_QUIT) running = false;
            if (e.type == SDL_CONTROLLERDEVICEADDED && !pad)
                pad = SDL_GameControllerOpen(e.cdevice.which);
            if (e.type == SDL_TEXTINPUT) Input::FeedText(e.text.text);   // real characters
            if (e.type == SDL_MOUSEWHEEL) Input::FeedMouseWheel((float)e.wheel.y);
            // Esc quits only when no input field is focused (otherwise it cancels it).
            if (e.type == SDL_KEYDOWN && e.key.keysym.sym == SDLK_ESCAPE) {
                bool typing = false;
                for (const auto& up : scene.Objects())
                    if (auto* f = up->GetComponent<UIInputField>()) if (f->focused) typing = true;
                if (!typing) running = false;
            }
        }

        // Feed keyboard into the engine Input.
        const Uint8* ks = SDL_GetKeyboardState(nullptr);
        std::vector<char> down;
        for (char c = 'a'; c <= 'z'; ++c)
            if (ks[SDL_GetScancodeFromKey(c)]) down.push_back(c);
        for (char c = '0'; c <= '9'; ++c)
            if (ks[SDL_GetScancodeFromKey(c)]) down.push_back(c);
        if (ks[SDL_SCANCODE_SPACE]) down.push_back(' ');
        // Editing keys for text fields (held-state; edge-detected by the field).
        if (ks[SDL_SCANCODE_BACKSPACE]) down.push_back((char)8);
        if (ks[SDL_SCANCODE_RETURN] || ks[SDL_SCANCODE_KP_ENTER]) down.push_back('\r');
        if (ks[SDL_SCANCODE_ESCAPE]) down.push_back((char)27);
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
                if (scene.mainCamera)   // listener for 3D/spatial audio sources
                    AudioMixer::SetListener(scene.mainCamera->gameObject->transform->Position());
                std::vector<float> ab(n);
                AudioMixer::Render(scene, ab.data(), n);
                SDL_QueueAudio(audioDev, ab.data(), (Uint32)(n * sizeof(float)));
            }
        }

        int w, h; SDL_GetRendererOutputSize(renderer, &w, &h);
        // Scroll-view membership: offset a widget's origin by its owning Scroll
        // View's scroll and clip drawing to the viewport, so scrollable content
        // shows and hides correctly in the built game. Call once per UI widget
        // right after resolving its origin; widgets outside a scroll view reset
        // the clip to none.
        auto enterScroll = [&](GameObject* g, Vec2& o) {
            if (UIScrollView* psv = OwningScrollView(g)) {
                Vec2 vp = ResolveAnchor(psv->anchor, psv->position, psv->size, (float)w, (float)h);
                o.y -= psv->scroll;
                SDL_Rect clip{(int)vp.x, (int)vp.y, (int)psv->size.x, (int)psv->size.y};
                SDL_RenderSetClipRect(renderer, &clip);
            } else {
                SDL_RenderSetClipRect(renderer, nullptr);
            }
        };
        Camera* cam = scene.mainCamera;
        Color bg = cam ? cam->backgroundColor : Color::Black;
        SDL_SetRenderDrawColor(renderer, (Uint8)(bg.r * 255), (Uint8)(bg.g * 255),
                               (Uint8)(bg.b * 255), 255);
        SDL_RenderClear(renderer);

        bool perspective = cam && cam->projection == Camera::Projection::Perspective;

        // Skybox: the same vertical sky gradient the editor previews, baked into
        // the scene's render settings so a built game looks identical. Drawn
        // first (behind everything) for 3D/perspective scenes.
        if (perspective && scene.renderSettings.skybox && w > 0 && h > 0) {
            const auto& rs = scene.renderSettings;
            auto sc = [](const Color& c) {
                SDL_Color o; o.r = (Uint8)(c.r * 255); o.g = (Uint8)(c.g * 255);
                o.b = (Uint8)(c.b * 255); o.a = 255; return o;
            };
            SDL_Color top = sc(rs.skyTop), mid = sc(rs.skyHorizon), bot = sc(rs.skyBottom);
            float my = h * 0.5f;
            auto band = [&](float y0, float y1, SDL_Color c0, SDL_Color c1) {
                SDL_Vertex v[4] = {
                    {{0.0f, y0}, c0, {0, 0}}, {{(float)w, y0}, c0, {0, 0}},
                    {{(float)w, y1}, c1, {0, 0}}, {{0.0f, y1}, c1, {0, 0}},
                };
                int idx[6] = {0, 1, 2, 0, 2, 3};
                SDL_RenderGeometry(renderer, nullptr, v, 4, idx, 6);
            };
            band(0.0f, my, top, mid);
            band(my, (float)h, mid, bot);
        }

        if (perspective) {
            // Z-buffered software render so overlapping faces occlude correctly,
            // then blit it under the 2D/UI layers (transparent where no geometry).
            Vec3 camPos = (cam && cam->transform) ? cam->transform->Position() : Vec3::Zero;
            Mat4 vp = cam->ProjectionMatrix(h > 0 ? (float)w / h : 1.0f) * cam->ViewMatrix();
            if (w > 0 && h > 0) {
                mesh3D.Resize(w, h);
                mesh3D.Clear(0u);                       // transparent
                ApplySceneLight(scene);                 // a Light object aims the shading
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
                SDL_Color ol{(Uint8)(tr->outlineColor.r * 255), (Uint8)(tr->outlineColor.g * 255),
                             (Uint8)(tr->outlineColor.b * 255), (Uint8)(tr->outlineColor.a * 255)};
                if (tr->screenSpace) {
                    Vec2 o = tr->ResolvedScreenPos((float)w, (float)h);
                    float tw = tr->PixelWidth() * tr->pixelSize;
                    if (tr->align == 1)      o.x -= tw * 0.5f;
                    else if (tr->align == 2) o.x -= tw;
                    float p = tr->pixelSize;
                    if (tr->shadow)
                        DrawText(renderer, tr->text, o.x + tr->shadowOffset.x * p,
                                 o.y + tr->shadowOffset.y * p, p, sh);
                    if (tr->outline) {
                        DrawText(renderer, tr->text, o.x - p, o.y, p, ol);
                        DrawText(renderer, tr->text, o.x + p, o.y, p, ol);
                        DrawText(renderer, tr->text, o.x, o.y - p, p, ol);
                        DrawText(renderer, tr->text, o.x, o.y + p, p, ol);
                    }
                    DrawText(renderer, tr->text, o.x, o.y, p, col);
                    if (tr->bold) DrawText(renderer, tr->text, o.x + p, o.y, p, col);
                } else {
                    SDL_Point o = W2S(up->transform->Position(), camPos, scale, w, h);
                    float px = tr->pixelSize * scale;
                    if (tr->shadow)
                        DrawText(renderer, tr->text, o.x + tr->shadowOffset.x * px,
                                 o.y + tr->shadowOffset.y * px, px, sh);
                    if (tr->outline) {
                        DrawText(renderer, tr->text, o.x - px, o.y, px, ol);
                        DrawText(renderer, tr->text, o.x + px, o.y, px, ol);
                        DrawText(renderer, tr->text, o.x, o.y - px, px, ol);
                        DrawText(renderer, tr->text, o.x, o.y + px, px, ol);
                    }
                    DrawText(renderer, tr->text, (float)o.x, (float)o.y, px, col);
                    if (tr->bold) DrawText(renderer, tr->text, (float)o.x + px, (float)o.y, px, col);
                }
            }
        }

        // In-game UI (screen space), drawn on top of everything.
        SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
        for (const auto& up : scene.Objects()) {           // images (logos/icons) first
            auto* im = up->GetComponent<UIImage>();
            if (!im || !up->active) continue;
            Vec2 o = ResolveAnchor(im->anchor, im->position, im->size, (float)w, (float)h);
            enterScroll(up.get(), o);
            SDL_Rect r{(int)o.x, (int)o.y, (int)im->size.x, (int)im->size.y};
            // Radial/linear fill: shrink the drawn rect to fillAmount along an axis.
            float fox, foy, fw, fh;
            im->FilledRect(im->size.x, im->size.y, fox, foy, fw, fh);
            SDL_Rect fr{(int)(o.x + fox), (int)(o.y + foy), (int)fw, (int)fh};
            bool filled = im->fillMode != UIImage::FillMode::None;
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
                } else if (filled) {                        // reveal a proportional slice
                    int tw = 0, th = 0; SDL_QueryTexture(tex, nullptr, nullptr, &tw, &th);
                    SDL_Rect src{
                        (int)(im->size.x > 0 ? fox / im->size.x * tw : 0),
                        (int)(im->size.y > 0 ? foy / im->size.y * th : 0),
                        (int)(im->size.x > 0 ? fw  / im->size.x * tw : tw),
                        (int)(im->size.y > 0 ? fh  / im->size.y * th : th)};
                    SDL_RenderCopy(renderer, tex, &src, &fr);
                } else {
                    SDL_RenderCopy(renderer, tex, nullptr, &r);
                }
            } else {                                        // no image -> colored fill
                SDL_SetRenderDrawColor(renderer, (Uint8)(im->color.r * 255), (Uint8)(im->color.g * 255),
                                       (Uint8)(im->color.b * 255), (Uint8)(im->color.a * 255));
                SDL_RenderFillRect(renderer, filled ? &fr : &r);
            }
        }
        for (const auto& up : scene.Objects()) {           // panels (backgrounds) first
            auto* pn = up->GetComponent<UIPanel>();
            if (!pn || !up->active) continue;
            Vec2 o = ResolveAnchor(pn->anchor, pn->position, pn->size, (float)w, (float)h);
            enterScroll(up.get(), o);
            SDL_Rect r{(int)o.x, (int)o.y, (int)pn->size.x, (int)pn->size.y};
            if (pn->shadow) {                               // drop shadow behind
                SDL_Rect sh{r.x + (int)pn->shadowOffset.x, r.y + (int)pn->shadowOffset.y, r.w, r.h};
                SDL_SetRenderDrawColor(renderer, (Uint8)(pn->shadowColor.r * 255), (Uint8)(pn->shadowColor.g * 255),
                                       (Uint8)(pn->shadowColor.b * 255), (Uint8)(pn->shadowColor.a * 255));
                SDL_RenderFillRect(renderer, &sh);
            }
            if (pn->useGradient) {                          // top->bottom fade in bands
                int bands = r.h > 0 ? (r.h < 64 ? r.h : 64) : 1;
                for (int i = 0; i < bands; ++i) {
                    float t = bands > 1 ? (float)i / (bands - 1) : 0.0f;
                    Color cc{pn->color.r + (pn->colorBottom.r - pn->color.r) * t,
                             pn->color.g + (pn->colorBottom.g - pn->color.g) * t,
                             pn->color.b + (pn->colorBottom.b - pn->color.b) * t,
                             pn->color.a + (pn->colorBottom.a - pn->color.a) * t};
                    SDL_Rect band{r.x, r.y + i * r.h / bands, r.w,
                                  (r.h / bands) + 1};
                    SDL_SetRenderDrawColor(renderer, (Uint8)(cc.r * 255), (Uint8)(cc.g * 255),
                                           (Uint8)(cc.b * 255), (Uint8)(cc.a * 255));
                    SDL_RenderFillRect(renderer, &band);
                }
            } else {
                SDL_SetRenderDrawColor(renderer, (Uint8)(pn->color.r * 255), (Uint8)(pn->color.g * 255),
                                       (Uint8)(pn->color.b * 255), (Uint8)(pn->color.a * 255));
                SDL_RenderFillRect(renderer, &r);
            }
            if (pn->borderWidth > 0.0f) {                   // outline (N nested rects)
                SDL_SetRenderDrawColor(renderer, (Uint8)(pn->borderColor.r * 255), (Uint8)(pn->borderColor.g * 255),
                                       (Uint8)(pn->borderColor.b * 255), (Uint8)(pn->borderColor.a * 255));
                for (int bw = 0; bw < (int)pn->borderWidth; ++bw) {
                    SDL_Rect br{r.x + bw, r.y + bw, r.w - 2 * bw, r.h - 2 * bw};
                    SDL_RenderDrawRect(renderer, &br);
                }
            }
        }
        for (const auto& up : scene.Objects()) {           // scroll-view backgrounds + scrollbar
            auto* sv = up->GetComponent<UIScrollView>();
            if (!sv || !up->active) continue;
            Vec2 o = ResolveAnchor(sv->anchor, sv->position, sv->size, (float)w, (float)h);
            SDL_Rect box{(int)o.x, (int)o.y, (int)sv->size.x, (int)sv->size.y};
            SDL_SetRenderDrawColor(renderer, (Uint8)(sv->background.r * 255), (Uint8)(sv->background.g * 255),
                                   (Uint8)(sv->background.b * 255), (Uint8)(sv->background.a * 255));
            SDL_RenderFillRect(renderer, &box);
            if (sv->ScrollMax() > 0.0f) {                   // scrollbar track + thumb
                int barW = 6;
                SDL_Rect track{box.x + box.w - barW - 2, box.y + 2, barW, box.h - 4};
                SDL_SetRenderDrawColor(renderer, 255, 255, 255, 30);
                SDL_RenderFillRect(renderer, &track);
                float frac = sv->size.y / (sv->contentHeight > 1.0f ? sv->contentHeight : 1.0f);
                int thumbH = (int)(track.h * (frac < 1.0f ? frac : 1.0f));
                int thumbY = track.y + (int)((track.h - thumbH) * sv->Fraction());
                SDL_Rect thumb{track.x, thumbY, barW, thumbH};
                SDL_SetRenderDrawColor(renderer, (Uint8)(sv->barColor.r * 255), (Uint8)(sv->barColor.g * 255),
                                       (Uint8)(sv->barColor.b * 255), (Uint8)(sv->barColor.a * 255));
                SDL_RenderFillRect(renderer, &thumb);
            }
        }
        for (const auto& up : scene.Objects()) {           // progress bars
            auto* pb = up->GetComponent<UIProgressBar>();
            if (!pb || !up->active) continue;
            Vec2 o = ResolveAnchor(pb->anchor, pb->position, pb->size, (float)w, (float)h);
            enterScroll(up.get(), o);
            SDL_Rect bg{(int)o.x, (int)o.y, (int)pb->size.x, (int)pb->size.y};
            SDL_SetRenderDrawColor(renderer, (Uint8)(pb->background.r * 255), (Uint8)(pb->background.g * 255),
                                   (Uint8)(pb->background.b * 255), (Uint8)(pb->background.a * 255));
            SDL_RenderFillRect(renderer, &bg);
            float pfox, pfoy, pfw, pfh; pb->FillRect(pb->size.x, pb->size.y, pfox, pfoy, pfw, pfh);
            SDL_Rect fl{(int)(o.x + pfox), (int)(o.y + pfoy), (int)pfw, (int)pfh};
            SDL_SetRenderDrawColor(renderer, (Uint8)(pb->fill.r * 255), (Uint8)(pb->fill.g * 255),
                                   (Uint8)(pb->fill.b * 255), (Uint8)(pb->fill.a * 255));
            SDL_RenderFillRect(renderer, &fl);
            if (pb->showPercent) {
                char pct[8]; std::snprintf(pct, sizeof(pct), "%d%%", (int)(pb->Fraction() * 100.0f + 0.5f));
                float px = 2.0f;
                float tw = std::strlen(pct) * (Font8x8::Width + 1) * px;
                SDL_Color tc{(Uint8)(pb->textColor.r * 255), (Uint8)(pb->textColor.g * 255),
                             (Uint8)(pb->textColor.b * 255), (Uint8)(pb->textColor.a * 255)};
                DrawText(renderer, pct, o.x + (pb->size.x - tw) * 0.5f,
                         o.y + (pb->size.y - Font8x8::Height * px) * 0.5f, px, tc);
            }
        }
        for (const auto& up : scene.Objects()) {           // sliders
            auto* sl = up->GetComponent<UISlider>();
            if (!sl || !up->active) continue;
            Vec2 o = ResolveAnchor(sl->anchor, sl->position, sl->size, (float)w, (float)h);
            enterScroll(up.get(), o);
            SDL_Rect bg{(int)o.x, (int)o.y, (int)sl->size.x, (int)sl->size.y};
            SDL_SetRenderDrawColor(renderer, (Uint8)(sl->background.r * 255), (Uint8)(sl->background.g * 255),
                                   (Uint8)(sl->background.b * 255), (Uint8)(sl->background.a * 255));
            SDL_RenderFillRect(renderer, &bg);
            float f = sl->Fraction();
            SDL_Rect fl, kn;
            if (sl->vertical) {
                fl = {(int)o.x, (int)(o.y + sl->size.y * (1.0f - f)), (int)sl->size.x, (int)(sl->size.y * f)};
                int kh = (int)(sl->size.x * sl->knobSize);
                kn = {(int)o.x - 2, (int)(o.y + sl->size.y * (1.0f - f)) - kh / 2, (int)sl->size.x + 4, kh};
            } else {
                fl = {(int)o.x, (int)o.y, (int)(sl->size.x * f), (int)sl->size.y};
                int kw = (int)(sl->size.y * sl->knobSize);
                kn = {(int)(o.x + sl->size.x * f) - kw / 2, (int)o.y - 2, kw, (int)sl->size.y + 4};
            }
            SDL_SetRenderDrawColor(renderer, (Uint8)(sl->fill.r * 255), (Uint8)(sl->fill.g * 255),
                                   (Uint8)(sl->fill.b * 255), (Uint8)(sl->fill.a * 255));
            SDL_RenderFillRect(renderer, &fl);
            SDL_SetRenderDrawColor(renderer, (Uint8)(sl->knob.r * 255), (Uint8)(sl->knob.g * 255),
                                   (Uint8)(sl->knob.b * 255), (Uint8)(sl->knob.a * 255));
            SDL_RenderFillRect(renderer, &kn);
            if (sl->showValue) {
                char vbuf[16]; std::snprintf(vbuf, sizeof(vbuf), "%.2f", sl->value);
                float px = 2.0f;
                SDL_Color tc{(Uint8)(sl->textColor.r * 255), (Uint8)(sl->textColor.g * 255),
                             (Uint8)(sl->textColor.b * 255), (Uint8)(sl->textColor.a * 255)};
                DrawText(renderer, vbuf, o.x + sl->size.x + 8.0f,
                         o.y + (sl->size.y - Font8x8::Height * px) * 0.5f, px, tc);
            }
            if (!sl->interactable) { SDL_Rect dr{(int)o.x, (int)o.y, (int)sl->size.x, (int)sl->size.y};
                SDL_SetRenderDrawColor(renderer, 30, 30, 35, 150); SDL_RenderFillRect(renderer, &dr); }
        }
        for (const auto& up : scene.Objects()) {           // toggles (checkboxes)
            auto* tg = up->GetComponent<UIToggle>();
            if (!tg || !up->active) continue;
            Vec2 o = ResolveAnchor(tg->anchor, tg->position, tg->size, (float)w, (float)h);
            enterScroll(up.get(), o);
            SDL_Rect box{(int)o.x, (int)o.y, (int)tg->size.x, (int)tg->size.y};
            if (tg->style == UIToggle::Style::Switch) {     // pill track + sliding knob
                const Color& trk = tg->on ? tg->checkColor : tg->boxColor;
                SDL_SetRenderDrawColor(renderer, (Uint8)(trk.r * 255), (Uint8)(trk.g * 255),
                                       (Uint8)(trk.b * 255), (Uint8)(trk.a * 255));
                SDL_RenderFillRect(renderer, &box);
                int kd = box.h - 4;
                int kx = tg->on ? (box.x + box.w - kd - 2) : (box.x + 2);
                SDL_Rect knob{kx, box.y + 2, kd, kd};
                SDL_SetRenderDrawColor(renderer, (Uint8)(tg->knobColor.r * 255), (Uint8)(tg->knobColor.g * 255),
                                       (Uint8)(tg->knobColor.b * 255), (Uint8)(tg->knobColor.a * 255));
                SDL_RenderFillRect(renderer, &knob);
            } else {
                SDL_SetRenderDrawColor(renderer, (Uint8)(tg->boxColor.r * 255), (Uint8)(tg->boxColor.g * 255),
                                       (Uint8)(tg->boxColor.b * 255), (Uint8)(tg->boxColor.a * 255));
                SDL_RenderFillRect(renderer, &box);
                if (tg->on) {                              // inset check fill
                    int pad = (int)(tg->size.x * 0.22f);
                    SDL_Rect chk{box.x + pad, box.y + pad, box.w - 2 * pad, box.h - 2 * pad};
                    SDL_SetRenderDrawColor(renderer, (Uint8)(tg->checkColor.r * 255), (Uint8)(tg->checkColor.g * 255),
                                           (Uint8)(tg->checkColor.b * 255), (Uint8)(tg->checkColor.a * 255));
                    SDL_RenderFillRect(renderer, &chk);
                }
            }
            float px = 2.0f;
            float tx = o.x + tg->size.x + 8.0f;
            float ty = o.y + (tg->size.y - Font8x8::Height * px) * 0.5f;
            SDL_Color tc{(Uint8)(tg->textColor.r * 255), (Uint8)(tg->textColor.g * 255),
                         (Uint8)(tg->textColor.b * 255), (Uint8)(tg->textColor.a * 255)};
            DrawText(renderer, tg->label, tx, ty, px, tc);
            if (!tg->interactable) { SDL_Rect dr{box.x, box.y, box.w, box.h};
                SDL_SetRenderDrawColor(renderer, 30, 30, 35, 150); SDL_RenderFillRect(renderer, &dr); }
        }
        for (const auto& up : scene.Objects()) {
            auto* btn = up->GetComponent<UIButton>();
            if (!btn || !up->active) continue;
            Color bg = btn->CurrentColor();
            Vec2 o = ResolveAnchor(btn->anchor, btn->position, btn->size, (float)w, (float)h);
            enterScroll(up.get(), o);
            SDL_Rect r{(int)o.x, (int)o.y, (int)btn->size.x, (int)btn->size.y};
            if (btn->hoverScale != 1.0f && (btn->IsHovered() || btn->IsFocused())) {
                int gx = (int)(btn->size.x * (btn->hoverScale - 1.0f) * 0.5f);
                int gy = (int)(btn->size.y * (btn->hoverScale - 1.0f) * 0.5f);
                r.x -= gx; r.y -= gy; r.w += 2 * gx; r.h += 2 * gy;
            }
            SDL_SetRenderDrawColor(renderer, (Uint8)(bg.r * 255), (Uint8)(bg.g * 255),
                                   (Uint8)(bg.b * 255), (Uint8)(bg.a * 255));
            SDL_RenderFillRect(renderer, &r);
            if (btn->borderWidth > 0.0f) {                  // outline (N nested rects)
                SDL_SetRenderDrawColor(renderer, (Uint8)(btn->borderColor.r * 255), (Uint8)(btn->borderColor.g * 255),
                                       (Uint8)(btn->borderColor.b * 255), (Uint8)(btn->borderColor.a * 255));
                for (int bw = 0; bw < (int)btn->borderWidth; ++bw) {
                    SDL_Rect br{r.x + bw, r.y + bw, r.w - 2 * bw, r.h - 2 * bw};
                    SDL_RenderDrawRect(renderer, &br);
                }
            }
            // Optional icon at the left; the label shifts right to make room.
            float isz = (!btn->icon.empty() && btn->iconSize > 0.0f) ? btn->iconSize : 0.0f;
            if (isz > 0.0f) {
                SDL_Texture* itex = GetTexture(renderer, btn->icon, baseDir, textureCache);
                SDL_Rect ir{(int)(o.x + 8), (int)(o.y + (btn->size.y - isz) * 0.5f), (int)isz, (int)isz};
                if (itex) { SDL_SetTextureColorMod(itex, 255, 255, 255); SDL_SetTextureAlphaMod(itex, 255);
                            SDL_RenderCopy(renderer, itex, nullptr, &ir); }
            }
            // Center the label (8px glyphs, ~1px gap) at the button's font scale,
            // within the area right of the icon.
            float px = btn->fontScale;
            float tw = btn->label.size() * (Font8x8::Width + 1) * px;
            float left = o.x + (isz > 0.0f ? isz + 12.0f : 0.0f);
            float avail = (o.x + btn->size.x) - left;
            float tx = left + (avail - tw) * 0.5f;
            float ty = o.y + (btn->size.y - Font8x8::Height * px) * 0.5f;
            SDL_Color tc{(Uint8)(btn->textColor.r * 255), (Uint8)(btn->textColor.g * 255),
                         (Uint8)(btn->textColor.b * 255), (Uint8)(btn->textColor.a * 255)};
            DrawText(renderer, btn->label, tx, ty, px, tc);
        }
        for (const auto& up : scene.Objects()) {           // dropdowns (header + open list)
            auto* dd = up->GetComponent<UIDropdown>();
            if (!dd || !up->active) continue;
            Vec2 o = ResolveAnchor(dd->anchor, dd->position, dd->size, (float)w, (float)h);
            enterScroll(up.get(), o);
            SDL_Rect hdr{(int)o.x, (int)o.y, (int)dd->size.x, (int)dd->size.y};
            SDL_SetRenderDrawColor(renderer, (Uint8)(dd->color.r * 255), (Uint8)(dd->color.g * 255),
                                   (Uint8)(dd->color.b * 255), (Uint8)(dd->color.a * 255));
            SDL_RenderFillRect(renderer, &hdr);
            SDL_SetRenderDrawColor(renderer, (Uint8)(dd->borderColor.r * 255), (Uint8)(dd->borderColor.g * 255),
                                   (Uint8)(dd->borderColor.b * 255), (Uint8)(dd->borderColor.a * 255));
            SDL_RenderDrawRect(renderer, &hdr);
            float px = 2.0f;
            float ty = o.y + (dd->size.y - Font8x8::Height * px) * 0.5f;
            SDL_Color tc{(Uint8)(dd->textColor.r * 255), (Uint8)(dd->textColor.g * 255),
                         (Uint8)(dd->textColor.b * 255), (Uint8)(dd->textColor.a * 255)};
            SDL_Color htc = dd->HasSelection() ? tc : SDL_Color{150, 152, 158, 255};
            DrawText(renderer, dd->HeaderText(), o.x + 8.0f, ty, px, htc);
            if (dd->open) {
                float top = o.y + dd->size.y;
                for (int i = 0; i < (int)dd->options.size(); ++i) {
                    SDL_Rect orow{(int)o.x, (int)(top + i * dd->size.y), (int)dd->size.x, (int)dd->size.y};
                    const Color& rc = (i == dd->HoveredOption()) ? dd->hoverColor : dd->listColor;
                    SDL_SetRenderDrawColor(renderer, (Uint8)(rc.r * 255), (Uint8)(rc.g * 255),
                                           (Uint8)(rc.b * 255), (Uint8)(rc.a * 255));
                    SDL_RenderFillRect(renderer, &orow);
                    DrawText(renderer, dd->options[i], o.x + 8.0f,
                             orow.y + (dd->size.y - Font8x8::Height * px) * 0.5f, px, tc);
                }
            }
            if (!dd->interactable) { SDL_Rect dr{(int)o.x, (int)o.y, (int)dd->size.x, (int)dd->size.y};
                SDL_SetRenderDrawColor(renderer, 30, 30, 35, 150); SDL_RenderFillRect(renderer, &dr); }
        }
        for (const auto& up : scene.Objects()) {           // input fields (box + text + caret)
            auto* in = up->GetComponent<UIInputField>();
            if (!in || !up->active) continue;
            Vec2 o = ResolveAnchor(in->anchor, in->position, in->size, (float)w, (float)h);
            enterScroll(up.get(), o);
            SDL_Rect box{(int)o.x, (int)o.y, (int)in->size.x, (int)in->size.y};
            Color bg = in->CurrentColor();
            SDL_SetRenderDrawColor(renderer, (Uint8)(bg.r * 255), (Uint8)(bg.g * 255),
                                   (Uint8)(bg.b * 255), (Uint8)(bg.a * 255));
            SDL_RenderFillRect(renderer, &box);
            if (in->focused) {                              // focus outline
                SDL_SetRenderDrawColor(renderer, 120, 170, 255, 255);
                SDL_RenderDrawRect(renderer, &box);
            }
            float px = 2.0f, pad = 6.0f;
            bool empty = in->text.empty();
            std::string full = empty ? in->placeholder : in->DisplayText();
            const Color& txc = empty ? in->placeholderColor : in->textColor;
            // Horizontal scroll: show the tail that fits so the caret stays visible.
            float adv = (Font8x8::Width + 1) * px;
            int fit = adv > 0 ? (int)((in->size.x - pad * 2) / adv) : (int)full.size();
            if (fit < 1) fit = 1;
            std::string shown = ((int)full.size() > fit) ? full.substr(full.size() - fit) : full;
            float ty = o.y + (in->size.y - Font8x8::Height * px) * 0.5f;
            SDL_Color tc{(Uint8)(txc.r * 255), (Uint8)(txc.g * 255), (Uint8)(txc.b * 255), (Uint8)(txc.a * 255)};
            DrawText(renderer, shown, o.x + pad, ty, px, tc);
            if (in->focused && in->CaretVisible()) {        // blinking caret after visible text
                int cx = (int)(o.x + pad + shown.size() * adv);
                SDL_SetRenderDrawColor(renderer, 255, 255, 255, 220);
                SDL_RenderDrawLine(renderer, cx, (int)ty, cx, (int)(ty + Font8x8::Height * px));
            }
        }
        SDL_RenderSetClipRect(renderer, nullptr);   // end scroll clipping
        for (const auto& up : scene.Objects()) {           // keyboard/gamepad focus ring
            if (!up->active || !IsUIFocused(up.get())) continue;
            UIRect r = GetUIRect(up.get());
            if (!r.valid || !r.position) continue;
            Vec2 o = ResolveAnchor(r.anchor, *r.position, r.size, (float)w, (float)h);
            SDL_Rect ring{(int)o.x - 2, (int)o.y - 2, (int)r.size.x + 4, (int)r.size.y + 4};
            SDL_SetRenderDrawColor(renderer, 255, 210, 90, 255);
            SDL_RenderDrawRect(renderer, &ring);
            SDL_Rect ring2{ring.x - 1, ring.y - 1, ring.w + 2, ring.h + 2};
            SDL_RenderDrawRect(renderer, &ring2);
        }
        for (const auto& up : scene.Objects()) {           // tooltips (hover hints)
            auto* tt = up->GetComponent<UITooltip>();
            if (!tt || !up->active || !tt->Ready()) continue;
            Vec2 m = Input::MousePosition();
            float px = 2.0f;
            float tw = tt->text.size() * (Font8x8::Width + 1) * px;
            float th = Font8x8::Height * px;
            SDL_Rect box{(int)(m.x + 14), (int)(m.y + 14), (int)(tw + 12), (int)(th + 10)};
            SDL_SetRenderDrawColor(renderer, (Uint8)(tt->background.r * 255), (Uint8)(tt->background.g * 255),
                                   (Uint8)(tt->background.b * 255), (Uint8)(tt->background.a * 255));
            SDL_RenderFillRect(renderer, &box);
            SDL_SetRenderDrawColor(renderer, (Uint8)(tt->borderColor.r * 255), (Uint8)(tt->borderColor.g * 255),
                                   (Uint8)(tt->borderColor.b * 255), (Uint8)(tt->borderColor.a * 255));
            SDL_RenderDrawRect(renderer, &box);
            SDL_Color tc{(Uint8)(tt->textColor.r * 255), (Uint8)(tt->textColor.g * 255),
                         (Uint8)(tt->textColor.b * 255), (Uint8)(tt->textColor.a * 255)};
            DrawText(renderer, tt->text, m.x + 20, m.y + 19, px, tc);
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
