#include "test_framework.hpp"
#include <Okay.hpp>
#include "okay/Scene/SceneSerializer.hpp"
#include "okay/Scene/EnsureCamera.hpp"
#include "okay/Scene/Tags.hpp"
#include <string>

using namespace okay;

namespace {
Camera* MakeCam(Scene& s, const char* name, float depth, bool main = true) {
    GameObject* g = s.CreateGameObject(name);
    Camera* c = g->AddComponent<Camera>();
    c->depth = depth; c->main = main;
    return c;
}
}

int main() {
    RUN_SUITE("layers_tags_cameras");

    // ---- layer collision matrix ------------------------------------------
    {
        Scene s("M");
        CHECK(s.LayersCollide(3, 4));
        CHECK(!s.HasCustomLayerCollisions());
        s.SetLayerCollision(3, 4, false);
        CHECK(!s.LayersCollide(3, 4) && !s.LayersCollide(4, 3));
        CHECK(!s.physics().LayersCollide(3, 4) && !s.physics3D().LayersCollide(3, 4));   // both worlds
        CHECK(s.HasCustomLayerCollisions());
        CHECK((s.LayerCollisionMask(3) & (1u << 4)) == 0);
        CHECK((s.LayerCollisionMask(3) & (1u << 3)) != 0);
        // Out-of-range layers wrap like the query masks (layer -1 is layer 31).
        s.SetLayerCollision(31, 1, false);
        CHECK(!s.LayersCollide(-1, 1));
        CHECK(s.LayersCollide(-1, 2));

        // Round trip through the scene file; an old scene (no record) resets it.
        std::string text = SceneSerializer::Serialize(s);
        CHECK(text.find("layermatrix") != std::string::npos);
        Scene t("T");
        t.SetLayerCollision(7, 8, false);   // stale state must not survive a load
        CHECK(SceneSerializer::Deserialize(t, text));
        CHECK(!t.LayersCollide(3, 4) && !t.LayersCollide(31, 1));
        CHECK(t.LayersCollide(7, 8));
        CHECK(!t.physics3D().LayersCollide(3, 4));

        Scene plain("P");
        std::string old = SceneSerializer::Serialize(plain);
        CHECK(old.find("layermatrix") == std::string::npos);   // optional record
        CHECK(SceneSerializer::Deserialize(t, old));
        CHECK(t.LayersCollide(3, 4) && !t.HasCustomLayerCollisions());

        // A truncated record fails the load instead of half-applying.
        std::string bad = "okayscene 3\nname \"x\"\nlayermatrix 1 2 3\n";
        CHECK(!SceneSerializer::Deserialize(t, bad));

        s.ResetLayerCollisions();
        CHECK(s.LayersCollide(3, 4) && !s.HasCustomLayerCollisions());
    }
    // The matrix gates real contacts in 2D and 3D, and its mask works as a query mask.
    {
        Scene s("C");
        GameObject* a = s.CreateGameObject("A");
        a->AddComponent<BoxCollider2D>()->layer = 3;
        a->AddComponent<Rigidbody2D>()->bodyType = Rigidbody2D::BodyType::Static;
        GameObject* b = s.CreateGameObject("B");
        b->AddComponent<BoxCollider2D>()->layer = 4;
        b->AddComponent<Rigidbody2D>();
        s.SetLayerCollision(3, 4, false);
        s.Start();
        for (int i = 0; i < 5; ++i) s.Update(0.02f);
        CHECK(!s.physics().LayersCollide(a->GetComponent<BoxCollider2D>()->layer, b->GetComponent<BoxCollider2D>()->layer));
        auto hit = s.physics().Raycast(s, {-5, 0}, {1, 0}, 10.0f, nullptr, s.LayerCollisionMask(4), true);
        CHECK(!hit.hit || hit.gameObject != a);   // a (layer 3) is filtered by layer 4's mask
        auto hit2 = s.physics().Raycast(s, {-5, 0}, {1, 0}, 10.0f, nullptr, s.LayerCollisionMask(2), true);
        CHECK(hit2.hit);
    }

    // ---- tags ---------------------------------------------------------------
    {
        Scene s("G");
        GameObject* a = s.CreateGameObject("A"); a->tag = "Enemy";
        GameObject* b = s.CreateGameObject("B");   // untagged
        GameObject* c = s.CreateGameObject("C"); c->tag = "Enemy";
        GameObject* d = s.CreateGameObject("D"); d->tag = "Player";
        CHECK(s.FindWithTag("Enemy") == a);
        CHECK(s.FindWithTag("Player") == d);
        CHECK(s.FindWithTag("Nope") == nullptr);
        CHECK(s.FindWithTag("") == nullptr);          // used to return the first untagged object
        CHECK(s.FindGameObjectsWithTag("").empty());
        auto en = s.FindGameObjectsWithTag("Enemy");
        CHECK(en.size() == 2 && en[0] == a && en[1] == c);
        (void)b;

        // The index follows tag edits, new objects and destroys.
        c->tag = "Player";
        CHECK(s.FindGameObjectsWithTag("Enemy").size() == 1);
        CHECK(s.FindGameObjectsWithTag("Player").size() == 2);
        a->tag = "";
        CHECK(s.FindWithTag("Enemy") == nullptr);
        GameObject* e = s.CreateGameObject("E"); e->tag = "Enemy";
        CHECK(s.FindWithTag("Enemy") == e);
        s.Destroy(e);
        s.Update(0.016f);
        CHECK(s.FindWithTag("Enemy") == nullptr);
        s.Clear();
        CHECK(s.FindWithTag("Player") == nullptr);
    }
    {
        // Validation and the project tag list.
        CHECK(Tags::IsValidName("Player") && Tags::IsValidName("Boss Fight"));
        CHECK(!Tags::IsValidName("") && !Tags::IsValidName(" x") && !Tags::IsValidName("x ") &&
              !Tags::IsValidName("a\nb") && !Tags::IsValidName(std::string(65, 'a')));
        CHECK(Tags::Contains("Player") && !Tags::Contains("Boss"));
        CHECK(Tags::Add("Boss") && Tags::Contains("Boss") && Tags::Add("Boss"));
        CHECK(!Tags::Add(" bad"));
        int n = 0; for (auto& t : Tags::All()) if (t == "Boss") ++n;
        CHECK(n == 1);
        Scene s("V");
        GameObject* g = s.CreateGameObject("G");
        CHECK(g->SetTag("Anything") && g->tag == "Anything");
        CHECK(!g->SetTag("bad ") && g->tag == "Anything");
        Tags::Strict() = true;
        CHECK(!g->SetTag("Unlisted") && g->tag == "Anything");
        CHECK(g->SetTag("Boss") && g->SetTag("") && g->tag.empty());
        Tags::Strict() = false;
        CHECK(Tags::Remove("Boss") && !Tags::Remove("Boss") && !Tags::Remove("Player"));
    }

    // ---- main camera by depth -----------------------------------------------
    {
        // A lower-depth main camera waking up after a higher one must not take over.
        Scene s("D");
        Camera* hi = MakeCam(s, "Hi", 5.0f);
        s.Start();
        CHECK(s.mainCamera == hi);
        Camera* lo = MakeCam(s, "Lo", 0.0f);
        lo->Awake();                               // what Start does for a camera created later
        CHECK(s.mainCamera == hi);
        s.Update(0.016f);
        CHECK(s.mainCamera == hi);
        Camera* top = MakeCam(s, "Top", 9.0f);
        top->Awake();
        CHECK(s.mainCamera == top);
        // Raising a depth at runtime switches the main camera.
        lo->depth = 20.0f;
        s.Update(0.016f);
        CHECK(s.mainCamera == lo);
        // Switching: disable the main camera, the next-best takes over; re-enable, it returns.
        lo->enabled = false;
        s.Update(0.016f);
        CHECK(s.mainCamera == top);
        top->gameObject->active = false;
        s.Update(0.016f);
        CHECK(s.mainCamera == hi);
        top->gameObject->active = true; lo->enabled = true;
        s.Update(0.016f);
        CHECK(s.mainCamera == lo);
        // A non-main camera never outranks a main one by depth.
        Camera* aux = MakeCam(s, "Aux", 100.0f, /*main*/ false);
        (void)aux;
        s.Update(0.016f);
        CHECK(s.mainCamera == lo);
    }

    // ---- camera stack -----------------------------------------------------------
    {
        Scene s("K");
        Camera* ui = MakeCam(s, "UI", 10.0f); ui->clearFlags = Camera::ClearFlags::DepthOnly;
        Camera* world = MakeCam(s, "World", 0.0f);
        Camera* world2 = MakeCam(s, "World2", 0.0f, false);
        Camera* off = MakeCam(s, "Off", 5.0f); off->enabled = false;
        Camera* hidden = MakeCam(s, "Hidden", 6.0f); hidden->gameObject->active = false;
        s.Start();
        auto st = s.CameraStack();
        CHECK(st.size() == 3);
        CHECK(st.size() == 3 && st[0] == world && st[1] == world2 && st[2] == ui);   // depth order, scene order on ties
        bool col = false, dep = false;
        world->ClearPlan(true, col, dep);   CHECK(col && dep);
        ui->clearFlags = Camera::ClearFlags::DepthOnly;
        ui->ClearPlan(false, col, dep);     CHECK(!col && dep);
        ui->clearFlags = Camera::ClearFlags::DontClear;
        ui->ClearPlan(false, col, dep);     CHECK(!col && !dep);
        ui->ClearPlan(true, col, dep);      CHECK(col && dep);   // the base camera always clears
        ui->clearFlags = Camera::ClearFlags::SolidColor;
        ui->ClearPlan(false, col, dep);     CHECK(col && dep);
        // enabling a camera at runtime joins the stack
        off->enabled = true;
        s.Update(0.016f);
        st = s.CameraStack();
        CHECK(st.size() == 4 && st[2] == off && st[3] == ui);

        // clear flags survive a save/load, including the stack-only ones
        ui->clearFlags = Camera::ClearFlags::DepthOnly;
        Scene t("T");
        CHECK(SceneSerializer::Deserialize(t, SceneSerializer::Serialize(s)));
        Camera* lui = nullptr;
        for (auto& up : t.Objects()) if (up->name == "UI") lui = up->GetComponent<Camera>();
        CHECK(lui && lui->clearFlags == Camera::ClearFlags::DepthOnly && lui->depth == 10.0f);
    }
    TEST_MAIN_RESULT();
}
