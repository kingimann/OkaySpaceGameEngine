// Pictures of the Visual Script window with no window on screen: load the example graphs, play them, and save a picture of each with
// the live run lit, drawn by the same Visual Script app the editor's window shows (shared/VisualScriptApp.hpp) through OkayUI's
// software painter.
//   OkayEngine --vs-shot <outdir> [graph-directory]
#include "VisualScriptApp.hpp"
#include "VisualScriptModel.hpp"
#include "okay/Platform/BmpWriter.hpp"
#include "okay/UI/OkayUI.hpp"
#if __has_include("OkayUI_SoftPainter.hpp")   // the software painter is there when the editor links okay_ui_soft
#include "OkayUI_SoftPainter.hpp"
#define OKAY_VS_SHOT_PAINTER 1
#endif

#include <cstdio>
#include <filesystem>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

namespace fs = std::filesystem;

int RunVsGraphShot(int argc, char** argv) {
    std::string outDir = "vs-shots";
    std::string graphDir = "examples/visual_script";
    bool saw = false;
    for (int i = 1; i < argc; ++i) {
        if (std::string(argv[i]) != "--vs-shot") continue;
        saw = true;
        if (i + 1 < argc) outDir = argv[++i];
        if (i + 1 < argc && std::string(argv[i + 1]).rfind("--", 0) != 0) graphDir = argv[++i];
    }
    if (!saw) return 2;
#ifndef OKAY_VS_SHOT_PAINTER
    std::cerr << "--vs-shot needs OkayUI's software painter (okay_ui_soft), which this build does not have\n";
    return 1;
#else
    const char* names[] = {"door.okayvs", "patrol.okayvs", "inventory.okayvs", "foreach_break.okayvs"};
    okay::editor::EditorState ed;
    ed.setProjectDir(fs::absolute(graphDir).parent_path().string());
    struct Item { std::string path; std::string stem; okay::GameObject* go; };
    std::vector<Item> items;
    for (const char* name : names) {
        fs::path p = fs::absolute(fs::path(graphDir) / name);
        if (!fs::exists(p)) { std::cerr << "missing " << p << "\n"; return 1; }
        auto* go = ed.scene().CreateGameObject(p.stem().string());
        auto* vc = go->AddComponent<okay::VisualScriptComponent>();
        std::string err;
        if (!vc->LoadFromFile(p.string(), &err)) { std::cerr << err << "\n"; return 1; }
        items.push_back({p.string(), p.stem().string(), go});
    }
    okay::vs::NodeGraph::Tracing() = true;
    ed.Play();
    for (int i = 0; i < 45; ++i) ed.Tick(1.0f / 60.0f);

    vsapp::VisualScriptApp::Config cfg;   // no settings file: the pictures do not depend on what a person chose
    cfg.idPrefix = "##vsshot";
    cfg.editor = [&ed]() { return &ed; };
    const okay::vs::NodeGraph* liveNow = nullptr;
    cfg.liveGraph = [&liveNow](const std::string&) { return liveNow; };
    vsapp::VisualScriptApp app(cfg);
    app.OpenFolder(ed.projectDir());
    const int W = 1680, H = 1000;
    fs::create_directories(outDir);
    for (const Item& it : items) {
        app.OpenFile(it.path);
        ed.Select(it.go);
        auto* vc = it.go->GetComponent<okay::VisualScriptComponent>();
        liveNow = vc ? vc->Graph() : nullptr;
        app.FrameAll();
        for (int f = 0; f < 4; ++f) {   // the canvas fits the graph once its blocks have been drawn
            OkayUI::Input in;
            in.mouseX = 2; in.mouseY = (float)H - 2; in.displayW = (float)W; in.displayH = (float)H;
            OkayUI::BeginFrame(in);
            app.Frame(0, 0, (float)W, (float)H);
            OkayUI::EndFrameData();
        }
        std::unique_ptr<okay::platform::Painter> p = OkayUI::soft::CreateBitmapPainter(W, H);
        if (!p) { std::cerr << "no software painter\n"; return 1; }
        p->SetClipRect(nullptr);
        p->SetBlendMode(okay::platform::BlendMode::None);
        p->SetDrawColor(24, 26, 32, 255);
        p->Clear();
        { OkayUI::soft::FrameDrawer fd(*p); fd.Draw(OkayUI::GetDrawData()); }
        std::vector<std::uint8_t> px;
        int w = 0, h = 0;
        const std::string bmp = (fs::path(outDir) / (it.stem + ".bmp")).string();
        if (p->ReadPixels(px, w, h) && w > 0 && h > 0 && okay::platform::WriteBmp32(bmp.c_str(), px.data(), w, h)) std::cout << "wrote " << bmp << "\n";
        else std::cerr << "save " << bmp << ": cannot write the file\n";
    }
    return 0;
#endif
}
