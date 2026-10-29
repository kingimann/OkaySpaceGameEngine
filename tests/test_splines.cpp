#include "test_framework.hpp"
#include <Okay.hpp>
#include "okay/Scripting/NextScriptVM.hpp"
#include <algorithm>
#include <cmath>
#include <sstream>
#include <string>
#include <vector>

using namespace okay;

// Splines (evaluation, arc length, frames, nearest point, editing), the follower,
// spline meshes and instancers, line and trail renderers, their scene records and
// the script bindings.

namespace {

SplinePoint Pt(Vec3 p, SplinePoint::Mode m = SplinePoint::Mode::AutoSmooth) {
    SplinePoint s; s.position = p; s.mode = m; return s;
}

bool Near(const Vec3& a, const Vec3& b, float eps) { return (a - b).Magnitude() <= eps; }

// A Bezier segment with hand-placed handles: controls (0,0,0) (1,2,0) (3,2,0) (4,0,0).
Spline* HandBezier(Scene& s) {
    Spline* sp = s.CreateGameObject("Bez")->AddComponent<Spline>();
    SplinePoint a = Pt({0, 0, 0}, SplinePoint::Mode::Free);
    a.tangentOut = {1, 2, 0};
    SplinePoint b = Pt({4, 0, 0}, SplinePoint::Mode::Free);
    b.tangentIn = {-1, 2, 0};
    sp->points = {a, b};
    return sp;
}

Spline* Wavy(Scene& s, const char* name = "Wavy") {
    Spline* sp = s.CreateGameObject(name)->AddComponent<Spline>();
    sp->points = {Pt({0, 0, 0}), Pt({4, 1, 3}), Pt({9, 0, -2}), Pt({13, 2, 4}), Pt({18, 0, 0})};
    return sp;
}

Spline* Straight(Scene& s, const char* name, Vec3 a, Vec3 b) {
    Spline* sp = s.CreateGameObject(name)->AddComponent<Spline>();
    sp->points = {Pt(a, SplinePoint::Mode::Linear), Pt(b, SplinePoint::Mode::Linear)};
    return sp;
}

float G(ScriptComponent* sc, const char* n) { return sc->VM()->GetGlobal(n).AsFloat(); }

} // namespace

int main() {
    RUN_SUITE("splines");

    // ---- Bezier by parameter, against hand values ----
    {
        Scene s;
        Spline* sp = HandBezier(s);
        CHECK(sp->SegmentCount() == 1);
        CHECK(Near(sp->EvaluatePosition(0.0f), {0, 0, 0}, 1e-5f));
        CHECK(Near(sp->EvaluatePosition(1.0f), {4, 0, 0}, 1e-5f));
        CHECK(Near(sp->EvaluatePosition(0.5f), {2.0f, 1.5f, 0.0f}, 1e-5f));
        CHECK(Near(sp->EvaluatePosition(0.25f), {0.90625f, 1.125f, 0.0f}, 1e-5f));
        CHECK(Near(sp->EvaluateTangent(0.5f), {1, 0, 0}, 1e-5f));
        CHECK(Near(sp->EvaluateTangent(0.0f), Vec3{1, 2, 0}.Normalized(), 1e-5f));
        // A moved and rotated object carries the curve with it.
        sp->transform->localPosition = {10, 0, 0};
        sp->transform->localRotation = Quat::Euler(0, 90, 0);
        const Vec3 w = sp->EvaluatePosition(0.5f);
        CHECK(Near(w, sp->transform->TransformPoint({2.0f, 1.5f, 0.0f}), 1e-4f));
        // The handles the editor shows: Free points keep their own.
        CHECK(Near(sp->HandleOut(0), {1, 2, 0}, 1e-6f) && Near(sp->HandleIn(1), {-1, 2, 0}, 1e-6f));
    }

    // ---- Catmull-Rom through the points, against the classic formula and DollyPath ----
    {
        Scene s;
        Spline* sp = s.CreateGameObject("CR")->AddComponent<Spline>();
        sp->type = Spline::Type::CatmullRom;
        sp->points = {Pt({0, 0, 0}), Pt({1, 0, 0}), Pt({2, 1, 0}), Pt({3, 1, 0})};
        // Middle of the middle segment: 0.5 * (2p1 + u(p2-p0) + u^2(2p0-5p1+4p2-p3) + u^3(3p1-3p2+p3-p0)).
        CHECK(Near(sp->EvaluatePosition(0.5f), {1.5f, 0.5f, 0.0f}, 1e-5f));
        for (int i = 0; i <= 3; ++i)   // it passes through every point
            CHECK(Near(sp->EvaluatePosition((float)i / 3.0f), sp->points[(std::size_t)i].position, 1e-5f));
        auto* dp = s.CreateGameObject("Dolly")->AddComponent<DollyPath>();
        dp->waypoints = {{0, 0, 0}, {1, 0, 0}, {2, 1, 0}, {3, 1, 0}};
        for (float t : {0.36f, 0.5f, 0.6f})   // same curve on the inner segment as the camera rail
            CHECK(Near(sp->EvaluatePosition(t), dp->EvaluatePosition(t), 1e-4f));
        // Two points: a straight line, evenly parameterised.
        Spline* two = s.CreateGameObject("Two")->AddComponent<Spline>();
        two->type = Spline::Type::CatmullRom;
        two->points = {Pt({0, 0, 0}), Pt({6, 0, 0})};
        CHECK(Near(two->EvaluatePosition(0.25f), {1.5f, 0, 0}, 1e-5f));
    }

    // ---- Arc length: even distances are even steps on the curve ----
    {
        Scene s;
        Spline* sp = Wavy(s);
        const float L = sp->Length();
        CHECK(L > 18.0f);
        const int N = 60;
        float worst = 0.0f, sum = 0.0f;
        Vec3 prev = sp->PositionAtDistance(0.0f);
        for (int k = 1; k <= N; ++k) {
            const Vec3 cur = sp->PositionAtDistance(L * (float)k / (float)N);
            const float step = (cur - prev).Magnitude();
            worst = std::max(worst, std::fabs(step - L / (float)N) / (L / (float)N));
            sum += step;
            prev = cur;
        }
        CHECK(worst < 0.02f);                    // within 2% of the even step
        CHECK(std::fabs(sum - L) / L < 0.01f);
        CHECK(Near(sp->PositionAtDistance(L), sp->points.back().position, 1e-3f));
        // The raw parameter is not even: that is what the table is for.
        float pWorst = 0.0f;
        Vec3 pp = sp->EvaluatePosition(0.0f);
        for (int k = 1; k <= N; ++k) {
            const Vec3 cur = sp->EvaluatePosition((float)k / (float)N);
            pWorst = std::max(pWorst, std::fabs((cur - pp).Magnitude() - L / (float)N) / (L / (float)N));
            pp = cur;
        }
        CHECK(pWorst > worst);
        // Distance and parameter convert both ways.
        const float t = sp->ParameterAtDistance(L * 0.37f);
        CHECK_NEAR(sp->DistanceAtParameter(t), L * 0.37f, L * 1e-3f);
        // A scaled object: world length scales, local length does not.
        const float local = sp->LocalLength();
        sp->transform->localScale = {2, 2, 2};
        CHECK_NEAR(sp->Length(), local * 2.0f, local * 1e-3f);
        CHECK_NEAR(sp->LocalLength(), local, 1e-4f);
    }

    // ---- Closed loops ----
    {
        Scene s;
        Spline* sp = s.CreateGameObject("Loop")->AddComponent<Spline>();
        sp->points = {Pt({0, 0, 0}), Pt({10, 0, 0}), Pt({10, 0, 10}), Pt({0, 0, 10})};
        const float open = sp->Length();
        sp->closed = true;
        CHECK(sp->SegmentCount() == 4);
        const float L = sp->Length();
        CHECK(L > open + 9.0f);
        CHECK(Near(sp->EvaluatePosition(1.0f), sp->EvaluatePosition(0.0f), 1e-5f));
        CHECK(Near(sp->EvaluatePosition(1.25f), sp->EvaluatePosition(0.25f), 1e-5f));     // wraps
        CHECK(Near(sp->PositionAtDistance(L + 3.0f), sp->PositionAtDistance(3.0f), 1e-3f));
        CHECK(Near(sp->PositionAtDistance(-2.0f), sp->PositionAtDistance(L - 2.0f), 1e-3f));
        // Smooth through the seam, and the frame meets itself after a lap.
        const SplineSample a = sp->SampleAtDistance(0.0f), b = sp->SampleAtDistance(L - 1e-3f);
        CHECK(Vec3::Dot(a.tangent, b.tangent) > 0.999f);
        CHECK(Vec3::Dot(a.up, b.up) > 0.999f);
        // A flat loop keeps up pointing up all the way round.
        for (int k = 0; k < 40; ++k) CHECK(sp->SampleAtDistance(L * (float)k / 40.0f).up.y > 0.999f);
    }

    // ---- Rotation-minimising frames: no flips on a helix, roll banks ----
    {
        Scene s;
        Spline* sp = s.CreateGameObject("Helix")->AddComponent<Spline>();
        sp->type = Spline::Type::CatmullRom;
        for (int i = 0; i <= 36; ++i) {
            const float a = (float)i * 0.5236f;   // 30 degrees a point: three turns
            sp->points.push_back(Pt({3.0f * std::cos(a), 0.35f * (float)i, 3.0f * std::sin(a)}));
        }
        const float L = sp->Length();
        Vec3 prevUp = sp->SampleAtDistance(0.0f).up;
        float minDot = 1.0f, worstPerp = 0.0f;
        for (int k = 1; k <= 400; ++k) {
            const SplineSample f = sp->SampleAtDistance(L * (float)k / 400.0f);
            minDot = std::min(minDot, Vec3::Dot(prevUp, f.up));
            worstPerp = std::max(worstPerp, std::fabs(Vec3::Dot(f.up, f.tangent)));
            CHECK_NEAR(f.up.Magnitude(), 1.0f, 1e-4f);
            CHECK_NEAR(Vec3::Dot(f.right, f.tangent), 0.0f, 1e-4f);
            prevUp = f.up;
        }
        CHECK(minDot > 0.99f);      // turns a little each step, never jumps
        CHECK(worstPerp < 1e-3f);
        // World up mode keeps it level-ish instead.
        sp->upMode = Spline::UpMode::World;
        for (int k = 0; k <= 50; ++k) CHECK(sp->SampleAtDistance(L * (float)k / 50.0f).up.y > 0.9f);

        // Roll: positive banks up towards the right; it blends between points.
        Spline* r = Straight(s, "Roll", {0, 0, 0}, {0, 0, 10});
        r->points[0].roll = 90.0f; r->points[1].roll = 90.0f;
        CHECK(Near(r->SampleAtDistance(5.0f).up, {1, 0, 0}, 1e-4f));
        r->points[0].roll = 0.0f;
        const float c = std::cos(0.7853982f);
        CHECK(Near(r->EvaluateUp(0.5f), {c, c, 0}, 1e-3f));
        CHECK(Near(r->EvaluateUp(0.0f), {0, 1, 0}, 1e-4f));
        // Per-point scale blends the same way.
        r->points[1].scale = {3, 1};
        CHECK_NEAR(r->EvaluateScale(0.5f).x, 2.0f, 1e-4f);
    }

    // ---- Nearest point ----
    {
        Scene s;
        Spline* sp = HandBezier(s);
        const float t0 = 0.3f;
        const Vec3 p = sp->EvaluatePosition(t0);
        const Vec3 tan = sp->EvaluateTangent(t0);
        const Vec3 off = Vec3::Cross(Vec3{0, 0, 1}, tan).Normalized() * 0.4f;   // across the curve, in its plane
        Vec3 hit;
        const float t = sp->NearestParameter(p + off, &hit);
        CHECK_NEAR(t, t0, 2e-3f);
        CHECK(Near(hit, p, 2e-3f));
        CHECK(Near(sp->NearestPoint({-5, 0, 0}), {0, 0, 0}, 1e-3f));   // past the start: the start
        CHECK(Near(sp->NearestPoint({9, 0, 0}), {4, 0, 0}, 1e-3f));
        sp->transform->localPosition = {0, 5, 0};
        CHECK(Near(sp->NearestPoint({2, 9, 0}), {2.0f, 6.5f, 0.0f}, 1e-3f));
        CHECK_NEAR(sp->NearestDistance(sp->EvaluatePosition(1.0f)), sp->Length(), 1e-3f);
    }

    // ---- Editing: split keeps the shape exactly, removing the split point restores it ----
    {
        Scene s;
        Spline* sp = Wavy(s);
        sp->points[2].mode = SplinePoint::Mode::Free;
        sp->points[2].tangentIn = {-2, 1, 0}; sp->points[2].tangentOut = {1, -1, 2};
        std::vector<Vec3> before;
        const float L0 = sp->Length();
        for (int k = 0; k <= 80; ++k) before.push_back(sp->EvaluatePosition((float)k / 80.0f));
        auto worstGap = [&](const std::vector<Vec3>& pts) {
            float w = 0.0f;
            for (const Vec3& q : pts) w = std::max(w, (sp->NearestPoint(q) - q).Magnitude());
            return w;
        };
        const int idx = sp->SplitAt(0.37f);
        CHECK(idx == 2);
        CHECK(sp->Count() == 6);
        CHECK(worstGap(before) < 2e-3f);
        CHECK_NEAR(sp->Length(), L0, L0 * 2e-3f);
        CHECK(sp->RemovePoint(idx));
        CHECK(sp->Count() == 5);
        CHECK(worstGap(before) < 2e-3f);
        for (int k = 0; k <= 80; ++k) CHECK(Near(sp->EvaluatePosition((float)k / 80.0f), before[(std::size_t)k], 2e-3f));
        // A straight (Linear) run stays straight and Linear.
        Spline* st = Straight(s, "St", {0, 0, 0}, {8, 0, 0});
        const int m = st->SplitAt(0.25f);
        CHECK(m == 1 && st->points[1].mode == SplinePoint::Mode::Linear);
        CHECK(Near(st->points[1].position, {2, 0, 0}, 1e-4f));
        CHECK(Near(st->EvaluatePosition(0.75f), {5, 0, 0}, 1e-4f));
        // Insert / add / remove, and handle editing.
        CHECK(st->AddPoint({8, 0, 4}) == 3);
        CHECK(st->InsertPoint(0, Pt({-4, 0, 0})) == 0 && st->Count() == 5);
        CHECK(st->RemovePoint(0) && Near(st->points[0].position, {0, 0, 0}, 1e-6f));
        Spline* h = Wavy(s, "Handles");
        const Vec3 autoOut = h->HandleOut(1);
        h->SetHandle(1, true, autoOut * 2.0f);                   // auto -> aligned, the other side follows
        CHECK(h->points[1].mode == SplinePoint::Mode::Aligned);
        CHECK(Vec3::Dot(h->HandleIn(1).Normalized(), autoOut.Normalized()) < -0.999f);
        h->SetHandle(1, false, {0, 3, 0}, /*breakAlignment*/ true);   // Shift-drag: free
        CHECK(h->points[1].mode == SplinePoint::Mode::Free);
        CHECK(Near(h->HandleOut(1), autoOut * 2.0f, 1e-5f) && Near(h->HandleIn(1), {0, 3, 0}, 1e-6f));
        h->SetMode(1, SplinePoint::Mode::Aligned);                 // free -> aligned: in lines up with out
        CHECK(Vec3::Dot(h->HandleIn(1).Normalized(), h->HandleOut(1).Normalized()) < -0.999f);
        CHECK_NEAR(h->HandleIn(1).Magnitude(), 3.0f, 1e-4f);
        const std::uint64_t rev = h->Revision();
        h->points[0].position.y += 1.0f;                            // a plain field edit is noticed
        CHECK(h->Revision() != rev);
    }

    // ---- SplineFollower: loop, ping-pong, once, time, events ----
    {
        Scene s;
        Straight(s, "Rail", {0, 0, 0}, {10, 0, 0});
        GameObject* cart = s.CreateGameObject("Cart");
        auto* f = cart->AddComponent<SplineFollower>();
        f->spline = "Rail"; f->speed = 4.0f;
        int ends = 0, starts = 0;
        f->onEnd = [&](bool atEnd) { if (atEnd) ++ends; else ++starts; };
        f->Start();
        CHECK(f->IsPlaying());
        f->Step(1.0f); f->Step(1.0f);
        CHECK_NEAR(f->Distance(), 8.0f, 1e-4f);
        CHECK(Near(cart->transform->Position(), {8, 0, 0}, 1e-3f));
        f->Step(1.0f);                                           // 12 -> wraps to 2
        CHECK_NEAR(f->Distance(), 2.0f, 1e-4f);
        CHECK(ends == 1 && starts == 0);
        CHECK(Near(cart->transform->Forward(), {1, 0, 0}, 1e-3f));   // faces along the rail

        f->wrap = SplineFollower::Wrap::PingPong;
        f->SetDistance(8.0f);
        f->Step(1.0f);                                           // 12 -> bounces back to 8
        CHECK_NEAR(f->Distance(), 8.0f, 1e-4f);
        CHECK(f->Direction() == -1 && ends == 2);
        CHECK(Near(cart->transform->Forward(), {-1, 0, 0}, 1e-3f));   // turned round
        f->Step(2.5f);                                           // 8 - 10 -> -2 -> bounces to 2
        CHECK_NEAR(f->Distance(), 2.0f, 1e-4f);
        CHECK(f->Direction() == 1 && starts == 1);

        f->wrap = SplineFollower::Wrap::Once;
        f->SetDistance(6.0f);
        f->Step(2.0f);
        CHECK_NEAR(f->Distance(), 10.0f, 1e-4f);
        CHECK(!f->IsPlaying() && ends == 3);
        f->Update(1.0f);                                         // stopped: nothing more
        CHECK(ends == 3);

        f->drive = SplineFollower::Drive::Time; f->duration = 5.0f; f->wrap = SplineFollower::Wrap::Loop;
        f->SetDistance(0.0f); f->Play();
        f->Step(2.5f);
        CHECK_NEAR(f->NormalizedPosition(), 0.5f, 1e-4f);
        // Offset in the curve's frame (up 1, right 0.5).
        f->offset = {0.5f, 1.0f, 0.0f};
        f->Place();
        CHECK(Near(cart->transform->Position(), {5.0f, 1.0f, -0.5f}, 1e-3f));

        // Through the scene: Start places it, Update moves it, scripts hear on_spline_end.
        Scene s2;
        Straight(s2, "Rail", {0, 0, 0}, {2, 0, 0});
        GameObject* c2 = s2.CreateGameObject("Platform");
        auto* f2 = c2->AddComponent<SplineFollower>();
        f2->spline = "Rail"; f2->speed = 2.0f; f2->wrap = SplineFollower::Wrap::PingPong; f2->startAt = 0.5f;
        f2->orient = false;
        auto* sc = c2->AddComponent<ScriptComponent>("okayscript");
        std::string err;
        CHECK(sc->LoadSource("var ends = 0\nvar which = \"\"\nfunction on_spline_end(w) { ends = ends + 1; which = w }\n", &err));
        s2.Start();
        CHECK(Near(c2->transform->Position(), {1, 0, 0}, 1e-3f));
        for (int i = 0; i < 40; ++i) s2.Update(1.0f / 60.0f);   // 1 + 1.33: past the far end and back
        CHECK(G(sc, "ends") == 1.0f);
        CHECK(sc->VM()->GetGlobal("which").AsString() == "end");
    }

    // ---- A dolly camera rides a Spline: a fraction of its length, or the point nearest the target ----
    {
        Scene s; s.physicsEnabled = false;
        Straight(s, "Track", {-10, 0, 0}, {10, 0, 0});
        GameObject* t = s.CreateGameObject("T");
        t->transform->localPosition = {6, 0, 3};
        GameObject* camGO = s.CreateGameObject("Cam");
        camGO->AddComponent<Camera>();
        camGO->AddComponent<CinemachineBrain>()->blendTime = 0.0f;
        auto* vc = s.CreateGameObject("V")->AddComponent<VirtualCamera>();
        vc->follow = "T"; vc->dolly = true; vc->dollyPath = "Track"; vc->dollyPosition = 0.25f;
        vc->positionDamping = 0.0f; vc->priority = 10;
        s.Start();
        s.Update(0.016f);
        CHECK(Near(camGO->transform->Position(), {-5, 0, 0}, 1e-2f));
        vc->autoDolly = true;
        s.Update(0.016f);
        CHECK(Near(camGO->transform->Position(), {6, 0, 0}, 1e-2f));
        CHECK_NEAR(vc->dollyPosition, 0.8f, 1e-3f);
    }

    // ---- SplineMesh: extrude counts, UV tiling, normals out, collider ----
    {
        Scene s;
        Spline* sp = Straight(s, "Road", {0, 0, 0}, {0, 0, 10});
        auto* sm = sp->gameObject->AddComponent<SplineMesh>();
        sm->profile = SplineMesh::Profile::Road; sm->width = 4.0f; sm->height = 0.25f;
        sm->segmentLength = 1.0f; sm->uvTileLength = 4.0f;
        CHECK(sm->Rebuild());
        auto* mr = sp->gameObject->GetComponent<MeshRenderer>();
        CHECK(mr != nullptr);
        const Mesh& m = mr->mesh;
        CHECK(m.vertices.size() == 11u * 4u);          // 11 cross-sections x 4 profile points
        CHECK(m.TriangleCount() == 10 * 3 * 2);        // 10 spans x 3 faces x 2
        Vec3 lo, hi; m.Bounds(lo, hi);
        CHECK(Near(lo, {-2.0f, -0.25f, 0.0f}, 1e-4f) && Near(hi, {2.0f, 0.02f, 10.0f}, 1e-4f));   // top 2 cm up
        float vMax = 0.0f, uMax = 0.0f;
        for (const Vec2& uv : m.uvs) { vMax = std::max(vMax, uv.y); uMax = std::max(uMax, uv.x); }
        CHECK_NEAR(vMax, 2.5f, 1e-4f);                 // 10 units / 4 per tile
        CHECK_NEAR(uMax, 1.0f, 1e-6f);
        int up = 0, down = 0;
        for (int t = 0; t < m.TriangleCount(); ++t) {   // the top faces up (outward winding)
            const Vec3 a = m.vertices[(std::size_t)m.triangles[(std::size_t)t * 3]];
            const Vec3 b = m.vertices[(std::size_t)m.triangles[(std::size_t)t * 3 + 1]];
            const Vec3 c = m.vertices[(std::size_t)m.triangles[(std::size_t)t * 3 + 2]];
            const Vec3 n = Vec3::Cross(b - a, c - a);
            if (std::fabs(a.y - 0.02f) < 1e-4f && std::fabs(b.y - 0.02f) < 1e-4f && std::fabs(c.y - 0.02f) < 1e-4f) { if (n.y > 0) ++up; else ++down; }
        }
        CHECK(up == 20 && down == 0);
        // Other profiles.
        sm->profile = SplineMesh::Profile::Strip;
        CHECK(sm->Rebuild() && mr->mesh.vertices.size() == 22u && mr->mesh.TriangleCount() == 20);
        sm->profile = SplineMesh::Profile::Tube; sm->sides = 8;
        CHECK(sm->Rebuild() && mr->mesh.vertices.size() == 11u * 9u && mr->mesh.TriangleCount() == 10 * 8 * 2);
        CHECK(mr->mesh.HasNormals());
        sm->profile = SplineMesh::Profile::Rail;
        CHECK(sm->Rebuild() && mr->mesh.vertices.size() == 11u * 5u && mr->mesh.TriangleCount() == 10 * 4 * 2);
        sm->profile = SplineMesh::Profile::Custom;
        sm->customProfile = {{-1, 0}, {0, 0.5f}, {1, 0}};
        CHECK(sm->Rebuild() && mr->mesh.vertices.size() == 33u && mr->mesh.TriangleCount() == 40);
        // A sharp corner gets a cross-section exactly on it.
        Spline* bend = s.CreateGameObject("Bend")->AddComponent<Spline>();
        bend->points = {Pt({0, 0, 0}, SplinePoint::Mode::Linear), Pt({0, 0, 3.3f}, SplinePoint::Mode::Linear),
                        Pt({3.3f, 0, 3.3f}, SplinePoint::Mode::Linear)};
        auto* bm = bend->gameObject->AddComponent<SplineMesh>();
        bm->profile = SplineMesh::Profile::Strip; bm->width = 1.0f; bm->segmentLength = 1.0f;
        CHECK(bm->Rebuild());
        int corner = 0;   // the mitred section: inner and outer corners of the two strips
        for (const Vec3& v : bend->gameObject->GetComponent<MeshRenderer>()->mesh.vertices)
            if (Near(v, {0.5f, 0.0f, 2.8f}, 1e-3f) || Near(v, {-0.5f, 0.0f, 3.8f}, 1e-3f)) ++corner;
        CHECK(corner == 2);
        // Live: a moved point rebuilds; nothing changed, nothing rebuilt.
        sm->profile = SplineMesh::Profile::Road;
        sm->Rebuild();
        CHECK(!sm->RebuildIfChanged());
        sp->points[1].position.z = 20.0f;
        CHECK(sm->RebuildIfChanged());
        mr->mesh.Bounds(lo, hi);
        CHECK_NEAR(hi.z, 20.0f, 1e-4f);
        // Collider: the road becomes solid ground.
        sm->generateCollider = true;
        sm->Rebuild();
        auto* mc = sp->gameObject->GetComponent<MeshCollider3D>();
        CHECK(mc != nullptr && mc->autoFit);
        if (mc) CHECK(Near(mc->size, {4.0f, 0.27f, 20.0f}, 1e-3f));
        RaycastHit3D hit = s.physics3D().Raycast(s, {0, 5, 8}, {0, -1, 0}, 20.0f);
        CHECK(hit.hit && hit.gameObject == sp->gameObject);
        CHECK_NEAR(hit.point.y, 0.02f, 1e-3f);
        // The generated mesh is never written to the scene.
        const std::string text = SceneSerializer::Serialize(s);
        std::istringstream lines(text);
        std::string line, cur;
        bool roadGeo = false;
        while (std::getline(lines, line)) {
            if (line.rfind("object ", 0) == 0 || line.rfind("go ", 0) == 0) cur = line;
            if (cur.find("\"Road\"") != std::string::npos && line.rfind("  meshgeo", 0) == 0) roadGeo = true;
        }
        CHECK(!roadGeo);
        CHECK(text.find("  splinemesh") != std::string::npos);
    }

    // ---- SplineMesh repeat: copies bent along the curve, within bounds ----
    {
        Scene s;
        Spline* sp = Straight(s, "Pipe", {0, 0, 0}, {0, 0, 10});
        auto* sm = sp->gameObject->AddComponent<SplineMesh>();
        sm->mode = SplineMesh::Mode::Repeat; sm->sourceMesh = "Cube"; sm->meshAxis = SplineMesh::Axis::Z;
        CHECK(sm->Rebuild());
        const Mesh& m = sp->gameObject->GetComponent<MeshRenderer>()->mesh;
        CHECK(sm->Copies() == 10);
        CHECK(m.vertices.size() == 80u && m.TriangleCount() == 120);
        Vec3 lo, hi; m.Bounds(lo, hi);
        CHECK(Near(lo, {-0.5f, -0.5f, 0.0f}, 1e-4f) && Near(hi, {0.5f, 0.5f, 10.0f}, 1e-4f));
        sm->spacing = 0.5f;                       // 7 copies and 6 gaps fill 10 exactly
        sm->Rebuild();
        CHECK(sm->Copies() == 7);
        sm->spacing = 0.6f;                       // stretched a little: round(10.6 / 1.6) = 7
        sm->Rebuild();
        CHECK(sm->Copies() == 7);
        sm->stretchToFit = false;                 // as long as they are: floor(10.6 / 1.6) = 6
        sm->Rebuild();
        CHECK(sm->Copies() == 6);
        // Bent round a curve: every vertex stays within the cube's reach of the curve.
        Spline* c = Wavy(s, "Curve");
        auto* cm = c->gameObject->AddComponent<SplineMesh>();
        cm->mode = SplineMesh::Mode::Repeat; cm->sourceMesh = "Cube"; cm->meshAxis = SplineMesh::Axis::Z;
        cm->meshScale = {0.5f, 0.5f, 1.0f};
        CHECK(cm->Rebuild());
        const Mesh& cmesh = c->gameObject->GetComponent<MeshRenderer>()->mesh;
        float worst = 0.0f;
        for (const Vec3& v : cmesh.vertices) worst = std::max(worst, (c->NearestPoint(v) - v).Magnitude());
        CHECK(worst <= 0.3536f + 0.02f);          // half the diagonal of the 0.5 x 0.5 cross-section
        CHECK(cm->Copies() == (int)std::lround(c->LocalLength()));
        // The fence prop runs along X and keeps its face colours per copy.
        cm->sourceMesh = "Fence"; cm->meshAxis = SplineMesh::Axis::X; cm->meshScale = {1, 1, 1};
        CHECK(cm->Rebuild());
        const Mesh fence = Mesh::FromName("Fence");
        CHECK(cmesh.HasFaceColors() && cmesh.TriangleCount() == cm->Copies() * fence.TriangleCount());
        float minY = 1e9f;
        for (const Vec3& v : cmesh.vertices) minY = std::min(minY, v.y);
        CHECK(minY > -2.5f);   // standing on the curve (the curve dips to y = 0 lowest)
        cm->sourceMesh = "missing.okaymesh";
        CHECK(!cm->Rebuild());
        CHECK(c->gameObject->GetComponent<MeshRenderer>()->mesh.vertices.empty());
    }

    // ---- SplineInstancer: counts, seeded placement, transient items ----
    {
        Scene s;
        Spline* sp = Straight(s, "Path", {0, 0, 0}, {20, 0, 0});
        auto* in = sp->gameObject->AddComponent<SplineInstancer>();
        in->meshName = "Cube"; in->spacing = 5.0f;
        CHECK(in->Rebuild() == 5);                         // 0, 5, 10, 15, 20
        CHECK(in->Instances()[0]->transient && in->Instances()[0]->transform->Parent() == sp->transform);
        CHECK(Near(in->Instances()[2]->transform->Position(), {10, 0, 0}, 1e-4f));
        in->bothSides = true; in->sideOffset = 3.0f;
        CHECK(in->Rebuild() == 10);
        CHECK(Near(in->Instances()[0]->transform->Position(), {0, 0, -3}, 1e-4f));   // right of +X is -Z
        CHECK(Near(in->Instances()[1]->transform->Position(), {0, 0, 3}, 1e-4f));
        in->startOffset = 2.5f;
        CHECK(in->Rebuild() == 8);                          // 2.5 .. 17.5
        in->bothSides = false; in->startOffset = 0.0f;
        in->jitter = 1.0f; in->rotationJitter = 45.0f; in->scaleMin = 0.5f; in->scaleMax = 1.5f; in->seed = 7;
        const auto a = in->Placements(*sp);
        const auto b = in->Placements(*sp);
        CHECK(a.size() == 5 && b.size() == 5);
        bool same = true, varied = false;
        for (std::size_t i = 0; i < a.size(); ++i) {
            same = same && Near(a[i].position, b[i].position, 0.0f) && a[i].scale == b[i].scale;
            varied = varied || std::fabs(a[i].scale - 1.0f) > 1e-3f;
            CHECK(a[i].scale >= 0.5f && a[i].scale <= 1.5f);
        }
        CHECK(same && varied);
        in->seed = 8;
        const auto c = in->Placements(*sp);
        bool differs = false;
        for (std::size_t i = 0; i < a.size(); ++i) differs = differs || !Near(a[i].position, c[i].position, 1e-4f);
        CHECK(differs);
        // A closed loop doesn't double the item at the seam.
        sp->points.push_back(Pt({10, 0, 10}, SplinePoint::Mode::Linear));
        sp->closed = true;
        in->jitter = 0.0f;
        const float L = sp->LocalLength();
        CHECK(in->Rebuild() == (int)std::ceil(L / 5.0f - 1e-4f));
        // Copying a scene object, and not saving the items.
        GameObject* lamp = s.CreateGameObject("Lamp");
        lamp->AddComponent<MeshRenderer>()->mesh = Mesh::FromName("StreetLamp");
        in->templateName = "Lamp";
        const int n = in->Rebuild();
        CHECK(n > 0 && in->Instances()[0]->GetComponent<MeshRenderer>() != nullptr);
        s.FlushDestroyed();
        const std::size_t live = s.Objects().size();
        Scene r;
        std::string err;
        CHECK(SceneSerializer::Deserialize(r, SceneSerializer::Serialize(s), &err));
        CHECK(r.Objects().size() == live - (std::size_t)n);
        r.Start();                                          // placed again on load
        auto* rin = r.Find("Path")->GetComponent<SplineInstancer>();
        CHECK(rin && rin->Count() == n);
        // Removing the component removes its items.
        GameObject* pathObj = sp->gameObject;
        pathObj->RemoveComponent(in);
        s.FlushDestroyed();
        CHECK(s.Objects().size() == live - (std::size_t)n);
    }

    // ---- LineRenderer: counts for view / flat, loop, corners, caps ----
    {
        Scene s;
        GameObject* g = s.CreateGameObject("Laser");
        auto* lr = g->AddComponent<LineRenderer>();
        lr->positions = {{0, 0, 0}, {4, 0, 0}, {4, 0, 4}};
        lr->width = 0.5f;
        Ribbon rb;
        lr->Build({2, 10, 2}, rb);
        CHECK(rb.vertices.size() == 6u && rb.TriangleCount() == 4);
        CHECK_NEAR((rb.vertices[0].position - rb.vertices[1].position).Magnitude(), 0.5f, 1e-4f);
        // View: each pair spans across the line and across the view.
        const Vec3 across = rb.vertices[0].position - rb.vertices[1].position;
        CHECK_NEAR(Vec3::Dot(across, Vec3{1, 0, 0}), 0.0f, 1e-4f);
        lr->alignment = LineAlignment::Flat;                       // lies on the ground
        lr->Build({2, 10, 2}, rb);
        CHECK(rb.vertices.size() == 6u && rb.TriangleCount() == 4);
        for (const RibbonVertex& v : rb.vertices) CHECK_NEAR(v.position.y, 0.0f, 1e-5f);
        lr->loop = true;
        lr->positions.push_back({0, 0, 4});
        lr->Build({2, 10, 2}, rb);
        CHECK(rb.vertices.size() == 10u && rb.TriangleCount() == 8);   // 2(n + 1), 2n
        lr->cornerVertices = 2;
        lr->Build({2, 10, 2}, rb);
        CHECK(rb.vertices.size() == 2u + 4u * 5u && rb.TriangleCount() == 8 + 4 * 3);
        lr->loop = false;
        lr->positions.pop_back();
        lr->Build({2, 10, 2}, rb);
        CHECK(rb.vertices.size() == 4u + 5u && rb.TriangleCount() == 4 + 3);
        lr->positions = {{0, 0, 0}, {2, 0, 0}, {4, 0, 0}};          // a straight joint is never rounded
        lr->Build({2, 10, 2}, rb);
        CHECK(rb.vertices.size() == 6u && rb.TriangleCount() == 4);
        lr->cornerVertices = 0; lr->capVertices = 3;
        lr->positions = {{0, 0, 0}, {4, 0, 0}};
        lr->Build({2, 10, 2}, rb);
        CHECK(rb.vertices.size() == 4u + 2u * 4u && rb.TriangleCount() == 2 + 2 * 4);
        float minX = 1e9f, maxX = -1e9f;
        for (const RibbonVertex& v : rb.vertices) { minX = std::min(minX, v.position.x); maxX = std::max(maxX, v.position.x); }
        CHECK_NEAR(minX, -0.25f, 1e-3f); CHECK_NEAR(maxX, 4.25f, 1e-3f);   // round ends reach half a width out
        // Width curve, gradient and texture u.
        lr->capVertices = 0;
        lr->widthCurve.SetLinear(1.0f, 0.0f);
        lr->colorGradient.SetTwo(Color{1, 0, 0, 1}, Color{0, 0, 1, 0});
        lr->textureMode = LineTextureMode::Tile; lr->tileLength = 2.0f;
        lr->Build({2, 10, 2}, rb);
        CHECK_NEAR((rb.vertices[2].position - rb.vertices[3].position).Magnitude(), 0.0f, 1e-5f);
        CHECK_NEAR(rb.vertices[0].color.r, 1.0f, 1e-5f); CHECK_NEAR(rb.vertices[3].color.a, 0.0f, 1e-5f);
        CHECK_NEAR(rb.vertices[2].uv.x, 2.0f, 1e-5f);
        // Local space follows the object; a Spline can be drawn instead.
        lr->useWorldSpace = false;
        g->transform->localPosition = {0, 3, 0};
        CHECK(Near(lr->WorldPoints()[1], {4, 3, 0}, 1e-5f));
        Wavy(s, "Arc");
        lr->spline = "Arc"; lr->splineSamples = 32;
        const std::vector<Vec3> wp = lr->WorldPoints();
        CHECK(wp.size() == 32u && Near(wp.back(), {18, 0, 0}, 1e-3f));
        lr->SetPositionCount(5); lr->SetPosition(4, {9, 9, 9});
        CHECK(lr->PositionCount() == 5 && Near(lr->GetPosition(4), {9, 9, 9}, 0.0f));
        // Seen from a camera: screen triangles, near-clipped, back to front.
        lr->spline.clear(); lr->useWorldSpace = true;
        lr->positions = {{-1, 0, 5}, {1, 0, 5}};
        const Mat4 vp = Mat4::Perspective(60.0f, 1.0f, 0.1f, 100.0f) * Mat4::LookAt({0, 0, 0}, {0, 0, 1}, {0, 1, 0});
        std::vector<LineDraw> draws;
        CollectLineDraws(s, vp, {0, 0, 0}, 0, 0, 200, 200, draws);
        CHECK(draws.size() == 1 && draws[0].tris.size() == 6u);
        lr->positions = {{0, 0, -5}, {0, 0, 5}};                    // half behind the camera
        CollectLineDraws(s, vp, {0, 0, 0}, 0, 0, 200, 200, draws);
        CHECK(!draws.empty());
        for (const LineDrawVertex& v : draws[0].tris) CHECK(std::isfinite(v.x) && std::isfinite(v.y));
        // With an occlusion test the line gets a vertex every 12 pixels, so it hides
        // just where something stands in front (a post in the middle of the view).
        lr->widthCurve.Clear(); lr->colorGradient.Clear();
        lr->positions = {{-1, 0, 5}, {1, 0, 5}};                    // x 65..135 px
        const LineOcclusionFn post = [](float nx, float, float) { return nx > 0.45f && nx < 0.55f; };
        CollectLineDraws(s, vp, {0, 0, 0}, 0, 0, 200, 200, draws, nullptr, ~0, post);
        CHECK(draws.size() == 1 && draws[0].tris.size() > 6u);
        int hidden = 0, ends = 0;
        for (const LineDrawVertex& v : draws[0].tris) {
            if (std::fabs(v.x - 100.0f) < 5.0f) { CHECK(v.color.a == 0.0f); ++hidden; }
            if (std::fabs(v.x - 100.0f) > 30.0f) { CHECK(v.color.a == 1.0f); ++ends; }
        }
        CHECK(hidden > 0 && ends > 0);
        lr->enabled = false;
        CollectLineDraws(s, vp, {0, 0, 0}, 0, 0, 200, 200, draws);
        CHECK(draws.empty());
    }

    // ---- TrailRenderer: emission by distance, expiry by time, teleports, Clear ----
    {
        Scene s;
        GameObject* g = s.CreateGameObject("Sword");
        auto* tr = g->AddComponent<TrailRenderer>();
        tr->time = 1.0f; tr->minVertexDistance = 0.5f; tr->teleportDistance = 5.0f;
        for (int i = 0; i <= 20; ++i) {                    // 2 units in steps of 0.1
            g->transform->localPosition = {0.1f * (float)i, 0, 0};
            tr->Update(0.01f);
        }
        CHECK(tr->PointCount() == 5);                      // 0, 0.5, 1.0, 1.5, 2.0
        Ribbon rb;
        tr->Build({1, 5, 0}, rb);
        CHECK(rb.vertices.size() == 10u && rb.TriangleCount() == 8);
        CHECK(rb.vertices.back().color.a > rb.vertices.front().color.a);   // fades towards the tail
        g->transform->localPosition = {2.2f, 0, 0};
        tr->Build({1, 5, 0}, rb);                          // the head follows between points
        CHECK(rb.vertices.size() == 12u);
        for (int i = 0; i < 120; ++i) tr->Update(0.01f);  // 1.2 s still: the old points expire
        CHECK(tr->PointCount() == 1);
        tr->emitting = false;
        tr->Update(1.5f);
        CHECK(tr->PointCount() == 0);
        tr->emitting = true;
        tr->Update(0.01f);
        g->transform->localPosition = {3.0f, 0, 0}; tr->Update(0.01f);
        g->transform->localPosition = {3.6f, 0, 0}; tr->Update(0.01f);
        CHECK(tr->PointCount() == 3);
        g->transform->localPosition = {100.0f, 0, 0};     // teleport: a new trail, no streak
        tr->Update(0.01f);
        CHECK(tr->PointCount() == 1 && Near(tr->Points()[0], {100, 0, 0}, 1e-5f));
        g->transform->localPosition = {100.6f, 0, 0}; tr->Update(0.01f);
        tr->Build({100, 5, 0}, rb);
        float reach = 0.0f;
        for (const RibbonVertex& v : rb.vertices) reach = std::max(reach, std::fabs(v.position.x - 100.0f));
        CHECK(reach < 1.0f);
        tr->Clear();
        CHECK(tr->PointCount() == 0);
        // Through the scene's update.
        s.Start();
        for (int i = 0; i < 10; ++i) { g->transform->localPosition = {0, (float)i, 0}; s.Update(0.02f); }
        CHECK(tr->PointCount() == 10);
    }

    // ---- Scene records round trip, and an older scene without them ----
    {
        Scene s("Records");
        Spline* sp = s.CreateGameObject("Track")->AddComponent<Spline>();
        sp->type = Spline::Type::CatmullRom; sp->closed = true; sp->upMode = Spline::UpMode::World; sp->resolution = 24;
        SplinePoint a = Pt({1.5f, 0, -2}, SplinePoint::Mode::Free);
        a.tangentIn = {-1, 0.5f, 0}; a.tangentOut = {2, 0, 1}; a.roll = 15.0f; a.scale = {2, 0.5f};
        sp->points = {a, Pt({6, 1, 0}, SplinePoint::Mode::Aligned), Pt({3, 0, 5}, SplinePoint::Mode::Linear)};
        auto* sf = s.CreateGameObject("Cart")->AddComponent<SplineFollower>();
        sf->spline = "Track"; sf->drive = SplineFollower::Drive::Time; sf->wrap = SplineFollower::Wrap::PingPong;
        sf->speed = 3.5f; sf->duration = 7.0f; sf->startAt = 0.25f; sf->playOnStart = false; sf->orient = false;
        sf->offset = {0.5f, 1, -2};
        auto* sm = sp->gameObject->AddComponent<SplineMesh>();
        sm->mode = SplineMesh::Mode::Repeat; sm->profile = SplineMesh::Profile::Custom;
        sm->customProfile = {{-1, 0}, {0, 1.5f}, {1, 0}}; sm->closedProfile = true; sm->width = 3; sm->height = 0.5f;
        sm->sides = 9; sm->segmentLength = 0.75f; sm->uvTileLength = 2.0f; sm->sourceMesh = "Fence";
        sm->meshAxis = SplineMesh::Axis::Z; sm->spacing = 0.25f; sm->meshScale = {1, 2, 3}; sm->stretchToFit = false;
        sm->generateCollider = true;
        auto* si = s.CreateGameObject("Trees")->AddComponent<SplineInstancer>();
        si->spline = "Track"; si->templateName = "Lamp"; si->prefabPath = "Assets/tree.okayprefab"; si->meshName = "Pine";
        si->spacing = 6; si->startOffset = 1; si->sideOffset = -2; si->bothSides = true; si->jitter = 0.5f;
        si->rotationJitter = 30; si->scaleMin = 0.8f; si->scaleMax = 1.2f; si->seed = 42;
        si->align = SplineInstancer::Align::Curve; si->rotationOffset = {0, 90, 0}; si->maxCount = 77;
        auto* lr = s.CreateGameObject("Beam")->AddComponent<LineRenderer>();
        lr->positions = {{0, 1, 2}, {3.25f, -4, 5}}; lr->useWorldSpace = false; lr->loop = true; lr->width = 0.3f;
        lr->color = Color{1, 0.5f, 0.25f, 0.75f}; lr->cornerVertices = 2; lr->capVertices = 4;
        lr->alignment = LineAlignment::Flat; lr->textureMode = LineTextureMode::Tile; lr->tileLength = 0.5f;
        lr->texture = "Assets/beam.png"; lr->spline = "Track"; lr->splineSamples = 40;
        lr->widthCurve.SetLinear(1.0f, 0.25f); lr->widthCurve.smooth = true;
        lr->colorGradient.SetTwo(Color{1, 1, 0, 1}, Color{1, 0, 0, 0});
        auto* tr = s.CreateGameObject("Comet")->AddComponent<TrailRenderer>();
        tr->time = 2.5f; tr->minVertexDistance = 0.2f; tr->width = 0.8f; tr->color = Color{0.5f, 1, 1, 1};
        tr->emitting = false; tr->alignment = LineAlignment::Flat; tr->textureMode = LineTextureMode::Tile;
        tr->tileLength = 3.0f; tr->texture = "Assets/trail.png"; tr->teleportDistance = 0.0f;
        tr->widthCurve.Clear(); tr->widthCurve.Add(0.0f, 2.0f); tr->widthCurve.Add(0.5f, 1.0f); tr->widthCurve.Add(1.0f, 0.0f);
        tr->colorGradient.Clear();

        const std::string text = SceneSerializer::Serialize(s);
        Scene r("R");
        std::string err;
        CHECK(SceneSerializer::Deserialize(r, text, &err));
        CHECK(err.empty());
        auto* rsp = r.Find("Track") ? r.Find("Track")->GetComponent<Spline>() : nullptr;
        CHECK(rsp != nullptr);
        if (rsp) {
            CHECK(rsp->type == Spline::Type::CatmullRom && rsp->closed && rsp->upMode == Spline::UpMode::World && rsp->resolution == 24);
            CHECK(rsp->Count() == 3);
            const SplinePoint& p = rsp->points[0];
            CHECK(Near(p.position, {1.5f, 0, -2}, 1e-6f) && Near(p.tangentIn, {-1, 0.5f, 0}, 1e-6f) && Near(p.tangentOut, {2, 0, 1}, 1e-6f));
            CHECK(p.mode == SplinePoint::Mode::Free && p.roll == 15.0f && p.scale.x == 2.0f && p.scale.y == 0.5f);
            CHECK(rsp->points[1].mode == SplinePoint::Mode::Aligned && rsp->points[2].mode == SplinePoint::Mode::Linear);
            CHECK_NEAR(rsp->Length(), sp->Length(), 1e-4f);
        }
        auto* rsf = r.Find("Cart") ? r.Find("Cart")->GetComponent<SplineFollower>() : nullptr;
        CHECK(rsf && rsf->spline == "Track" && rsf->drive == SplineFollower::Drive::Time && rsf->wrap == SplineFollower::Wrap::PingPong);
        if (rsf) CHECK(rsf->speed == 3.5f && rsf->duration == 7.0f && rsf->startAt == 0.25f && !rsf->playOnStart && !rsf->orient &&
                       Near(rsf->offset, {0.5f, 1, -2}, 1e-6f));
        auto* rsm = rsp ? rsp->gameObject->GetComponent<SplineMesh>() : nullptr;
        CHECK(rsm != nullptr);
        if (rsm) {
            CHECK(rsm->mode == SplineMesh::Mode::Repeat && rsm->profile == SplineMesh::Profile::Custom && rsm->closedProfile);
            CHECK(rsm->customProfile.size() == 3 && rsm->customProfile[1].y == 1.5f);
            CHECK(rsm->width == 3 && rsm->height == 0.5f && rsm->sides == 9 && rsm->segmentLength == 0.75f && rsm->uvTileLength == 2.0f);
            CHECK(rsm->sourceMesh == "Fence" && rsm->meshAxis == SplineMesh::Axis::Z && rsm->spacing == 0.25f);
            CHECK(Near(rsm->meshScale, {1, 2, 3}, 1e-6f) && !rsm->stretchToFit && rsm->generateCollider);
        }
        auto* rsi = r.Find("Trees") ? r.Find("Trees")->GetComponent<SplineInstancer>() : nullptr;
        CHECK(rsi != nullptr);
        if (rsi) {
            CHECK(rsi->spline == "Track" && rsi->templateName == "Lamp" && rsi->prefabPath == "Assets/tree.okayprefab" && rsi->meshName == "Pine");
            CHECK(rsi->spacing == 6 && rsi->startOffset == 1 && rsi->sideOffset == -2 && rsi->bothSides && rsi->jitter == 0.5f);
            CHECK(rsi->rotationJitter == 30 && rsi->scaleMin == 0.8f && rsi->scaleMax == 1.2f && rsi->seed == 42u);
            CHECK(rsi->align == SplineInstancer::Align::Curve && Near(rsi->rotationOffset, {0, 90, 0}, 1e-6f) && rsi->maxCount == 77);
        }
        auto* rlr = r.Find("Beam") ? r.Find("Beam")->GetComponent<LineRenderer>() : nullptr;
        CHECK(rlr != nullptr);
        if (rlr) {
            CHECK(rlr->positions.size() == 2 && Near(rlr->positions[1], {3.25f, -4, 5}, 1e-6f));
            CHECK(!rlr->useWorldSpace && rlr->loop && rlr->width == 0.3f && rlr->color.g == 0.5f && rlr->color.a == 0.75f);
            CHECK(rlr->cornerVertices == 2 && rlr->capVertices == 4 && rlr->alignment == LineAlignment::Flat);
            CHECK(rlr->textureMode == LineTextureMode::Tile && rlr->tileLength == 0.5f && rlr->texture == "Assets/beam.png");
            CHECK(rlr->spline == "Track" && rlr->splineSamples == 40);
            CHECK(rlr->widthCurve.count == 2 && rlr->widthCurve.smooth && rlr->widthCurve.keys[1].value == 0.25f);
            CHECK(rlr->colorGradient.count == 2 && rlr->colorGradient.stops[1].color.a == 0.0f && rlr->colorGradient.stops[0].color.g == 1.0f);
        }
        auto* rtr = r.Find("Comet") ? r.Find("Comet")->GetComponent<TrailRenderer>() : nullptr;
        CHECK(rtr != nullptr);
        if (rtr) {
            CHECK(rtr->time == 2.5f && rtr->minVertexDistance == 0.2f && rtr->width == 0.8f && rtr->color.r == 0.5f);
            CHECK(!rtr->emitting && rtr->alignment == LineAlignment::Flat && rtr->textureMode == LineTextureMode::Tile);
            CHECK(rtr->tileLength == 3.0f && rtr->texture == "Assets/trail.png" && rtr->teleportDistance == 0.0f);
            CHECK(rtr->widthCurve.count == 3 && rtr->widthCurve.keys[0].value == 2.0f);
            CHECK(!rtr->colorGradient.Active());   // cleared stays cleared (not the default fade)
        }
        CHECK(SceneSerializer::Serialize(r) == text);       // stable
        // Build Game ships the files they point at.
        const std::vector<std::string> assets = SceneSerializer::CollectAssetPaths(s);
        for (const char* f : {"Assets/beam.png", "Assets/trail.png", "Assets/tree.okayprefab"})
            CHECK(std::find(assets.begin(), assets.end(), std::string(f)) != assets.end());

        // A scene from before these records existed: loads unchanged.
        std::string older;
        {
            std::istringstream in(text); std::string line;
            const char* recs[] = {"  spline", "  linerenderer", "  trailrenderer"};
            while (std::getline(in, line)) {
                bool drop = false;
                for (const char* rec : recs) drop = drop || line.rfind(rec, 0) == 0;
                if (!drop) older += line + "\n";
            }
        }
        Scene o("O");
        CHECK(SceneSerializer::Deserialize(o, older, &err));
        CHECK(o.Find("Track") && !o.Find("Track")->GetComponent<Spline>() && !o.Find("Track")->GetComponent<SplineMesh>());
        CHECK(o.Find("Beam") && !o.Find("Beam")->GetComponent<LineRenderer>());
        CHECK(SceneSerializer::Serialize(o) == older);
        // A starter scene saved before: byte-for-byte the same after a load and save.
        Scene t("T");
        Templates::ThirdPerson(t);
        const std::string tp = SceneSerializer::Serialize(t);
        Scene t2("T2");
        CHECK(SceneSerializer::Deserialize(t2, tp, &err));
        CHECK(SceneSerializer::Serialize(t2) == tp);
    }

    // ---- OkayScript builtins ----
    {
        Scene s;
        Straight(s, "Rail", {0, 0, 0}, {10, 0, 0});
        GameObject* cart = s.CreateGameObject("Cart");
        auto* f = cart->AddComponent<SplineFollower>();
        f->spline = "Rail";
        GameObject* hero = s.CreateGameObject("Hero");
        auto* lr = hero->AddComponent<LineRenderer>();
        auto* tr = hero->AddComponent<TrailRenderer>();
        auto* sc = hero->AddComponent<ScriptComponent>("okayscript");
        std::string err;
        CHECK(sc->LoadSource(
            "var len = 0\nvar mid = vec3(0, 0, 0)\nvar n = 0\nvar ok = 0\n"
            "function go() {\n"
            "    len = spline_length(\"Rail\")\n"
            "    mid = spline_point(\"Rail\", 0.25)\n"
            "    ok = follower_set_speed(\"Cart\", 7)\n"
            "    n = line_set_points([vec3(0, 0, 0), vec3(1, 2, 3), vec3(4, 5, 6)])\n"
            "    trail_clear()\n"
            "}\n"
            "function two() { n = line_set_points(vec3(1, 1, 1), vec3(2, 2, 2)) }\n", &err));
        s.Start();
        for (int i = 0; i < 5; ++i) { hero->transform->localPosition = {(float)i, 0, 0}; tr->Update(0.01f); }
        CHECK(tr->PointCount() == 5);
        sc->VM()->CallEvent("go");
        CHECK_NEAR(G(sc, "len"), 10.0f, 1e-4f);
        CHECK(Near(sc->VM()->GetGlobal("mid").AsVec3(), {2.5f, 0, 0}, 1e-4f));
        CHECK(G(sc, "ok") == 1.0f && f->speed == 7.0f);
        CHECK(G(sc, "n") == 3.0f && lr->positions.size() == 3 && Near(lr->positions[2], {4, 5, 6}, 1e-6f));
        CHECK(tr->PointCount() == 0);
        sc->VM()->CallEvent("two");
        CHECK(lr->positions.size() == 2 && Near(lr->positions[1], {2, 2, 2}, 1e-6f));
    }

    // ---- C#-style types ----
    {
        Scene s;
        Straight(s, "Rail", {0, 0, 0}, {10, 0, 0});
        GameObject* cart = s.CreateGameObject("Cart");
        auto* f = cart->AddComponent<SplineFollower>();
        f->spline = "Rail";
        GameObject* hero = s.CreateGameObject("Hero");
        auto* lr = hero->AddComponent<LineRenderer>();
        auto* tr = hero->AddComponent<TrailRenderer>();
        auto* sc = hero->AddComponent<ScriptComponent>(NextScriptVM::kLanguage);
        std::string err;
        CHECK(sc->LoadSource(
            "class T : Behaviour {\n"
            "    public float len; public Vector3 mid; public Vector3 dir; public Vector3 near; public int count;\n"
            "    public bool closed; public float dist; public bool playing; public int lineCount; public float w;\n"
            "    void Start() {\n"
            "        Spline s = GameObject.Find(\"Rail\").GetComponent<Spline>();\n"
            "        len = s.Length(); mid = s.EvaluatePosition(0.5f); dir = s.EvaluateTangent(0.5f);\n"
            "        near = s.NearestPoint(new Vector3(3, 4, 0)); count = s.pointCount; closed = s.closed;\n"
            "        SplineFollower f = GameObject.Find(\"Cart\").GetComponent<SplineFollower>();\n"
            "        f.speed = 3; f.distance = 2.5f; f.Stop(); dist = f.distance; playing = f.isPlaying;\n"
            "        LineRenderer l = GetComponent<LineRenderer>();\n"
            "        l.SetPositions(new Vector3[] { new Vector3(0, 0, 0), new Vector3(1, 0, 0) });\n"
            "        l.SetPosition(2, new Vector3(1, 2, 3));\n"
            "        lineCount = l.positionCount; l.width = 0.25f; w = l.width;\n"
            "        List<Vector3> pts = new List<Vector3>(); pts.Add(new Vector3(5, 5, 5));\n"
            "        TrailRenderer t = GetComponent<TrailRenderer>(); t.emitting = false; t.Clear();\n"
            "    }\n"
            "    public void UseList() { List<Vector3> pts = new List<Vector3>(); pts.Add(new Vector3(5, 5, 5)); GetComponent<LineRenderer>().SetPositions(pts); }\n"
            "}\n", &err));
        if (!err.empty()) std::printf("  script error: %s\n", err.c_str());
        s.Start();
        auto V = [&](const char* n) { return sc->VM()->GetGlobal(n); };
        CHECK_NEAR(V("len").AsFloat(), 10.0f, 1e-4f);
        CHECK(Near(V("mid").AsVec3(), {5, 0, 0}, 1e-4f) && Near(V("dir").AsVec3(), {1, 0, 0}, 1e-4f));
        CHECK(Near(V("near").AsVec3(), {3, 0, 0}, 1e-3f));
        CHECK(V("count").AsFloat() == 2.0f && !V("closed").AsBool());
        CHECK(f->speed == 3.0f && !f->IsPlaying() && !V("playing").AsBool());
        CHECK_NEAR(V("dist").AsFloat(), 2.5f, 1e-4f);
        CHECK(Near(cart->transform->Position(), {2.5f, 0, 0}, 1e-4f));
        CHECK(V("lineCount").AsFloat() == 3.0f && lr->positions.size() == 3 && Near(lr->positions[2], {1, 2, 3}, 1e-6f));
        CHECK_NEAR(V("w").AsFloat(), 0.25f, 1e-6f);
        CHECK(!tr->emitting && tr->PointCount() == 0);
        sc->VM()->CallEvent("UseList");
        CHECK(lr->positions.size() == 1 && Near(lr->positions[0], {5, 5, 5}, 1e-6f));
    }
    // A C#-style script hears the end in a public OnSplineEnd().
    {
        Scene s;
        Straight(s, "Rail", {0, 0, 0}, {1, 0, 0});
        GameObject* cart = s.CreateGameObject("Cart");
        auto* f = cart->AddComponent<SplineFollower>();
        f->spline = "Rail"; f->speed = 2.0f; f->wrap = SplineFollower::Wrap::Once; f->orient = false;
        auto* sc = cart->AddComponent<ScriptComponent>(NextScriptVM::kLanguage);
        std::string err;
        CHECK(sc->LoadSource("class E : Behaviour {\n    public int ends;\n    public void OnSplineEnd() { ends += 1; }\n}\n", &err));
        s.Start();
        for (int i = 0; i < 60; ++i) s.Update(1.0f / 60.0f);
        CHECK(sc->VM()->GetGlobal("ends").AsFloat() == 1.0f && !f->IsPlaying());
    }

    // ---- Visual-script nodes ----
    {
        Scene s;
        Straight(s, "Rail", {0, 0, 0}, {10, 0, 0});
        GameObject* hero = s.CreateGameObject("Hero");
        auto* lr = hero->AddComponent<LineRenderer>();
        auto* tr = hero->AddComponent<TrailRenderer>();
        auto* vs = hero->AddComponent<VisualScriptComponent>();
        const std::string graph =
            "node 1 OnStart\n"
            "node 2 GetSplinePoint\n"
            "node 3 Const 0.75\n"
            "node 4 Const \"Rail\"\n"
            "node 5 SetVar p\n"
            "node 6 SplineLength\n"
            "node 7 SetVar len\n"
            "node 8 NewList\n"
            "node 9 ListAdd\n"
            "node 10 SetLinePoints\n"
            "node 11 ClearTrail\n"
            "node 12 SetVar pts\n"
            "node 13 GetVar pts\n"
            "exec 1 0 5\nexec 5 0 7\nexec 7 0 12\nexec 12 0 9\nexec 9 0 10\nexec 10 0 11\n"
            "data 2 0 3 0\ndata 2 1 4 0\ndata 5 0 2 0\n"
            "data 6 0 4 0\ndata 7 0 6 0\n"
            "data 12 0 8 0\ndata 9 0 13 0\ndata 9 1 2 0\ndata 10 0 13 0\n"
            "entry OnStart 1\n";
        std::string err;
        CHECK(vs->LoadFromText(graph, &err));
        if (!err.empty()) std::printf("  graph error: %s\n", err.c_str());
        for (int i = 0; i < 3; ++i) { hero->transform->localPosition = {(float)i, 0, 0}; tr->Update(0.01f); }
        s.Start();
        CHECK(Near(vs->GetVariable("p").AsVec3(), {7.5f, 0, 0}, 1e-4f));
        CHECK_NEAR(vs->GetVariable("len").AsFloat(), 10.0f, 1e-4f);
        CHECK(lr->positions.size() == 1 && Near(lr->positions[0], {7.5f, 0, 0}, 1e-4f));
        CHECK(tr->PointCount() == 0);
        for (const char* t : {"GetSplinePoint", "SplineLength", "SetLinePoints", "ClearTrail"}) {
            CHECK(vs::MakeNode(t, {}, &err) != nullptr);
            CHECK(vs::SignatureFor(t) != nullptr);
        }
        CHECK(vs::SignatureFor("GetSplinePoint")->ins == "NO" && vs::SignatureFor("SetLinePoints")->ins == "LO");
    }

    TEST_MAIN_RESULT();
}
