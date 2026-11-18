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
    TEST_MAIN_RESULT();
}
