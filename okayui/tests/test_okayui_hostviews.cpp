// Window mode: the Scene and Game views (editor/OkayUIHostViews.cpp over editor/SceneViewCore.hpp) with a real okay::Scene, run through the real frame loop with a
// fake backend and a software painter. Docs 6.24 step 6. The scene is rendered by the engine's software renderer into a painter texture shown in a docked window.
#define OKAY_HAVE_OKAYUI 1
#define OKAYUI_HOST_PLATFORM 1
#include "test_framework.hpp"
#include "OkayUITestKit.hpp"
#include "OkayUIFrameLoop.hpp"
#include "OkayUIHostViews.hpp"
#include "SceneViewCore.hpp"
#include "FakePlatformBackend.hpp"
#include "okay/UI/OkayUIDock.hpp"

#include <cstdio>
#include <cstdlib>
#include <vector>

namespace UI = OkayUI;
namespace plat = okay::platform;
using fakeplat::FakeBackend; using fakeplat::Move; using fakeplat::Btn;

int main() {
    RUN_SUITE("okayui window mode: the Scene and Game views");
    okay::Scene scene("Views");
    okay::GameObject* cube = scene.CreateGameObject("Cube");
    cube->AddComponent<okay::MeshRenderer>()->mesh = okay::Mesh::Cube(2.0f);
    okay::GameObject* camGo = scene.CreateGameObject("Camera");
    camGo->transform->SetPosition({0, 0, 6});
    scene.mainCamera = camGo->AddComponent<okay::Camera>();
    scene.Start(); scene.Update(0.0f);

    float yaw = 35, pitch = 22, dist = 8; okay::Vec3 target{0, 0, 0};
    okayuitest::Reset();
    auto painter = UI::soft::CreateBitmapPainter(900, 600);
    FakeBackend be; okayuihost::platform::FrameLoop loop(be, *painter);
    oked::HostModeState st;
    oked::wm::ViewSource src; src.scene = [&] { return &scene; }; src.yaw = &yaw; src.pitch = &pitch; src.dist = &dist; src.target = &target;
    bool twoD = false; okay::Vec2 cam2pos{0, 0}; float cam2zoom = 12.0f;
    src.is2D = [&] { return twoD; }; src.cam2dPos = &cam2pos; src.cam2dZoom = &cam2zoom;
    okay::GameObject* selected = nullptr; int edits = 0, edited = 0; std::string lastEdit;
    src.selected = [&] { return selected; };
    src.select = [&](okay::GameObject* g, bool) { selected = g; };
    src.beginEdit = [&](const char* w) { ++edits; lastEdit = w; };
    src.edited = [&] { ++edited; };
    oked::wm::AddSceneViews(st.panels, &st, painter.get(), src);
    CHECK(st.panels.scene && st.panels.game);
    auto frame = [&] { be.now += 16; return loop.Frame([&] { oked::DrawHostModeUi(st); }); };
    for (int i = 0; i < 5; ++i) CHECK(frame());

    const UI::DockLayout& L = UI::GetDockLayout();
    std::string why; CHECK(L.Validate(&why));
    CHECK(L.Contains(oked::kPanelScene) && L.Contains(oked::kPanelGame) && L.NodeOf(oked::kPanelScene) == L.NodeOf(oked::kPanelGame));

    // the Scene tab is the active one: it drew a picture of the cube
    const oked::wm::ViewProbe& sp = oked::wm::GetViewProbe(false);
    CHECK(sp.frames > 0 && std::string(sp.note).empty());
    CHECK(sp.image.w > 300.0f && sp.image.h > 200.0f && sp.texW == (int)sp.image.w && sp.texH == (int)sp.image.h);
    CHECK(sp.litPixels > 1000 && sp.litPixels < sp.texW * sp.texH);
    const UI::DrawData dd = UI::GetDrawData();
    CHECK(dd.dropped == 0);
    int textured = 0; for (int i = 0; i < dd.cmdCount; ++i) if (dd.cmds[i].texture && dd.cmds[i].texture != dd.atlasTexture) ++textured;
    CHECK(textured >= 1);   // the picture goes out as a painter texture
    // the cube is in the picture: sample the window pixels in the middle of the image area
    {
        std::vector<std::uint8_t> px; int w = 0, h = 0;
        CHECK(painter->ReadPixels(px, w, h) && w == 900 && h == 600);
        auto at = [&](float x, float y) { const std::size_t o = ((std::size_t)y * w + (std::size_t)x) * 4; return std::array<int, 3>{px[o], px[o + 1], px[o + 2]}; };
        const auto mid = at(sp.image.x + sp.image.w * 0.5f, sp.image.y + sp.image.h * 0.5f);
        const auto corner = at(sp.image.x + 3.0f, sp.image.y + 3.0f);
        CHECK(mid != corner);                                            // the cube differs from the backdrop
        CHECK(corner[0] == 42 && corner[1] == 45 && corner[2] == 51);    // the backdrop colour
        if (const char* shot = std::getenv("OKAYUI_SHOT")) okay::ImageWrite::WritePNG(shot, w, h, 4, px.data());
    }

    // orbit: a left drag that starts over the picture turns the camera; the wheel zooms
    const float y0 = yaw, p0 = pitch, d0 = dist;
    const int cx = (int)(sp.image.x + sp.image.w * 0.5f), cy = (int)(sp.image.y + sp.image.h * 0.5f);
    be.queue.push_back(Move(cx, cy)); frame();
    be.queue.push_back(Btn(true, cx, cy)); frame();
    be.queue.push_back(Move(cx + 50, cy + 20)); frame(); frame();
    be.queue.push_back(Btn(false, cx + 50, cy + 20)); frame();
    CHECK(yaw < y0 - 10.0f && pitch > p0 + 3.0f);
    { plat::Event e; e.type = plat::EventType::MouseWheel; e.wheelY = 2.0f; e.x = cx; e.y = cy; be.queue.push_back(e); }
    frame(); frame();
    CHECK(dist < d0);
    // a drag that starts outside the picture does nothing
    const float y1 = yaw; be.queue.push_back(Move(2, 590)); frame(); be.queue.push_back(Btn(true, 2, 590)); frame(); be.queue.push_back(Move(60, 590)); frame(); be.queue.push_back(Btn(false, 60, 590)); frame();
    CHECK(yaw == y1);

    // the Game view through the main camera: bring its tab forward
    for (const UI::DockTabInfo& t : UI::GetDockTabs()) if (t.window == oked::kPanelGame) {
        const int tx = (int)(t.rect.x + t.rect.w * 0.4f), ty = (int)(t.rect.y + t.rect.h * 0.5f);
        be.queue.push_back(Move(tx, ty)); frame(); be.queue.push_back(Btn(true, tx, ty)); frame(); be.queue.push_back(Btn(false, tx, ty)); frame(); frame();
    }
    const oked::wm::ViewProbe& gp = oked::wm::GetViewProbe(true);
    CHECK(gp.frames > 0 && std::string(gp.note).empty() && gp.litPixels > 500);
    // no camera: the Game view says so instead of a picture
    scene.mainCamera = nullptr; camGo->GetComponent<okay::Camera>()->enabled = false;
    for (int i = 0; i < 3; ++i) frame();
    CHECK(std::string(gp.note) == "no camera");

    // ---- round 11: grid, picking, outline, gizmos ----
    for (const UI::DockTabInfo& t : UI::GetDockTabs()) if (t.window == oked::kPanelScene) {   // back to the Scene tab
        const int tx = (int)(t.rect.x + t.rect.w * 0.4f), ty = (int)(t.rect.y + t.rect.h * 0.5f);
        be.queue.push_back(Move(tx, ty)); frame(); be.queue.push_back(Btn(true, tx, ty)); frame(); be.queue.push_back(Btn(false, tx, ty)); frame(); frame();
    }
    scene.mainCamera = camGo->GetComponent<okay::Camera>(); camGo->GetComponent<okay::Camera>()->enabled = true;
    yaw = 35; pitch = 22; dist = 8; target = {0, 0, 0};
    for (int i = 0; i < 3; ++i) frame();
    const oked::wm::ViewProbe& sp2 = oked::wm::GetViewProbe(false);
    CHECK(sp2.gridLines > 20 && sp2.outlineEdges == 0 && sp2.gizmoAxes == 0);   // a grid, nothing selected
    // click the cube: it is picked and gets an outline and three gizmo handles
    {
        const int px = (int)(sp2.image.x + sp2.image.w * 0.5f), py = (int)(sp2.image.y + sp2.image.h * 0.5f);
        be.queue.push_back(Move(px, py)); frame(); be.queue.push_back(Btn(true, px, py)); frame(); be.queue.push_back(Btn(false, px, py)); frame(); frame();
        CHECK(selected == cube && oked::wm::GetViewProbe(false).picked == cube);
        CHECK(oked::wm::GetViewProbe(false).outlineEdges == 12 && oked::wm::GetViewProbe(false).gizmoAxes == 3);
        if (const char* shot = std::getenv("OKAYUI_SHOT2")) { std::vector<std::uint8_t> px2; int w = 0, h = 0; if (painter->ReadPixels(px2, w, h)) okay::ImageWrite::WritePNG(shot, w, h, 4, px2.data()); }
        // click on empty space clears the selection
        const int ex = (int)(sp2.image.x + 8), ey = (int)(sp2.image.y + 8);
        be.queue.push_back(Move(ex, ey)); frame(); be.queue.push_back(Btn(true, ex, ey)); frame(); be.queue.push_back(Btn(false, ex, ey)); frame(); frame();
        CHECK(selected == nullptr);
        selected = cube; frame(); frame();
    }
    // Move gizmo: drag the X handle to the right: the cube moves along +X only, one undo step, then the same for Scale and Rotate
    auto dragHandle = [&](int axis, int dx, int dy) {
        const oked::wm::ViewProbe& q = oked::wm::GetViewProbe(false);
        const int hx = (int)(q.gizmoHandle[axis].x + 5), hy = (int)(q.gizmoHandle[axis].y + 5);
        be.queue.push_back(Move(hx, hy)); frame(); be.queue.push_back(Btn(true, hx, hy)); frame();
        be.queue.push_back(Move(hx + dx / 2, hy + dy / 2)); frame(); be.queue.push_back(Move(hx + dx, hy + dy)); frame(); frame();
        be.queue.push_back(Btn(false, hx + dx, hy + dy)); frame(); frame();
    };
    cube->transform->SetPosition({0, 0, 0}); cube->transform->localScale = {1, 1, 1};
    const int edits0 = edits;
    dragHandle(0, 60, 0);
    CHECK(edits == edits0 + 1 && lastEdit == "Move" && edited > 0);
    CHECK(cube->transform->Position().x > 0.3f && std::fabs(cube->transform->Position().y) < 1e-3f && std::fabs(cube->transform->Position().z) < 1e-3f);
    CHECK(!oked::wm::GetViewProbe(false).gizmoDragging && selected == cube);   // released: not dragging, and the cube stays selected
    // Scale
    {
        const oked::wm::ViewProbe& q = oked::wm::GetViewProbe(false);
        const int bx = (int)(q.modeButton[2].x + q.modeButton[2].w / 2), by = (int)(q.modeButton[2].y + q.modeButton[2].h / 2);
        be.queue.push_back(Move(bx, by)); frame(); be.queue.push_back(Btn(true, bx, by)); frame(); be.queue.push_back(Btn(false, bx, by)); frame(); frame();
        CHECK(q.mode == oked::wm::GizmoMode::Scale);
    }
    cube->transform->SetPosition({0, 0, 0}); frame(); frame();
    dragHandle(1, 0, -50);   // up the Y handle: taller
    CHECK(lastEdit == "Scale" && cube->transform->localScale.y > 1.2f && std::fabs(cube->transform->localScale.x - 1.0f) < 1e-3f);
    // Rotate
    {
        const oked::wm::ViewProbe& q = oked::wm::GetViewProbe(false);
        const int bx = (int)(q.modeButton[1].x + q.modeButton[1].w / 2), by = (int)(q.modeButton[1].y + q.modeButton[1].h / 2);
        be.queue.push_back(Move(bx, by)); frame(); be.queue.push_back(Btn(true, bx, by)); frame(); be.queue.push_back(Btn(false, bx, by)); frame(); frame();
        CHECK(q.mode == oked::wm::GizmoMode::Rotate);
    }
    const okay::Quat r0 = cube->transform->Rotation();
    dragHandle(1, 60, 0);
    CHECK(lastEdit == "Rotate" && std::fabs(cube->transform->Rotation().y - r0.y) > 0.01f);
    // the grid can be switched off
    {
        const oked::wm::ViewProbe& q = oked::wm::GetViewProbe(false);
        const int gx = (int)(q.gridCheck.x + 6), gy = (int)(q.gridCheck.y + q.gridCheck.h / 2);
        be.queue.push_back(Move(gx, gy)); frame(); be.queue.push_back(Btn(true, gx, gy)); frame(); be.queue.push_back(Btn(false, gx, gy)); frame(); frame();
        CHECK(q.gridLines == 0);
    }

    // ---- round 12: the 2D view ----
    {
        okay::GameObject* sprite = scene.CreateGameObject("Sprite1"); auto* sr = sprite->AddComponent<okay::SpriteRenderer>(); sr->size = {3, 3}; sr->color = okay::Color(0.9f, 0.3f, 0.2f, 1.0f);
        okay::GameObject* sprite2 = scene.CreateGameObject("Sprite2"); auto* sr2 = sprite2->AddComponent<okay::SpriteRenderer>(); sr2->size = {2, 2}; sr2->sortOrder = 3; sprite2->transform->SetPosition({-4, 0, 0});
        twoD = true; selected = nullptr;
        for (int i = 0; i < 3; ++i) frame();
        const oked::wm::ViewProbe& q = oked::wm::GetViewProbe(false);
        { const int gx = (int)(q.gridCheck.x + 6), gy = (int)(q.gridCheck.y + q.gridCheck.h / 2); be.queue.push_back(Move(gx, gy)); frame(); be.queue.push_back(Btn(true, gx, gy)); frame(); be.queue.push_back(Btn(false, gx, gy)); frame(); frame(); }   // the grid was switched off above: on again
        CHECK(q.view2D && q.spritesDrawn == 2 && q.gridLines > 10 && q.litPixels > 1000);
        if (const char* shot = std::getenv("OKAYUI_SHOT3")) { std::vector<std::uint8_t> px2; int w = 0, h = 0; if (painter->ReadPixels(px2, w, h)) okay::ImageWrite::WritePNG(shot, w, h, 4, px2.data()); }
        // click the middle: Sprite1 is picked (not the cube: the 2D view picks sprites), outline = 4 edges
        const int px = (int)(q.image.x + q.image.w * 0.5f), py = (int)(q.image.y + q.image.h * 0.5f);
        be.queue.push_back(Move(px, py)); frame(); be.queue.push_back(Btn(true, px, py)); frame(); be.queue.push_back(Btn(false, px, py)); frame(); frame();
        CHECK(selected == sprite && q.outlineEdges == 4);
        const int ex = (int)(q.image.x + q.image.w - 10), ey = (int)(q.image.y + q.image.h - 10);
        be.queue.push_back(Move(ex, ey)); frame(); be.queue.push_back(Btn(true, ex, ey)); frame(); be.queue.push_back(Btn(false, ex, ey)); frame(); frame();
        CHECK(selected == nullptr);
        // the wheel zooms, a right drag pans (the world moves with the pointer)
        const float z0 = cam2zoom;
        { plat::Event e; e.type = plat::EventType::MouseWheel; e.wheelY = 2.0f; e.x = px; e.y = py; be.queue.push_back(e); }
        frame(); frame();
        CHECK(cam2zoom < z0);
        const float cx0 = cam2pos.x;
        { plat::Event e; e.type = plat::EventType::MouseMove; e.x = px; e.y = py; be.queue.push_back(e); }
        frame();
        { plat::Event e; e.type = plat::EventType::MouseButtonDown; e.button = plat::MouseRight; e.x = px; e.y = py; be.queue.push_back(e); }
        frame();
        { plat::Event e; e.type = plat::EventType::MouseMove; e.x = px + 60; e.y = py; be.queue.push_back(e); }
        frame(); frame();
        { plat::Event e; e.type = plat::EventType::MouseButtonUp; e.button = plat::MouseRight; e.x = px + 60; e.y = py; be.queue.push_back(e); }
        frame();
        CHECK(cam2pos.x < cx0 - 0.5f);   // dragged right: the camera moves left
        twoD = false; for (int i = 0; i < 2; ++i) frame();
        CHECK(!q.view2D);
    }
    TEST_MAIN_RESULT();
}
