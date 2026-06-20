#include "test_framework.hpp"
#include <Okay.hpp>
#include <cmath>
#include <cstdio>
#include <fstream>

using namespace okay;

int main() {
    RUN_SUITE("mesh3d");

    // --- New primitives have sensible geometry and triangle counts ---
    {
        Mesh plane = Mesh::Plane(4.0f);
        CHECK(plane.name == "Plane");
        CHECK(plane.vertices.size() == 4);
        CHECK(plane.TriangleCount() == 2);
        // Plane lies in the XZ plane (y == 0 for all verts).
        for (auto& v : plane.vertices) CHECK_NEAR(v.y, 0.0f, 0.0001f);

        Mesh sph = Mesh::Sphere(0.5f, 8, 12);
        CHECK(sph.name == "Sphere");
        CHECK(sph.vertices.size() == (8 + 1) * (12 + 1));
        CHECK(sph.TriangleCount() == 8 * 12 * 2);
        // Every vertex is ~radius from the center.
        for (auto& v : sph.vertices) CHECK_NEAR(v.Magnitude(), 0.5f, 0.01f);

        Mesh cyl = Mesh::Cylinder(0.5f, 2.0f, 12);
        CHECK(cyl.name == "Cylinder");
        CHECK(cyl.TriangleCount() == 12 * 2 /*sides*/ + 12 * 2 /*caps*/);

        Mesh tube = Mesh::Tube(0.5f, 0.3f, 2.0f, 24);
        CHECK(tube.name == "Tube");
        CHECK(tube.vertices.size() == 24u * 4u);
        CHECK(tube.TriangleCount() == 24 * 8);        // outer+inner walls + 2 rings
        CHECK_NEAR(tube.Size().y, 2.0f, 0.001f);      // height
        CHECK_NEAR(tube.Size().x, 1.0f, 0.01f);       // outer diameter
        CHECK(Mesh::FromName("Tube").name == "Tube");

        Mesh wedge = Mesh::Wedge(2.0f);
        CHECK(wedge.name == "Wedge");
        CHECK(wedge.vertices.size() == 6u);
        CHECK(wedge.TriangleCount() == 8);           // base + front + slope + 2 sides
        CHECK_NEAR(wedge.Size().x, 2.0f, 0.001f);    // spans the full cube box
        CHECK_NEAR(wedge.Size().y, 2.0f, 0.001f);

        Mesh cone = Mesh::Cone(0.5f, 1.0f, 16);
        CHECK(cone.name == "Cone");
        CHECK(cone.vertices.size() == 16u + 2u);          // ring + apex + base center
        CHECK(cone.TriangleCount() == 16 * 2);            // sides + base cap

        Mesh tor = Mesh::Torus(0.5f, 0.2f, 16, 10);
        CHECK(tor.name == "Torus");
        CHECK(tor.vertices.size() == 16u * 10u);
        CHECK(tor.TriangleCount() == 16 * 10 * 2);

        Mesh cap = Mesh::Capsule(0.5f, 2.0f, 12, 6);
        CHECK(cap.name == "Capsule");
        CHECK(!cap.vertices.empty());
        CHECK(cap.TriangleCount() > 0);
        // Capsule height ~2 -> spans about -1..1 in Y.
        CHECK_NEAR(cap.Size().y, 2.0f, 0.05f);
    }

    // --- Subdivide quadruples triangle count and stays welded ---
    {
        Mesh quad = Mesh::Quad();          // 2 triangles, 4 verts
        int t0 = quad.TriangleCount();
        quad.Subdivide();
        CHECK(quad.TriangleCount() == t0 * 4);
        CHECK(quad.name.empty());          // no longer a tagged primitive
        // Shared edge midpoints are welded: a single quad's 2 tris share one
        // diagonal edge, so 5 unique midpoints are added (not 6).
        CHECK(quad.vertices.size() == 4u + 5u);
    }

    // --- ProjectToSphere puts every vertex on the given radius ---
    {
        Mesh m = Mesh::Cube();
        m.Subdivide();
        m.ProjectToSphere(2.0f);
        for (auto& v : m.vertices) CHECK_NEAR(v.Magnitude(), 2.0f, 0.001f);
    }

    // --- Normals are unit length and radial on a sphere ---
    {
        Mesh sph = Mesh::Sphere(1.0f, 6, 8);
        auto normals = sph.Normals();
        CHECK(normals.size() == sph.vertices.size());
        int checked = 0;
        for (std::size_t i = 0; i < sph.vertices.size(); ++i) {
            float mag = normals[i].Magnitude();
            if (mag < 0.5f) continue;             // skip seam/pole verts whose faces cancel
            CHECK_NEAR(mag, 1.0f, 0.001f);
            // On a sphere about the origin the surface normal is (anti)parallel
            // to the radial direction; |dot| ~ 1 regardless of winding.
            Vec3 p = sph.vertices[i];
            CHECK(std::fabs(Vec3::Dot(normals[i], p.Normalized())) > 0.7f);
            ++checked;
        }
        CHECK(checked > 0);
    }

    // --- Icosphere: 20*4^n triangles, all verts on the radius ---
    {
        Mesh ico0 = Mesh::Icosphere(1.0f, 0);
        CHECK(ico0.name == "Icosphere");
        CHECK(ico0.vertices.size() == 12u);          // raw icosahedron
        CHECK(ico0.TriangleCount() == 20);
        for (auto& v : ico0.vertices) CHECK_NEAR(v.Magnitude(), 1.0f, 0.001f);

        Mesh ico2 = Mesh::Icosphere(2.0f, 2);
        CHECK(ico2.TriangleCount() == 20 * 4 * 4);   // two subdivisions
        for (auto& v : ico2.vertices) CHECK_NEAR(v.Magnitude(), 2.0f, 0.001f);
        CHECK(Mesh::FromName("Icosphere").name == "Icosphere");
    }

    // --- WeldVertices merges coincident verts and drops degenerate triangles ---
    {
        // Two cubes at the same spot: Combine duplicates all 8 verts; welding
        // collapses them back to 8 (and identical triangles stay valid).
        Mesh a = Mesh::Cube();
        Mesh both = Mesh::Combined(a, a);
        CHECK(both.vertices.size() == 16u);
        int removed = both.WeldVertices();
        CHECK(removed == 8);
        CHECK(both.vertices.size() == 8u);

        // A triangle whose two corners coincide is degenerate and dropped.
        Mesh tri;
        tri.vertices = {{0, 0, 0}, {0, 0, 0}, {1, 0, 0}};   // first two identical
        tri.triangles = {0, 1, 2};
        tri.WeldVertices();
        CHECK(tri.vertices.size() == 2u);
        CHECK(tri.TriangleCount() == 0);                    // collapsed -> removed
    }

    // --- Grid: (cols+1)(rows+1) verts, cols*rows*2 tris, flat on XZ, centered ---
    {
        Mesh g = Mesh::Grid(10.0f, 6.0f, 5, 3);
        CHECK(g.name == "Grid");
        CHECK(g.vertices.size() == (5u + 1) * (3u + 1));
        CHECK(g.TriangleCount() == 5 * 3 * 2);
        for (auto& v : g.vertices) CHECK_NEAR(v.y, 0.0f, 0.0001f);   // flat
        Vec3 s = g.Size();
        CHECK_NEAR(s.x, 10.0f, 0.001f);
        CHECK_NEAR(s.z, 6.0f, 0.001f);
        Vec3 c = g.Center();
        CHECK_NEAR(c.x, 0.0f, 0.001f);                              // centered at origin
        CHECK_NEAR(c.z, 0.0f, 0.001f);
        CHECK(Mesh::FromName("Grid").name == "Grid");
    }

    // --- RecenterToOrigin / ScaleToFit normalize imported geometry ---
    {
        // A unit cube shoved off to (10, 0, 0).
        Mesh m = Mesh::Cube().Transformed({1, 1, 1}, {10, 0, 0});
        CHECK_NEAR(m.Center().x, 10.0f, 0.001f);
        m.RecenterToOrigin();
        CHECK_NEAR(m.Center().x, 0.0f, 0.001f);
        CHECK_NEAR(m.Center().y, 0.0f, 0.001f);
        CHECK_NEAR(m.Size().x, 1.0f, 0.001f);                       // size unchanged

        // A 4-unit cube scaled to fit 2 units.
        Mesh big = Mesh::Cube(4.0f);
        big.ScaleToFit(2.0f);
        Vec3 s = big.Size();
        CHECK_NEAR(s.x, 2.0f, 0.001f);
        CHECK_NEAR(s.y, 2.0f, 0.001f);
        CHECK_NEAR(s.z, 2.0f, 0.001f);
    }

    // --- Extrude: a 2D outline becomes a prism (2n verts, caps + walls) ---
    {
        std::vector<Vec2> square = {{-1, -1}, {1, -1}, {1, 1}, {-1, 1}};  // n = 4
        Mesh m = Mesh::Extrude(square, 2.0f);
        CHECK(m.vertices.size() == 8u);                     // 2 * n
        CHECK(m.TriangleCount() == 2 * (4 - 2) + 2 * 4);    // caps(4) + walls(8) = 12
        CHECK_NEAR(m.Size().x, 2.0f, 0.001f);
        CHECK_NEAR(m.Size().z, 2.0f, 0.001f);               // depth along Z
        // Too-small outlines yield an empty mesh.
        std::vector<Vec2> line = {{0, 0}, {1, 0}};
        CHECK(Mesh::Extrude(line, 1.0f).vertices.empty());
    }

    // --- Lathe: revolve a profile into a surface of revolution ---
    {
        // A 3-point profile revolved in 12 segments.
        std::vector<Vec2> profile = {{0.0f, -1.0f}, {1.0f, 0.0f}, {0.0f, 1.0f}};
        Mesh m = Mesh::Lathe(profile, 12);
        CHECK(m.vertices.size() == 12u * 3u);
        CHECK(m.TriangleCount() == 12 * (3 - 1) * 2);
        // Widest ring (radius 1) reaches x = +/-1; height spans -1..1.
        CHECK_NEAR(m.Size().y, 2.0f, 0.001f);
        CHECK_NEAR(m.Size().x, 2.0f, 0.01f);
        CHECK(Mesh::Lathe(profile, 2).vertices.empty());    // needs >= 3 segments
    }

    // --- Rotated bakes an Euler rotation into the vertices ---
    {
        // A point on +X, rotated 90deg about Y (Quat::Euler applies Z,X,Y), lands
        // on the Z axis; magnitude is preserved and it's no longer a named prim.
        Mesh m;
        m.vertices = {{1, 0, 0}};
        m.name = "Custom";
        Mesh r = m.Rotated({0, 90, 0});
        CHECK(r.name.empty());
        CHECK_NEAR(r.vertices[0].Magnitude(), 1.0f, 0.001f);
        CHECK_NEAR(r.vertices[0].x, 0.0f, 0.01f);          // left +X
        CHECK_NEAR(std::fabs(r.vertices[0].z), 1.0f, 0.01f);
        // Rotating a cube by 0 leaves it unchanged in size.
        Mesh c = Mesh::Cube().Rotated({0, 0, 0});
        CHECK_NEAR(c.Size().x, 1.0f, 0.001f);
    }

    // --- FlipWinding reverses each triangle's orientation ---
    {
        Mesh m;
        m.vertices = {{0, 0, 0}, {1, 0, 0}, {0, 1, 0}};
        m.triangles = {0, 1, 2};
        m.FlipWinding();
        CHECK(m.triangles[0] == 0);
        CHECK(m.triangles[1] == 2);   // 2nd and 3rd swapped
        CHECK(m.triangles[2] == 1);
        // Flipping twice restores the original winding.
        m.FlipWinding();
        CHECK(m.triangles[1] == 1);
        CHECK(m.triangles[2] == 2);
    }

    // --- Bounds / Center / Size of the AABB ---
    {
        Mesh cube = Mesh::Cube(2.0f);                      // spans -1..1 on each axis
        Vec3 lo, hi; cube.Bounds(lo, hi);
        CHECK_NEAR(lo.x, -1.0f, 0.001f);
        CHECK_NEAR(hi.z, 1.0f, 0.001f);
        Vec3 c = cube.Center();
        CHECK_NEAR(c.x, 0.0f, 0.001f);
        CHECK_NEAR(c.y, 0.0f, 0.001f);
        Vec3 s = cube.Size();
        CHECK_NEAR(s.x, 2.0f, 0.001f);
        CHECK_NEAR(s.y, 2.0f, 0.001f);

        // A translated mesh reports a shifted center but the same size.
        Mesh moved = cube.Transformed({1, 1, 1}, {10, 0, 0});
        CHECK_NEAR(moved.Center().x, 10.0f, 0.001f);
        CHECK_NEAR(moved.Size().x, 2.0f, 0.001f);

        // Empty mesh is safe.
        Mesh empty;
        Vec3 el, eh; empty.Bounds(el, eh);
        CHECK_NEAR(el.x, 0.0f, 0.001f);
        CHECK_NEAR(eh.y, 0.0f, 0.001f);
    }

    // --- FromName round-trips the new primitives (so they serialize) ---
    {
        CHECK(Mesh::FromName("Sphere").name == "Sphere");
        CHECK(Mesh::FromName("Cylinder").name == "Cylinder");
        CHECK(Mesh::FromName("Plane").name == "Plane");
        CHECK(Mesh::FromName("Cube").name == "Cube");
        CHECK(Mesh::FromName("Cone").name == "Cone");
        CHECK(Mesh::FromName("Torus").name == "Torus");
        CHECK(Mesh::FromName("Capsule").name == "Capsule");
        CHECK(Mesh::FromName("Wedge").name == "Wedge");

        Scene scene("S");
        GameObject* go = scene.CreateGameObject("Ball");
        go->AddComponent<MeshRenderer>()->mesh = Mesh::Sphere();
        std::string text = SceneSerializer::Serialize(scene);
        Scene loaded("L");
        std::string err;
        CHECK(SceneSerializer::Deserialize(loaded, text, &err));
        CHECK(loaded.Find("Ball")->GetComponent<MeshRenderer>()->mesh.name == "Sphere");
    }

    // --- OBJ save/load round-trips a cube (8 verts, 12 triangles) ---
    {
        Mesh cube = Mesh::Cube();
        const std::string path = "okay_test_cube.obj";
        CHECK(cube.SaveOBJ(path));
        bool ok = false;
        Mesh loaded = Mesh::LoadOBJ(path, &ok);
        CHECK(ok);
        CHECK(loaded.vertices.size() == cube.vertices.size());      // 8
        CHECK(loaded.TriangleCount() == cube.TriangleCount());      // 12
        for (std::size_t i = 0; i < cube.vertices.size(); ++i) {
            CHECK_NEAR(loaded.vertices[i].x, cube.vertices[i].x, 0.001f);
            CHECK_NEAR(loaded.vertices[i].y, cube.vertices[i].y, 0.001f);
            CHECK_NEAR(loaded.vertices[i].z, cube.vertices[i].z, 0.001f);
        }
        std::remove(path.c_str());
        // Missing file reports failure and yields an empty mesh.
        bool ok2 = true;
        Mesh missing = Mesh::LoadOBJ("does_not_exist.obj", &ok2);
        CHECK(!ok2);
        CHECK(missing.vertices.empty());
    }

    // --- LoadOBJ handles f indices with v/vt/vn slashes and negatives ---
    {
        const std::string path = "okay_test_face.obj";
        {
            std::ofstream f(path);
            f << "v 0 0 0\nv 1 0 0\nv 1 1 0\nv 0 1 0\n";
            f << "f 1/1/1 2/2/1 3/3/1 4/4/1\n";   // quad -> fan -> 2 triangles
        }
        bool ok = false;
        Mesh m = Mesh::LoadOBJ(path, &ok);
        CHECK(ok);
        CHECK(m.vertices.size() == 4);
        CHECK(m.TriangleCount() == 2);
        std::remove(path.c_str());
    }

    // --- Transformed / Combine build compound models ---
    {
        Mesh a = Mesh::Cube();
        Mesh b = a.Transformed({2.0f, 2.0f, 2.0f}, {10.0f, 0.0f, 0.0f});
        CHECK(b.name.empty());                       // no longer a primitive
        CHECK(b.vertices.size() == a.vertices.size());
        // Original first vert (-0.5) scaled by 2 then +10 -> 9.
        CHECK_NEAR(b.vertices[0].x, a.vertices[0].x * 2.0f + 10.0f, 0.001f);

        Mesh combo = Mesh::Combined(a, b);
        CHECK(combo.vertices.size() == a.vertices.size() + b.vertices.size());
        CHECK(combo.TriangleCount() == a.TriangleCount() + b.TriangleCount());
        // Re-indexed: the appended triangles point past the first mesh's verts.
        CHECK(combo.triangles[a.triangles.size()] >= (int)a.vertices.size());
    }

    // --- MeshRenderer.meshPath survives serialization ---
    {
        Scene scene("Model");
        GameObject* go = scene.CreateGameObject("Imported");
        go->AddComponent<MeshRenderer>()->meshPath = "models/ship.obj";
        std::string text = SceneSerializer::Serialize(scene);
        Scene loaded("L");
        std::string err;
        CHECK(SceneSerializer::Deserialize(loaded, text, &err));
        CHECK(loaded.Find("Imported")->GetComponent<MeshRenderer>()->meshPath
              == "models/ship.obj");
    }

    // --- LambertShade: facing the light is bright, away is ambient-floored ---
    {
        Vec3 light = DefaultLightDir();
        // A normal pointing straight back at the light source is fully lit.
        float lit = LambertShade(light * -1.0f, light);
        CHECK_NEAR(lit, 1.0f, 0.001f);
        // A normal pointing along the light (away) gets only the ambient floor.
        float dark = LambertShade(light, light);
        CHECK_NEAR(dark, 0.25f, 0.001f);
        // Perpendicular is the ambient floor too (Lambert clamped at 0).
        Vec3 perp = Vec3::Cross(light, Vec3{0, 1, 0}).Normalized();
        float side = LambertShade(perp, light);
        CHECK_NEAR(side, 0.25f, 0.02f);
        // Custom ambient is respected and the result never exceeds 1.
        CHECK_NEAR(LambertShade(light * -1.0f, light, 0.1f), 1.0f, 0.001f);
        CHECK(LambertShade(light, light, 0.4f) >= 0.4f);
    }

    // --- SceneLight global drives shading and is script-controllable ---
    {
        SceneLight::Reset();
        CHECK_NEAR(SceneLight::Ambient(), 0.25f, 0.001f);
        // A normal facing the (default) light is fully lit through the global.
        CHECK_NEAR(SceneLight::Shade(SceneLight::Direction() * -1.0f), 1.0f, 0.001f);

        // Point the light straight down (0,-1,0); a flat ground (normal +Y) is lit.
        SceneLight::SetDirection({0, -1, 0});
        CHECK_NEAR(SceneLight::Shade({0, 1, 0}), 1.0f, 0.001f);
        // Raise the ambient floor; a surface facing away still gets it.
        SceneLight::SetAmbient(0.6f);
        CHECK_NEAR(SceneLight::Shade({0, -1, 0}), 0.6f, 0.001f);

        // Scripts set the same global via set_light / set_ambient.
        SceneLight::Reset();
        Scene scene("Lit");
        GameObject* go = scene.CreateGameObject("Director");
        auto* sc = go->AddComponent<ScriptComponent>("okayscript");
        CHECK(sc->LoadSource(
            "function start() { set_light(0, -1, 0); set_ambient(0.5); set_x(ambient()); }"));
        scene.Start();
        CHECK_NEAR(go->transform->localPosition.x, 0.5f, 0.001f);   // ambient() readback
        CHECK_NEAR(SceneLight::Ambient(), 0.5f, 0.001f);
        CHECK_NEAR(SceneLight::Shade({0, 1, 0}), 1.0f, 0.001f);     // ground lit by down-light
        SceneLight::Reset();                                        // leave global clean
    }

    // --- Scripts can move and rotate in 3D ---
    {
        Scene scene("3D");
        GameObject* go = scene.CreateGameObject("Mover");
        auto* sc = go->AddComponent<ScriptComponent>("okayscript");
        CHECK(sc->LoadSource(
            "function start() { set_pos3(1, 2, 3); move3(0, 0, 5); set_z(pos_z() + 1); }"));
        scene.Start();
        CHECK_NEAR(go->transform->localPosition.x, 1.0f, 0.001f);
        CHECK_NEAR(go->transform->localPosition.y, 2.0f, 0.001f);
        CHECK_NEAR(go->transform->localPosition.z, 9.0f, 0.001f); // 3 + 5 + 1
    }

    // --- GroundPivot centers X/Z and drops the base to Y = 0 ---
    {
        Mesh cube = Mesh::Cube(2.0f).Transformed({1, 1, 1}, {5, 3, -4}); // off-center
        cube.GroundPivot();
        Vec3 lo, hi; cube.Bounds(lo, hi);
        CHECK_NEAR(lo.y, 0.0f, 0.001f);                  // base sits on the ground
        CHECK_NEAR((lo.x + hi.x) * 0.5f, 0.0f, 0.001f);  // centered in X
        CHECK_NEAR((lo.z + hi.z) * 0.5f, 0.0f, 0.001f);  // centered in Z
        CHECK_NEAR(hi.y, 2.0f, 0.001f);                  // height preserved
    }

    // --- vel_toward sets a sibling body's velocity at a named target ---
    {
        Scene scene("Homing");
        GameObject* target = scene.CreateGameObject("Player");
        target->transform->localPosition = {3, 4, 0};    // distance 5 (3-4-5)
        GameObject* go = scene.CreateGameObject("Missile");
        auto* rb = go->AddComponent<Rigidbody2D>();
        rb->bodyType = Rigidbody2D::BodyType::Kinematic;
        auto* sc = go->AddComponent<ScriptComponent>("okayscript");
        CHECK(sc->LoadSource("function start() { vel_toward(\"Player\", 10); }"));
        scene.Start();
        // Unit dir (0.6, 0.8) * 10 = (6, 8).
        CHECK_NEAR(rb->velocity.x, 6.0f, 0.01f);
        CHECK_NEAR(rb->velocity.y, 8.0f, 0.01f);
    }

    // --- Light: serializes and aims the global SceneLight via its forward ---
    {
        SceneLight::Reset();
        Scene scene("Lit");
        GameObject* go = scene.CreateGameObject("Sun");
        auto* l = go->AddComponent<Light>();
        l->ambient = 0.5f;
        go->transform->localRotation = Quat::Euler({90, 0, 0}); // forward +Z -> down -Y
        ApplySceneLight(scene);
        CHECK_NEAR(SceneLight::Ambient(), 0.5f, 0.001f);
        Vec3 d = SceneLight::Direction();
        CHECK_NEAR(d.y, -1.0f, 0.02f);          // light shines downward

        // Round-trips through serialization.
        std::string text = SceneSerializer::Serialize(scene);
        Scene loaded("L"); std::string err;
        CHECK(SceneSerializer::Deserialize(loaded, text, &err));
        auto* r = loaded.Find("Sun")->GetComponent<Light>();
        CHECK(r != nullptr);
        CHECK_NEAR(r->ambient, 0.5f, 0.001f);
        SceneLight::Reset();
    }

    // --- MeshRenderer.doubleSided round-trips through serialization ---
    {
        Scene scene("DS");
        GameObject* go = scene.CreateGameObject("Flag");
        auto* mr = go->AddComponent<MeshRenderer>();
        mr->mesh = Mesh::Quad();
        mr->doubleSided = true;
        mr->wireframe = false;
        std::string text = SceneSerializer::Serialize(scene);
        Scene loaded("L");
        std::string err;
        CHECK(SceneSerializer::Deserialize(loaded, text, &err));
        auto* r = loaded.Find("Flag")->GetComponent<MeshRenderer>();
        CHECK(r != nullptr);
        CHECK(r->doubleSided == true);
        CHECK(r->wireframe == false);
    }

    // --- move_toward3 closes 3D distance without overshooting ---
    {
        Scene scene("Chase");
        GameObject* target = scene.CreateGameObject("Prey");
        target->transform->localPosition = {0, 0, 10};
        GameObject* go = scene.CreateGameObject("Hunter");
        auto* sc = go->AddComponent<ScriptComponent>("okayscript");
        // speed 100 with dt 0.016 -> 1.6 per frame.
        CHECK(sc->LoadSource("function update(dt) { move_toward3(\"Prey\", 100); }"));
        scene.Start();
        scene.Update(0.016f);
        CHECK_NEAR(go->transform->localPosition.z, 1.6f, 0.01f);   // stepped toward +Z
        for (int i = 0; i < 600; ++i) scene.Update(0.016f);        // plenty of time
        CHECK_NEAR(go->transform->localPosition.z, 10.0f, 0.01f);  // arrived, no overshoot
    }

    // --- look_at3 turns Forward() toward a named target (+X) ---
    {
        Scene scene("Aim3");
        GameObject* target = scene.CreateGameObject("T");
        target->transform->localPosition = {10, 0, 0};            // straight along +X
        GameObject* go = scene.CreateGameObject("Turret");
        auto* sc = go->AddComponent<ScriptComponent>("okayscript");
        CHECK(sc->LoadSource("function start() { look_at3(\"T\"); }"));
        scene.Start();
        Vec3 fwd = go->transform->Forward();
        CHECK_NEAR(fwd.x, 1.0f, 0.01f);
        CHECK_NEAR(fwd.y, 0.0f, 0.01f);
    }

    // --- 3D scale / dist3_to / set_mesh script builtins ---
    {
        Scene scene("3DX");
        GameObject* target = scene.CreateGameObject("Target");
        target->transform->localPosition = {0, 0, 4};       // 4 units away on Z

        GameObject* go = scene.CreateGameObject("Obj");
        go->AddComponent<MeshRenderer>()->mesh = Mesh::Cube();   // starts as Cube
        auto* sc = go->AddComponent<ScriptComponent>("okayscript");
        CHECK(sc->LoadSource(
            "function start() {"
            "  set_scale3(2, 3, 4);"
            "  set_mesh(\"Sphere\");"
            "  set_z(dist3_to(\"Target\"));" // measured at origin: (0,0,0)->(0,0,4) = 4
            "  set_x(scale_y());"            // -> 3 (after, so it doesn't skew the dist)
            "}"));
        scene.Start();
        CHECK_NEAR(go->transform->localScale.x, 2.0f, 0.001f);
        CHECK_NEAR(go->transform->localScale.z, 4.0f, 0.001f);
        CHECK_NEAR(go->transform->localPosition.x, 3.0f, 0.001f);   // scale_y readback
        CHECK_NEAR(go->transform->localPosition.z, 4.0f, 0.001f);   // dist3_to
        CHECK(go->GetComponent<MeshRenderer>()->mesh.name == "Sphere"); // set_mesh swapped it
    }

    // --- set_rot3 sets an absolute orientation (face +X via 90deg about Y) ---
    {
        Scene scene("Rot");
        GameObject* go = scene.CreateGameObject("Turner");
        auto* sc = go->AddComponent<ScriptComponent>("okayscript");
        CHECK(sc->LoadSource("function start() { set_rot3(0, 90, 0); }"));
        scene.Start();
        // Forward (local +Z) rotated 90deg about Y points down +X.
        Vec3 fwd = go->transform->Forward();
        CHECK_NEAR(fwd.x, 1.0f, 0.01f);
        CHECK_NEAR(fwd.z, 0.0f, 0.01f);
    }

    // --- Humanoid + the low-poly -> high-poly pipeline ---
    {
        Mesh human = Mesh::Humanoid();
        CHECK(human.name == "Human");
        CHECK(!human.vertices.empty());
        CHECK(human.TriangleCount() > 0);
        CHECK(Mesh::FromName("Human").name == "Human");
        // Stands on the ground-ish and is roughly 2 units tall.
        Vec3 s = human.Size();
        CHECK(s.y > 1.5f);

        // Subdivide quadruples triangles; the smoothing pass keeps the silhouette
        // close (a little shrinkage is expected) and doesn't blow up the bounds.
        Mesh hi = Mesh::Humanoid();
        int t0 = hi.TriangleCount();
        hi.SubdivideSmooth(1, 0.5f);
        CHECK(hi.TriangleCount() == t0 * 4);
        Vec3 s2 = hi.Size();
        CHECK(s2.y > s.y * 0.7f);          // still recognizably the same height
        CHECK(s2.y <= s.y + 0.001f);       // smoothing never inflates past the original

        // Smooth() alone moves vertices toward neighbours but preserves count.
        Mesh cube = Mesh::Cube();
        std::size_t vn = cube.vertices.size();
        cube.Smooth(0.5f);
        CHECK(cube.vertices.size() == vn);
    }

    // --- CharacterBody: parametric humanoid that rebuilds + serializes ---
    {
        Scene scene("Chars");
        GameObject* go = scene.CreateGameObject("Hero");
        auto* cb = go->AddComponent<CharacterBody>();
        cb->params.height   = 1.3f;
        cb->params.build    = 1.5f;
        cb->params.handSize = 1.7f;
        cb->params.armSpread= 45.0f;
        cb->params.torsoLength = 1.4f;
        cb->outfit = Color::FromBytes(20, 200, 40);
        cb->subdivisions    = 0;
        int baseTris = cb->Build().TriangleCount();   // base (with hair cap, no subdiv)
        cb->subdivisions    = 1;
        cb->Apply();                                  // builds into a MeshRenderer
        auto* mr = go->GetComponent<MeshRenderer>();
        CHECK(mr != nullptr);
        CHECK(mr->mesh.TriangleCount() > 0);
        // Per-part colors: face colors stay parallel through subdivision.
        CHECK(mr->mesh.HasFaceColors());
        CHECK((int)mr->mesh.triColors.size() == mr->mesh.TriangleCount());
        CHECK(mr->mesh.TriangleCount() == baseTris * 4);   // one subdivision pass

        // Taller params make a taller mesh than the default figure.
        CHECK(mr->mesh.Size().y > Mesh::Humanoid().Size().y);

        // Round-trips through serialization (params restored, mesh rebuilt).
        std::string text = SceneSerializer::Serialize(scene);
        Scene loaded("L"); std::string err;
        CHECK(SceneSerializer::Deserialize(loaded, text, &err));
        auto* lc = loaded.Find("Hero")->GetComponent<CharacterBody>();
        CHECK(lc != nullptr);
        CHECK_NEAR(lc->params.height, 1.3f, 0.001f);
        CHECK_NEAR(lc->params.build, 1.5f, 0.001f);
        CHECK_NEAR(lc->params.handSize, 1.7f, 0.001f);
        CHECK_NEAR(lc->params.armSpread, 45.0f, 0.001f);
        CHECK_NEAR(lc->params.torsoLength, 1.4f, 0.001f);
        CHECK_NEAR(lc->outfit.g, Color::FromBytes(20, 200, 40).g, 0.01f);  // outfit color round-trips
        CHECK(lc->subdivisions == 1);
        CHECK(loaded.Find("Hero")->GetComponent<MeshRenderer>()->mesh.TriangleCount() == baseTris * 4);
    }

    TEST_MAIN_RESULT();
}
