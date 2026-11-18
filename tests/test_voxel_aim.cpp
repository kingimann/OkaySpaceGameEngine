#include "test_framework.hpp"
#include <Okay.hpp>
#include <cmath>

using namespace okay;

// Digging and building in first AND third person: where the aim ray starts, the reach from the character, the body and wall
// rules (VoxelAim.hpp), the named input actions, the hotbar, and the dig progress feedback.
namespace {

// A flat slab of ground, surface near y = 15.
struct Ground {
    Scene s{"AIM"};
    VoxelTerrain* v = nullptr;
    VoxelDigger* d = nullptr;
    Transform* vt = nullptr;
    Ground() {
        s.physicsEnabled = false;
        GameObject* g = s.CreateGameObject("Terrain");
        v = g->AddComponent<VoxelTerrain>();
        v->Resize(40, 30, 40); v->voxelSize = 1.0f; v->FillSlab(0.5f); v->Apply();
        vt = g->transform;
    }
    float Surface() const { float y = 0; v->SurfaceY(0, 0, y); return y; }
};

VoxelAimInput FirstPerson(float feetY, float pitchDownDeg) {
    VoxelAimInput in;
    in.hasBody = true; in.feet = {0, feetY, 0};
    in.camPos = {0, feetY + 1.62f, 0};
    const float p = pitchDownDeg * 0.0174532925f;
    in.camDir = {0, -std::sin(p), -std::cos(p)};
    return in;
}

// Camera 4 m behind and 2.5 m above the character, looking forward and down through the crosshair.
VoxelAimInput ThirdPerson(float feetY, float pitchDownDeg) {
    VoxelAimInput in;
    in.hasBody = true; in.feet = {0, feetY, 0};
    in.camPos = {0, feetY + 2.5f, 4.0f};
    const float p = pitchDownDeg * 0.0174532925f;
    in.camDir = {0, -std::sin(p), -std::cos(p)};
    return in;
}

void Fill(VoxelTerrain& v, float x0, float x1, float y0, float y1, float z0, float z1) {
    for (int z = 0; z < v.nz; ++z) for (int y = 0; y < v.ny; ++y) for (int x = 0; x < v.nx; ++x) {
        const float wx = x * v.voxelSize - v.HalfX(), wy = y * v.voxelSize, wz = z * v.voxelSize - v.HalfZ();
        if (wx >= x0 && wx <= x1 && wy >= y0 && wy <= y1 && wz >= z0 && wz <= z1) v.density[v.Index(x, y, z)] = 1.0f;
    }
}

} // namespace

int main() {
    RUN_SUITE("voxel_aim");
    VoxelTerrain::ForceOldRemesh(0);

    // --- first person: the ray starts at the eyes and hits the ground in front, face pointing up -------------------------------
    {
        Ground g; const float sy = g.Surface();
        VoxelAimInput in = FirstPerson(sy, 35.0f);
        VoxelAimHit h = VoxelAimFind(*g.v, *g.vt, in);
        CHECK(h.hit && !h.thirdPerson);
        CHECK(h.world.z < -1.0f && std::fabs(h.world.y - sy) < 0.8f);
        CHECK(h.normal.y > 0.8f);
        CHECK(h.fromBody <= in.reach + 0.01f);
        // building: the last air point, above the surface
        in.place = true;
        VoxelAimHit p = VoxelAimFind(*g.v, *g.vt, in);
        CHECK(p.hit && p.world.y >= h.world.y - 0.01f);
        // too far: looking nearly level, the ground is beyond the reach
        VoxelAimHit far = VoxelAimFind(*g.v, *g.vt, FirstPerson(sy, 5.0f));
        CHECK(!far.hit);
        // looking at the sky
        VoxelAimInput up = FirstPerson(sy, -20.0f);
        CHECK(!VoxelAimFind(*g.v, *g.vt, up).hit);
    }

    // --- third person: through the crosshair, started past the camera, reach measured from the character ------------------------
    {
        Ground g; const float sy = g.Surface();
        VoxelAimInput in = ThirdPerson(sy, 30.0f);
        VoxelAimHit h = VoxelAimFind(*g.v, *g.vt, in);
        CHECK(h.hit && h.thirdPerson);
        CHECK(h.world.z < 0.0f);                              // in front of the character, not behind it
        CHECK(h.fromBody <= in.reach + 0.01f);
        CHECK(h.normal.y > 0.8f);
        // the ray's own hit point (camera view line), not a line from the feet or head: it lies on the camera ray
        const Vec3 camHit = in.camPos + in.camDir * h.fromCamera;
        CHECK(Vec3::Distance(camHit, h.world) < 1.0f);
        // the reach counts from the character: a spot 11 m from the camera's line start is fine, 30 m is not
        VoxelAimInput farIn = ThirdPerson(sy, 8.0f);
        farIn.reach = 6.0f;
        CHECK(!VoxelAimFind(*g.v, *g.vt, farIn).hit);
        VoxelAimInput nearIn = ThirdPerson(sy, 8.0f);
        nearIn.reach = 40.0f; nearIn.range = 80.0f;
        CHECK(VoxelAimFind(*g.v, *g.vt, nearIn).hit);
        // a lens held down at the ground with the character in the way: the ground beside the lens is not what is hit
        VoxelAimInput steep = ThirdPerson(sy, 70.0f);
        VoxelAimHit sh = VoxelAimFind(*g.v, *g.vt, steep);
        CHECK(sh.hit && Vec3::Distance(sh.world, steep.feet) < 6.0f);
    }

    // --- no camera ray start at the lens in third person: a wall between the lens and the character is ignored ------------------
    {
        Ground g; const float sy = g.Surface();
        Fill(*g.v, -3, 3, 0, sy + 3.0f, 2.0f, 3.0f);   // a wall behind the character, between it and the camera
        VoxelAimInput in = ThirdPerson(sy, 15.0f);
        in.camPos.z = 6.0f;
        VoxelAimHit h = VoxelAimFind(*g.v, *g.vt, in);
        CHECK(h.hit && h.world.z < 0.0f);                    // it digs the ground in front, not the wall at the lens
    }

    // --- aiming over a wall: the ground behind the wall cannot be dug -------------------------------------------------------------
    {
        Ground g; const float sy = g.Surface();
        Fill(*g.v, -6, 6, 0, sy + 3.0f, -3.5f, -2.5f);   // a wall 3 m high, 3 m in front
        VoxelAimInput in;
        in.hasBody = true; in.feet = {0, sy, 0};
        in.camPos = {0, sy + 12.0f, 10.0f};                // a high third-person camera looking over the wall at ground behind it
        in.camDir = Vec3{0, -12.0f, -19.0f}.Normalized();
        VoxelAimHit h = VoxelAimFind(*g.v, *g.vt, in);
        CHECK(!h.hit && h.miss == VoxelAimMiss::Blocked);
        // the wall's own face can be dug
        VoxelAimInput face = FirstPerson(sy, 0.0f);
        face.camPos.y = sy + 1.62f;
        VoxelAimHit w = VoxelAimFind(*g.v, *g.vt, face);
        CHECK(w.hit && w.world.z > -3.6f && w.world.z < -2.0f);
    }

    // --- not the block you are in -----------------------------------------------------------------------------------------------
    {
        Ground g; const float sy = g.Surface();
        VoxelAimInput in = FirstPerson(sy - 2.9f, 40.0f);  // eyes pushed into the ground
        CHECK(!VoxelAimFind(*g.v, *g.vt, in).hit);
        CHECK(VoxelAimFind(*g.v, *g.vt, in).miss == VoxelAimMiss::HeadInGround);
        // waist deep: a hit is never inside the body
        VoxelAimInput waist = ThirdPerson(sy - 1.0f, 55.0f);
        VoxelAimHit h = VoxelAimFind(*g.v, *g.vt, waist);
        CHECK(!h.hit || !VoxelAimInsideBody(waist, h.world));
    }

    // --- the digger in a real scene, both views: the aim comes from the right place and the dig lands there ----------------------
    for (int third = 0; third < 2; ++third) {
        Ground g; const float sy = g.Surface();
        g.s.physicsEnabled = false;
        GameObject* player = g.s.CreateGameObject("Player");
        player->transform->localPosition = {0, sy, 0};
        auto* pc = PlayerController::Add(player, third ? PlayerController::ViewMode::Third : PlayerController::ViewMode::First);
        GameObject* cam = Templates::MountEyeCamera(g.s, player, "Main Camera");
        g.d = g.v->gameObject->AddComponent<VoxelDigger>();
        g.d->radius = 2.0f; g.d->strength = 20.0f; g.d->removeFloaters = 0; g.d->showBrush = false; g.d->reach = 12.0f;
        if (pc->first()) { pc->first()->yaw = 0; pc->first()->pitch = 35.0f; pc->first()->lookSmoothing = 0.0f; }
        if (pc->third()) { pc->third()->yaw = 0; pc->third()->pitch = 30.0f; }
        for (int i = 0; i < 4; ++i) g.s.Update(1.0f / 60.0f);
        Vec3 fwd = cam->GetComponent<Camera>()->gameObject->transform->Forward() * -1.0f;
        if (fwd.y > 0.0f && pc->first()) { pc->first()->pitch = -35.0f; for (int i = 0; i < 3; ++i) g.s.Update(1.0f / 60.0f); }
        Vec3 local;
        const bool aim = g.d->AimAtVoxel(g.v, local, false);
        CHECK(aim);
        if (aim) {
            const VoxelAimHit& a = g.d->LastAim();
            CHECK(a.thirdPerson == (third == 1));
            CHECK(a.fromBody <= 12.0f + 0.01f);
            CHECK(a.world.z < 0.0f);
            // Hold the dig button: the ground at the aimed point goes.
            const float before = g.v->SampleDensity(local);
            Input::FeedMouse({0, 0}, 1u);
            for (int i = 0; i < 40; ++i) g.s.Update(1.0f / 60.0f);
            Input::FeedMouse({0, 0}, 0u);
            g.s.Update(1.0f / 60.0f);
            CHECK(g.v->SampleDensity(local) < before - 0.2f);
            // and not the ground under the player's feet
            CHECK(g.v->SampleDensity({0, sy - 0.8f, 0}) > g.v->iso);
        }
    }

    // --- named input actions, rebinding, the hotbar -----------------------------------------------------------------------------
    {
        InputActions::Restore(); InputActions::Reset();
        CHECK(InputActions::Has("Dig") && InputActions::Has("Place") && InputActions::Has("PickBlock"));
        CHECK(InputActions::Has("HotbarNext") && InputActions::Has("HotbarPrev"));
        CHECK(!VoxelDigger::ActionRebound("Dig"));
        CHECK(InputActions::Rebind("Dig", "key:f"));
        CHECK(VoxelDigger::ActionRebound("Dig"));
        InputActions::Restore();

        Ground g; const float sy = g.Surface();
        g.v->EnableMaterials(0);
        g.v->Apply();
        GameObject* cam = g.s.CreateGameObject("Cam");
        cam->AddComponent<Camera>()->main = true;
        cam->transform->localPosition = {0, sy + 6.0f, 0};
        cam->transform->localRotation = Quat::LookRotation(Vec3{0, 1, 0}, Vec3{0, 0, -1});   // straight down
        auto* d = g.v->gameObject->AddComponent<VoxelDigger>();
        d->radius = 2.5f; d->strength = 20.0f; d->removeFloaters = 0; d->showBrush = false;
        g.s.Update(0.0f);
        Input::FeedMouse({0, 0}, 0u); Input::FeedKeys({});
        // the dig button through the named action (a pad trigger or any rebinding) digs
        InputActions::Rebind("Dig", "key:f");
        const float before = g.v->SampleDensity({0, sy - 0.3f, 0});
        Input::FeedMouse({0, 0}, 1u);                       // the old left button no longer digs once Dig is rebound
        for (int i = 0; i < 20; ++i) { InputActions::Tick(); g.s.Update(1.0f / 60.0f); }
        CHECK(std::fabs(g.v->SampleDensity({0, sy - 0.3f, 0}) - before) < 1e-4f);
        Input::FeedMouse({0, 0}, 0u);
        Input::FeedKeys({'f'});
        for (int i = 0; i < 20; ++i) { InputActions::Tick(); g.s.Update(1.0f / 60.0f); }
        Input::FeedKeys({});
        CHECK(g.v->SampleDensity({0, sy - 0.3f, 0}) < before - 0.2f);
        InputActions::Restore(); InputActions::Reset();   // a rebinding is kept in Prefs: put the defaults back

        // hotbar: number keys choose the build material, the wheel steps through them
        const int nm = (int)g.v->materials.size();
        CHECK(nm >= 2);
        Input::FeedKeys({'2'}); g.s.Update(1.0f / 60.0f); Input::FeedKeys({});
        CHECK(d->addMaterial == 1);
        g.s.Update(1.0f / 60.0f);
        Input::FeedMouseWheel(1.0f); g.s.Update(1.0f / 60.0f); Input::ClearTypedText();
        CHECK(d->addMaterial == (2 % nm));
        Input::FeedMouseWheel(-1.0f); g.s.Update(1.0f / 60.0f); Input::ClearTypedText();
        CHECK(d->addMaterial == 1);
        Input::FeedMouseWheel(0.0f);
        // pick block: the material under the aim becomes the build material
        d->addMaterial = 0;
        Input::FeedKeys({'t'}); InputActions::Tick(); g.s.Update(1.0f / 60.0f); Input::FeedKeys({}); InputActions::Tick();
        CHECK(d->addMaterial >= 0 && d->addMaterial < nm);
    }

    // --- progress feedback: a dig works through the ground and the progress rises; a strong tool never reads as blocked ----------
    {
        Ground g; const float sy = g.Surface();
        GameObject* cam = g.s.CreateGameObject("Cam");
        cam->AddComponent<Camera>()->main = true;
        cam->transform->localPosition = {0, sy + 6.0f, 0};
        cam->transform->localRotation = Quat::LookRotation(Vec3{0, 1, 0}, Vec3{0, 0, -1});
        auto* d = g.v->gameObject->AddComponent<VoxelDigger>();
        d->radius = 2.5f; d->strength = 10.0f; d->removeFloaters = 0; d->showBrush = true;
        g.s.Update(0.0f);
        CHECK(d->StrokeProgress() == 0.0f);
        Input::FeedMouse({0, 0}, 1u);
        float peak = 0.0f;
        for (int i = 0; i < 90; ++i) { g.s.Update(1.0f / 60.0f); peak = std::max(peak, d->StrokeProgress()); }
        Input::FeedMouse({0, 0}, 0u);
        g.s.Update(1.0f / 60.0f);
        CHECK(peak > 0.05f && peak <= 1.0f);
        CHECK(d->StrokeProgress() == 0.0f);                  // released: back to zero
        CHECK(!d->AimBlocked());
    }

    // --- a scene saved with the new fields keeps them ---------------------------------------------------------------------------
    {
        Scene a("SAVE");
        GameObject* g = a.CreateGameObject("T");
        g->AddComponent<VoxelTerrain>()->Resize(8, 8, 8);
        auto* d = g->AddComponent<VoxelDigger>();
        d->reach = 7.5f; d->playerAim = false; d->hotbarKeys = false;
        const std::string text = SceneSerializer::Serialize(a);
        Scene b("LOAD");
        CHECK(SceneSerializer::Deserialize(b, text));
        auto* e = b.FindObjectOfType<VoxelDigger>();
        CHECK(e && std::fabs(e->reach - 7.5f) < 1e-4f && !e->playerAim && e->useActions && !e->hotbarKeys && e->showProgress);
    }

    TEST_MAIN_RESULT();
}
