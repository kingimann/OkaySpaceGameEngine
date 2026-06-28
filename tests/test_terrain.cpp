#include "test_framework.hpp"
#include <Okay.hpp>
#include <cstdio>

using namespace okay;

int main() {
    RUN_SUITE("terrain");

    // --- Heightmap sizing + mesh generation ---------------------------
    {
        Scene s("T"); s.physicsEnabled = false;
        GameObject* go = s.CreateGameObject("Ground");
        auto* t = go->AddComponent<Terrain>();
        t->Resize(8);                         // 9x9 vertices
        CHECK((int)t->heights.size() == 9 * 9);

        t->SetHeight(4, 4, 3.0f);
        CHECK_NEAR(t->GetHeight(4, 4), 3.0f, 1e-4f);

        Mesh m = t->BuildMesh();
        CHECK((int)m.vertices.size() == 9 * 9);
        CHECK(m.uvs.size() == m.vertices.size());
        CHECK(m.TriangleCount() == 8 * 8 * 2);   // two tris per cell

        // The peak vertex carries the height we set.
        CHECK_NEAR(m.vertices[4 * 9 + 4].y, 3.0f, 1e-4f);

        // Apply() builds the mesh into a (new) MeshRenderer.
        t->Apply();
        auto* mr = go->GetComponent<MeshRenderer>();
        CHECK(mr != nullptr);
        CHECK((int)mr->mesh.vertices.size() == 9 * 9);
    }

    // --- Sculpt brush raises within its radius and falls off ----------
    {
        Terrain t;
        t.Resize(16); t.size = 16.0f;         // 1 unit per cell, centered
        t.Flatten(0.0f);
        t.RaiseAt(0.0f, 0.0f, 4.0f, 5.0f);    // raise around the center
        // Center vertex (8,8) is at local (0,0): full strength.
        CHECK(t.GetHeight(8, 8) > 4.0f);
        // A vertex outside the radius is untouched.
        CHECK_NEAR(t.GetHeight(0, 0), 0.0f, 1e-4f);
        // A negative delta lowers.
        float before = t.GetHeight(8, 8);
        t.RaiseAt(0.0f, 0.0f, 4.0f, -2.0f);
        CHECK(t.GetHeight(8, 8) < before);
    }

    // --- Generators change the surface ---------------------------------
    {
        Terrain t; t.Resize(16);
        t.Hills(5, 6.0f, 7u);
        float maxH = 0.0f;
        for (float h : t.heights) if (h > maxH) maxH = h;
        CHECK(maxH > 0.0f);                    // hills raised something
        t.Flatten(1.5f);
        CHECK_NEAR(t.GetHeight(3, 3), 1.5f, 1e-4f);
    }

    // --- Procedural generation + smooth normals + auto-color layers ----
    {
        Terrain t; t.Resize(32);
        t.Generate(0, 15.0f, 4.0f, 5, 99u);   // Mountains
        float maxH = 0.0f, minH = 1e9f;
        for (float h : t.heights) { if (h > maxH) maxH = h; if (h < minH) minH = h; }
        CHECK(maxH > 1.0f);                    // produced relief
        CHECK(maxH > minH);

        // Same seed is deterministic; a different type yields a different surface.
        Terrain a; a.Resize(32); a.Generate(0, 15.0f, 4.0f, 5, 99u);
        CHECK_NEAR(a.GetHeight(10, 10), t.GetHeight(10, 10), 1e-4f);
        Terrain b; b.Resize(32); b.Generate(2, 15.0f, 4.0f, 5, 99u);  // Plains
        bool differ = false;
        for (int i = 0; i < (int)b.heights.size(); ++i)
            if (std::fabs(b.heights[i] - t.heights[i]) > 1e-3f) { differ = true; break; }
        CHECK(differ);

        // BuildMesh emits per-face colors + per-vertex smooth normals when autoColor.
        Mesh m = t.BuildMesh();
        CHECK(m.HasFaceColors());
        CHECK(m.HasNormals());
        CHECK((int)m.normals.size() == (int)m.vertices.size());

        // Disabling autoColor drops the face colors (single albedo).
        t.autoColor = false;
        Mesh m2 = t.BuildMesh();
        CHECK(!m2.HasFaceColors());
    }

    // --- Smooth + Flatten brushes -------------------------------------
    {
        Terrain t; t.Resize(16); t.size = 16.0f; t.Flatten(0.0f);
        t.SetHeight(8, 8, 10.0f);              // a single spike
        float spike = t.GetHeight(8, 8);
        t.SmoothAt(0.0f, 0.0f, 4.0f, 1.0f);    // relax around the center
        CHECK(t.GetHeight(8, 8) < spike);      // spike pulled down toward neighbors

        Terrain f; f.Resize(16); f.size = 16.0f; f.Randomize(5.0f, 3u);
        f.FlattenAt(0.0f, 0.0f, 5.0f, 2.0f, 1.0f);  // full-strength flatten to 2.0
        CHECK_NEAR(f.GetHeight(8, 8), 2.0f, 1e-3f); // center reaches the target
    }

    // --- Terrain serializes (heights round-trip; mesh rebuilt on load) -
    {
        Scene s("T2"); s.physicsEnabled = false;
        GameObject* go = s.CreateGameObject("Ground");
        auto* t = go->AddComponent<Terrain>();
        t->Resize(6); t->size = 30.0f; t->color = Color::FromBytes(120, 90, 60);
        t->SetHeight(2, 3, 4.5f);
        t->snowLevel = 22.0f; t->rockSlope = 0.72f; t->autoColor = true;
        t->snowColor = Color::FromBytes(250, 250, 255);

        std::string txt = SceneSerializer::Serialize(s);
        Scene s2("x"); SceneSerializer::Deserialize(s2, txt);
        GameObject* g2 = s2.Find("Ground");
        CHECK(g2 != nullptr);
        auto* t2 = g2->GetComponent<Terrain>();
        CHECK(t2 != nullptr);
        if (t2) {
            CHECK(t2->resolution == 6);
            CHECK_NEAR(t2->size, 30.0f, 1e-3f);
            CHECK_NEAR(t2->GetHeight(2, 3), 4.5f, 1e-3f);
            CHECK(t2->autoColor);
            CHECK_NEAR(t2->snowLevel, 22.0f, 1e-3f);
            CHECK_NEAR(t2->rockSlope, 0.72f, 1e-3f);
            CHECK_NEAR(t2->snowColor.r, t->snowColor.r, 0.02f);
            // Apply ran on load: the MeshRenderer exists with the right vert count.
            auto* mr2 = g2->GetComponent<MeshRenderer>();
            CHECK(mr2 != nullptr);
            if (mr2) CHECK((int)mr2->mesh.vertices.size() == 7 * 7);
        }
    }

    // --- Material preset: from renderer, round-trip text + file, apply --
    {
        MeshRenderer mr;
        mr.color = Color::FromBytes(200, 100, 50);
        mr.specular = 0.7f; mr.shininess = 40.0f; mr.unlit = true;
        mr.texture = "wall.png"; mr.tiling = {3, 2};

        Material m = Material::FromRenderer(mr);
        Material m2 = Material::FromText(m.ToText());
        CHECK_NEAR(m2.color.r, mr.color.r, 0.01f);
        CHECK_NEAR(m2.specular, 0.7f, 1e-3f);
        CHECK(m2.unlit);
        CHECK(m2.texture == "wall.png");
        CHECK_NEAR(m2.tiling.x, 3.0f, 1e-3f);

        CHECK(m.SaveToFile("test.okaymat"));
        Material m3;
        CHECK(Material::LoadFromFile("test.okaymat", m3));
        CHECK(m3.texture == "wall.png");

        MeshRenderer dst;
        m3.ApplyTo(dst);
        CHECK_NEAR(dst.shininess, 40.0f, 1e-3f);
        CHECK(dst.unlit);
        std::remove("test.okaymat");
    }

    // Terrain texturing fields round-trip through the scene serializer.
    {
        Scene s("TT"); s.physicsEnabled = false;
        auto* tr = s.CreateGameObject("Ground")->AddComponent<Terrain>();
        tr->Resize(8); tr->Flatten(0.0f);
        tr->texture = "grass.png"; tr->textureTiling = 16.0f; tr->triplanarTex = true;
        tr->normalMap = "grass_n.png"; tr->normalStrength = 0.7f;
        Scene s2("x"); SceneSerializer::Deserialize(s2, SceneSerializer::Serialize(s));
        auto* r = s2.Find("Ground") ? s2.Find("Ground")->GetComponent<Terrain>() : nullptr;
        CHECK(r != nullptr);
        if (r) {
            CHECK(r->texture == "grass.png");
            CHECK_NEAR(r->textureTiling, 16.0f, 1e-3f);
            CHECK(r->triplanarTex);
            CHECK(r->normalMap == "grass_n.png");
            CHECK_NEAR(r->normalStrength, 0.7f, 1e-3f);
            r->Apply();   // pushes the texture onto the sibling MeshRenderer (triplanar on)
            auto* mr = r->gameObject->GetComponent<MeshRenderer>();
            CHECK(mr && mr->texture == "grass.png" && mr->triplanar);
        }
    }

    // Walkable terrain: a dynamic body falls and comes to rest on the terrain
    // surface, and digging a crater under it lets it settle lower.
    {
        Scene s("WALK");
        auto* tr = s.CreateGameObject("Ground")->AddComponent<Terrain>();
        tr->Resize(16); tr->size = 40.0f; tr->Flatten(2.0f);   // flat ground at y=2
        GameObject* body = s.CreateGameObject("Body");
        body->transform->localPosition = {0, 12, 0};
        auto* rb = body->AddComponent<Rigidbody3D>();
        rb->bodyType = Rigidbody3D::BodyType::Dynamic;
        auto* bc = body->AddComponent<BoxCollider3D>();
        bc->size = {1.0f, 1.0f, 1.0f};   // half-extent 0.5 -> foot offset ~0.5
        for (int i = 0; i < 240; ++i) s.Update(1.0f / 60.0f);   // ~4s: fall + settle
        float restY = body->transform->Position().y;
        CHECK(restY > 2.0f);                       // sits on top of the ground (surface + foot)
        CHECK_NEAR(restY, 2.5f, 0.3f);             // ~ surface(2) + foot(0.5)

        // Dig a crater under the body; it should settle lower.
        tr->RaiseAt(0.0f, 0.0f, 8.0f, -3.0f);      // lower the ground to ~ -1 at the centre
        for (int i = 0; i < 240; ++i) s.Update(1.0f / 60.0f);
        float dugY = body->transform->Position().y;
        CHECK(dugY < restY - 1.0f);                // fell into the dug hole
    }

    // TerrainDigger: Dig lowers the terrain under an aimed crater; fields round-trip.
    {
        Scene s("DIG"); s.physicsEnabled = false;
        auto* tr = s.CreateGameObject("Ground")->AddComponent<Terrain>();
        tr->Resize(16); tr->size = 32.0f; tr->Flatten(5.0f);   // flat plateau at y=5
        GameObject* camgo = s.CreateGameObject("Cam");
        camgo->transform->localPosition = {0, 20, 0};
        // Aim is -Forward() (see BlockBuilder), so a downward look needs Forward()=+Y.
        camgo->transform->localRotation = Quat::Euler(-90, 0, 0);   // look straight down
        camgo->AddComponent<Camera>();
        auto* dig = tr->gameObject->AddComponent<TerrainDigger>();
        dig->mode = TerrainDigger::Mode::Dig; dig->radius = 6.0f; dig->strength = 10.0f;
        float before = tr->SampleHeight(0.0f, 0.0f);
        float lx = 1.0f, lz = 1.0f;
        CHECK(dig->AimAtTerrain(tr, lx, lz));           // the ray reaches the ground
        tr->RaiseAt(0.0f, 0.0f, dig->radius, -1.0f);    // dig one unit at the centre
        CHECK(tr->SampleHeight(0.0f, 0.0f) < before);   // the crater lowered the surface

        dig->mode = TerrainDigger::Mode::Flatten; dig->radius = 4.5f;
        dig->range = 99.0f; dig->relax = 2.5f; dig->button = 1; dig->key = 'f';
        Scene s2("x"); SceneSerializer::Deserialize(s2, SceneSerializer::Serialize(s));
        auto* r = s2.Find("Ground") ? s2.Find("Ground")->GetComponent<TerrainDigger>() : nullptr;
        CHECK(r != nullptr);
        if (r) {
            CHECK(r->mode == TerrainDigger::Mode::Flatten);
            CHECK_NEAR(r->radius, 4.5f, 1e-3f);
            CHECK_NEAR(r->range, 99.0f, 1e-3f);
            CHECK(r->button == 1);
            CHECK(r->key == 'f');
        }
    }

    // --- Hydraulic erosion carves the surface (lowers peaks, conserves dims) ---
    {
        Terrain t; t.Resize(48); t.size = 48.0f;
        t.Generate(0, 20.0f, 4.0f, 5, 42u);            // blobby mountains
        float maxBefore = 0.0f, sumBefore = 0.0f;
        for (float h : t.heights) { if (h > maxBefore) maxBefore = h; sumBefore += h; }
        t.Erode(2000, 0.4f, 7u);
        float maxAfter = 0.0f; bool finite = true;
        for (float h : t.heights) { if (h > maxAfter) maxAfter = h; if (!(h == h)) finite = false; }
        CHECK(finite);                                 // no NaNs from the sim
        CHECK((int)t.heights.size() == 49 * 49);       // resolution preserved
        CHECK(maxAfter < maxBefore);                   // peaks worn down
        // Erosion redistributes (carve + deposit), it doesn't add/remove bulk mass.
        float sumAfter = 0.0f; for (float h : t.heights) sumAfter += h;
        CHECK(std::fabs(sumAfter - sumBefore) < std::fabs(sumBefore) * 0.5f + 50.0f);
    }

    // --- Thermal erosion slumps a steep spike toward its neighbours -----------
    {
        Terrain t; t.Resize(16); t.size = 16.0f; t.Flatten(0.0f);
        t.SetHeight(8, 8, 20.0f);                      // a sheer spike
        float spike = t.GetHeight(8, 8);
        t.ThermalErode(20, 1.0f, 0.5f);
        CHECK(t.GetHeight(8, 8) < spike);              // top slumped down
        CHECK(t.GetHeight(7, 8) > 0.0f);               // material piled on a neighbour
    }

    // --- Ridged + Canyon generators produce deterministic relief --------------
    {
        Terrain r; r.Resize(32); r.Generate(5, 18.0f, 4.0f, 5, 5u);   // Ridged Mountains
        float mx = 0, mn = 1e9f; for (float h : r.heights) { mx = std::fmax(mx, h); mn = std::fmin(mn, h); }
        CHECK(mx > mn + 1.0f);                          // real relief
        Terrain r2; r2.Resize(32); r2.Generate(5, 18.0f, 4.0f, 5, 5u);
        CHECK_NEAR(r2.GetHeight(11, 7), r.GetHeight(11, 7), 1e-4f);   // deterministic

        Terrain c; c.Resize(32); c.Generate(6, 18.0f, 4.0f, 5, 5u);   // Canyons
        float cmx = 0, cmn = 1e9f; for (float h : c.heights) { cmx = std::fmax(cmx, h); cmn = std::fmin(cmn, h); }
        CHECK(cmx > cmn + 1.0f);
    }

    // --- Heightmap PNG export -> import round-trips the shape ------------------
    {
        Terrain t; t.Resize(24); t.size = 24.0f;
        t.Generate(1, 12.0f, 3.0f, 4, 9u);
        float lo, hi; t.HeightRange(lo, hi);
        const char* png = "/tmp/okay_terrain_heightmap.png";
        CHECK(t.ExportHeightmap(png));

        Terrain t2; t2.Resize(24); t2.size = 24.0f;     // same resolution
        CHECK(t2.ImportHeightmap(png, lo, hi));         // reconstruct with the same range
        // 8-bit PNG quantizes, so allow a small tolerance per sample.
        float tol = (hi - lo) / 255.0f * 2.0f + 1e-3f;
        int dim = t.Dim(), checks = 0;
        for (int z = 0; z < dim; z += 5)
            for (int x = 0; x < dim; x += 5) {
                CHECK_NEAR(t2.GetHeight(x, z), t.GetHeight(x, z), tol);
                ++checks;
            }
        CHECK(checks > 0);
        std::remove(png);
    }

    // --- Brush hardness: a harder brush keeps a wider full-strength core -------
    {
        Terrain soft; soft.Resize(32); soft.size = 32.0f; soft.Flatten(0.0f);
        Terrain hard; hard.Resize(32); hard.size = 32.0f; hard.Flatten(0.0f);
        soft.RaiseAt(0, 0, 8.0f, 5.0f, 0.0f);          // soft: smoothstep from centre
        hard.RaiseAt(0, 0, 8.0f, 5.0f, 0.9f);          // hard: broad flat core
        // Sample partway out (~half radius). The hard brush is nearer full strength.
        float sH = soft.SampleHeight(4.0f, 0.0f);
        float hH = hard.SampleHeight(4.0f, 0.0f);
        CHECK(hH > sH);
        CHECK_NEAR(hard.SampleHeight(0, 0), 5.0f, 1e-3f);   // centre still full on both
        CHECK_NEAR(soft.SampleHeight(0, 0), 5.0f, 1e-3f);
    }

    // --- NormalAt: flat ground points up; a slope tilts the normal ------------
    {
        Terrain flat; flat.Resize(16); flat.size = 16.0f; flat.Flatten(3.0f);
        Vec3 nf = flat.NormalAt(0, 0);
        CHECK_NEAR(nf.y, 1.0f, 1e-3f);                 // straight up on flat ground

        Terrain ramp; ramp.Resize(16); ramp.size = 16.0f;
        for (int z = 0; z < ramp.Dim(); ++z)
            for (int x = 0; x < ramp.Dim(); ++x) ramp.SetHeight(x, z, (float)x);   // slope along +X
        Vec3 ns = ramp.NormalAt(0, 0);
        CHECK(ns.y < 0.99f);                           // tilted off vertical
        CHECK(std::fabs(ns.x) > 0.05f);                // leans along the slope
    }

    // --- Jumping on terrain: a resting body is flagged grounded-on-terrain so
    //     controllers can jump repeatedly (heightmap has no collider) -----------
    {
        Scene s("JUMP");
        auto* tr = s.CreateGameObject("Ground")->AddComponent<Terrain>();
        tr->Resize(16); tr->size = 40.0f; tr->Flatten(0.0f);
        GameObject* body = s.CreateGameObject("Player");
        body->transform->localPosition = {0, 6, 0};
        auto* rb = body->AddComponent<Rigidbody3D>();
        rb->bodyType = Rigidbody3D::BodyType::Dynamic;
        auto* bc = body->AddComponent<BoxCollider3D>(); bc->size = {1, 1, 1};
        for (int i = 0; i < 240; ++i) s.Update(1.0f / 60.0f);   // fall + settle
        CHECK(rb->groundedOnTerrain);                  // resting on terrain reads as grounded

        // A jump lifts it; mid-air it is NOT flagged grounded (so no infinite jumps).
        rb->velocity.y = 6.0f;
        s.Update(1.0f / 60.0f); s.Update(1.0f / 60.0f);
        CHECK(!rb->groundedOnTerrain);
        for (int i = 0; i < 240; ++i) s.Update(1.0f / 60.0f);   // land again
        CHECK(rb->groundedOnTerrain);                  // grounded restored after landing
    }

    // --- TerrainDigger new fields round-trip (hardness, marker, color) --------
    {
        Scene s("DIG2"); s.physicsEnabled = false;
        auto* tr = s.CreateGameObject("Ground")->AddComponent<Terrain>();
        tr->Resize(8); tr->Flatten(0.0f);
        auto* dig = tr->gameObject->AddComponent<TerrainDigger>();
        dig->hardness = 0.8f; dig->showBrush = false;
        dig->brushColor = Color::FromBytes(255, 120, 40, 200);
        Scene s2("x"); SceneSerializer::Deserialize(s2, SceneSerializer::Serialize(s));
        auto* r = s2.Find("Ground") ? s2.Find("Ground")->GetComponent<TerrainDigger>() : nullptr;
        CHECK(r != nullptr);
        if (r) {
            CHECK_NEAR(r->hardness, 0.8f, 1e-3f);
            CHECK(r->showBrush == false);
            CHECK_NEAR(r->brushColor.r, 1.0f, 0.02f);
            CHECK_NEAR(r->brushColor.a, 200.0f / 255.0f, 0.02f);
        }
    }

    // --- Water: builds an animated mesh and round-trips through the serializer -
    {
        Scene s("WATER");
        GameObject* go = s.CreateGameObject("Lake");
        auto* w = go->AddComponent<Water>();
        w->size = 60.0f; w->resolution = 24; w->waveHeight = 0.5f;
        w->color = Color::FromBytes(30, 100, 150, 180); w->opacity = 0.6f;
        w->Apply();
        auto* mr = go->GetComponent<MeshRenderer>();
        CHECK(mr != nullptr);
        if (mr) {
            CHECK((int)mr->mesh.vertices.size() == 25 * 25);   // (res+1)^2 grid
            CHECK(mr->doubleSided);
            CHECK(mr->reflectivity > 0.0f);
            CHECK_NEAR(mr->color.a, 0.6f, 1e-3f);              // opacity drives alpha
        }

        // The surface actually animates: stepping time moves the vertices.
        float y0 = mr ? mr->mesh.vertices[312].y : 0.0f;
        for (int i = 0; i < 30; ++i) s.Update(1.0f / 60.0f);   // Start() ran on first update
        float y1 = mr ? mr->mesh.vertices[312].y : 0.0f;
        CHECK(std::fabs(y1 - y0) > 1e-4f);                      // waves rolled

        Scene s2("x"); SceneSerializer::Deserialize(s2, SceneSerializer::Serialize(s));
        auto* w2 = s2.Find("Lake") ? s2.Find("Lake")->GetComponent<Water>() : nullptr;
        CHECK(w2 != nullptr);
        if (w2) {
            CHECK_NEAR(w2->size, 60.0f, 1e-3f);
            CHECK(w2->resolution == 24);
            CHECK_NEAR(w2->waveHeight, 0.5f, 1e-3f);
            CHECK_NEAR(w2->opacity, 0.6f, 1e-3f);
        }
    }

    // --- The Terrain Sandbox template builds a playable dig scene -------------
    {
        Scene s("sandbox");
        Templates::TerrainSandbox(s);
        GameObject* g = s.Find("Terrain");
        CHECK(g != nullptr);
        CHECK(g && g->GetComponent<Terrain>() != nullptr);
        CHECK(g && g->GetComponent<TerrainDigger>() != nullptr);
        CHECK(s.Find("Player") != nullptr);
        // It has a main camera and a directional light.
        CHECK(s.FindObjectOfType<Camera>() != nullptr);
        CHECK(s.FindObjectOfType<Light>() != nullptr);
        // The generated terrain actually has relief (not flat).
        if (auto* t = g->GetComponent<Terrain>()) {
            float lo, hi; t->HeightRange(lo, hi);
            CHECK(hi > lo + 0.5f);
        }
    }

    TEST_MAIN_RESULT();
}
