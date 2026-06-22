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

// Fill a UI shape (rectangle / rounded / circle / pill) into screen rect `r`,
// scanline by scanline so any silhouette uses one code path. Supports a linear
// gradient (top->bottom, or left->right when `horizontal`); pass equal colors for
// a flat fill. `op` is the canvas master opacity.
static void FillUIShape(SDL_Renderer* ren, const SDL_Rect& r, UIShape shape, float radius,
                        const Color& top, const Color& bottom, bool gradient, bool horizontal,
                        float op) {
    if (r.w <= 0 || r.h <= 0) return;
    auto lerp = [](const Color& a, const Color& b, float t) {
        return Color{a.r + (b.r - a.r) * t, a.g + (b.g - a.g) * t,
                     a.b + (b.b - a.b) * t, a.a + (b.a - a.a) * t};
    };
    for (int row = 0; row < r.h; ++row) {
        float x0, x1;
        if (!UIShapeRowSpan(shape, (float)r.w, (float)r.h, radius, row, x0, x1)) continue;
        if (!gradient) {
            SDL_SetRenderDrawColor(ren, (Uint8)(top.r * 255), (Uint8)(top.g * 255),
                                   (Uint8)(top.b * 255), (Uint8)(top.a * 255 * op));
            SDL_Rect span{r.x + (int)x0, r.y + row, (int)(x1 - x0) + 1, 1};
            SDL_RenderFillRect(ren, &span);
        } else if (!horizontal) {
            float t = r.h > 1 ? (float)row / (r.h - 1) : 0.0f;
            Color c = lerp(top, bottom, t);
            SDL_SetRenderDrawColor(ren, (Uint8)(c.r * 255), (Uint8)(c.g * 255),
                                   (Uint8)(c.b * 255), (Uint8)(c.a * 255 * op));
            SDL_Rect span{r.x + (int)x0, r.y + row, (int)(x1 - x0) + 1, 1};
            SDL_RenderFillRect(ren, &span);
        } else {
            // Horizontal gradient: step across the span pixel-cluster by cluster.
            int ix0 = (int)x0, ix1 = (int)x1;
            for (int x = ix0; x <= ix1; ++x) {
                float t = r.w > 1 ? (float)x / (r.w - 1) : 0.0f;
                Color c = lerp(top, bottom, t);
                SDL_SetRenderDrawColor(ren, (Uint8)(c.r * 255), (Uint8)(c.g * 255),
                                       (Uint8)(c.b * 255), (Uint8)(c.a * 255 * op));
                SDL_Rect px{r.x + x, r.y + row, 1, 1};
                SDL_RenderFillRect(ren, &px);
            }
        }
    }
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
                     float px, SDL_Color col, float letterSp = 0.0f, float lineSp = 0.0f) {
    if (px < 1.0f) px = 1.0f;
    SDL_SetRenderDrawColor(r, col.r, col.g, col.b, col.a);
    float cx = ox;
    for (char ch : text) {
        if (ch == '\n') { oy += (Font8x8::Height + 1 + lineSp) * px; cx = ox; continue; }
        for (int y = 0; y < Font8x8::Height; ++y)
            for (int x = 0; x < Font8x8::Width; ++x)
                if (Font8x8::Pixel(ch, x, y)) {
                    SDL_Rect cell{(int)(cx + x * px), (int)(oy + y * px),
                                  (int)px + 1, (int)px + 1};
                    SDL_RenderFillRect(r, &cell);
                }
        cx += (Font8x8::Width + 1 + letterSp) * px; // inter-glyph gap + letter spacing
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
        bool fullscreen = false, borderless = false, resizable = true, vsync = true;
        bool showCursor = true, quitOnEscape = true, showFps = false;
        int  fpsCap = 0;
        float volume = 1.0f;
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
            else if (k == "borderless") cfg.borderless = std::atoi(v.c_str()) != 0;
            else if (k == "resizable")  cfg.resizable = std::atoi(v.c_str()) != 0;
            else if (k == "vsync")      cfg.vsync = std::atoi(v.c_str()) != 0;
            else if (k == "cursor")     cfg.showCursor = std::atoi(v.c_str()) != 0;
            else if (k == "fps_cap")    cfg.fpsCap = std::atoi(v.c_str());
            else if (k == "quit_on_escape") cfg.quitOnEscape = std::atoi(v.c_str()) != 0;
            else if (k == "volume")     cfg.volume = (float)std::atof(v.c_str());
            else if (k == "show_fps")   cfg.showFps = std::atoi(v.c_str()) != 0;
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
    DataAsset::SetBaseDir(baseDir);   // resolve .okaydata assets beside the game

    Scene scene("Game");
    std::string err;
    if (!SceneSerializer::LoadFromFile(scene, scenePath, &err)) {
        SDL_Log("Could not load %s: %s", scenePath.c_str(), err.c_str());
        // Keep running with an empty scene rather than failing outright.
    }

    Uint32 winFlags = SDL_WINDOW_ALLOW_HIGHDPI;
    if (cfg.resizable)  winFlags |= SDL_WINDOW_RESIZABLE;
    if (cfg.fullscreen) winFlags |= SDL_WINDOW_FULLSCREEN_DESKTOP;
    if (cfg.borderless) winFlags |= SDL_WINDOW_BORDERLESS;
    if (!cfg.showCursor) SDL_ShowCursor(SDL_DISABLE);
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
    AudioMixer::masterVolume = cfg.volume;   // global mix scale from build settings

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
        Uint64 fStart = SDL_GetPerformanceCounter();
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
                if (!typing && cfg.quitOnEscape) running = false;
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
            const int strips = h < 128 ? h : 128;     // smooth enough, cheap
            for (int s = 0; s < strips; ++s) {
                float t = (float)s / (float)(strips - 1);   // 0 (top) .. 1 (bottom)
                // Two-stop gradient: top->horizon for the upper half, horizon->bottom below.
                Color c = (t < 0.5f) ? lerp(rs.skyTop, rs.skyHorizon, t * 2.0f)
                                     : lerp(rs.skyHorizon, rs.skyBottom, (t - 0.5f) * 2.0f);
                SDL_SetRenderDrawColor(renderer, (Uint8)(c.r * 255), (Uint8)(c.g * 255),
                                       (Uint8)(c.b * 255), 255);
                int y0 = (int)((float)s / strips * h);
                int y1 = (int)((float)(s + 1) / strips * h);
                SDL_Rect rrect{0, y0, w, (y1 > y0 ? y1 - y0 : 1)};
                SDL_RenderFillRect(renderer, &rrect);
            }
        }

        if (perspective) {
            // Z-buffered software render so overlapping faces occlude correctly,
            // then blit it under the 2D/UI layers (transparent where no geometry).
            Vec3 camPos = (cam && cam->transform) ? cam->transform->Position() : Vec3::Zero;
            Mat4 vp = cam->ProjectionMatrix(h > 0 ? (float)w / h : 1.0f) * cam->ViewMatrix();
            if (w > 0 && h > 0) {
                ApplySceneLight(scene);                 // a Light object aims the shading
                // Native-resolution software render (FXAA handles edge AA). 2x
                // supersampling is 4x the pixels — far too slow with the full
                // shadow/SSAO/bloom pipeline — so it's off by default.
                static std::vector<std::uint32_t> mesh3DDown;
                const std::uint32_t* px = RenderMeshesSS(mesh3D, mesh3DDown, scene, vp, camPos, w, h, 1,
                                                         cam ? cam->ignoreObject : nullptr);
                if (!mesh3DTex || mesh3DW != w || mesh3DH != h) {
                    if (mesh3DTex) SDL_DestroyTexture(mesh3DTex);
                    mesh3DTex = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ABGR8888,
                                                  SDL_TEXTUREACCESS_STREAMING, w, h);
                    SDL_SetTextureBlendMode(mesh3DTex, SDL_BLENDMODE_BLEND);
                    mesh3DW = w; mesh3DH = h;
                }
                SDL_UpdateTexture(mesh3DTex, nullptr, px, w * 4);
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
            float ortho = cam ? cam->orthographicSize : 5.0f;
            Vec3 camPos = (cam && cam->transform) ? cam->transform->Position() : Vec3::Zero;
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

        // In-game UI (screen space), drawn on top of everything.
        SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
        for (const auto& up : scene.Objects()) {           // drop-target slot backgrounds (behind items)
            auto* dt = up->GetComponent<UIDropTarget>();
            if (!dt || !up->active || !dt->drawBackground || UIHidden(up.get())) continue;
            Vec2 o, sz; if (!GetUIScreenRect(up.get(), (float)w, (float)h, o, sz)) continue;
            SDL_Rect r{(int)o.x, (int)o.y, (int)sz.x, (int)sz.y};
            SDL_SetRenderDrawColor(renderer, (Uint8)(dt->background.r*255), (Uint8)(dt->background.g*255),
                                   (Uint8)(dt->background.b*255), (Uint8)(dt->background.a*255));
            SDL_RenderFillRect(renderer, &r);
            for (int b = 0; b < (int)dt->borderWidth; ++b) {   // outline
                SDL_Rect br{r.x+b, r.y+b, r.w-2*b, r.h-2*b};
                SDL_SetRenderDrawColor(renderer, (Uint8)(dt->borderColor.r*255), (Uint8)(dt->borderColor.g*255),
                                       (Uint8)(dt->borderColor.b*255), (Uint8)(dt->borderColor.a*255));
                SDL_RenderDrawRect(renderer, &br);
            }
        }
        for (const auto& up : scene.Objects()) {           // images (logos/icons) first
            auto* im = up->GetComponent<UIImage>();
            if (!im || !up->active || UIHidden(up.get())) continue;
            float op = UIOpacity(up.get());   // canvas master fade
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
                SDL_SetTextureAlphaMod(tex, (Uint8)(im->color.a * 255 * op));
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
            } else {                                        // no image -> colored shape fill
                const SDL_Rect& rr = filled ? fr : r;
                FillUIShape(renderer, rr, im->shape, im->cornerRadius,
                            im->color, im->color, false, false, op);
            }
        }
        for (const auto& up : scene.Objects()) {           // panels (backgrounds) first
            auto* pn = up->GetComponent<UIPanel>();
            if (!pn || !up->active || UIHidden(up.get())) continue;
            float op = UIOpacity(up.get());   // canvas master fade
            Vec2 o = ResolveAnchor(pn->anchor, pn->position, pn->size, (float)w, (float)h);
            enterScroll(up.get(), o);
            SDL_Rect r{(int)o.x, (int)o.y, (int)pn->size.x, (int)pn->size.y};
            if (pn->shadow) {                               // drop shadow behind (same shape)
                SDL_Rect sh{r.x + (int)pn->shadowOffset.x, r.y + (int)pn->shadowOffset.y, r.w, r.h};
                FillUIShape(renderer, sh, pn->shape, pn->cornerRadius,
                            pn->shadowColor, pn->shadowColor, false, false, op);
            }
            if (pn->borderWidth > 0.0f) {                   // border = outer shape, then inner fill
                FillUIShape(renderer, r, pn->shape, pn->cornerRadius,
                            pn->borderColor, pn->borderColor, false, false, op);
                int b = (int)pn->borderWidth;
                SDL_Rect inner{r.x + b, r.y + b, r.w - 2 * b, r.h - 2 * b};
                float innerR = pn->cornerRadius - b; if (innerR < 0.0f) innerR = 0.0f;
                FillUIShape(renderer, inner, pn->shape, innerR,
                            pn->color, pn->colorBottom, pn->useGradient, pn->gradientHorizontal, op);
            } else {
                FillUIShape(renderer, r, pn->shape, pn->cornerRadius,
                            pn->color, pn->colorBottom, pn->useGradient, pn->gradientHorizontal, op);
            }
        }
        for (const auto& up : scene.Objects()) {           // drop-target highlight (drag feedback)
            auto* dt = up->GetComponent<UIDropTarget>();
            if (!dt || !up->active || !dt->showHighlight || !dt->IsHovered()) continue;
            Vec2 o, sz;
            if (!GetUIScreenRect(up.get(), (float)w, (float)h, o, sz)) continue;
            SDL_Rect hr{(int)o.x, (int)o.y, (int)sz.x, (int)sz.y};
            const Color& hc = dt->HasValid() ? dt->highlight : dt->rejectHighlight;  // green-ish vs reject
            SDL_SetRenderDrawColor(renderer, (Uint8)(hc.r * 255), (Uint8)(hc.g * 255),
                                   (Uint8)(hc.b * 255), (Uint8)(hc.a * 255));
            SDL_RenderFillRect(renderer, &hr);
        }
        for (const auto& up : scene.Objects()) {           // scroll-view backgrounds + scrollbar
            auto* sv = up->GetComponent<UIScrollView>();
            if (!sv || !up->active || UIHidden(up.get())) continue;
            float op = UIOpacity(up.get());
            Vec2 o = ResolveAnchor(sv->anchor, sv->position, sv->size, (float)w, (float)h);
            SDL_Rect box{(int)o.x, (int)o.y, (int)sv->size.x, (int)sv->size.y};
            SDL_SetRenderDrawColor(renderer, (Uint8)(sv->background.r * 255), (Uint8)(sv->background.g * 255),
                                   (Uint8)(sv->background.b * 255), (Uint8)(sv->background.a * 255 * op));
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
                                       (Uint8)(sv->barColor.b * 255), (Uint8)(sv->barColor.a * 255 * op));
                SDL_RenderFillRect(renderer, &thumb);
            }
        }
        for (const auto& up : scene.Objects()) {           // progress bars
            auto* pb = up->GetComponent<UIProgressBar>();
            if (!pb || !up->active || UIHidden(up.get())) continue;
            float op = UIOpacity(up.get());
            Vec2 o = ResolveAnchor(pb->anchor, pb->position, pb->size, (float)w, (float)h);
            enterScroll(up.get(), o);
            SDL_Rect bg{(int)o.x, (int)o.y, (int)pb->size.x, (int)pb->size.y};
            FillUIShape(renderer, bg, pb->shape, pb->cornerRadius,
                        pb->background, pb->background, false, false, op);
            float pfox, pfoy, pfw, pfh; pb->FillRect(pb->size.x, pb->size.y, pfox, pfoy, pfw, pfh);
            SDL_Rect fl{(int)(o.x + pfox), (int)(o.y + pfoy), (int)pfw, (int)pfh};
            bool horiz = pb->fillDir == UIProgressBar::FillDir::LeftRight ||
                         pb->fillDir == UIProgressBar::FillDir::RightLeft;
            FillUIShape(renderer, fl, pb->shape, pb->cornerRadius,
                        pb->fill, pb->fillEnd, pb->gradientFill, horiz, op);
            if (pb->showPercent) {
                char pct[8]; std::snprintf(pct, sizeof(pct), "%d%%", (int)(pb->Fraction() * 100.0f + 0.5f));
                float px = 2.0f;
                float tw = std::strlen(pct) * (Font8x8::Width + 1) * px;
                SDL_Color tc{(Uint8)(pb->textColor.r * 255), (Uint8)(pb->textColor.g * 255),
                             (Uint8)(pb->textColor.b * 255), (Uint8)(pb->textColor.a * 255 * op)};
                DrawText(renderer, pct, o.x + (pb->size.x - tw) * 0.5f,
                         o.y + (pb->size.y - Font8x8::Height * px) * 0.5f, px, tc);
            }
        }
        for (const auto& up : scene.Objects()) {           // sliders
            auto* sl = up->GetComponent<UISlider>();
            if (!sl || !up->active || UIHidden(up.get())) continue;
            float op = UIOpacity(up.get());
            Vec2 o = ResolveAnchor(sl->anchor, sl->position, sl->size, (float)w, (float)h);
            enterScroll(up.get(), o);
            SDL_Rect bg{(int)o.x, (int)o.y, (int)sl->size.x, (int)sl->size.y};
            FillUIShape(renderer, bg, sl->trackShape, sl->cornerRadius,
                        sl->background, sl->background, false, false, op);
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
            FillUIShape(renderer, fl, sl->trackShape, sl->cornerRadius,
                        sl->fill, sl->fill, false, false, op);
            // The handle: a circle when roundKnob, else a rounded tab.
            FillUIShape(renderer, kn, sl->roundKnob ? UIShape::Circle : UIShape::Rounded,
                        sl->cornerRadius, sl->knob, sl->knob, false, false, op);
            if (sl->showValue) {
                char vbuf[16]; std::snprintf(vbuf, sizeof(vbuf), "%.2f", sl->value);
                float px = 2.0f;
                SDL_Color tc{(Uint8)(sl->textColor.r * 255), (Uint8)(sl->textColor.g * 255),
                             (Uint8)(sl->textColor.b * 255), (Uint8)(sl->textColor.a * 255 * op)};
                DrawText(renderer, vbuf, o.x + sl->size.x + 8.0f,
                         o.y + (sl->size.y - Font8x8::Height * px) * 0.5f, px, tc);
            }
            if (!sl->interactable) { SDL_Rect dr{(int)o.x, (int)o.y, (int)sl->size.x, (int)sl->size.y};
                SDL_SetRenderDrawColor(renderer, 30, 30, 35, 150); SDL_RenderFillRect(renderer, &dr); }
        }
        for (const auto& up : scene.Objects()) {           // toggles (checkboxes)
            auto* tg = up->GetComponent<UIToggle>();
            if (!tg || !up->active || UIHidden(up.get())) continue;
            float op = UIOpacity(up.get());
            Vec2 o = ResolveAnchor(tg->anchor, tg->position, tg->size, (float)w, (float)h);
            enterScroll(up.get(), o);
            SDL_Rect box{(int)o.x, (int)o.y, (int)tg->size.x, (int)tg->size.y};
            float t = tg->AnimT();                          // 0=off..1=on (smoothed)
            if (tg->style == UIToggle::Style::Switch) {     // pill track + sliding knob
                auto mix = [t](const Color& a, const Color& b) {
                    return Color{a.r + (b.r - a.r) * t, a.g + (b.g - a.g) * t,
                                 a.b + (b.b - a.b) * t, a.a + (b.a - a.a) * t};
                };
                Color trk = mix(tg->boxColor, tg->checkColor);   // cross-fade the track
                FillUIShape(renderer, box, UIShape::Pill, 0.0f, trk, trk, false, false, op);
                int kd = box.h - 4;
                int kx = box.x + 2 + (int)((box.w - kd - 4) * t);   // glide the knob
                SDL_Rect knob{kx, box.y + 2, kd, kd};
                FillUIShape(renderer, knob, UIShape::Circle, 0.0f,
                            tg->knobColor, tg->knobColor, false, false, op);
            } else {
                FillUIShape(renderer, box, UIShape::Rounded, tg->cornerRadius,
                            tg->boxColor, tg->boxColor, false, false, op);
                if (t > 0.01f) {                            // inset check fill (fades in)
                    int pad = (int)(tg->size.x * 0.22f);
                    SDL_Rect chk{box.x + pad, box.y + pad, box.w - 2 * pad, box.h - 2 * pad};
                    Color cc = tg->checkColor; cc.a *= t;
                    FillUIShape(renderer, chk, UIShape::Rounded, tg->cornerRadius * 0.6f,
                                cc, cc, false, false, op);
                }
            }
            float px = 2.0f;
            float tx = o.x + tg->size.x + 8.0f;
            float ty = o.y + (tg->size.y - Font8x8::Height * px) * 0.5f;
            SDL_Color tc{(Uint8)(tg->textColor.r * 255), (Uint8)(tg->textColor.g * 255),
                         (Uint8)(tg->textColor.b * 255), (Uint8)(tg->textColor.a * 255 * op)};
            DrawText(renderer, tg->label, tx, ty, px, tc);
            if (!tg->interactable) { SDL_Rect dr{box.x, box.y, box.w, box.h};
                SDL_SetRenderDrawColor(renderer, 30, 30, 35, 150); SDL_RenderFillRect(renderer, &dr); }
        }
        for (const auto& up : scene.Objects()) {
            auto* btn = up->GetComponent<UIButton>();
            if (!btn || !up->active || UIHidden(up.get())) continue;
            float op = UIOpacity(up.get());
            Color bg = btn->DisplayColor();
            Vec2 o = ResolveAnchor(btn->anchor, btn->position, btn->size, (float)w, (float)h);
            enterScroll(up.get(), o);
            SDL_Rect r{(int)o.x, (int)o.y, (int)btn->size.x, (int)btn->size.y};
            if (btn->hoverScale != 1.0f && (btn->IsHovered() || btn->IsFocused())) {
                int gx = (int)(btn->size.x * (btn->hoverScale - 1.0f) * 0.5f);
                int gy = (int)(btn->size.y * (btn->hoverScale - 1.0f) * 0.5f);
                r.x -= gx; r.y -= gy; r.w += 2 * gx; r.h += 2 * gy;
            }
            if (btn->shadow) {                              // drop shadow behind (same shape)
                SDL_Rect sh{r.x + (int)btn->shadowOffset.x, r.y + (int)btn->shadowOffset.y, r.w, r.h};
                FillUIShape(renderer, sh, btn->shape, btn->cornerRadius,
                            btn->shadowColor, btn->shadowColor, false, false, op);
            }
            if (btn->borderWidth > 0.0f) {                  // border = outer shape, then inner fill
                FillUIShape(renderer, r, btn->shape, btn->cornerRadius,
                            btn->borderColor, btn->borderColor, false, false, op);
                int b = (int)btn->borderWidth;
                SDL_Rect inner{r.x + b, r.y + b, r.w - 2 * b, r.h - 2 * b};
                float innerR = btn->cornerRadius - b; if (innerR < 0.0f) innerR = 0.0f;
                FillUIShape(renderer, inner, btn->shape, innerR, bg, bg, false, false, op);
            } else {
                FillUIShape(renderer, r, btn->shape, btn->cornerRadius, bg, bg, false, false, op);
            }
            // Optional icon (left by default, right when iconRight); the label
            // takes the remaining space. Press shifts content down slightly.
            float shift = btn->PressShift();
            float isz = (!btn->icon.empty() && btn->iconSize > 0.0f) ? btn->iconSize : 0.0f;
            if (isz > 0.0f) {
                SDL_Texture* itex = GetTexture(renderer, btn->icon, baseDir, textureCache);
                float ix = btn->iconRight ? (o.x + btn->size.x - isz - 8.0f) : (o.x + 8.0f);
                SDL_Rect ir{(int)ix, (int)(o.y + (btn->size.y - isz) * 0.5f + shift), (int)isz, (int)isz};
                if (itex) { SDL_SetTextureColorMod(itex, 255, 255, 255); SDL_SetTextureAlphaMod(itex, 255);
                            SDL_RenderCopy(renderer, itex, nullptr, &ir); }
            }
            // Center the label at the button's font scale, within the area beside
            // the icon.
            float px = btn->fontScale;
            float tw = btn->label.size() * (Font8x8::Width + 1) * px;
            float left  = o.x + (isz > 0.0f && !btn->iconRight ? isz + 12.0f : 0.0f);
            float right = o.x + btn->size.x - (isz > 0.0f && btn->iconRight ? isz + 12.0f : 0.0f);
            float tx = left + ((right - left) - tw) * 0.5f;
            float ty = o.y + (btn->size.y - Font8x8::Height * px) * 0.5f + shift;
            Color tcc = btn->CurrentTextColor();
            SDL_Color tc{(Uint8)(tcc.r * 255), (Uint8)(tcc.g * 255), (Uint8)(tcc.b * 255), (Uint8)(tcc.a * 255 * op)};
            DrawText(renderer, btn->label, tx, ty, px, tc);
        }
        for (const auto& up : scene.Objects()) {           // screen-space text — on top of panels/controls
            auto* tr = up->GetComponent<TextRenderer>();
            if (!tr || !up->active || !tr->screenSpace || UIHidden(up.get())) continue;
            float op = UIOpacity(up.get());   // canvas master fade
            SDL_Color col{(Uint8)(tr->color.r * 255), (Uint8)(tr->color.g * 255),
                          (Uint8)(tr->color.b * 255), (Uint8)(tr->color.a * 255 * op)};
            SDL_Color sh{(Uint8)(tr->shadowColor.r * 255), (Uint8)(tr->shadowColor.g * 255),
                         (Uint8)(tr->shadowColor.b * 255), (Uint8)(tr->shadowColor.a * 255 * op)};
            SDL_Color ol{(Uint8)(tr->outlineColor.r * 255), (Uint8)(tr->outlineColor.g * 255),
                         (Uint8)(tr->outlineColor.b * 255), (Uint8)(tr->outlineColor.a * 255 * op)};
            if (tr->background) {                       // label background box
                Vec2 b = tr->BoxTopLeft((float)w, (float)h);
                SDL_Rect br{(int)b.x, (int)b.y, (int)tr->size.x, (int)tr->size.y};
                SDL_SetRenderDrawColor(renderer, (Uint8)(tr->backgroundColor.r * 255),
                                       (Uint8)(tr->backgroundColor.g * 255), (Uint8)(tr->backgroundColor.b * 255),
                                       (Uint8)(tr->backgroundColor.a * 255 * op));
                SDL_RenderFillRect(renderer, &br);
            }
            Vec2 o = tr->ResolvedScreenPos((float)w, (float)h);   // align handled inside
            std::string disp = tr->DisplayText();
            float p = tr->pixelSize, ls = tr->letterSpacing, lp = tr->lineSpacing;
            if (tr->shadow)
                DrawText(renderer, disp, o.x + tr->shadowOffset.x * p,
                         o.y + tr->shadowOffset.y * p, p, sh, ls, lp);
            if (tr->outline) {
                DrawText(renderer, disp, o.x - p, o.y, p, ol, ls, lp);
                DrawText(renderer, disp, o.x + p, o.y, p, ol, ls, lp);
                DrawText(renderer, disp, o.x, o.y - p, p, ol, ls, lp);
                DrawText(renderer, disp, o.x, o.y + p, p, ol, ls, lp);
            }
            DrawText(renderer, disp, o.x, o.y, p, col, ls, lp);
            if (tr->bold) DrawText(renderer, disp, o.x + p, o.y, p, col, ls, lp);
        }
        for (const auto& up : scene.Objects()) {           // dropdowns (header + open list)
            auto* dd = up->GetComponent<UIDropdown>();
            if (!dd || !up->active || UIHidden(up.get())) continue;
            Vec2 o = ResolveAnchor(dd->anchor, dd->position, dd->size, (float)w, (float)h);
            enterScroll(up.get(), o);
            SDL_Rect hdr{(int)o.x, (int)o.y, (int)dd->size.x, (int)dd->size.y};
            FillUIShape(renderer, hdr, dd->shape, dd->cornerRadius,
                        dd->color, dd->color, false, false, 1.0f);
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
            if (!in || !up->active || UIHidden(up.get())) continue;
            Vec2 o = ResolveAnchor(in->anchor, in->position, in->size, (float)w, (float)h);
            enterScroll(up.get(), o);
            SDL_Rect box{(int)o.x, (int)o.y, (int)in->size.x, (int)in->size.y};
            Color bg = in->CurrentColor();
            // Focus ring: draw the border shape behind, then the field inset over it.
            if (in->focused && in->borderWidth > 0.0f) {
                FillUIShape(renderer, box, in->shape, in->cornerRadius,
                            in->borderColor, in->borderColor, false, false, 1.0f);
                int b = (int)in->borderWidth;
                SDL_Rect inner{box.x + b, box.y + b, box.w - 2 * b, box.h - 2 * b};
                float ir = in->cornerRadius - b; if (ir < 0.0f) ir = 0.0f;
                FillUIShape(renderer, inner, in->shape, ir, bg, bg, false, false, 1.0f);
            } else {
                FillUIShape(renderer, box, in->shape, in->cornerRadius, bg, bg, false, false, 1.0f);
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

        // Optional FPS overlay (top-left) when enabled in build settings.
        if (cfg.showFps) {
            static float fpsSmooth = 0.0f;
            float inst = dt > 0.0001f ? 1.0f / dt : 0.0f;
            fpsSmooth = fpsSmooth <= 0.0f ? inst : fpsSmooth * 0.9f + inst * 0.1f;
            char buf[32]; std::snprintf(buf, sizeof(buf), "FPS %d", (int)(fpsSmooth + 0.5f));
            float px = 2.0f;
            float tw = (float)std::char_traits<char>::length(buf) * (Font8x8::Width + 1) * px;
            SDL_Rect bg{4, 4, (int)(tw + 8), (int)(Font8x8::Height * px + 8)};
            SDL_SetRenderDrawColor(renderer, 0, 0, 0, 160);
            SDL_RenderFillRect(renderer, &bg);
            DrawText(renderer, buf, 8, 8, px, SDL_Color{80, 255, 120, 255});
        }
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
