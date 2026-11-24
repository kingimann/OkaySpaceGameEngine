// Character push-out, joints linked by object, shape casts that report every hit, closest point.
#include "test_framework.hpp"
#include <Okay.hpp>
#include "okay/Physics/PlayerCollision.hpp"
#include "okay/Physics/ColliderShapes3D.hpp"
#include "okay/Scene/SceneSerializer.hpp"
#include "okay/Components/MeshRenderer.hpp"
#include "okay/Components/Terrain.hpp"
#include <cmath>
#include <cstdio>

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

    TEST_MAIN_RESULT();
}
