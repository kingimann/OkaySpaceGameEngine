// OkaySpace sandbox — a tiny "solar system" that doubles as a feature tour.
//
// It demonstrates: GameObjects & Components, custom scripts (Behaviour) with
// the Awake/Start/Update lifecycle, the Transform hierarchy (moons parented to
// planets), an orthographic Camera, the SpriteRenderer, Input, and Time.
#include <Okay.hpp>
#include <cstdlib>
#include <cstdio>
#include <string>

#if defined(_WIN32)
#  include <io.h>
#  define OKAY_ISATTY(fd) _isatty(fd)
#  define OKAY_FILENO(f)  _fileno(f)
#else
#  include <unistd.h>
#  define OKAY_ISATTY(fd) isatty(fd)
#  define OKAY_FILENO(f)  fileno(f)
#endif

using namespace okay;

// True when launched in an interactive console (e.g. double-clicked .exe or a
// terminal) rather than piped/redirected (CI, tests, logs).
static bool IsInteractive() {
    return OKAY_ISATTY(OKAY_FILENO(stdin)) != 0;
}

// ---------------------------------------------------------------------------
// A script that orbits its Transform around the local origin of its parent.
// ---------------------------------------------------------------------------
class Orbit : public Behaviour {
public:
    float radius      = 4.0f;
    float speedDeg    = 60.0f;   // degrees per second
    float phaseDeg    = 0.0f;    // starting angle

    void Update(float dt) override {
        m_angle += speedDeg * dt;
        float r = (m_angle + phaseDeg) * Mathf::Deg2Rad;
        transform->localPosition = {Mathf::Cos(r) * radius,
                                    Mathf::Sin(r) * radius, 0.0f};
    }
private:
    float m_angle = 0.0f;
};

// ---------------------------------------------------------------------------
// A spin script: continuously rotates the Transform about the Z axis.
// ---------------------------------------------------------------------------
class Spin : public Behaviour {
public:
    float speedDeg = 90.0f;
    void Update(float dt) override { transform->Rotate({0, 0, speedDeg * dt}); }
};

// ---------------------------------------------------------------------------
// Player ship: WASD movement when interactive, with a gentle autopilot
// fallback so the demo still animates in a headless terminal.
// ---------------------------------------------------------------------------
class PlayerController : public Behaviour {
public:
    float speed = 6.0f;

    void Start() override {
        OKAY_INFO("Player '", gameObject->name, "' ready. WASD to move, Q to quit.");
    }

    void Update(float dt) override {
        Vec2 axis = Input::AxisWASD();
        if (axis.SqrMagnitude() > 0.0f) {
            transform->Translate(Vec3{axis.Normalized() * (speed * dt)});
        } else {
            // Autopilot: trace a Lissajous figure using elapsed time.
            float t = Time::ElapsedTime();
            transform->localPosition = {Mathf::Sin(t * 1.3f) * 8.0f,
                                        Mathf::Sin(t * 0.9f) * 4.0f, 0.0f};
        }
    }
};

static GameObject* MakeBody(Scene& scene, const std::string& name,
                            float size, char glyph, Color color) {
    GameObject* go = scene.CreateGameObject(name);
    go->transform->localScale = Vec3{size};
    auto* sr  = go->AddComponent<SpriteRenderer>();
    sr->glyph = glyph;
    sr->color = color;
    return go;
}

int main(int argc, char** argv) {
    OKAY_INFO("=== OkaySpaceGameEngine sandbox ===");

    Application::Config cfg;
    cfg.title     = "OkaySpace Solar System";
    cfg.width     = 70;
    cfg.height    = 28;
    cfg.targetFps = 30.0f;
    // An explicit frame count always wins. Otherwise: run until the player
    // presses Q when interactive (double-clicked .exe / terminal), or a finite
    // run when piped/redirected so headless invocations terminate cleanly.
    if (argc > 1)            cfg.maxFrames = std::atoi(argv[1]);
    else if (IsInteractive()) cfg.maxFrames = 0;     // until Q
    else                      cfg.maxFrames = 150;

    Application app(cfg);
    Scene scene("SolarSystem");

    // Camera at the origin looking at the system.
    GameObject* camObj = scene.CreateGameObject("MainCamera");
    auto* cam = camObj->AddComponent<Camera>();
    cam->orthographicSize = 12.0f;
    cam->backgroundColor  = Color::FromBytes(5, 5, 20);

    // The sun, fixed at the center.
    MakeBody(scene, "Sun", 1.6f, '@', Color::Yellow);

    // A planet on an orbit, with a moon parented to it.
    GameObject* planet = MakeBody(scene, "Planet", 1.0f, 'O', Color::Cyan);
    auto* planetOrbit  = planet->AddComponent<Orbit>();
    planetOrbit->radius   = 7.0f;
    planetOrbit->speedDeg = 40.0f;
    planet->AddComponent<Spin>();

    GameObject* moon = MakeBody(scene, "Moon", 0.5f, 'o', Color::White);
    moon->transform->SetParent(planet->transform);   // hierarchy in action
    auto* moonOrbit   = moon->AddComponent<Orbit>();
    moonOrbit->radius   = 2.0f;
    moonOrbit->speedDeg = 160.0f;

    // A second, faster inner planet.
    GameObject* inner = MakeBody(scene, "InnerPlanet", 0.7f, '*', Color::Magenta);
    auto* innerOrbit  = inner->AddComponent<Orbit>();
    innerOrbit->radius   = 4.0f;
    innerOrbit->speedDeg = 90.0f;
    innerOrbit->phaseDeg = 180.0f;

    // The player ship.
    GameObject* ship = MakeBody(scene, "Ship", 0.8f, 'A', Color::Green);
    ship->AddComponent<PlayerController>();

    app.Run(scene);

    OKAY_INFO("Thanks for flying OkaySpace!");
    return 0;
}
