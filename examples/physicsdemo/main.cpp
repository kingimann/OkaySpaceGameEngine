// OkaySpace physics demo — boxes fall under gravity, bounce, and settle on a
// floor, all rendered in the console. Shows Rigidbody2D + colliders + the
// Physics2D world driving the scene.
#include <Okay.hpp>
#include <cstdlib>

using namespace okay;

// Respawns a fallen body back at the top so the demo keeps going.
class Respawner : public Behaviour {
public:
    float resetBelowY = -10.0f;
    float spawnY = 9.0f;
    Rigidbody2D* body = nullptr;
    void Update(float) override {
        if (transform->localPosition.y < resetBelowY) {
            transform->localPosition.y = spawnY;
            transform->localPosition.x = Random::Shared().Range(-8.0f, 8.0f);
            if (body) body->velocity = Vec2::Zero;
        }
    }
};

static GameObject* MakeBox(Scene& s, Vec2 pos, Vec2 size, char glyph, Color col,
                           bool dynamic, float bounce) {
    GameObject* go = s.CreateGameObject("Box");
    go->transform->localPosition = {pos.x, pos.y, 0};
    auto* sr = go->AddComponent<SpriteRenderer>();
    sr->size = size; sr->glyph = glyph; sr->color = col;
    go->AddComponent<BoxCollider2D>()->size = size;
    if (dynamic) {
        auto* rb = go->AddComponent<Rigidbody2D>();
        rb->bounciness = bounce;
        auto* r = go->AddComponent<Respawner>();
        r->body = rb;
    }
    return go;
}

int main(int argc, char** argv) {
    Application::Config cfg;
    cfg.title  = "OkaySpace Physics";
    cfg.width  = 70;
    cfg.height = 30;
    cfg.targetFps = 30.0f;
    cfg.maxFrames = (argc > 1) ? std::atoi(argv[1]) : 200;
    Application app(cfg);

    Scene scene("Physics");
    scene.physics().gravity = {0.0f, -12.0f};

    auto* cam = scene.CreateGameObject("Camera")->AddComponent<Camera>();
    cam->orthographicSize = 12.0f;
    cam->backgroundColor  = Color::FromBytes(5, 5, 15);

    // Floor and side walls (static colliders, no rigidbody).
    MakeBox(scene, {0, -10}, {30, 2}, '=', Color::FromBytes(120, 120, 140), false, 0);
    MakeBox(scene, {-11, 0}, {2, 22}, '|', Color::FromBytes(80, 80, 100), false, 0);
    MakeBox(scene, {11, 0},  {2, 22}, '|', Color::FromBytes(80, 80, 100), false, 0);

    // Falling, bouncing boxes.
    const char* glyphs = "@O*#%&";
    Color colors[] = {Color::Green, Color::Cyan, Color::Yellow,
                      Color::Magenta, Color::Red, Color::White};
    for (int i = 0; i < 6; ++i) {
        float x = -7.0f + i * 2.6f;
        MakeBox(scene, {x, 4.0f + i}, {1.4f, 1.4f}, glyphs[i], colors[i],
                true, 0.4f + 0.1f * i);
    }

    app.Run(scene);
    return 0;
}
