// OkaySpace multiplayer demo.
//
//   netdemo server [port]            # host a session (default port 45000)
//   netdemo client <host> [port]     # join a session
//
// Each peer flies a ship; you see every other connected peer move in real time.
// Run a server in one terminal and one or more clients in others.
#include <Okay.hpp>
#include <cstdlib>
#include <string>

using namespace okay;

// Flies the local ship: WASD when interactive, otherwise a lazy orbit so the
// demo animates on its own.
class Pilot : public Behaviour {
public:
    float speed = 8.0f;
    void Update(float dt) override {
        Vec2 axis = Input::AxisWASD();
        if (axis.SqrMagnitude() > 0.0f) {
            transform->Translate(Vec3{axis.Normalized() * (speed * dt)});
        } else {
            float t = Time::ElapsedTime();
            transform->localPosition = {Mathf::Cos(t) * 5.0f, Mathf::Sin(t) * 5.0f, 0.0f};
        }
    }
};

int main(int argc, char** argv) {
    std::string mode = argc > 1 ? argv[1] : "server";

    Application::Config cfg;
    cfg.title  = "OkaySpace Multiplayer";
    cfg.width  = 70;
    cfg.height = 28;
    cfg.targetFps = 30.0f;
    cfg.maxFrames = 0; // run until Q
    Application app(cfg);

    Scene scene("Net");
    auto* cam = scene.CreateGameObject("Camera")->AddComponent<Camera>();
    cam->orthographicSize = 12.0f;
    cam->backgroundColor  = Color::FromBytes(5, 5, 20);

    // Local ship.
    GameObject* ship = scene.CreateGameObject("LocalShip");
    auto* sr = ship->AddComponent<SpriteRenderer>();
    sr->glyph = '@';
    sr->color = Color::Green;
    ship->AddComponent<Pilot>();

    // Networking.
    auto* net = scene.CreateGameObject("Net")->AddComponent<NetworkManager>();
    net->SetLocalAvatar(ship->transform, '@');
    net->SetRemoteFactory([&](std::uint32_t id, char glyph) {
        GameObject* go = scene.CreateGameObject("Peer" + std::to_string(id));
        auto* rs = go->AddComponent<SpriteRenderer>();
        rs->glyph = glyph ? glyph : 'o';
        rs->color = Color::Cyan;
        return go;
    });

    if (mode == "client") {
        std::string host = argc > 2 ? argv[2] : "127.0.0.1";
        std::uint16_t port = (std::uint16_t)(argc > 3 ? std::atoi(argv[3]) : 45000);
        if (!net->StartClient(host, port)) { OKAY_ERROR("could not start client"); return 1; }
    } else {
        std::uint16_t port = (std::uint16_t)(argc > 2 ? std::atoi(argv[2]) : 45000);
        if (!net->StartServer(port)) { OKAY_ERROR("could not start server"); return 1; }
    }

    app.Run(scene);
    return 0;
}
