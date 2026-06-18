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

#include <string>
#include <vector>

using namespace okay;

static SDL_Point W2S(const Vec3& p, const Vec3& camPos, float scale, int w, int h) {
    return SDL_Point{(int)(w * 0.5f + (p.x - camPos.x) * scale),
                     (int)(h * 0.5f - (p.y - camPos.y) * scale)};
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
            Mat4 vp = cam->ProjectionMatrix(h > 0 ? (float)w / h : 1.0f) * cam->ViewMatrix();
            for (const auto& up : scene.Objects()) {
                auto* mr = up->GetComponent<MeshRenderer>();
                if (!mr || !up->active) continue;
                Mat4 m = vp * up->transform->LocalToWorldMatrix();
                SDL_SetRenderDrawColor(renderer, (Uint8)(mr->color.r * 255),
                                       (Uint8)(mr->color.g * 255), (Uint8)(mr->color.b * 255), 255);
                const auto& v = mr->mesh.vertices;
                const auto& t = mr->mesh.triangles;
                for (size_t i = 0; i + 2 < t.size(); i += 3) {
                    SDL_Point p[3]; bool ok = true;
                    for (int k = 0; k < 3; ++k) {
                        Vec4 c = m * Vec4{v[t[i + k]], 1.0f};
                        if (c.w <= 0.05f) { ok = false; break; }
                        p[k].x = (int)(w * 0.5f + (c.x / c.w) * w * 0.5f);
                        p[k].y = (int)(h * 0.5f - (c.y / c.w) * h * 0.5f);
                    }
                    if (!ok) continue;
                    SDL_RenderDrawLine(renderer, p[0].x, p[0].y, p[1].x, p[1].y);
                    SDL_RenderDrawLine(renderer, p[1].x, p[1].y, p[2].x, p[2].y);
                    SDL_RenderDrawLine(renderer, p[2].x, p[2].y, p[0].x, p[0].y);
                }
            }
        } else {
            float ortho = cam ? cam->orthographicSize : 5.0f;
            Vec3 camPos = (cam && cam->transform) ? cam->transform->Position() : Vec3::Zero;
            float scale = h / (2.0f * ortho);
            for (const auto& up : scene.Objects()) {
                auto* sr = up->GetComponent<SpriteRenderer>();
                if (!sr || !up->active) continue;
                Vec3 wp = up->transform->Position();
                Vec3 ls = up->transform->LossyScale();
                SDL_Point c = W2S(wp, camPos, scale, w, h);
                int hw = (int)(sr->size.x * ls.x * 0.5f * scale);
                int hh = (int)(sr->size.y * ls.y * 0.5f * scale);
                SDL_Rect r{c.x - hw, c.y - hh, hw * 2, hh * 2};
                SDL_SetRenderDrawColor(renderer, (Uint8)(sr->color.r * 255),
                                       (Uint8)(sr->color.g * 255), (Uint8)(sr->color.b * 255), 255);
                SDL_RenderFillRect(renderer, &r);
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
