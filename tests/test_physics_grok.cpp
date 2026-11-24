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
            for (int i = 0; i <= rings; ++i)
                for (int j = 0; j < around; ++j) {
                    const float th = pi * (float)i / (float)rings, ph = 2.0f * pi * (float)j / (float)around;
                    m.vertices.push_back({radius * std::sin(th) * std::cos(ph), radius * std::cos(th), radius * std::sin(th) * std::sin(ph)});
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
        const RaycastHit3D ground = s.physics3D().Raycast(s, {0, 20, 0}, {0, -1, 0}, 40.0f, nullptr, Physics3D::kAllLayers, false);
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

    TEST_MAIN_RESULT();
}
