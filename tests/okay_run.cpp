// okay_run: run a scene the way the editor's Play mode runs it (EditorState::Play + Tick, the same Scene::Update path the
// player uses), for N frames at a fixed time step, with scripted input, and write what happened to a folder:
//   trace.jsonl   one JSON object per frame: chosen objects (position, rotation, velocity, grounded, animator state and
//                 parameters), the main camera (position, rotation, FOV, inside a collider or not), the frame's wall time
//   log.txt       every Console line with its level
//   frame_NNNNN.png  software-rendered pictures at the frames asked for (3D meshes only: no sky, sprites or UI)
//   summary.json  frames run, errors, warnings, frame time, exit status
// It is the engine side of scripts/okay_run.py (which turns a JSON input file into the plain event file read here and
// evaluates check files). NOT a test on its own. Written from scratch for this repository on the engine's public API.
//
//   okay_run --scene FILE --out DIR [--frames N] [--dt S] [--events FILE] [--trace NAME,NAME] [--shots F,F] [--size WxH]
// Event file, one per line, `#` comments:   <frame> <verb> [args]
//   key NAME down|up      a key (Input::KeyFromName names: w, space, shift, up, f5 ...)
//   mouse X Y             pointer to X, Y (UI canvas pixels)
//   look DX DY            pointer moves by DX, DY this frame (mouse look)
//   button N down|up      mouse button 0 left, 1 right, 2 middle
//   stick LX LY [RX RY]   controller 0 sticks, held until changed
//   click NAME            move to the centre of the UI button called NAME, press this frame, release the next
//   shot                  save a picture after this frame
#include <Okay.hpp>
#include "EditorState.hpp"
#include "okay/Testing/Regression.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <vector>

using namespace okay;
namespace fs = std::filesystem;

namespace {

struct Ev { int frame = 0; std::string verb; std::vector<std::string> args; int line = 0; };

std::string Esc(const std::string& s) {
    std::string o;
    for (char c : s) {
        if (c == '"' || c == '\\') { o += '\\'; o += c; }
        else if ((unsigned char)c < 0x20) { char b[8]; std::snprintf(b, sizeof b, "\\u%04x", (unsigned)(unsigned char)c); o += b; }
        else o += c;
    }
    return o;
}
std::string Num(double v) {
    if (!std::isfinite(v)) return "null";
    char b[32]; std::snprintf(b, sizeof b, "%.5g", v); return b;
}
std::string V3(const Vec3& v) { return "[" + Num(v.x) + "," + Num(v.y) + "," + Num(v.z) + "]"; }

std::vector<std::string> Split(const std::string& s, char sep) {
    std::vector<std::string> out; std::string cur;
    for (char c : s) { if (c == sep) { if (!cur.empty()) out.push_back(cur); cur.clear(); } else cur += c; }
    if (!cur.empty()) out.push_back(cur);
    return out;
}

bool ReadEvents(const std::string& path, std::vector<Ev>& out, std::string& err) {
    std::ifstream f(path);
    if (!f) { err = "cannot read events file " + path; return false; }
    std::string line; int n = 0;
    while (std::getline(f, line)) {
        ++n;
        if (auto h = line.find('#'); h != std::string::npos) line.resize(h);
        std::istringstream in(line);
        Ev e; e.line = n;
        if (!(in >> e.frame)) continue;
        if (!(in >> e.verb)) { err = "line " + std::to_string(n) + ": no verb"; return false; }
        if (e.verb == "click") {   // the rest of the line is the name (it may have spaces)
            std::string rest; std::getline(in, rest);
            rest.erase(0, rest.find_first_not_of(' '));
            e.args.push_back(rest);
        } else {
            std::string a; while (in >> a) e.args.push_back(a);
        }
        static const std::set<std::string> verbs = {"key", "mouse", "look", "button", "stick", "click", "shot"};
        if (!verbs.count(e.verb)) { err = "line " + std::to_string(n) + ": unknown verb '" + e.verb + "'"; return false; }
        out.push_back(e);
    }
    return true;
}

// The centre of a screen-space UI button by name: the mean of the canvas points its own hit test accepts (8 px grid).
bool ButtonCentre(Scene& sc, const std::string& name, Vec2& at) {
    for (const auto& up : sc.Objects()) {
        if (up->name != name || !up->ActiveInHierarchy()) continue;
        auto* b = up->GetComponent<UIButton>();
        if (!b) continue;
        double sx = 0, sy = 0; int n = 0;
        for (float y = 0; y < UICanvas::Height(); y += 8)
            for (float x = 0; x < UICanvas::Width(); x += 8)
                if (b->Contains({x, y})) { sx += x; sy += y; ++n; }
        if (n) { at = {(float)(sx / n), (float)(sy / n)}; return true; }
    }
    return false;
}

// True when the point is inside a solid collider's world bounds (box test on the axis-aligned bounds, so a turned box
// counts a little larger than it is), ignoring colliders on `skipRoot` and its children (the camera's own body).
bool InsideCollider(Scene& sc, const Vec3& p, const GameObject* skipRoot, std::string* who) {
    for (Collider3D* c : sc.FindObjectsOfType<Collider3D>()) {
        if (!c->enabled || c->isTrigger || !c->gameObject || !c->gameObject->ActiveInHierarchy()) continue;
        bool mine = false;
        for (Transform* t = c->gameObject->transform; t; t = t->Parent()) if (t->gameObject == skipRoot) { mine = true; break; }
        if (mine) continue;
        Vec3 mn, mx; c->WorldAABB(mn, mx);
        if (p.x > mn.x && p.x < mx.x && p.y > mn.y && p.y < mx.y && p.z > mn.z && p.z < mx.z) { if (who) *who = c->gameObject->name; return true; }
    }
    return false;
}

std::string ObjectJson(GameObject* go) {
    std::string o = "{\"pos\":" + V3(go->transform->Position()) + ",\"rot\":" + V3(go->transform->EulerAngles());
    if (auto* rb = go->GetComponent<Rigidbody3D>()) o += ",\"vel\":" + V3(rb->velocity);
    int g = -1;
    if (auto* pc = go->GetComponent<PlayerController>()) {   // view Both: both controllers exist; ask the one in use
        if (pc->CurrentView() == 1) { if (auto* tp = pc->third()) g = tp->IsGrounded(); }
        else if (auto* fp = go->GetComponent<FirstPersonController>()) g = fp->IsGrounded();
        o += ",\"view\":" + std::to_string(pc->CurrentView());
    }
    else if (auto* fp = go->GetComponent<FirstPersonController>(); fp && fp->enabled) g = fp->IsGrounded();
    else if (auto* tp = go->GetComponent<ThirdPersonController>(); tp && tp->enabled) g = tp->IsGrounded();
    if (g >= 0) o += std::string(",\"grounded\":") + (g ? "true" : "false");
    o += std::string(",\"active\":") + (go->ActiveInHierarchy() ? "true" : "false");
    if (auto* b = go->GetComponent<UIButton>())
        o += std::string(",\"button\":{\"hover\":") + (b->IsHovered() ? "true" : "false") + ",\"pressed\":" + (b->IsPressed() ? "true" : "false") +
             ",\"clicked\":" + (b->WasClicked() ? "true" : "false") + "}";
    auto sms = go->GetComponents<AnimStateMachine>();
    if (!sms.empty()) {
        o += ",\"anim\":[";
        for (size_t i = 0; i < sms.size(); ++i) {
            AnimStateMachine* sm = sms[i];
            o += (i ? "," : "") + std::string("{\"state\":\"") + Esc(sm->Current()) + "\",\"params\":{";
            bool first = true;
            for (const auto& p : sm->params) {
                if (p.type == 2) continue;   // triggers are consumed at once; nothing useful to show
                o += (first ? "" : ",") + std::string("\"") + Esc(p.name) + "\":" + Num(sm->GetFloat(p.name));
                first = false;
            }
            o += "}}";
        }
        o += "]";
    }
    return o + "}";
}

int Usage() {
    std::fprintf(stderr, "usage: okay_run --scene FILE --out DIR [--frames N] [--dt S] [--events FILE] [--trace NAME,NAME] [--shots F,F] [--size WxH]\n");
    return 2;
}

}  // namespace

int main(int argc, char** argv) {
    std::string scenePath, outDir, eventsPath, traceList, shotList;
    int frames = 120, width = 640, height = 360;
    float dt = 1.0f / 60.0f;
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        auto val = [&]() -> std::string { return i + 1 < argc ? argv[++i] : ""; };
        if (a == "--scene") scenePath = val();
        else if (a == "--out") outDir = val();
        else if (a == "--frames") frames = std::atoi(val().c_str());
        else if (a == "--dt") dt = (float)std::atof(val().c_str());
        else if (a == "--events") eventsPath = val();
        else if (a == "--trace") traceList = val();
        else if (a == "--shots") shotList = val();
        else if (a == "--size") { const std::string s = val(); std::sscanf(s.c_str(), "%dx%d", &width, &height); }
        else return Usage();
    }
    if (scenePath.empty() || outDir.empty() || frames < 1 || dt <= 0 || width < 8 || height < 8) return Usage();
    std::error_code ec;
    fs::create_directories(outDir, ec);
    std::ofstream logf(fs::path(outDir) / "log.txt");
    int errors = 0, warnings = 0, frame = -1;
    Log::console = false;
    Log::sink = [&](Log::Level l, const std::string& m) {
        const char* lv = l == Log::Level::Error ? "error" : l == Log::Level::Warning ? "warning" : l == Log::Level::Info ? "info" : "trace";
        if (l == Log::Level::Error) ++errors; else if (l == Log::Level::Warning) ++warnings;
        logf << "[" << frame << "] " << lv << ": " << m << "\n";
        logf.flush();
    };
    auto finish = [&](int code, const std::string& why, int ran, double msMean, double msMax) {
        std::ofstream s(fs::path(outDir) / "summary.json");
        s << "{\"scene\":\"" << Esc(scenePath) << "\",\"frames_run\":" << ran << ",\"dt\":" << Num(dt)
          << ",\"errors\":" << errors << ",\"warnings\":" << warnings << ",\"frame_ms_mean\":" << Num(msMean)
          << ",\"frame_ms_max\":" << Num(msMax) << ",\"fps_sim\":" << Num(msMean > 0 ? 1000.0 / msMean : 0)
          << ",\"exit\":" << code << ",\"why\":\"" << Esc(why) << "\"}\n";
        if (!why.empty()) std::fprintf(stderr, "okay_run: %s\n", why.c_str());
        return code;
    };

    std::vector<Ev> events;
    std::string err;
    if (!eventsPath.empty() && !ReadEvents(eventsPath, events, err)) return finish(2, err, 0, 0, 0);
    std::set<int> shots;
    for (const std::string& s : Split(shotList, ',')) shots.insert(std::atoi(s.c_str()));
    for (const Ev& e : events) if (e.verb == "shot") shots.insert(e.frame);
    const std::vector<std::string> traced = Split(traceList, ',');

    editor::EditorState ed;
    if (!ed.Load(scenePath, &err)) return finish(3, "could not open the scene: " + err, 0, 0, 0);
    Input::FeedKeys({}); Input::FeedMouse({UICanvas::Width() / 2, UICanvas::Height() / 2}, 0);
    ed.Play();
    if (!ed.isPlaying()) return finish(3, "Play did not start: " + ed.PlayBlockedReason(), 0, 0, 0);

    std::ofstream trace(fs::path(outDir) / "trace.jsonl");
    std::set<char> held;
    unsigned buttons = 0;
    Vec2 mouse{UICanvas::Width() / 2, UICanvas::Height() / 2};
    Vec2 lstick{0, 0}, rstick{0, 0};
    bool stickOn = false;
    std::vector<int> releaseButtonAt;   // click: let go on the next frame
    double msSum = 0, msMax = 0;
    int ran = 0;
    for (frame = 0; frame < frames && ed.isPlaying(); ++frame) {
        for (const Ev& e : events) {
            if (e.frame != frame) continue;
            auto arg = [&](size_t k) { return k < e.args.size() ? e.args[k] : std::string(); };
            if (e.verb == "key") {
                const char k = Input::KeyFromName(arg(0));
                if (arg(1) == "up") held.erase(k); else held.insert(k);
            } else if (e.verb == "mouse") mouse = {(float)std::atof(arg(0).c_str()), (float)std::atof(arg(1).c_str())};
            else if (e.verb == "look") { mouse.x += (float)std::atof(arg(0).c_str()); mouse.y += (float)std::atof(arg(1).c_str()); }
            else if (e.verb == "button") {
                const unsigned bit = 1u << std::clamp(std::atoi(arg(0).c_str()), 0, 2);
                if (arg(1) == "up") buttons &= ~bit; else buttons |= bit;
            } else if (e.verb == "stick") {
                lstick = {(float)std::atof(arg(0).c_str()), (float)std::atof(arg(1).c_str())};
                rstick = {(float)std::atof(arg(2).c_str()), (float)std::atof(arg(3).c_str())};
                stickOn = true;
            } else if (e.verb == "click") {
                Vec2 at;
                if (!ButtonCentre(ed.scene(), arg(0), at)) Log::Error("okay_run: frame " + std::to_string(frame) + ": no visible UI button called '" + arg(0) + "'");
                else { mouse = at; buttons |= 1u; releaseButtonAt.push_back(frame + 1); }
            }
        }
        for (int r : releaseButtonAt) if (r == frame) buttons &= ~1u;
        Input::ClearTypedText();
        Input::FeedKeys(std::vector<char>(held.begin(), held.end()));
        Input::FeedMouse(mouse, buttons);
        if (stickOn) { Input::FeedGamepad(lstick, 0); Input::FeedGamepadExtra(rstick, 0, 0, true); }
        const auto t0 = std::chrono::steady_clock::now();
        try { ed.Tick(dt); }
        catch (const std::exception& e) { Log::Error(std::string("exception: ") + e.what()); break; }
        catch (...) { Log::Error("unknown exception"); break; }
        const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
        msSum += ms; msMax = std::max(msMax, ms); ++ran;

        Scene& sc = ed.scene();
        trace << "{\"frame\":" << frame << ",\"t\":" << Num((frame + 1) * dt) << ",\"ms\":" << Num(ms) << ",\"objects\":{";
        bool first = true;
        for (const std::string& name : traced) {
            GameObject* go = sc.Find(name);
            trace << (first ? "" : ",") << "\"" << Esc(name) << "\":" << (go ? ObjectJson(go) : std::string("null"));
            first = false;
        }
        trace << "}";
        if (Camera* cam = sc.mainCamera; cam && cam->gameObject) {
            GameObject* root = cam->gameObject;
            while (root->transform->Parent() && root->transform->Parent()->gameObject) root = root->transform->Parent()->gameObject;
            const Vec3 cp = cam->gameObject->transform->Position();
            std::string who;
            const bool inside = InsideCollider(sc, cp, root, &who);
            trace << ",\"camera\":{\"name\":\"" << Esc(cam->gameObject->name) << "\",\"pos\":" << V3(cp) << ",\"rot\":" << V3(cam->gameObject->transform->EulerAngles())
                  << ",\"fov\":" << Num(cam->fieldOfView) << ",\"inside\":" << (inside ? "\"" + Esc(who) + "\"" : std::string("null")) << "}";
        } else trace << ",\"camera\":null";
        trace << "}\n";

        if (shots.count(frame)) {
            Image img;
            testing::SceneRenderOptions ro; ro.width = width; ro.height = height;
            char name[32]; std::snprintf(name, sizeof name, "frame_%05d.png", frame);
            if (!testing::RenderSceneToImage(sc, img, ro, &err)) Log::Warning("okay_run: no picture at frame " + std::to_string(frame) + ": " + err);
            else if (!testing::SaveImageFile(img, (fs::path(outDir) / name).string())) Log::Warning(std::string("okay_run: could not save ") + name);
        }
    }
    const bool stopped = ran < frames;
    ed.Stop();
    trace.close();
    return finish(stopped ? 4 : 0, stopped ? "Play stopped early at frame " + std::to_string(ran) : "", ran, ran ? msSum / ran : 0, msMax);
}
