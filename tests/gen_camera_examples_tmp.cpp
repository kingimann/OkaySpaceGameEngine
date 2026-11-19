// TEMPORARY generator for examples/camera/*.okayscene (not committed): builds the scenes through the engine so they are valid.
#include <Okay.hpp>
#include <cstdio>
#include <fstream>
using namespace okay;

static GameObject* Mesh3(Scene& s, const char* name, const char* mesh, Vec3 pos, Vec3 scale, Color c, bool collider) {
    GameObject* g = s.CreateGameObject(name);
    g->transform->localPosition = pos; g->transform->localScale = scale;
    auto* mr = g->AddComponent<MeshRenderer>(); mr->mesh = Mesh::FromName(mesh); mr->color = c; mr->wireframe = false;
    if (collider) { auto* bc = g->AddComponent<BoxCollider3D>(); bc->size = {1, 1, 1}; }
    return g;
}
static void Save(Scene& s, const char* path) {
    std::ofstream f(path); f << SceneSerializer::Serialize(s);
    std::printf("wrote %s\n", path);
}
static GameObject* Label(Scene& s, const char* name, const char* text, float x, float y) {
    GameObject* g = s.CreateGameObject(name);
    auto* t = g->AddComponent<TextRenderer>(); t->text = text; t->screenSpace = true; t->screenPos = {x, y}; t->pixelSize = 1.6f;
    t->color = Color{1, 1, 1, 1}; t->shadow = true;
    return g;
}

int main(int argc, char** argv) {
    const std::string dir = argc > 1 ? argv[1] : ".";
    {   // ---- 3D ----
        Scene s("Camera Rigs"); s.physicsEnabled = true;
        GameObject* cam = s.CreateGameObject("Main Camera");
        cam->transform->localPosition = {0, 4, -10};
        auto* c = cam->AddComponent<Camera>(); c->fieldOfView = 60.0f; c->backgroundColor = Color{0.45f, 0.62f, 0.85f, 1.0f};
        auto* br = cam->AddComponent<CinemachineBrain>(); br->blendTime = 0.8f; br->blendStyle = cam::BlendStyle::EaseInOut;
        GameObject* sun = s.CreateGameObject("Sun"); sun->transform->localRotation = Quat::Euler(50, -30, 0);
        sun->AddComponent<Light>();
        Mesh3(s, "Ground", "Cube", {0, -0.5f, 0}, {70, 1, 70}, Color{0.35f, 0.55f, 0.35f, 1}, true);
        Mesh3(s, "Wall", "Cube", {0, 3, -6}, {10, 6, 1}, Color{0.75f, 0.45f, 0.35f, 1}, true);
        Mesh3(s, "Pillar A", "Cube", {10, 2, 6}, {2, 4, 2}, Color{0.8f, 0.8f, 0.85f, 1}, true);
        Mesh3(s, "Pillar B", "Cube", {-10, 2, 8}, {2, 4, 2}, Color{0.8f, 0.8f, 0.85f, 1}, true);
        Mesh3(s, "Pillar C", "Cube", {16, 2, -10}, {2, 4, 2}, Color{0.8f, 0.8f, 0.85f, 1}, true);
        Mesh3(s, "Pillar D", "Cube", {-16, 2, -12}, {2, 4, 2}, Color{0.8f, 0.8f, 0.85f, 1}, true);
        Mesh3(s, "Ramp Box", "Cube", {4, 0.5f, 12}, {6, 1, 3}, Color{0.9f, 0.8f, 0.3f, 1}, true);

        GameObject* hero = Mesh3(s, "Hero", "Capsule", {0, 1, 0}, {1, 1, 1}, Color{0.2f, 0.5f, 0.95f, 1}, false);
        auto* hs = hero->AddComponent<ScriptComponent>("okayscript");
        hs->LoadSource(
            "// Walk with W A S D (or the arrow keys).\n"
            "function update(dt) {\n"
            "  var s = 6 * dt\n"
            "  if (key(\"w\")) { set_z(pos_z() + s) }\n"
            "  if (key(\"s\")) { set_z(pos_z() - s) }\n"
            "  if (key(\"a\")) { set_x(pos_x() - s) }\n"
            "  if (key(\"d\")) { set_x(pos_x() + s) }\n"
            "}\n");
        GameObject* buddy = Mesh3(s, "Buddy", "Sphere", {8, 0.7f, 4}, {1.4f, 1.4f, 1.4f}, Color{0.95f, 0.55f, 0.2f, 1}, false);
        auto* bs = buddy->AddComponent<ScriptComponent>("okayscript");
        bs->LoadSource(
            "// Buddy walks in a big circle so the Group shot has something to frame.\n"
            "function update(dt) {\n"
            "  var t = time() * 0.35\n"
            "  set_x(9 * cos(t))\n"
            "  set_z(9 * sin(t))\n"
            "}\n");

        auto shot = [&](const char* name, int style, int priority) {
            GameObject* g = s.CreateGameObject(name);
            g->transform->localPosition = {0, 4, -10};
            auto* v = g->AddComponent<VirtualCamera>();
            v->ApplyStyle(style); v->priority = priority;
            v->follow = style == 2 ? "" : "Hero"; v->lookAt = "Hero";
            return v;
        };
        shot("Shot Follow", 0, 10);
        shot("Shot Orbit", 1, 5);
        auto* look = shot("Shot Look At", 2, 5); look->follow.clear();
        look->lookAt = "Hero"; look->lookAtOffset = {0, 1, 0};
        s.Find("Shot Look At")->transform->localPosition = {-14, 7, 14};
        auto* grp = shot("Shot Group", 0, 5); grp->groupTargets = {"Hero", "Buddy"}; grp->followOffset = {0, 7, -11}; grp->lookaheadTime = 0.0f; grp->groupPadding = 2.5f;
        auto* see = shot("Shot See-Through", 0, 5); see->followOffset = {0, 2.5f, -9}; see->collision = false; see->fadeOccluders = true; see->lookaheadTime = 0.0f;

        GameObject* dir = s.CreateGameObject("Camera Director");
        auto* ds = dir->AddComponent<ScriptComponent>("okayscript");
        ds->LoadSource(
            "// Keys: 1 Follow, 2 Orbit (hold the right mouse button and drag), 3 Look At, 4 Group, 5 See-Through, 0 back to automatic.\n"
            "// Effects: Z impact, X explosion, C footstep, V field of view kick, B hit flash, H handheld on or off.\n"
            "var handheld = 0\n"
            "function update(dt) {\n"
            "  if (key_down(\"1\")) { Camera.BlendTo(\"Shot Follow\", 0.8) }\n"
            "  if (key_down(\"2\")) { Camera.BlendTo(\"Shot Orbit\", 0.8) }\n"
            "  if (key_down(\"3\")) { Camera.BlendTo(\"Shot Look At\", 0.8) }\n"
            "  if (key_down(\"4\")) { Camera.BlendTo(\"Shot Group\", 0.8) }\n"
            "  if (key_down(\"5\")) { Camera.BlendTo(\"Shot See-Through\", 0.8) }\n"
            "  if (key_down(\"0\")) { Camera.BlendTo(\"\") }\n"
            "  if (key_down(\"z\")) { Camera.Shake(\"impact\", 1) }\n"
            "  if (key_down(\"x\")) { Camera.Shake(\"explosion\", 1) }\n"
            "  if (key_down(\"c\")) { Camera.Shake(\"footstep\", 1) }\n"
            "  if (key_down(\"v\")) { Camera.FovKick(10, 0.5) }\n"
            "  if (key_down(\"b\")) { Camera.HitPulse(0.9) }\n"
            "  if (key_down(\"h\")) { handheld = 1 - handheld  Camera.SetHandheld(handheld) }\n"
            "}\n");
        Label(s, "Help 1", "Camera rigs   1 Follow   2 Orbit (right mouse drag)   3 Look At   4 Group   5 See-Through   0 automatic", 14, 14);
        Label(s, "Help 2", "Move: W A S D      Effects: Z impact   X explosion   C footstep   V field of view kick   B hit flash   H handheld", 14, 36);
        Label(s, "Help 3", "Walk behind the orange wall with the Follow shot: the camera moves in. With 5 the wall turns see-through instead.", 14, 58);
        Save(s, (dir + "/camera_rigs.okayscene").c_str());
    }
    {   // ---- 2D ----
        Scene s("Camera 2D rooms"); s.physicsEnabled = false;
        GameObject* cam = s.CreateGameObject("Main Camera");
        auto* c = cam->AddComponent<Camera>(); c->projection = Camera::Projection::Orthographic; c->orthographicSize = 5.0f;
        c->pixelPerfect = true; c->pixelsPerUnit = 16.0f; c->backgroundColor = Color{0.08f, 0.09f, 0.12f, 1.0f};
        cam->transform->localPosition = {0, 0, -10};
        auto* br = cam->AddComponent<CinemachineBrain>(); br->blendTime = 0.0f;
        struct R { const char* n; float x, y; Color c; };
        const R rooms[3] = {{"Room A", 0, 0, {0.25f, 0.45f, 0.35f, 1}}, {"Room B", 30, 0, {0.45f, 0.35f, 0.55f, 1}}, {"Room C", 30, 16, {0.55f, 0.45f, 0.3f, 1}}};
        for (const R& r : rooms) {
            GameObject* g = s.CreateGameObject(r.n);
            g->transform->localPosition = {r.x, r.y, 0};
            g->AddComponent<CameraRoom>()->size = {28, 14};
            GameObject* bg = s.CreateGameObject(std::string(r.n) + " Floor");
            bg->transform->localPosition = {r.x, r.y, 1};
            auto* sp = bg->AddComponent<SpriteRenderer>(); sp->size = {28, 14}; sp->color = r.c; sp->sortOrder = -10;
            for (int k = 0; k < 6; ++k) {
                GameObject* b = s.CreateGameObject(std::string(r.n) + " Block " + std::to_string(k));
                b->transform->localPosition = {r.x - 10 + k * 4, r.y + (k % 2 ? 3.0f : -3.0f), 0};
                auto* bs = b->AddComponent<SpriteRenderer>(); bs->size = {1.5f, 1.5f}; bs->color = Color{r.c.r * 1.5f, r.c.g * 1.5f, r.c.b * 1.5f, 1}; bs->sortOrder = -5;
            }
        }
        GameObject* hero = s.CreateGameObject("Hero");
        hero->transform->localPosition = {-8, 0, 0};
        auto* sp = hero->AddComponent<SpriteRenderer>(); sp->size = {1, 1}; sp->color = Color{0.2f, 0.6f, 1.0f, 1}; sp->sortOrder = 5;
        hero->AddComponent<ScriptComponent>("okayscript")->LoadSource(
            "// Walk with W A S D. The camera holds each room and slides to the next when you cross over.\n"
            "function update(dt) {\n"
            "  var s = 9 * dt\n"
            "  if (key(\"w\")) { set_y(pos_y() + s) }\n"
            "  if (key(\"s\")) { set_y(pos_y() - s) }\n"
            "  if (key(\"a\")) { set_x(pos_x() - s) }\n"
            "  if (key(\"d\")) { set_x(pos_x() + s) }\n"
            "}\n");
        GameObject* rg = s.CreateGameObject("Shot 2D");
        auto* v = rg->AddComponent<VirtualCamera>();
        v->follow = "Hero"; v->ortho2D = true; v->orthoSize = 5.0f; v->followOffset = {0, 0, -10};
        v->useRooms = true; v->roomBlendTime = 0.6f; v->useZones = true; v->deadZoneW = 0.2f; v->deadZoneH = 0.25f; v->softZoneW = 0.7f; v->softZoneH = 0.7f;
        v->dampingMode = VirtualCamera::DampingMode::Spring; v->positionDamping = 6.0f; v->collision = false; v->lookaheadTime = 0.2f; v->lookaheadMax = 1.5f;
        Label(s, "Help 1", "2D camera rooms: walk with W A S D. Walk right out of Room A, then up out of Room B.", 14, 14);
        Label(s, "Help 2", "The picture is held inside the room; the camera slides to the next room when you cross over. Pixel perfect is on.", 14, 36);
        Save(s, (dir + "/camera_2d_rooms.okayscene").c_str());
    }
    return 0;
}
