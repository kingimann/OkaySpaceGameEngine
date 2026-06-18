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

#include <algorithm>
#include <string>
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

int main(int argc, char** argv) {
    SDL_SetMainReady();
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER | SDL_INIT_AUDIO) != 0) {
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

    scene.Start();

    bool running = true;
    Uint64 last = SDL_GetPerformanceCounter();
    while (running) {
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_QUIT) running = false;
            if (e.type == SDL_KEYDOWN && e.key.keysym.sym == SDLK_ESCAPE) running = false;
        }

        // Feed keyboard into the engine Input.
        const Uint8* ks = SDL_GetKeyboardState(nullptr);
        std::vector<char> down;
        for (char c = 'a'; c <= 'z'; ++c)
            if (ks[SDL_GetScancodeFromKey(c)]) down.push_back(c);
        if (ks[SDL_SCANCODE_SPACE]) down.push_back(' ');
        Input::FeedKeys(down);

        Uint64 now = SDL_GetPerformanceCounter();
        float dt = (float)((now - last) / (double)SDL_GetPerformanceFrequency());
        last = now;
        if (dt > 0.1f) dt = 0.1f;

        scene.Update(dt);

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
            // Filled, flat-shaded, back-face-culled, painter-sorted triangles.
            Vec3 camPos = (cam && cam->transform) ? cam->transform->Position() : Vec3::Zero;
            Mat4 vp = cam->ProjectionMatrix(h > 0 ? (float)w / h : 1.0f) * cam->ViewMatrix();
            Vec3 lightDir = Vec3{-0.4f, -1.0f, -0.6f}.Normalized(); // fixed key light

            struct Tri { float depth; SDL_Vertex v[3]; };
            std::vector<Tri> tris;
            for (const auto& up : scene.Objects()) {
                auto* mr = up->GetComponent<MeshRenderer>();
                if (!mr || !up->active) continue;
                Mat4 model = up->transform->LocalToWorldMatrix();
                const auto& v = mr->mesh.vertices;
                const auto& t = mr->mesh.triangles;
                for (size_t i = 0; i + 2 < t.size(); i += 3) {
                    Vec3 wp[3];
                    for (int k = 0; k < 3; ++k) wp[k] = model.MultiplyPoint(v[t[i + k]]);
                    Vec3 normal = Vec3::Cross(wp[1] - wp[0], wp[2] - wp[0]).Normalized();
                    Vec3 centroid = (wp[0] + wp[1] + wp[2]) * (1.0f / 3.0f);
                    // Back-face cull: skip triangles facing away from the camera.
                    if (Vec3::Dot(normal, camPos - centroid) < 0.0f) continue;

                    SDL_FPoint sp[3]; float wsum = 0; bool ok = true;
                    for (int k = 0; k < 3; ++k) {
                        Vec4 c = vp * Vec4{wp[k], 1.0f};
                        if (c.w <= 0.05f) { ok = false; break; }
                        sp[k].x = w * 0.5f + (c.x / c.w) * w * 0.5f;
                        sp[k].y = h * 0.5f - (c.y / c.w) * h * 0.5f;
                        wsum += c.w;
                    }
                    if (!ok) continue;
                    float lambert = Vec3::Dot(normal, lightDir * -1.0f);
                    float shade = 0.25f + 0.75f * (lambert > 0 ? lambert : 0);
                    SDL_Color col{(Uint8)(mr->color.r * 255 * shade),
                                  (Uint8)(mr->color.g * 255 * shade),
                                  (Uint8)(mr->color.b * 255 * shade), 255};
                    Tri tri; tri.depth = wsum / 3.0f;
                    for (int k = 0; k < 3; ++k) tri.v[k] = SDL_Vertex{sp[k], col, {0, 0}};
                    tris.push_back(tri);
                }
            }
            std::sort(tris.begin(), tris.end(),
                      [](const Tri& a, const Tri& b) { return a.depth > b.depth; });
            for (const auto& tr : tris)
                SDL_RenderGeometry(renderer, nullptr, tr.v, 3, nullptr, 0);
        } else {
            float ortho = cam ? cam->orthographicSize : 5.0f;
            Vec3 camPos = (cam && cam->transform) ? cam->transform->Position() : Vec3::Zero;
            float scale = h / (2.0f * ortho);
            for (const auto& up : scene.Objects()) {
                auto* sr = up->GetComponent<SpriteRenderer>();
                if (!sr || !up->active) continue;
                // Rotate/scale the sprite quad through the full transform so 2D
                // games can spin and skew sprites, not just place axis-aligned ones.
                Mat4 model = up->transform->LocalToWorldMatrix();
                float hx = sr->size.x * 0.5f, hy = sr->size.y * 0.5f;
                Vec3 corners[4] = {{-hx, -hy, 0}, {hx, -hy, 0}, {hx, hy, 0}, {-hx, hy, 0}};
                SDL_Color col{(Uint8)(sr->color.r * 255), (Uint8)(sr->color.g * 255),
                              (Uint8)(sr->color.b * 255), (Uint8)(sr->color.a * 255)};
                SDL_Vertex vtx[4];
                for (int k = 0; k < 4; ++k) {
                    Vec3 wpos = model.MultiplyPoint(corners[k]);
                    SDL_Point s = W2S(wpos, camPos, scale, w, h);
                    vtx[k] = SDL_Vertex{{(float)s.x, (float)s.y}, col, {0, 0}};
                }
                const int idx[6] = {0, 1, 2, 0, 2, 3};
                SDL_RenderGeometry(renderer, nullptr, vtx, 4, idx, 6);
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
        SDL_RenderPresent(renderer);
    }

    if (audioDev) SDL_CloseAudioDevice(audioDev);
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}
