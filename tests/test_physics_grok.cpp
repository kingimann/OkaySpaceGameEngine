// Character push-out, joints linked by object, shape casts that report every hit, closest point.
#include "test_framework.hpp"
#include <Okay.hpp>
#include "okay/Physics/PlayerCollision.hpp"
#include "okay/Physics/ColliderShapes3D.hpp"
#include "okay/Scene/SceneSerializer.hpp"
#include "okay/Components/MeshRenderer.hpp"
#include "okay/Components/Terrain.hpp"
#include <chrono>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <functional>
#include <utility>
#include <vector>

using namespace okay;

namespace {
void Floor(Scene& s) {
    auto* g = s.CreateGameObject("Ground");
    g->transform->localPosition = {0, -0.5f, 0};
    g->AddComponent<Rigidbody3D>()->bodyType = Rigidbody3D::BodyType::Static;
    g->AddComponent<BoxCollider3D>()->size = {40, 1, 40};
}
} // namespace

int main() {
    // A tall character (a box 0.6 x 1.8 x 0.6) under a low ceiling. One sphere around the waist
    // never reaches the ceiling, so the head stays inside it. A capsule fitted to the box does not.
    RUN_SUITE("character capsule push-out");
    {
        Scene s("ceiling");
        auto* player = s.CreateGameObject("Player");
        player->transform->localPosition = {0, 0.9f, 0};
        player->AddComponent<BoxCollider3D>()->size = {0.6f, 1.8f, 0.6f};
        auto* ceil = s.CreateGameObject("Ceiling");
        ceil->transform->localPosition = {0, 1.7f, 0};
        ceil->AddComponent<BoxCollider3D>()->size = {4, 0.4f, 4};   // bottom at y = 1.5, head was at 1.8
        const float before = player->transform->Position().y;
        ResolvePlayerBody(s, player);
        CHECK(player->transform->Position().y < before - 0.15f);
        CHECK(player->transform->Position().y < 0.75f);

        // A cube still uses the waist sphere and is pushed out of a wall it overlaps.
        Scene w("cube");
        auto* cube = w.CreateGameObject("Cube");
        cube->transform->localPosition = {0.4f, 0.5f, 0};
        cube->AddComponent<BoxCollider3D>()->size = {1, 1, 1};
        auto* wall = w.CreateGameObject("Wall");
        wall->transform->localPosition = {1.0f, 0.5f, 0};
        wall->AddComponent<BoxCollider3D>()->size = {1, 2, 2};
        ResolvePlayerBody(w, cube);
        CHECK(cube->transform->Position().x < 0.4f);
    }

    // Two objects share a name. The joint is tied to the second one by object, not to the first
    // one the name would find, and not to the world origin.
    RUN_SUITE("joint linked by object");
    {
        Scene s("link");
        auto* first = s.CreateGameObject("Post");
        first->transform->localPosition = {0, 0, 0};
        first->AddComponent<Rigidbody3D>()->bodyType = Rigidbody3D::BodyType::Static;
        auto* second = s.CreateGameObject("Post");
        second->transform->localPosition = {4, 0, 0};
        second->AddComponent<Rigidbody3D>()->bodyType = Rigidbody3D::BodyType::Static;
        auto* weight = s.CreateGameObject("Weight");
        weight->transform->localPosition = {4, 2, 0};
        auto* rb = weight->AddComponent<Rigidbody3D>();
        rb->gravityScale = 0.0f;
        rb->velocity = {-3, 0, 0};
        auto* j = weight->AddComponent<Joint3D>();
        j->mode = (int)Joint3D::Mode::Distance;
        j->autoConfigure = false;
        j->distance = 0.5f;
        j->anchor = {0, 0, 0};
        j->ConnectTo(second);
        s.Start();
        for (int i = 0; i < 120; ++i) s.Update(1.0f / 60.0f);
        CHECK(weight->transform->Position().x > 3.0f);
    }

    // A sphere cast through two walls reports both, the nearer one first.
    RUN_SUITE("shape cast all hits");
    {
        Scene s("casts");
        auto* near = s.CreateGameObject("Near");
        near->transform->localPosition = {2, 0, 0};
        near->AddComponent<BoxCollider3D>()->size = {1, 1, 1};
        auto* far = s.CreateGameObject("Far");
        far->transform->localPosition = {6, 0, 0};
        far->AddComponent<BoxCollider3D>()->size = {1, 1, 1};
        std::vector<RaycastHit3D> hits = s.physics3D().SphereCastAll(s, {0, 0, 0}, {1, 0, 0}, 0.2f, 20.0f);
        CHECK(hits.size() == 2);
        CHECK(hits[0].hit && hits[0].gameObject == near);
        CHECK(hits[1].gameObject == far);
        CHECK(hits[0].distance < hits[1].distance);
        RaycastHit3D only = s.physics3D().SphereCast(s, {0, 0, 0}, {1, 0, 0}, 0.2f, 20.0f);
        CHECK(only.gameObject == near);

        std::vector<RaycastHit3D> boxes = s.physics3D().BoxCastAll(s, {0, 0, 0}, {0.2f, 0.2f, 0.2f}, {1, 0, 0});
        CHECK(boxes.size() == 2 && boxes[0].gameObject == near);
        std::vector<RaycastHit3D> caps = s.physics3D().CapsuleCastAll(s, {-0.1f, 0, 0}, {0.1f, 0, 0}, 0.2f, {1, 0, 0}, 20.0f);
        CHECK(caps.size() == 2 && caps[0].gameObject == near);
    }

    // Closest point on a box sits on the face, not at the centre.
    RUN_SUITE("closest point");
    {
        Scene s("nearpt");
        auto* box = s.CreateGameObject("Box");
        box->AddComponent<BoxCollider3D>()->size = {2, 2, 2};
        NearestSolid3D q = s.physics3D().ClosestPoint(s, {3, 0, 0});
        CHECK(q.hit);
        CHECK(q.gameObject == box);
        CHECK_NEAR(q.point.x, 1.0f, 0.05f);
        CHECK_NEAR(q.point.y, 0.0f, 0.05f);
        CHECK_NEAR(q.distance, 2.0f, 0.05f);
        NearestSolid3D inside = s.physics3D().ClosestPoint(s, {0.2f, 0, 0});
        CHECK(inside.hit && inside.distance < 0.001f);
        auto* trigger = s.CreateGameObject("Trig");
        trigger->transform->localPosition = {3, 0, 0};
        auto* tc = trigger->AddComponent<SphereCollider3D>();
        tc->radius = 0.2f;
        tc->isTrigger = true;
        NearestSolid3D skip = s.physics3D().ClosestPoint(s, {3, 0, 0}, 0.5f);
        CHECK(!skip.hit);   // the trigger is ignored and the box face is 2 units away
    }

    // A low ledge is stepped onto. A tall wall is not. A steep ramp slides; flat ground does not.
    RUN_SUITE("character step and slope");
    {
        auto body = [](Scene& s, const char* name, Vec3 at) {
            auto* g = s.CreateGameObject(name);
            g->transform->localPosition = at;
            g->AddComponent<BoxCollider3D>()->size = {0.5f, 1.8f, 0.5f};
            return g;
        };
        Scene step("step");
        auto* floor = step.CreateGameObject("Floor");
        floor->transform->localPosition = {0, -0.5f, 0};
        floor->AddComponent<BoxCollider3D>()->size = {20, 1, 20};
        auto* ledge = step.CreateGameObject("Ledge");
        ledge->transform->localPosition = {1.3f, 0.15f, 0};
        ledge->AddComponent<BoxCollider3D>()->size = {1.0f, 0.3f, 2.0f};
        auto* p = body(step, "Player", {0.3f, 0.9f, 0});
        MovePlayerBody(step, p, {0.9f, 0, 0}, 0.45f, 50.0f);
        CHECK(p->transform->Position().y > 1.05f);
        CHECK(p->transform->Position().x > 0.9f);

        Scene wall("wall");
        auto* floor2 = wall.CreateGameObject("Floor");
        floor2->transform->localPosition = {0, -0.5f, 0};
        floor2->AddComponent<BoxCollider3D>()->size = {20, 1, 20};
        auto* tall = wall.CreateGameObject("Wall");
        tall->transform->localPosition = {1.3f, 0.6f, 0};
        tall->AddComponent<BoxCollider3D>()->size = {1.0f, 1.2f, 2.0f};
        auto* w = body(wall, "Player", {0.3f, 0.9f, 0});
        MovePlayerBody(wall, w, {0.9f, 0, 0}, 0.25f, 50.0f);
        CHECK(w->transform->Position().y < 1.0f);
        CHECK(w->transform->Position().x < 0.7f);

        Scene flat("flat");
        auto* floor3 = flat.CreateGameObject("Floor");
        floor3->transform->localPosition = {0, -0.5f, 0};
        floor3->AddComponent<BoxCollider3D>()->size = {20, 1, 20};
        auto* f = body(flat, "Player", {0, 0.9f, 0});
        MovePlayerBody(flat, f, {0, 0, 0}, 0.3f, 40.0f);
        CHECK(std::fabs(f->transform->Position().x) < 0.05f);

        Scene ramp("ramp");
        auto* slope = ramp.CreateGameObject("Ramp");
        slope->transform->localPosition = {0, 0, 0};
        slope->transform->localRotation = Quat::AngleAxis(55.0f, {0, 0, 1});
        slope->AddComponent<BoxCollider3D>()->size = {6, 0.2f, 4};
        auto* r = body(ramp, "Player", {0, 0.5f, 0});
        float x0 = r->transform->Position().x;
        MovePlayerBody(ramp, r, {0, 0, 0}, 0.2f, 30.0f);
        CHECK(r->transform->Position().x < x0 - 0.1f);
    }

    // A resting crate stays awake when the sleep timer is long, and sleeps when it is short.
    RUN_SUITE("sleep timer");
    {
        auto rest = [](float seconds, float watch) {
            Scene s("sleep");
            auto* ground = s.CreateGameObject("Ground");
            ground->transform->localPosition = {0, -0.5f, 0};
            ground->AddComponent<Rigidbody3D>()->bodyType = Rigidbody3D::BodyType::Static;
            ground->AddComponent<BoxCollider3D>()->size = {20, 1, 20};
            auto* box = s.CreateGameObject("Crate");
            box->transform->localPosition = {0, 0.55f, 0};
            auto* rb = box->AddComponent<Rigidbody3D>();
            box->AddComponent<BoxCollider3D>()->size = {1, 1, 1};
            s.physics3D().SetSleep(0.08f, 0.08f, seconds);
            s.Start();
            const int n = (int)(watch / (1.0f / 50.0f));
            for (int i = 0; i < n; ++i) s.Update(1.0f / 50.0f);
            return rb->sleeping;
        };
        CHECK(!rest(8.0f, 1.5f));
        CHECK(rest(0.05f, 2.0f));
    }

    // Closest point on a 2D box sits on the face.
    RUN_SUITE("2d closest point");
    {
        Scene s("near2");
        auto* box = s.CreateGameObject("Box");
        box->AddComponent<BoxCollider2D>()->size = {2, 2};
        SurfaceQuery2D q = s.physics().ClosestPoint(s, {3, 0});
        CHECK(q.hit);
        CHECK(q.gameObject == box);
        CHECK_NEAR(q.point.x, 1.0f, 0.05f);
        CHECK_NEAR(q.distance, 2.0f, 0.05f);
        SurfaceQuery2D inside = s.physics().ClosestPoint(s, {0.2f, 0});
        CHECK(inside.hit && inside.distance < 0.001f);
    }

    // A 2D circle cast through two boxes reports both, the nearer one first.
    RUN_SUITE("2d shape cast all hits");
    {
        Scene s("cast2d");
        auto* near = s.CreateGameObject("Near");
        near->transform->localPosition = {2, 0, 0};
        near->AddComponent<BoxCollider2D>()->size = {1, 1};
        auto* far = s.CreateGameObject("Far");
        far->transform->localPosition = {6, 0, 0};
        far->AddComponent<BoxCollider2D>()->size = {1, 1};
        std::vector<RaycastHit2D> hits = s.physics().CircleCastAll(s, {0, 0}, {1, 0}, 0.2f, 20.0f);
        CHECK(hits.size() == 2);
        CHECK(hits[0].gameObject == near && hits[1].gameObject == far);
        CHECK(hits[0].distance < hits[1].distance);
        RaycastHit2D only = s.physics().CircleCast(s, {0, 0}, {1, 0}, 0.2f, 20.0f);
        CHECK(only.gameObject == near);
        std::vector<RaycastHit2D> boxes = s.physics().BoxCastAll(s, {0, 0}, {0.2f, 0.2f}, {1, 0});
        CHECK(boxes.size() == 2 && boxes[0].gameObject == near);
    }

    // A body 1 cm into a walkable ramp must not creep downhill. A low kerb is still walked over.
    RUN_SUITE("slope stand and low step");
    {
        Scene ramp("ramp25");
        const Quat rot = Quat::AngleAxis(25.0f, {0, 0, 1});
        const Vec3 N = rot * Vec3{0, 1, 0};
        auto* slope = ramp.CreateGameObject("Ramp");
        slope->transform->localRotation = rot;
        slope->AddComponent<BoxCollider3D>()->size = {8.0f, 0.2f, 4.0f};
        auto* body = ramp.CreateGameObject("Player");
        body->transform->localPosition = N * 0.39f + Vec3{0.0f, 0.6f, 0.0f};   // 1 cm into the 25 degree face
        body->AddComponent<BoxCollider3D>()->size = {0.6f, 1.8f, 0.6f};
        const float x0 = body->transform->Position().x;
        const float y0 = body->transform->Position().y;
        ResolvePlayerBody(ramp, body);
        const float y1 = body->transform->Position().y;
        std::printf("slope stand first: x %+.4f y %+.4f (start x %+.4f y %+.4f)\n",
                    body->transform->Position().x, y1, x0, y0);
        CHECK(y1 > y0 + 0.004f);
        CHECK(std::fabs(body->transform->Position().x - x0) < 0.01f);
        for (int i = 0; i < 59; ++i) ResolvePlayerBody(ramp, body);
        std::printf("slope stand 60: x %+.4f y %+.4f z %+.4f\n",
                    body->transform->Position().x, body->transform->Position().y, body->transform->Position().z);
        CHECK(std::fabs(body->transform->Position().x - x0) < 0.015f);
        CHECK(std::fabs(body->transform->Position().z) < 0.015f);
        CHECK(std::fabs(body->transform->Position().y - y1) < 0.03f);

        Scene step("lowstep");
        auto* floor = step.CreateGameObject("Floor");
        floor->transform->localPosition = {0, -0.5f, 0};
        floor->AddComponent<BoxCollider3D>()->size = {20, 1, 20};
        auto* ledge = step.CreateGameObject("Ledge");
        ledge->transform->localPosition = {1.2f, 0.125f, 0};
        ledge->AddComponent<BoxCollider3D>()->size = {1.0f, 0.25f, 2.0f};
        auto* p = step.CreateGameObject("Player");
        p->transform->localPosition = {0.35f, 0.9f, 0};
        p->AddComponent<BoxCollider3D>()->size = {0.6f, 1.8f, 0.6f};
        p->transform->Translate({0.40f, 0, 0});
        ResolvePlayerBody(step, p);
        std::printf("low step: x %+.4f y %+.4f\n", p->transform->Position().x, p->transform->Position().y);
        CHECK(p->transform->Position().y > 1.05f);
        CHECK(p->transform->Position().x > 0.65f);
    }

    // Closest point follows the real surface: a tilted plane, a round cylinder, a sloped mesh.
    RUN_SUITE("closest point on real shapes");
    {
        Scene s("shapes");
        auto* pl = s.CreateGameObject("Plane");
        pl->transform->localRotation = Quat::AngleAxis(25.0f, {0, 0, 1});
        pl->AddComponent<PlaneCollider3D>();
        const Vec3 n = pl->transform->Rotation() * Vec3{0, 1, 0};
        NearestSolid3D plane = s.physics3D().ClosestPoint(s, n * 3.0f);
        CHECK(plane.hit);
        CHECK(plane.distance > 2.5f && plane.distance < 3.5f);

        Scene round("round");
        auto* cy = round.CreateGameObject("Cyl");
        auto* cc = cy->AddComponent<CylinderCollider3D>();
        cc->radius = 1.0f; cc->height = 2.0f;
        NearestSolid3D cyl = round.physics3D().ClosestPoint(round, {2.0f, 0, 2.0f});
        CHECK(cyl.gameObject == cy);
        CHECK(cyl.distance > 1.6f && cyl.distance < 2.1f);

        auto* mesh = s.CreateGameObject("RampMesh");
        mesh->transform->localPosition = {0, 0, 10};
        auto* mr = mesh->AddComponent<MeshRenderer>();
        mr->mesh.vertices = {{0, 0, 0}, {4, 2, 0}, {0, 0, 4}, {4, 2, 4}};
        mr->mesh.triangles = {0, 1, 2, 2, 1, 3};
        mesh->AddComponent<MeshCollider3D>();
        NearestSolid3D above = s.physics3D().ClosestPoint(s, {2.0f, 4.5f, 12.0f});
        CHECK(above.gameObject == mesh);
        CHECK(above.distance > 2.0f);
        CHECK(above.point.y < 3.0f);
    }

    // A cast with no direction, or no distance, reports the overlap where the shape already stands.
    // A box cast and a zero-size box cast both reach terrain.
    RUN_SUITE("cast where it stands, and terrain");
    {
        Scene s("stand");
        auto* box = s.CreateGameObject("Box");
        box->AddComponent<BoxCollider3D>()->size = {2, 2, 2};
        std::vector<RaycastHit3D> stood = s.physics3D().SphereCastAll(s, {0, 0, 0}, {0, 0, 0}, 0.3f, 0.0f);
        CHECK(!stood.empty() && stood[0].gameObject == box);
        std::vector<RaycastHit3D> clear = s.physics3D().SphereCastAll(s, {5, 0, 0}, {0, 0, 0}, 0.3f, 0.0f);
        CHECK(clear.empty());

        Scene ground("terr");
        auto* tr = ground.CreateGameObject("Ground")->AddComponent<Terrain>();
        tr->Resize(8); tr->size = 20.0f; tr->Flatten(0.0f);
        auto on = [&](const std::vector<RaycastHit3D>& hits) {
            for (const RaycastHit3D& h : hits) if (h.gameObject == tr->gameObject) return true;
            return false;
        };
        CHECK(on(ground.physics3D().BoxCastAll(ground, {0, 5, 0}, {0.4f, 0.4f, 0.4f}, {0, -1, 0}, Quat::Identity, 10.0f)));
        CHECK(on(ground.physics3D().SphereCastAll(ground, {0, 5, 0}, {0, -1, 0}, 0.4f, 10.0f)));
        CHECK(on(ground.physics3D().BoxCastAll(ground, {0, 5, 0}, {0, 0, 0}, {0, -1, 0}, Quat::Identity, 10.0f)));
        CHECK(on(ground.physics3D().CapsuleCastAll(ground, {0, 5, 0}, {0, 5, 0}, 0.0f, {0, -1, 0}, 10.0f)));
    }

    // ConnectTo survives a save and a clone of the hierarchy.
    RUN_SUITE("joint link save and clone");
    {
        Scene s("linksave");
        auto* rig = s.CreateGameObject("Rig");
        auto* post = s.CreateGameObject("Post");
        post->transform->SetParent(rig->transform, false);
        post->transform->localPosition = {1, 0, 0};
        auto* weight = s.CreateGameObject("Weight");
        weight->transform->SetParent(rig->transform, false);
        weight->transform->localPosition = {1, 2, 0};
        auto* j = weight->AddComponent<Joint3D>();
        j->ConnectTo(post);
        CHECK(j->connectedBody == "Post");
        CHECK(!j->connectedGuid.empty());

        Scene loaded("loaded");
        CHECK(SceneSerializer::Deserialize(loaded, SceneSerializer::Serialize(s)));
        GameObject* w = loaded.Find("Weight");
        Joint3D* jl = w ? w->GetComponent<Joint3D>() : nullptr;
        GameObject* partner = jl ? Joint3DPartner(loaded, jl) : nullptr;
        CHECK(partner && partner->name == "Post");
        CHECK(partner->transform->Position().x > 0.5f);

        GameObject* copy = s.Duplicate(*rig);
        Joint3D* jc = nullptr;
        GameObject* copyPost = nullptr;
        std::vector<GameObject*> stack{copy};
        while (!stack.empty()) {
            GameObject* g = stack.back();
            stack.pop_back();
            if (!g) continue;
            if (Joint3D* jj = g->GetComponent<Joint3D>()) jc = jj;
            if (g->name == "Post") copyPost = g;
            if (g->transform)
                for (Transform* ch : g->transform->Children())
                    if (ch && ch->gameObject) stack.push_back(ch->gameObject);
        }
        CHECK(jc && copyPost);
        CHECK(Joint3DPartner(s, jc) == copyPost);
        CHECK(Joint3DPartner(s, j) == post);
    }

    // Review round 3, finding 1: a ConnectTo link must survive being placed from text or from a prefab file, with the
    // original still in the scene (the copy links the copy, not the original) and in a scene without the original.
    RUN_SUITE("joint link placed from text and prefab file");
    {
        Scene s("place");
        auto* rig = s.CreateGameObject("Rig");
        auto* post = s.CreateGameObject("Post");
        post->transform->SetParent(rig->transform, false);
        auto* weight = s.CreateGameObject("Weight");
        weight->transform->SetParent(rig->transform, false);
        weight->AddComponent<Joint3D>()->ConnectTo(post);
        const std::string text = SceneSerializer::SerializeObject(*rig);
        auto find = [](GameObject* root, const char* name) {
            GameObject* found = nullptr;
            std::vector<GameObject*> stack{root};
            while (!stack.empty()) {
                GameObject* g = stack.back();
                stack.pop_back();
                if (g->name == name) found = g;
                for (Transform* ch : g->transform->Children()) stack.push_back(ch->gameObject);
            }
            return found;
        };
        GameObject* pasted = SceneSerializer::InstantiateFromText(s, text);
        CHECK(pasted != nullptr);
        GameObject* pastedPost = pasted ? find(pasted, "Post") : nullptr;
        GameObject* pastedWeight = pasted ? find(pasted, "Weight") : nullptr;
        Joint3D* pj = pastedWeight ? pastedWeight->GetComponent<Joint3D>() : nullptr;
        CHECK(pj && pastedPost && pastedPost != post);
        CHECK(pj && Joint3DPartner(s, pj) == pastedPost);

        const std::string path = (std::filesystem::temp_directory_path() / "okay_joint_prefab_round3.prefab").string();
        CHECK(SceneSerializer::SaveObjectToFile(*rig, path));
        GameObject* placed = SceneSerializer::InstantiateFromFile(s, path);
        GameObject* placedPost = placed ? find(placed, "Post") : nullptr;
        GameObject* placedWeight = placed ? find(placed, "Weight") : nullptr;
        Joint3D* fj = placedWeight ? placedWeight->GetComponent<Joint3D>() : nullptr;
        CHECK(fj && placedPost && placedPost != post && placedPost != pastedPost);
        CHECK(fj && Joint3DPartner(s, fj) == placedPost);

        Scene other("other");
        GameObject* alone = SceneSerializer::InstantiateFromFile(other, path);
        GameObject* alonePost = alone ? find(alone, "Post") : nullptr;
        GameObject* aloneWeight = alone ? find(alone, "Weight") : nullptr;
        Joint3D* aj = aloneWeight ? aloneWeight->GetComponent<Joint3D>() : nullptr;
        CHECK(aj && Joint3DPartner(other, aj) == alonePost && alonePost != nullptr);
        std::error_code ec;
        std::filesystem::remove(path, ec);
    }

    // Findings 2 and 6a: closest point on a big mesh reuses the triangle tree, and a point inside a closed mesh is 0 away.
    RUN_SUITE("closest point on a large mesh");
    {
        auto sphere = [](Mesh& m, int rings, int around, float radius, bool inward) {
            const float pi = 3.14159265f;
            m.vertices.clear();   // (a new MeshRenderer starts with a cube)
            m.triangles.clear();
            for (int i = 0; i <= rings; ++i)
                for (int j = 0; j < around; ++j) {
                    const float th = pi * (float)i / (float)rings, ph = 2.0f * pi * (float)j / (float)around;
                    const bool pole = i == 0 || i == rings;   // the poles are single points, exactly
                    const float ring = pole ? 0.0f : std::sin(th);
                    m.vertices.push_back({radius * ring * std::cos(ph), i == rings ? -radius : radius * std::cos(th), radius * ring * std::sin(ph)});
                }
            for (int i = 0; i < rings; ++i)
                for (int j = 0; j < around; ++j) {
                    const int j2 = (j + 1) % around;
                    const int a = i * around + j, b = (i + 1) * around + j, c = i * around + j2, d = (i + 1) * around + j2;
                    const int t[6] = {a, b, c, c, b, d};
                    for (int k = 0; k < 6; k += 3) {
                        const Vec3 p0 = m.vertices[t[k]], p1 = m.vertices[t[k + 1]], p2 = m.vertices[t[k + 2]];
                        const Vec3 n = Vec3::Cross(p1 - p0, p2 - p0);
                        const bool outwardNow = Vec3::Dot(n, p0 + p1 + p2) > 0.0f;
                        if (outwardNow != inward) { m.triangles.push_back(t[k]); m.triangles.push_back(t[k + 1]); m.triangles.push_back(t[k + 2]); }
                        else { m.triangles.push_back(t[k]); m.triangles.push_back(t[k + 2]); m.triangles.push_back(t[k + 1]); }
                    }
                }
        };
        Scene s("bigmesh");
        auto* ball = s.CreateGameObject("Ball");
        auto* mr = ball->AddComponent<MeshRenderer>();
        sphere(mr->mesh, 200, 200, 5.0f, false);
        CHECK(mr->mesh.triangles.size() / 3 >= 79000);
        ball->AddComponent<MeshCollider3D>();
        auto* crate = s.CreateGameObject("Crate");
        crate->transform->localPosition = {40, 0, 0};
        crate->AddComponent<BoxCollider3D>()->size = {1, 1, 1};
        NearestSolid3D warm = s.physics3D().ClosestPoint(s, {0, 8, 0});
        CHECK(warm.hit && warm.gameObject == ball);
        CHECK_NEAR(warm.distance, 3.0f, 0.03f);
        const auto t0 = std::chrono::steady_clock::now();
        const int calls = 5;
        NearestSolid3D last;
        for (int i = 0; i < calls; ++i) last = s.physics3D().ClosestPoint(s, {0, 8.0f + 0.01f * (float)i, 0});
        const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count() / calls;
        std::printf("closest point on 80k triangles: %.2f ms per call\n", ms);
        CHECK(last.hit && last.gameObject == ball);
        CHECK(ms < 20.0);
        NearestSolid3D inside = s.physics3D().ClosestPoint(s, {0.3f, 0.2f, -0.4f});
        CHECK(inside.hit && inside.gameObject == ball);
        CHECK(inside.distance < 0.001f);
        NearestSolid3D nearCrate = s.physics3D().ClosestPoint(s, {37, 0, 0});
        CHECK(nearCrate.gameObject == crate);
        CHECK_NEAR(nearCrate.distance, 2.5f, 0.02f);

        ball->transform->localScale = {2, 1, 1};   // 10 across in x, 5 in z
        NearestSolid3D sideways = s.physics3D().ClosestPoint(s, {12, 0, 0});
        CHECK(sideways.hit && sideways.gameObject == ball);
        CHECK_NEAR(sideways.distance, 2.0f, 0.05f);
        NearestSolid3D alongZ = s.physics3D().ClosestPoint(s, {0, 0, 8});
        CHECK_NEAR(alongZ.distance, 3.0f, 0.05f);
        NearestSolid3D stillInside = s.physics3D().ClosestPoint(s, {7, 0, 0});
        CHECK(stillInside.distance < 0.001f);

        Scene flipped("flipped");
        auto* inner = flipped.CreateGameObject("Inside out");
        sphere(inner->AddComponent<MeshRenderer>()->mesh, 24, 24, 5.0f, true);
        inner->AddComponent<MeshCollider3D>();
        CHECK(flipped.physics3D().ClosestPoint(flipped, {0.3f, 0.2f, 0.1f}).distance < 0.001f);
        CHECK_NEAR(flipped.physics3D().ClosestPoint(flipped, {0, 8, 0}).distance, 3.0f, 0.1f);

        Scene sheet("sheet");   // an open mesh has no inside
        auto* quad = sheet.CreateGameObject("Sheet");
        auto* sr = quad->AddComponent<MeshRenderer>();
        sr->mesh.vertices = {{-2, 0, -2}, {2, 0, -2}, {-2, 0, 2}, {2, 0, 2}};
        sr->mesh.triangles = {0, 2, 1, 1, 2, 3};
        quad->AddComponent<MeshCollider3D>();
        CHECK_NEAR(sheet.physics3D().ClosestPoint(sheet, {0, -0.8f, 0}).distance, 0.8f, 0.01f);
        CHECK_NEAR(sheet.physics3D().ClosestPoint(sheet, {0, 0.8f, 0}).distance, 0.8f, 0.01f);
    }

    // Findings 3 and 4: the step height is the caller's, and a ledge taller than it does not lift the body.
    RUN_SUITE("player step height is honoured");
    {
        auto player = [](Scene& s) {
            auto* g = s.CreateGameObject("Player");
            g->transform->localPosition = {0, 0.9f, 0};
            g->AddComponent<BoxCollider3D>()->size = {0.6f, 1.8f, 0.6f};
            return g;
        };
        Scene a("noStep");
        Floor(a);
        auto* ledge = a.CreateGameObject("Ledge");
        ledge->transform->localPosition = {1.4f, 0.17f, 0};
        ledge->AddComponent<BoxCollider3D>()->size = {1.2f, 0.34f, 2.0f};
        auto* pa = player(a);
        for (int i = 0; i < 20; ++i) MovePlayerBody(a, pa, {0.05f, 0, 0}, 0.0f, 50.0f);
        std::printf("stepHeight 0 against a 0.34 ledge: x %+.3f y %+.3f\n", pa->transform->Position().x, pa->transform->Position().y);
        CHECK(pa->transform->Position().y < 0.95f);
        CHECK(pa->transform->Position().x < 0.65f);

        Scene b("withStep");
        Floor(b);
        auto* ledgeB = b.CreateGameObject("Ledge");
        ledgeB->transform->localPosition = {1.4f, 0.17f, 0};
        ledgeB->AddComponent<BoxCollider3D>()->size = {1.2f, 0.34f, 2.0f};
        auto* pb = player(b);
        for (int i = 0; i < 20; ++i) MovePlayerBody(b, pb, {0.05f, 0, 0}, 0.4f, 50.0f);
        CHECK(pb->transform->Position().y > 1.2f);   // a 0.4 step does climb it

        Scene c("crate");
        Floor(c);
        auto* crate = c.CreateGameObject("Crate");
        crate->transform->localPosition = {0.5f, 0.15f, 0};
        crate->AddComponent<BoxCollider3D>()->size = {0.5f, 0.3f, 0.5f};
        auto* pc = player(c);
        MovePlayerBody(c, pc, {0, 0, 0}, 0.0f, 50.0f);
        std::printf("stepHeight 0 beside a 0.3 crate: y %+.3f\n", pc->transform->Position().y);
        CHECK(std::fabs(pc->transform->Position().y - 0.9f) < 0.02f);

        Scene d("tall");
        Floor(d);
        auto* tall = d.CreateGameObject("Ledge");
        tall->transform->localPosition = {1.9f, 0.225f, 0};
        tall->AddComponent<BoxCollider3D>()->size = {2.0f, 0.45f, 2.0f};
        auto* pd = player(d);
        float worst = 0.0f;
        for (int i = 0; i < 40; ++i) {
            MovePlayerBody(d, pd, {0.05f, 0, 0}, 0.3f, 50.0f);
            worst = std::max(worst, pd->transform->Position().y - 0.9f);
        }
        std::printf("0.45 ledge, 40 frames: x %+.3f y %+.3f (highest %+.3f above rest)\n",
                    pd->transform->Position().x, pd->transform->Position().y, worst);
        CHECK(pd->transform->Position().y < 0.95f);
        CHECK(worst < 0.05f);
        CHECK(pd->transform->Position().x < 0.7f);
    }

    // Finding 6c: the steep-slope check looks from the body's feet, so a centre-pivoted box still finds the ground.
    RUN_SUITE("steep slope seen from the feet");
    {
        Scene s("steep");
        const Quat rot = Quat::AngleAxis(55.0f, {0, 0, 1});
        auto* slope = s.CreateGameObject("Ramp");
        slope->transform->localRotation = rot;
        slope->AddComponent<BoxCollider3D>()->size = {30.0f, 0.2f, 6.0f};
        auto* body = s.CreateGameObject("Player");
        body->AddComponent<BoxCollider3D>()->size = {0.5f, 1.8f, 0.5f};
        const RaycastHit3D ground = s.physics3D().Raycast(s, {0, 20, 0}, {0, -1, 0}, 40.0f, body, Physics3D::kAllLayers, false);
        CHECK(ground.hit);
        body->transform->localPosition = {0, 20.0f - ground.distance + 1.0f, 0};   // 1 m above the slope under it, pivot at the centre
        const float x0 = body->transform->Position().x;
        MovePlayerBody(s, body, {0, 0, 0}, 0.2f, 30.0f);
        std::printf("steep slope from a centre pivot: moved x %+.3f\n", body->transform->Position().x - x0);
        CHECK(body->transform->Position().x < x0 - 0.1f);
    }

    // Finding 6b: after ConnectTo, typing another name in the Connected Body field links that body instead.
    RUN_SUITE("joint name edited after ConnectTo");
    {
        Scene s("rename");
        auto* a = s.CreateGameObject("Alpha");
        auto* b = s.CreateGameObject("Beta");
        auto* w = s.CreateGameObject("Weight");
        auto* j = w->AddComponent<Joint3D>();
        j->ConnectTo(a);
        CHECK(Joint3DPartner(s, j) == a);
        a->name = "Alpha renamed";                // a rename of the linked body keeps the link
        CHECK(Joint3DPartner(s, j) == a);
        j->connectedBody = "Beta";                // typing a new name is a new choice
        CHECK(Joint3DPartner(s, j) == b);
        j->connectedBody.clear();                 // an emptied field is a world anchor
        CHECK(Joint3DPartner(s, j) == nullptr);

        Scene loaded("loadedRename");
        auto* la = loaded.CreateGameObject("Alpha");
        loaded.CreateGameObject("Beta");
        auto* lw = loaded.CreateGameObject("Weight");
        lw->AddComponent<Joint3D>()->ConnectTo(la);
        Scene again("again");
        CHECK(SceneSerializer::Deserialize(again, SceneSerializer::Serialize(loaded)));
        Joint3D* aj = again.Find("Weight")->GetComponent<Joint3D>();
        CHECK(Joint3DPartner(again, aj) == again.Find("Alpha"));
        aj->connectedBody = "Beta";
        CHECK(Joint3DPartner(again, aj) == again.Find("Beta"));
    }

    // Finding 5: 2D closest point follows the real Polygon, Edge and Composite shapes, not their bounding boxes.
    RUN_SUITE("2d closest point on polygon edge composite");
    {
        Scene s("poly2d");
        auto* g = s.CreateGameObject("Tri");
        g->AddComponent<PolygonCollider2D>();   // (-0.5,-0.5) (0.5,-0.5) (0,0.5)
        SurfaceQuery2D q = s.physics().ClosestPoint(s, {1.0f, 0.5f});
        CHECK(q.hit && q.gameObject == g);
        CHECK_NEAR(q.distance, 0.894f, 0.02f);       // the bounding box would say 0.5
        CHECK_NEAR(q.point.x, 0.2f, 0.03f);
        CHECK_NEAR(q.point.y, 0.1f, 0.03f);
        CHECK(s.physics().ClosestPoint(s, {0.0f, 0.0f}).distance < 0.001f);
        CHECK(s.physics().ClosestPoint(s, {0.45f, 0.4f}).distance > 0.1f);   // inside the box, outside the triangle

        Scene e("edge2d");
        auto* eg = e.CreateGameObject("Slope");
        eg->AddComponent<EdgeCollider2D>()->points = {{-1, -1}, {1, 1}};
        SurfaceQuery2D eq = e.physics().ClosestPoint(e, {1.0f, -1.0f});
        CHECK(eq.hit);
        CHECK_NEAR(eq.distance, 1.414f, 0.03f);

        Scene c("composite2d");
        auto* cg = c.CreateGameObject("Comp");
        auto* comp = cg->AddComponent<CompositeCollider2D>();
        comp->AddPolygon({{-0.5f, -0.5f}, {0.5f, -0.5f}, {0.0f, 0.5f}});
        SurfaceQuery2D cq = c.physics().ClosestPoint(c, {1.0f, 0.5f});
        CHECK(cq.hit);
        CHECK_NEAR(cq.distance, 0.894f, 0.02f);
    }

    // Finding 5: the one sweep march gives the same answer for the nearest hit and for the all-hits list.
    RUN_SUITE("2d sweep first hit matches all hits");
    {
        Scene s("sweep2d");
        auto* near = s.CreateGameObject("Near");
        near->transform->localPosition = {2, 0.1f, 0};
        near->AddComponent<BoxCollider2D>()->size = {1, 1};
        auto* far = s.CreateGameObject("Far");
        far->transform->localPosition = {5, 0, 0};
        far->AddComponent<CircleCollider2D>()->radius = 0.5f;
        auto* tri = s.CreateGameObject("Tri");
        tri->transform->localPosition = {8, 0, 0};
        tri->AddComponent<PolygonCollider2D>();
        auto all = s.physics().CircleCastAll(s, {0, 0}, {1, 0}, 0.2f, 30.0f);
        auto one = s.physics().CircleCast(s, {0, 0}, {1, 0}, 0.2f, 30.0f);
        CHECK(all.size() == 3);
        CHECK(!all.empty() && one.hit && one.gameObject == all[0].gameObject);
        CHECK(!all.empty() && std::fabs(one.distance - all[0].distance) < 0.01f);
        auto boxAll = s.physics().BoxCastAll(s, {0, 0}, {0.2f, 0.2f}, {1, 0});
        auto boxOne = s.physics().BoxCast(s, {0, 0}, {0.2f, 0.2f}, {1, 0});
        CHECK(!boxAll.empty() && boxOne.hit && boxOne.gameObject == boxAll[0].gameObject);
        CHECK(!boxAll.empty() && std::fabs(boxOne.distance - boxAll[0].distance) < 0.01f);
    }

    // Review round 4, finding 1: a point outside a closed mesh, near a sharp edge or corner, is never "inside, 0 away".
    // Sharp wedges, a thin tetrahedron and an L-shaped (concave) block, each also turned, stretched and mirrored.
    RUN_SUITE("inside a closed mesh at sharp edges");
    {
        struct Rng { std::uint32_t s = 12345u; float Next() { s = s * 1664525u + 1013904223u; return (float)(s >> 8) / 16777216.0f; } };
        // A closed block: the outline (counter-clockwise, in x/y) pushed through z = -depth .. +depth; `caps` triangulates the outline.
        auto prism = [](Mesh& m, const std::vector<Vec2>& outline, const std::vector<int>& caps, float depth) {
            m.vertices.clear(); m.triangles.clear();
            const int n = (int)outline.size();
            for (const Vec2& p : outline) m.vertices.push_back({p.x, p.y, -depth});
            for (const Vec2& p : outline) m.vertices.push_back({p.x, p.y, depth});
            for (std::size_t k = 0; k + 2 < caps.size(); k += 3) {
                m.triangles.insert(m.triangles.end(), {caps[k] + n, caps[k + 1] + n, caps[k + 2] + n});
                m.triangles.insert(m.triangles.end(), {caps[k], caps[k + 2], caps[k + 1]});
            }
            for (int i = 0; i < n; ++i) {
                const int j = (i + 1) % n;
                m.triangles.insert(m.triangles.end(), {i, j, j + n, i, j + n, i + n});
            }
        };
        auto inOutline = [](const std::vector<Vec2>& o, double x, double y) {   // even-odd crossing test
            bool in = false;
            for (std::size_t i = 0, j = o.size() - 1; i < o.size(); j = i++) {
                const double xi = o[i].x, yi = o[i].y, xj = o[j].x, yj = o[j].y;
                if ((yi > y) != (yj > y) && x < (xj - xi) * (y - yi) / (yj - yi) + xi) in = !in;
            }
            return in;
        };
        struct Case { const char* name; Mesh mesh; std::function<bool(const Vec3&)> insideLocal; };
        std::vector<Case> cases;
        for (float degrees : {25.0f, 10.0f, 5.0f}) {
            const float rise = 2.0f * std::tan(degrees * 3.14159265f / 180.0f);
            const std::vector<Vec2> tri = {{-1, 0}, {1, 0}, {1, rise}};
            Case c; c.name = degrees > 20.0f ? "wedge 25" : degrees > 7.0f ? "wedge 10" : "wedge 5";
            prism(c.mesh, tri, {0, 1, 2}, 1.0f);
            c.insideLocal = [tri, inOutline](const Vec3& p) { return std::fabs(p.z) < 1.0f && inOutline(tri, p.x, p.y); };
            cases.push_back(std::move(c));
        }
        {
            const std::vector<Vec2> ell = {{0, 0}, {2, 0}, {2, 1}, {1, 1}, {1, 2}, {0, 2}};
            Case c; c.name = "L block";
            prism(c.mesh, ell, {0, 1, 2, 0, 2, 3, 0, 3, 5, 3, 4, 5}, 0.5f);
            c.insideLocal = [ell, inOutline](const Vec3& p) { return std::fabs(p.z) < 0.5f && inOutline(ell, p.x, p.y); };
            cases.push_back(std::move(c));
        }
        {
            Case c; c.name = "thin tetrahedron";
            const Vec3 v[4] = {{-1, 0, -0.6f}, {1, 0, -0.6f}, {0, 0, 1}, {0.1f, 0.12f, 0.1f}};
            c.mesh.vertices.assign(v, v + 4);
            c.mesh.triangles.clear();
            const int f[4][3] = {{0, 1, 2}, {0, 1, 3}, {1, 2, 3}, {2, 0, 3}};
            const Vec3 mid = (v[0] + v[1] + v[2] + v[3]) * 0.25f;
            std::vector<std::pair<Vec3, Vec3>> planes;   // a point of the face, its outward normal
            for (const auto& t : f) {
                Vec3 nrm = Vec3::Cross(v[t[1]] - v[t[0]], v[t[2]] - v[t[0]]);
                const bool out = Vec3::Dot(nrm, v[t[0]] - mid) > 0.0f;
                c.mesh.triangles.insert(c.mesh.triangles.end(), {t[0], out ? t[1] : t[2], out ? t[2] : t[1]});
                planes.push_back({v[t[0]], out ? nrm : nrm * -1.0f});
            }
            c.insideLocal = [planes](const Vec3& p) {
                for (const auto& pl : planes) if (Vec3::Dot(p - pl.first, pl.second) > 0.0f) return false;
                return true;
            };
            cases.push_back(std::move(c));
        }
        struct Pose { Vec3 pos; Quat rot; Vec3 scale; };
        const Pose poses[3] = {{{0, 0, 0}, Quat::Identity, {1, 1, 1}},
                               {{3, -2, 1}, Quat::AngleAxis(37.0f, Vec3{1, 2, 3}.Normalized()), {1.5f, 0.7f, 2.0f}},
                               {{-1, 4, 2}, Quat::AngleAxis(-70.0f, Vec3{0, 1, 0}), {-1.0f, 1.0f, 1.0f}}};   // mirrored
        for (Case& c : cases) {
            for (int pi = 0; pi < 3; ++pi) {
                Scene s("sharp");
                auto* g = s.CreateGameObject(c.name);
                g->transform->localPosition = poses[pi].pos;
                g->transform->localRotation = poses[pi].rot;
                g->transform->localScale = poses[pi].scale;
                g->AddComponent<MeshRenderer>()->mesh = c.mesh;
                auto* mc = g->AddComponent<MeshCollider3D>();
                CHECK(mc->HasTriangles() && mc->Tris().ClosedWinding() != 0);
                const Mat4 l2w = mc->WorldMatrix();
                std::vector<Vec3> world;
                for (const Vec3& v : c.mesh.vertices) world.push_back(l2w.MultiplyPoint(v));
                auto surface = [&](const Vec3& p) {   // every triangle, one by one
                    float best = 1e30f;
                    for (std::size_t t = 0; t + 2 < c.mesh.triangles.size(); t += 3) {
                        const Vec3 q = trimesh::ClosestPointOnTriangle(p, world[c.mesh.triangles[t]], world[c.mesh.triangles[t + 1]],
                                                                       world[c.mesh.triangles[t + 2]]);
                        best = std::min(best, (p - q).Magnitude());
                    }
                    return best;
                };
                Rng rng;
                int outsideZero = 0, insideNot = 0, wrongDistance = 0, outside = 0, inside = 0;
                Vec3 firstBad{0, 0, 0};
                const std::size_t tris = c.mesh.triangles.size() / 3;
                for (int k = 0; k < 6000; ++k) {
                    Vec3 local;
                    if (k % 3 == 0) {   // anywhere around the mesh
                        local = {-2.0f + 5.0f * rng.Next(), -1.5f + 5.0f * rng.Next(), -2.0f + 4.0f * rng.Next()};
                    } else {            // close to an edge or a corner: a point of an edge, moved a little any way
                        const std::size_t t = (std::size_t)(rng.Next() * (float)tris) % tris;
                        const int e = (int)(rng.Next() * 3.0f) % 3;
                        const Vec3 a = c.mesh.vertices[c.mesh.triangles[3 * t + e]], b = c.mesh.vertices[c.mesh.triangles[3 * t + (e + 1) % 3]];
                        const float along = k % 3 == 1 ? rng.Next() : (rng.Next() < 0.5f ? 0.0f : 1.0f);
                        Vec3 dir{rng.Next() - 0.5f, rng.Next() - 0.5f, rng.Next() - 0.5f};
                        if (dir.Magnitude() < 0.05f) dir = {0.3f, 0.2f, 0.1f};
                        local = a + (b - a) * along + dir.Normalized() * (0.003f + 0.4f * rng.Next() * rng.Next());
                    }
                    const Vec3 p = l2w.MultiplyPoint(local);
                    const float truth = surface(p);
                    if (truth < 2e-3f) continue;   // on the skin: either answer is right
                    const bool in = c.insideLocal(local);
                    const NearestSolid3D q = s.physics3D().ClosestPoint(s, p);
                    bool bad = false;
                    if (in) { ++inside; if (!q.hit || q.distance != 0.0f) { ++insideNot; bad = true; } }
                    else {
                        ++outside;
                        if (!q.hit || q.distance <= 0.0f) { ++outsideZero; bad = true; }
                        else if (std::fabs(q.distance - truth) > 1e-3f + truth * 1e-3f) { ++wrongDistance; bad = true; }
                    }
                    if (bad && outsideZero + insideNot + wrongDistance == 1) firstBad = p;
                }
                std::printf("%s, pose %d: %d outside points (%d answered 0, %d a wrong distance), %d inside points (%d not 0)",
                            c.name, pi, outside, outsideZero, wrongDistance, inside, insideNot);
                if (outsideZero + insideNot + wrongDistance > 0) std::printf("; first bad point (%.3f, %.3f, %.3f)", firstBad.x, firstBad.y, firstBad.z);
                std::printf("\n");
                CHECK(outside > 1000 && inside > 100);
                CHECK(outsideZero == 0);
                CHECK(insideNot == 0);
                CHECK(wrongDistance == 0);
            }
        }
        // The reviewer's own example: just in front of the toe edge of the 25 degree wedge.
        Scene s("toe");
        auto* g = s.CreateGameObject("Wedge");
        g->AddComponent<MeshRenderer>()->mesh = cases[0].mesh;
        g->AddComponent<MeshCollider3D>();
        int zero = 0, total = 0;
        for (int i = 0; i < 27; ++i)
            for (int k = 0; k < 13; ++k, ++total) {
                const float ang = (100.0f + 160.0f * (float)i / 26.0f) * 3.14159265f / 180.0f;   // around the toe, on the outside
                const Vec3 p{-1.0f + 0.25f * std::cos(ang), 0.25f * std::sin(ang), -0.9f + 0.15f * (float)k};
                if (cases[0].insideLocal(p)) { --total; continue; }
                if (s.physics3D().ClosestPoint(s, p).distance <= 0.0f) ++zero;
            }
        std::printf("25 degree wedge, %d points in front of the toe edge: %d answered 0\n", total, zero);
        CHECK(total > 250);
        CHECK(zero == 0);
    }

    // Finding 5: a point deep inside a big closed mesh is answered without looking for the nearest triangle.
    RUN_SUITE("deep inside a large closed mesh is cheap");
    {
        Scene s("deep");
        auto* ball = s.CreateGameObject("Ball");
        Mesh& m = ball->AddComponent<MeshRenderer>()->mesh;
        m.vertices.clear(); m.triangles.clear();
        const int rings = 200, around = 200;
        const float pi = 3.14159265f, radius = 5.0f;
        for (int i = 0; i <= rings; ++i)
            for (int j = 0; j < around; ++j) {
                const float th = pi * (float)i / (float)rings, ph = 2.0f * pi * (float)j / (float)around;
                const float ring = (i == 0 || i == rings) ? 0.0f : std::sin(th);
                m.vertices.push_back({radius * ring * std::cos(ph), i == rings ? -radius : radius * std::cos(th), radius * ring * std::sin(ph)});
            }
        for (int i = 0; i < rings; ++i)
            for (int j = 0; j < around; ++j) {
                const int j2 = (j + 1) % around;
                const int q[6] = {i * around + j, i * around + j2, (i + 1) * around + j, i * around + j2, (i + 1) * around + j2, (i + 1) * around + j};
                m.triangles.insert(m.triangles.end(), q, q + 6);
            }
        auto* mc = ball->AddComponent<MeshCollider3D>();
        CHECK(mc->HasTriangles() && mc->Tris().TriangleCount() >= 79000);
        CHECK(mc->Tris().ClosedWinding() != 0);
        CHECK(s.physics3D().ClosestPoint(s, {0, 0, 0}).distance == 0.0f);   // (the first call builds the tree)
        const int calls = 50;
        int zeros = 0;
        const auto t0 = std::chrono::steady_clock::now();
        for (int i = 0; i < calls; ++i) {
            const float f = 0.002f * (float)i;
            if (s.physics3D().ClosestPoint(s, {f, -f, 0.5f * f}).distance == 0.0f) ++zeros;
        }
        const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count() / calls;
        std::printf("centre of an 80k-triangle sphere: %.3f ms per call\n", ms);
        CHECK(zeros == calls);
        CHECK(ms < 0.5);
        // The poles and the seam: rays through shared corners and edges still count once.
        CHECK(s.physics3D().ClosestPoint(s, {0, 4.9f, 0}).distance == 0.0f);
        CHECK(s.physics3D().ClosestPoint(s, {-4.9f, 0, 0}).distance == 0.0f);
        CHECK_NEAR(s.physics3D().ClosestPoint(s, {-5.5f, 0, 0}).distance, 0.5f, 0.01f);
        CHECK_NEAR(s.physics3D().ClosestPoint(s, {0, 5.5f, 0}).distance, 0.5f, 0.01f);
        CHECK_NEAR(s.physics3D().ClosestPoint(s, {-8.0f, 0, 0}, 1.0f).hit ? 1.0f : 0.0f, 0.0f, 0.01f);   // too far: no answer
    }

    // Finding 6b (probe): the nearest-triangle walk skips nothing, also on a mesh of stacked look-alike triangles
    // (the tree halves its triangles at every level, so it is far shallower than the walk's stack).
    RUN_SUITE("triangle tree walk on a degenerate pile");
    {
        Mesh m;
        m.vertices.clear(); m.triangles.clear();
        for (int i = 0; i < 30000; ++i) {   // long thin slivers piled along one line, plus many exact copies of one triangle
            const float x = (i % 3 == 0) ? 0.0f : 1e-4f * (float)i;
            const int base = (int)m.vertices.size();
            m.vertices.push_back({x, 0, 0}); m.vertices.push_back({x + 50.0f, 0.001f, 0}); m.vertices.push_back({x, 0.001f, 0.002f});
            m.triangles.insert(m.triangles.end(), {base, base + 1, base + 2});
        }
        TriMesh t;
        t.Build(m);
        CHECK(t.TriangleCount() == 30000);
        std::printf("triangle tree: %d triangles, %d nodes\n", t.TriangleCount(), t.NodeCount());
        const trimesh::WorldView view(t, Mat4::Identity());
        Vec3 cp; float d = 0.0f; bool in = false;
        const Vec3 p{1.0f, 0.5f, 0.3f};
        CHECK(view.Nearest(p, 1e9f, cp, d, in));
        float brute = 1e30f;
        for (int i = 0; i < t.TriangleCount(); ++i) {
            Vec3 a, b, c; t.Triangle(i, a, b, c);
            brute = std::min(brute, (p - trimesh::ClosestPointOnTriangle(p, a, b, c)).Magnitude());
        }
        CHECK_NEAR(d, brute, 1e-5f);
    }

    // Finding 2: a flipped sprite (negative scale) still has an inside.
    RUN_SUITE("2d closest point on a flipped collider");
    {
        const Vec3 flips[4] = {{1, 1, 1}, {-1, 1, 1}, {1, -1, 1}, {-1, -1, 1}};
        for (const Vec3& f : flips) {
            Scene s("flip2d");
            auto* box = s.CreateGameObject("Box");
            box->transform->localScale = f;
            box->AddComponent<BoxCollider2D>()->size = {2, 2};
            SurfaceQuery2D in = s.physics().ClosestPoint(s, {0.2f, 0.1f});
            std::printf("2x2 box, scale (%+.0f, %+.0f): inside point (0.2, 0.1) is %.3f away at (%.2f, %.2f)\n", f.x, f.y, in.distance, in.point.x, in.point.y);
            CHECK(in.hit && in.distance == 0.0f);
            CHECK_NEAR(s.physics().ClosestPoint(s, {1.5f, 0.1f}).distance, 0.5f, 0.01f);
            CHECK_NEAR(s.physics().ClosestPoint(s, {-0.3f, -1.75f}).distance, 0.75f, 0.01f);

            Scene p("flippoly");
            auto* tri = p.CreateGameObject("Tri");
            tri->transform->localScale = f;
            tri->AddComponent<PolygonCollider2D>();   // (-0.5,-0.5) (0.5,-0.5) (0,0.5)
            CHECK(p.physics().ClosestPoint(p, {0.0f, -0.1f * f.y}).distance == 0.0f);
            CHECK(p.physics().ClosestPoint(p, {0.45f, 0.4f * f.y}).distance > 0.1f);

            Scene far("flipfar");   // far from the origin the corner order is still worked out exactly
            auto* fb = far.CreateGameObject("Box");
            fb->transform->localPosition = {5000, 3000, 0};
            fb->transform->localScale = f;
            fb->AddComponent<BoxCollider2D>()->size = {2, 2};
            auto* ft = far.CreateGameObject("Tri");
            ft->transform->localPosition = {5010, 3000, 0};
            ft->transform->localScale = f;
            ft->AddComponent<PolygonCollider2D>();
            CHECK(far.physics().ClosestPoint(far, {5000.25f, 3000.25f}).distance == 0.0f);
            CHECK_NEAR(far.physics().ClosestPoint(far, {5001.5f, 3000.25f}).distance, 0.5f, 0.01f);
            CHECK(far.physics().ClosestPoint(far, {5010.0f, 3000.0f - 0.125f * f.y}).distance == 0.0f);

            Scene c("flipcap");
            auto* cap = c.CreateGameObject("Cap");
            cap->transform->localScale = f;
            auto* cc = cap->AddComponent<CapsuleCollider2D>();
            Vec2 a, b; cc->Segment(a, b);
            const float r = cc->WorldRadius();
            const Vec2 mid{(a.x + b.x) * 0.5f, (a.y + b.y) * 0.5f};
            CHECK(r > 0.0f);
            CHECK(c.physics().ClosestPoint(c, {mid.x + 0.5f * std::fabs(r), mid.y}).distance == 0.0f);
            CHECK_NEAR(c.physics().ClosestPoint(c, {mid.x + std::fabs(r) + 0.4f, mid.y}).distance, 0.4f, 0.01f);

            Scene k("flipcomp");
            auto* comp = k.CreateGameObject("Comp");
            comp->transform->localScale = f;
            comp->AddComponent<CompositeCollider2D>()->AddBox({0, 0}, {2, 2});
            CHECK(k.physics().ClosestPoint(k, {0.2f, 0.1f}).distance == 0.0f);
            CHECK_NEAR(k.physics().ClosestPoint(k, {1.5f, 0.1f}).distance, 0.5f, 0.01f);
        }
    }

    // Finding 6a: ConnectTo an object with no name, then type a name: the typed name is the partner.
    RUN_SUITE("joint name typed after linking a nameless object");
    {
        Scene s("nameless");
        auto* blank = s.CreateGameObject("");
        auto* b = s.CreateGameObject("Beta");
        auto* w = s.CreateGameObject("Weight");
        auto* j = w->AddComponent<Joint3D>();
        j->ConnectTo(blank);
        CHECK(Joint3DPartner(s, j) == blank);
        CHECK(Joint3DPartner(s, j) == blank);
        j->connectedBody = "Beta";
        GameObject* now = Joint3DPartner(s, j);
        std::printf("nameless link, then \"Beta\" typed: partner is %s\n", now == b ? "Beta" : now == blank ? "still the nameless object" : "something else");
        CHECK(now == b);
    }

    // Finding 3: the step limit also holds when the top of the ledge is not exactly flat: a little tilted, round or
    // uneven. A real slope is still walked with no step at all.
    RUN_SUITE("step limit on tilted, round and uneven tops");
    {
        auto player = [](Scene& s) {
            auto* g = s.CreateGameObject("Player");
            g->transform->localPosition = {0, 0.9f, 0};
            g->AddComponent<BoxCollider3D>()->size = {0.6f, 1.8f, 0.6f};
            return g;
        };
        auto walk = [](Scene& s, GameObject* p, float stepHeight, int frames, float& highest) {
            highest = 0.0f;
            for (int i = 0; i < frames; ++i) {
                MovePlayerBody(s, p, {0.05f, 0, 0}, stepHeight, 50.0f);
                highest = std::max(highest, p->transform->Position().y - 0.9f);
            }
        };
        auto bumpy = [](Scene& s) {   // a block with an upright 0.34 side whose top goes up and down by a few centimetres
            auto* g = s.CreateGameObject("Bumpy");
            Mesh& m = g->AddComponent<MeshRenderer>()->mesh;
            m.vertices.clear(); m.triangles.clear();
            const Vec2 outline[7] = {{0.8f, 0}, {3, 0}, {3, 0.34f}, {2.2f, 0.31f}, {1.6f, 0.39f}, {1.1f, 0.32f}, {0.8f, 0.34f}};
            for (const Vec2& o : outline) { m.vertices.push_back({o.x, o.y, -1}); m.vertices.push_back({o.x, o.y, 1}); }
            for (int i = 0; i < 7; ++i) {
                const int a = 2 * i, b = 2 * ((i + 1) % 7);
                m.triangles.insert(m.triangles.end(), {a, b, b + 1, a, b + 1, a + 1});
            }
            g->AddComponent<MeshCollider3D>();
        };
        struct Top { const char* name; int kind; float degrees; };
        const Top tops[] = {{"flat", 0, 0.0f}, {"tilted 2 degrees up", 0, 2.0f}, {"tilted 3 degrees up", 0, 3.0f}, {"tilted 3 degrees down", 0, -3.0f},
                            {"tilted 8 degrees up", 0, 8.0f}, {"tilted 3 degrees sideways", 1, 3.0f}, {"an uneven mesh", 3, 0.0f}};
        for (const Top& t : tops) {
            for (int withStep = 0; withStep < 2; ++withStep) {
                Scene s("top");
                Floor(s);
                if (t.kind == 3) {
                    bumpy(s);
                } else {   // a box turned about the middle of its near top edge, which stays at x 0.8, 0.34 above the floor
                    auto* ledge = s.CreateGameObject("Ledge");
                    const Quat rot = Quat::AngleAxis(t.degrees, t.kind == 1 ? Vec3{1, 0, 0} : Vec3{0, 0, 1});
                    ledge->transform->localRotation = rot;
                    ledge->transform->localPosition = Vec3{0.8f, 0.34f, 0} - rot * Vec3{-1.1f, 0.4f, 0};
                    ledge->AddComponent<BoxCollider3D>()->size = {2.2f, 0.8f, 2.0f};
                }
                auto* p = player(s);
                float highest = 0.0f;
                walk(s, p, withStep ? 0.4f : 0.0f, 30, highest);
                std::printf("0.34 ledge, top %s, stepHeight %.1f: x %+.3f y %+.3f (highest %+.3f above rest)\n", t.name,
                            withStep ? 0.4f : 0.0f, p->transform->Position().x, p->transform->Position().y, highest);
                if (withStep) CHECK(p->transform->Position().y > 1.1f);   // a 0.4 step climbs every one of them
                else { CHECK(highest < 0.12f); CHECK(p->transform->Position().y < 1.02f); }   // (climbing it would be 0.34)
            }
        }
        for (float degrees : {10.0f, 20.0f, 35.0f}) {   // a ramp from the floor is walked with no step at all
            Scene s("ramp");
            Floor(s);
            auto* ramp = s.CreateGameObject("Ramp");
            const float rad = degrees * 3.14159265f / 180.0f;
            // a 6 long, 0.2 thick slab whose top face starts on the floor at x = 0.6
            ramp->transform->localRotation = Quat::AngleAxis(degrees, {0, 0, 1});
            ramp->transform->localPosition = Vec3{0.6f, 0, 0} + Vec3{std::cos(rad), std::sin(rad), 0} * 3.0f + Vec3{std::sin(rad), -std::cos(rad), 0} * 0.1f;
            ramp->AddComponent<BoxCollider3D>()->size = {6.0f, 0.2f, 4.0f};
            auto* p = player(s);
            float highest = 0.0f;
            walk(s, p, 0.0f, 40, highest);
            const float x = p->transform->Position().x, y = p->transform->Position().y;
            std::printf("%.0f degree ramp, stepHeight 0: x %+.3f y %+.3f\n", degrees, x, y);
            CHECK(x > 1.2f);
            CHECK(y > 0.9f + (x - 0.9f) * std::tan(rad) * 0.8f);
        }
    }

    // Finding 4: the controllers pass their own step setting on. stepOffset 0 does not climb, the default climbs a small step.
    RUN_SUITE("controllers honour their step setting");
    {
        auto run = [](int which, float stepOffset, float ledgeHeight, const char* label) {
            Scene s("controller");
            Floor(s);
            auto* ledge = s.CreateGameObject("Ledge");
            ledge->transform->localPosition = {0, ledgeHeight * 0.5f, -1.9f};
            ledge->AddComponent<BoxCollider3D>()->size = {3.0f, ledgeHeight, 2.2f};   // its near side is at z = -0.8
            auto* p = s.CreateGameObject("Player");
            p->transform->localPosition = {0, 0.9f, 0};
            p->AddComponent<BoxCollider3D>()->size = {0.6f, 1.8f, 0.6f};
            auto set = [stepOffset](auto* c) { if (stepOffset >= 0.0f) c->stepOffset = stepOffset; return c; };
            ClickToMoveController* ctm = nullptr;
            switch (which) {
            case 0: set(p->AddComponent<FirstPersonController>()); break;
            case 1: set(p->AddComponent<ThirdPersonController>()); break;
            case 2: set(p->AddComponent<TopDownController>()); break;
            case 3: set(p->AddComponent<TopDownShooterController>()); break;
            default: ctm = set(p->AddComponent<ClickToMoveController>()); ctm->usePathfinding = false; ctm->giveUpWhenStuck = false; break;
            }
            s.Start();
            if (ctm) ctm->MoveTo({0.0f, 0.9f, -4.0f});
            else Input::FeedKeys({'w'});   // forward is -Z
            float highest = 0.0f;
            for (int i = 0; i < 90; ++i) { s.Update(1.0f / 60.0f); highest = std::max(highest, p->transform->Position().y - 0.9f); }
            Input::FeedKeys({});
            const Vec3 at = p->transform->Position();
            static const char* const kNames[] = {"first-person", "third-person", "top-down", "top-down shooter", "click-to-move"};
            std::printf("%s controller, %s, %.2f ledge: z %+.3f y %+.3f (highest %+.3f above rest)\n", kNames[which],
                        label, ledgeHeight, at.z, at.y, highest);
            return highest;
        };
        for (int which = 0; which < 5; ++which) {   // first-, third-person, top-down, top-down shooter, click-to-move
            CHECK(run(which, 0.0f, 0.3f, "stepOffset 0") < 0.05f);
            CHECK(run(which, 0.2f, 0.3f, "stepOffset 0.2") < 0.05f);
            CHECK(run(which, -1.0f, 0.25f, "default stepOffset") > 0.2f);
            CHECK(run(which, 0.3f, 0.25f, "stepOffset 0.3") > 0.2f);
        }
    }

    TEST_MAIN_RESULT();
}
