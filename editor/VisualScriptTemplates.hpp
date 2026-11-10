#pragma once
// Complete visual-script graphs. Each one does something you can see as soon as
// it is on an object and you press Play. Numbers named in `var` lines show up
// in the Inspector.
//
// The old Starter Scripts list (editor/main.cpp, ScriptRecipes) is a set of
// one-step Actions recipes: "Drift right", "Say hello on start", "Do something
// every 5s" (it only logs "tick"), and several that point at files that are not
// in the project (enemy.okayprefab, click.wav, a scene named Win). Those stay
// available on an Actions component. The visual-script Templates menu uses this
// list instead, because those recipes are not working graphs.
#include "VisualScriptLevels.hpp"
#include "okay/Components/AudioSource.hpp"
#include "okay/Components/MeshRenderer.hpp"
#include "okay/Components/ParticleSystem.hpp"
#include "okay/Components/TextRenderer.hpp"
#include "okay/Components/UIBarBind.hpp"
#include "okay/Components/UIProgressBar.hpp"
#include "okay/Physics/Collider3D.hpp"
#include "okay/Physics/Rigidbody3D.hpp"
#include "okay/Scene/GameObject.hpp"
#include <string>
#include <vector>

namespace okay::editor::vsed {

struct VsTemplate {
    const char* category;
    const char* name;
    const char* blurb;     // one line in the picker
    VsLevel level;
    const char* needs;     // empty when nothing else is required
    const char* add;       // components added on the selected object: Rigidbody, Trigger, Text, Audio, Particles
    std::string graph;
};

namespace tpl {

struct G {
    std::string s;
    int n = 1;
    int Node(const char* type, const char* arg, float x, float y, const char* entry = nullptr) {
        const int id = n++;
        s += "node " + std::to_string(id) + " " + type;
        if (arg && arg[0]) { s += " "; s += arg; }
        s += "\npos " + std::to_string(id) + " " + std::to_string((int)x) + " " + std::to_string((int)y) + "\n";
        if (entry && entry[0]) s += std::string("entry ") + entry + " " + std::to_string(id) + "\n";
        return id;
    }
    void Ex(int a, int pin, int b) { s += "exec " + std::to_string(a) + " " + std::to_string(pin) + " " + std::to_string(b) + "\n"; }
    void Data(int c, int ip, int p, int op = 0) {
        s += "data " + std::to_string(c) + " " + std::to_string(ip) + " " + std::to_string(p) + " " + std::to_string(op) + "\n";
    }
    void Def(int id, int pin, const char* v) { s += "default " + std::to_string(id) + " " + std::to_string(pin) + " " + v + "\n"; }
    void Var(const char* name, const char* value) { s += std::string("var ") + name + " " + value + "\n"; }
    void Note(const char* text) { s += std::string("comment 16 8 640 120 1 \"") + text + "\"\n"; }
};

inline std::string MoveWasd() {
    G g;
    g.Var("speed", "5");
    g.Note("Moves this object with W A S D or the arrow keys. Change speed in the Inspector.");
    const int st = g.Node("OnStart", nullptr, 40, 40, "OnStart");
    const int pr = g.Node("Print", nullptr, 300, 40);
    g.Def(pr, 0, "\"Ready. Move with W A S D.\"");
    g.Ex(st, 0, pr);
    const int up = g.Node("OnUpdate", nullptr, 40, 180, "OnUpdate");
    const int speed = g.Node("GetVar", "speed", 40, 340);
    const int ax = g.Node("AxisX", nullptr, 40, 440);
    const int ay = g.Node("AxisY", nullptr, 40, 540);
    const int dt = g.Node("DeltaTime", nullptr, 40, 640);
    const int mx = g.Node("Mul", nullptr, 280, 380);
    const int my = g.Node("Mul", nullptr, 280, 520);
    const int mxdt = g.Node("Mul", nullptr, 520, 380);
    const int mydt = g.Node("Mul", nullptr, 520, 520);
    const int mv = g.Node("Translate", nullptr, 760, 180);
    g.Def(mv, 2, "0");
    g.Data(mx, 0, ax); g.Data(mx, 1, speed);
    g.Data(my, 0, ay); g.Data(my, 1, speed);
    g.Data(mxdt, 0, mx); g.Data(mxdt, 1, dt);
    g.Data(mydt, 0, my); g.Data(mydt, 1, dt);
    g.Data(mv, 0, mxdt); g.Data(mv, 1, mydt);
    g.Ex(up, 0, mv);
    return g.s;
}

inline std::string Spin() {
    G g;
    g.Var("speed", "90");
    g.Note("Spins this object. Speed is degrees per second.");
    const int up = g.Node("OnUpdate", nullptr, 40, 160, "OnUpdate");
    const int sp = g.Node("GetVar", "speed", 40, 300);
    const int dt = g.Node("DeltaTime", nullptr, 40, 400);
    const int mul = g.Node("Mul", nullptr, 280, 320);
    const int rot = g.Node("Rotate", nullptr, 520, 160);
    g.Data(mul, 0, sp); g.Data(mul, 1, dt); g.Data(rot, 0, mul);
    g.Ex(up, 0, rot);
    return g.s;
}

inline std::string Float() {
    G g;
    g.Var("height", "0.6");
    g.Var("rate", "2");
    g.Note("Bobs up and down. Height is how far, rate is how fast.");
    const int up = g.Node("OnUpdate", nullptr, 40, 160, "OnUpdate");
    const int dt = g.Node("DeltaTime", nullptr, 40, 300);
    const int rate = g.Node("GetVar", "rate", 40, 400);
    const int step = g.Node("Mul", nullptr, 260, 340);
    const int add = g.Node("AddVar", "bob", 480, 160);
    const int t = g.Node("GetVar", "bob", 260, 480);
    const int sn = g.Node("Sin", nullptr, 480, 400);
    const int h = g.Node("GetVar", "height", 480, 500);
    const int y = g.Node("Mul", nullptr, 700, 440);
    const int gx = g.Node("GetX", nullptr, 700, 280);
    const int gz = g.Node("GetZ", nullptr, 700, 560);
    const int set = g.Node("SetPosition", nullptr, 920, 160);
    g.Data(step, 0, dt); g.Data(step, 1, rate); g.Data(add, 0, step);
    g.Data(sn, 0, t); g.Data(y, 0, sn); g.Data(y, 1, h);
    g.Data(set, 0, gx); g.Data(set, 1, y); g.Data(set, 2, gz);
    g.Ex(up, 0, add); g.Ex(add, 0, set);
    return g.s;
}

inline std::string Follow() {
    G g;
    g.Var("speed", "3");
    g.Note("Walks toward the object named Player. Change speed in the Inspector.");
    const int up = g.Node("OnUpdate", nullptr, 40, 160, "OnUpdate");
    const int sp = g.Node("GetVar", "speed", 40, 300);
    const int mv = g.Node("MoveTowards", "Player", 320, 160);
    g.Data(mv, 0, sp);
    g.Ex(up, 0, mv);
    return g.s;
}

inline std::string Patrol(const char* note) {
    G g;
    g.Var("speed", "2");
    g.Var("span", "3");
    g.Var("dir", "1");
    g.Note(note);
    const int up = g.Node("OnUpdate", nullptr, 40, 160, "OnUpdate");
    const int x = g.Node("GetX", nullptr, 40, 320);
    const int span = g.Node("GetVar", "span", 40, 420);
    const int hi = g.Node("Compare", ">", 260, 340);
    const int hiBr = g.Node("Branch", nullptr, 480, 160);
    const int neg = g.Node("Const", "-1", 700, 80);
    const int setNeg = g.Node("SetVar", "dir", 700, 180);
    const int zero = g.Node("Const", "0", 40, 540);
    const int lo = g.Node("Compare", "<", 260, 520);
    const int loBr = g.Node("Branch", nullptr, 480, 360);
    const int one = g.Node("Const", "1", 700, 360);
    const int setPos = g.Node("SetVar", "dir", 700, 460);
    const int dir = g.Node("GetVar", "dir", 40, 680);
    const int sp = g.Node("GetVar", "speed", 40, 780);
    const int dt = g.Node("DeltaTime", nullptr, 260, 780);
    const int step = g.Node("Mul", nullptr, 260, 680);
    const int signedStep = g.Node("Mul", nullptr, 480, 640);
    const int mv = g.Node("Translate", nullptr, 720, 600);
    g.Def(mv, 1, "0"); g.Def(mv, 2, "0");
    g.Data(hi, 0, x); g.Data(hi, 1, span); g.Data(hiBr, 0, hi);
    g.Data(setNeg, 0, neg);
    g.Data(lo, 0, x); g.Data(lo, 1, zero); g.Data(loBr, 0, lo);
    g.Data(setPos, 0, one);
    g.Data(step, 0, sp); g.Data(step, 1, dt);
    g.Data(signedStep, 0, step); g.Data(signedStep, 1, dir);
    g.Data(mv, 0, signedStep);
    g.Ex(up, 0, hiBr);
    g.Ex(hiBr, 0, setNeg); g.Ex(setNeg, 0, loBr);
    g.Ex(hiBr, 1, loBr);
    g.Ex(loBr, 0, setPos); g.Ex(setPos, 0, mv);
    g.Ex(loBr, 1, mv);
    return g.s;
}

inline std::string Jump() {
    G g;
    g.Var("strength", "7");
    g.Note("Press Space to jump. Needs a Rigidbody, which is added for you.");
    const int key = g.Node("OnKey", "space", 40, 160, "OnKey:space");
    const int s = g.Node("GetVar", "strength", 40, 300);
    const int kick = g.Node("AddImpulse", nullptr, 320, 160);
    g.Def(kick, 0, "0"); g.Def(kick, 2, "0");
    g.Data(kick, 1, s);
    g.Ex(key, 0, kick);
    return g.s;
}

inline std::string Door() {
    G g;
    g.Var("lift", "2");
    g.Var("open", "0");
    g.Note("Opens once, when a collider overlaps it. A fast mover is not missed between frames.");
    const int ev = g.Node("OnTrigger", nullptr, 40, 160, "OnTrigger");
    const int stay = g.Node("OnTriggerStay", nullptr, 40, 280, "OnTriggerStay");
    const int up = g.Node("OnUpdate", nullptr, 40, 400, "OnUpdate");
    const int here = g.Node("GetPosition", nullptr, 40, 540);
    const int half = g.Node("MakeVec3", nullptr, 280, 540);
    g.Def(half, 0, "1.2"); g.Def(half, 1, "1.2"); g.Def(half, 2, "1.2");
    const int box = g.Node("OverlapBox3D", nullptr, 520, 480);
    const int zeroN = g.Node("Const", "0", 520, 640);
    const int any = g.Node("Compare", ">", 760, 560);
    const int gate = g.Node("Branch", nullptr, 980, 400);
    const int open = g.Node("GetVar", "open", 40, 300);
    const int zero = g.Node("Const", "0", 40, 400);
    const int shut = g.Node("Compare", "==", 260, 320);
    const int br = g.Node("Branch", nullptr, 480, 160);
    const int one = g.Node("Const", "1", 700, 80);
    const int set = g.Node("SetVar", "open", 700, 180);
    const int gx = g.Node("GetX", nullptr, 700, 300);
    const int lift = g.Node("GetVar", "lift", 700, 400);
    const int gz = g.Node("GetZ", nullptr, 700, 500);
    const int pos = g.Node("SetPosition", nullptr, 940, 180);
    g.Data(box, 0, here); g.Data(box, 1, half);
    g.Data(any, 0, box, 0); g.Data(any, 1, zeroN); g.Data(gate, 0, any);
    g.Data(shut, 0, open); g.Data(shut, 1, zero); g.Data(br, 0, shut);
    g.Data(set, 0, one);
    g.Data(pos, 0, gx); g.Data(pos, 1, lift); g.Data(pos, 2, gz);
    g.Ex(ev, 0, br);
    g.Ex(stay, 0, br);
    g.Ex(up, 0, gate); g.Ex(gate, 0, br);
    g.Ex(br, 0, set); g.Ex(set, 0, pos);
    return g.s;
}

inline std::string Pickup() {
    G g;
    g.Var("points", "1");
    g.Note("Adds points to score and disappears when something enters this trigger.");
    const int ev = g.Node("OnTrigger", nullptr, 40, 160, "OnTrigger");
    const int pts = g.Node("GetVar", "points", 40, 300);
    const int add = g.Node("AddVar", "score", 320, 160);
    const int now = g.Node("GetVar", "score", 320, 300);
    const int pub = g.Node("SetSceneVar", "score", 560, 160);
    const int off = g.Node("SetActive", "self", 800, 160);
    g.Def(off, 0, "0");
    g.Data(add, 0, pts); g.Data(pub, 0, now);
    g.Ex(ev, 0, add); g.Ex(add, 0, pub); g.Ex(pub, 0, off);
    return g.s;
}

inline std::string PressE() {
    G g;
    g.Var("on", "0");
    g.Note("Press E to switch this object between two colours.");
    const int key = g.Node("OnKey", "e", 40, 160, "OnKey:e");
    const int on = g.Node("GetVar", "on", 40, 300);
    const int zero = g.Node("Const", "0", 40, 400);
    const int shut = g.Node("Compare", "==", 260, 320);
    const int br = g.Node("Branch", nullptr, 480, 160);
    const int one = g.Node("Const", "1", 700, 40);
    const int setOn = g.Node("SetVar", "on", 700, 140);
    const int green = g.Node("SetColor", nullptr, 940, 80);
    g.Def(green, 0, "0.2"); g.Def(green, 1, "0.85"); g.Def(green, 2, "0.35"); g.Def(green, 3, "1");
    const int setOff = g.Node("SetVar", "on", 700, 320);
    const int red = g.Node("SetColor", nullptr, 940, 300);
    g.Def(red, 0, "0.9"); g.Def(red, 1, "0.25"); g.Def(red, 2, "0.2"); g.Def(red, 3, "1");
    g.Data(shut, 0, on); g.Data(shut, 1, zero); g.Data(br, 0, shut);
    g.Data(setOn, 0, one); g.Data(setOff, 0, zero);
    g.Ex(key, 0, br);
    g.Ex(br, 0, setOn); g.Ex(setOn, 0, green);
    g.Ex(br, 1, setOff); g.Ex(setOff, 0, red);
    return g.s;
}

inline std::string Health() {
    G g;
    g.Var("health", "100");
    g.Var("damage", "25");
    g.Note("Send a hit event to hurt this object. At 0 health it respawns at the origin.");
    const int ev = g.Node("OnEvent", "hit", 40, 160, "OnEvent:hit");
    const int hp = g.Node("GetVar", "health", 40, 300);
    const int dmg = g.Node("GetVar", "damage", 40, 400);
    const int left = g.Node("Sub", nullptr, 280, 320);
    const int set = g.Node("SetVar", "health", 500, 160);
    const int now = g.Node("GetVar", "health", 500, 300);
    const int zero = g.Node("Const", "0", 500, 400);
    const int dead = g.Node("Compare", "<=", 720, 320);
    const int br = g.Node("Branch", nullptr, 940, 160);
    const int full = g.Node("Const", "100", 1160, 40);
    const int reset = g.Node("SetVar", "health", 1160, 140);
    const int deaths = g.Node("AddVar", "deaths", 1400, 140);
    const int one = g.Node("Const", "1", 1400, 240);
    const int home = g.Node("SetPosition", nullptr, 1640, 140);
    g.Def(home, 0, "0"); g.Def(home, 1, "0"); g.Def(home, 2, "0");
    g.Data(left, 0, hp); g.Data(left, 1, dmg); g.Data(set, 0, left);
    g.Data(dead, 0, now); g.Data(dead, 1, zero); g.Data(br, 0, dead);
    g.Data(reset, 0, full); g.Data(deaths, 0, one);
    g.Ex(ev, 0, set); g.Ex(set, 0, br);
    g.Ex(br, 0, reset); g.Ex(reset, 0, deaths); g.Ex(deaths, 0, home);
    return g.s;
}

inline std::string Checkpoint() {
    G g;
    g.Var("saved", "0");
    g.Note("Entering the trigger remembers this spot. A respawn event comes back to it.");
    const int ev = g.Node("OnTrigger", nullptr, 40, 160, "OnTrigger");
    const int x = g.Node("GetX", nullptr, 40, 300);
    const int y = g.Node("GetY", nullptr, 40, 400);
    const int z = g.Node("GetZ", nullptr, 40, 500);
    const int sx = g.Node("SetVar", "cx", 280, 160);
    const int sy = g.Node("SetVar", "cy", 520, 160);
    const int sz = g.Node("SetVar", "cz", 760, 160);
    const int saved = g.Node("SetVar", "saved", 1000, 160);
    const int one = g.Node("Const", "1", 1000, 280);
    g.Data(sx, 0, x); g.Data(sy, 0, y); g.Data(sz, 0, z); g.Data(saved, 0, one);
    g.Ex(ev, 0, sx); g.Ex(sx, 0, sy); g.Ex(sy, 0, sz); g.Ex(sz, 0, saved);
    const int back = g.Node("OnEvent", "respawn", 40, 640, "OnEvent:respawn");
    const int cx = g.Node("GetVar", "cx", 40, 780);
    const int cy = g.Node("GetVar", "cy", 40, 880);
    const int cz = g.Node("GetVar", "cz", 260, 880);
    const int pos = g.Node("SetPosition", nullptr, 320, 640);
    g.Data(pos, 0, cx); g.Data(pos, 1, cy); g.Data(pos, 2, cz);
    g.Ex(back, 0, pos);
    return g.s;
}

inline std::string Countdown() {
    G g;
    g.Var("seconds", "5");
    g.Note("Counts down on this object's text. At 0 it says you win.");
    const int t = g.Node("OnTimer", "1", 40, 160, "OnTimer:1");
    const int sec = g.Node("GetVar", "seconds", 40, 300);
    const int zero = g.Node("Const", "0", 40, 400);
    const int left = g.Node("Compare", ">", 260, 320);
    const int br = g.Node("Branch", nullptr, 480, 160);
    const int one = g.Node("Const", "1", 700, 80);
    const int sub = g.Node("Sub", nullptr, 700, 200);
    const int set = g.Node("SetVar", "seconds", 920, 160);
    const int now = g.Node("GetVar", "seconds", 920, 280);
    const int text = g.Node("ToString", nullptr, 1140, 240);
    const int show = g.Node("SetText", "self", 1360, 160);
    const int won = g.Node("SetVar", "won", 700, 400);
    const int winText = g.Node("SetText", "self", 940, 400);
    g.Def(won, 0, "1");
    g.Def(winText, 0, "\"You win\"");
    g.Data(left, 0, sec); g.Data(left, 1, zero); g.Data(br, 0, left);
    g.Data(sub, 0, sec); g.Data(sub, 1, one); g.Data(set, 0, sub);
    g.Data(text, 0, now); g.Data(show, 0, text);
    g.Ex(t, 0, br);
    g.Ex(br, 0, set); g.Ex(set, 0, show);
    g.Ex(br, 1, won); g.Ex(won, 0, winText);
    return g.s;
}

inline std::string ScoreText() {
    G g;
    g.Var("score", "0");
    g.Note("Shows Score: and the score variable on this object's text.");
    const int up = g.Node("OnUpdate", nullptr, 40, 160, "OnUpdate");
    const int sc = g.Node("GetVar", "score", 40, 300);
    const int num = g.Node("ToString", nullptr, 260, 300);
    const int label = g.Node("Const", "\"Score: \"", 260, 420);
    const int join = g.Node("Concat", nullptr, 500, 340);
    const int show = g.Node("SetText", "self", 740, 160);
    g.Data(num, 0, sc); g.Data(join, 0, label); g.Data(join, 1, num); g.Data(show, 0, join);
    g.Ex(up, 0, show);
    return g.s;
}

inline std::string Message() {
    G g;
    g.Var("message", "\"Hello\"");
    g.Note("Shows the message on this object's text when play starts.");
    const int st = g.Node("OnStart", nullptr, 40, 160, "OnStart");
    const int msg = g.Node("GetVar", "message", 40, 300);
    const int show = g.Node("SetText", "self", 320, 160);
    g.Data(show, 0, msg);
    g.Ex(st, 0, show);
    return g.s;
}

inline std::string HitFeedback() {
    G g;
    g.Var("shake", "0.6");
    g.Note("A hit event plays a sound, bursts particles, and shakes the camera.");
    const int ev = g.Node("OnEvent", "hit", 40, 160, "OnEvent:hit");
    const int snd = g.Node("PlaySound", nullptr, 280, 160);
    const int burst = g.Node("ParticleBurst", nullptr, 520, 160);
    g.Def(burst, 0, "12");
    const int sh = g.Node("GetVar", "shake", 520, 300);
    const int cam = g.Node("CameraShake", nullptr, 760, 160);
    const int one = g.Node("Const", "1", 760, 300);
    const int hits = g.Node("AddVar", "hits", 1000, 160);
    g.Data(cam, 0, sh); g.Data(hits, 0, one);
    g.Ex(ev, 0, snd); g.Ex(snd, 0, burst); g.Ex(burst, 0, cam); g.Ex(cam, 0, hits);
    return g.s;
}

inline std::string CamShake() {
    G g;
    g.Var("shake", "0.7");
    g.Note("Shakes the camera as soon as play starts. Needs a camera in the scene.");
    const int st = g.Node("OnStart", nullptr, 40, 160, "OnStart");
    const int sh = g.Node("GetVar", "shake", 40, 300);
    const int cam = g.Node("CameraShake", nullptr, 320, 160);
    g.Data(cam, 0, sh);
    g.Ex(st, 0, cam);
    return g.s;
}

inline std::string CamFollow() {
    G g;
    g.Var("smoothing", "4");
    g.Note("The camera eases after the object named Player.");
    const int st = g.Node("OnStart", nullptr, 40, 160, "OnStart");
    const int name = g.Node("Const", "\"Player\"", 40, 300);
    const int sm = g.Node("GetVar", "smoothing", 40, 400);
    const int fol = g.Node("CameraFollow", nullptr, 320, 160);
    g.Data(fol, 0, name); g.Data(fol, 1, sm);
    g.Ex(st, 0, fol);
    return g.s;
}

inline std::string CamZoom() {
    G g;
    g.Var("fov", "30");
    g.Note("Press C to set the camera's field of view. Needs a camera in the scene.");
    const int key = g.Node("OnKey", "c", 40, 160, "OnKey:c");
    const int fov = g.Node("GetVar", "fov", 40, 300);
    const int set = g.Node("SetFov", nullptr, 320, 160);
    g.Data(set, 0, fov);
    g.Ex(key, 0, set);
    return g.s;
}

inline std::string Chase() {
    G g;
    g.Var("speed", "3");
    g.Note("Chases the object named Player.");
    const int up = g.Node("OnUpdate", nullptr, 40, 160, "OnUpdate");
    const int sp = g.Node("GetVar", "speed", 40, 300);
    const int mv = g.Node("MoveTowards", "Player", 320, 160);
    g.Data(mv, 0, sp);
    g.Ex(up, 0, mv);
    return g.s;
}

inline std::string CooldownAbility() {
    G g;
    g.Var("gap", "0.75");
    g.Var("last", "-100");
    g.Note("Press Space. It only fires again after gap seconds.");
    const int key = g.Node("OnKey", "space", 40, 160, "OnKey:space");
    const int clock = g.Node("GetVar", "clock", 40, 300);
    const int last = g.Node("GetVar", "last", 40, 400);
    const int waited = g.Node("Sub", nullptr, 260, 340);
    const int gap = g.Node("GetVar", "gap", 260, 460);
    const int ready = g.Node("Compare", ">=", 480, 360);
    const int br = g.Node("Branch", nullptr, 700, 160);
    const int dt = g.Node("DeltaTime", nullptr, 40, 560);
    const int addClock = g.Node("AddVar", "clock", 260, 160);
    const int now = g.Node("GetVar", "clock", 920, 80);
    const int stamp = g.Node("SetVar", "last", 920, 180);
    const int one = g.Node("Const", "1", 920, 300);
    const int uses = g.Node("AddVar", "uses", 1160, 180);
    g.Data(addClock, 0, dt);
    g.Data(waited, 0, clock); g.Data(waited, 1, last);
    g.Data(ready, 0, waited); g.Data(ready, 1, gap); g.Data(br, 0, ready);
    g.Data(stamp, 0, now); g.Data(uses, 0, one);
    g.Ex(key, 0, addClock); g.Ex(addClock, 0, br);
    g.Ex(br, 0, stamp); g.Ex(stamp, 0, uses);
    return g.s;
}

inline std::string StateMachine() {
    G g;
    g.Var("range", "2.5");
    g.Var("speed", "3");
    g.Note("Idle until Player is within range, then chase. state is 0 or 1.");
    const int up = g.Node("OnUpdate", nullptr, 40, 160, "OnUpdate");
    const int dist = g.Node("DistanceTo", "Player", 40, 300);
    const int range = g.Node("GetVar", "range", 40, 400);
    const int isNear = g.Node("Compare", "<", 280, 320);
    const int br = g.Node("Branch", nullptr, 500, 160);
    const int one = g.Node("Const", "1", 720, 40);
    const int chase = g.Node("SetVar", "state", 720, 140);
    const int sp = g.Node("GetVar", "speed", 720, 260);
    const int mv = g.Node("MoveTowards", "Player", 960, 140);
    const int zero = g.Node("Const", "0", 720, 400);
    const int idle = g.Node("SetVar", "state", 720, 500);
    g.Data(isNear, 0, dist); g.Data(isNear, 1, range); g.Data(br, 0, isNear);
    g.Data(chase, 0, one); g.Data(mv, 0, sp); g.Data(idle, 0, zero);
    g.Ex(up, 0, br);
    g.Ex(br, 0, chase); g.Ex(chase, 0, mv);
    g.Ex(br, 1, idle);
    return g.s;
}

inline std::string Spawner() {
    G g;
    g.Var("every", "1");
    g.Note("Every second, creates a marker cube. The marker file ships with the examples.");
    const int t = g.Node("OnTimer", "1", 40, 160, "OnTimer:1");
    const int x = g.Node("GetX", nullptr, 40, 300);
    const int y = g.Node("GetY", nullptr, 40, 400);
    const int z = g.Node("GetZ", nullptr, 40, 500);
    const int one = g.Node("Const", "1", 260, 400);
    const int up = g.Node("Add", nullptr, 260, 320);
    const int n = g.Node("AddVar", "made", 520, 160);
    g.Def(n, 0, "1");
    const int sp = g.Node("Spawn", "examples/node_playground/marker.okayprefab", 760, 160);
    g.Data(up, 0, y); g.Data(up, 1, one);
    g.Data(sp, 0, x); g.Data(sp, 1, up); g.Data(sp, 2, z);
    g.Ex(t, 0, n); g.Ex(n, 0, sp);
    return g.s;
}

inline std::string Shooter() {
    G g;
    g.Var("gap", "0.4");
    g.Var("last", "-100");
    g.Note("Press Space. A shot cube flies to the right. It cannot fire again until gap seconds pass.");
    const int up = g.Node("OnUpdate", nullptr, 40, 40, "OnUpdate");
    const int dt = g.Node("DeltaTime", nullptr, 40, 180);
    const int clock = g.Node("AddVar", "clock", 280, 40);
    g.Data(clock, 0, dt);
    g.Ex(up, 0, clock);
    const int key = g.Node("OnKey", "space", 40, 320, "OnKey:space");
    const int now = g.Node("GetVar", "clock", 40, 460);
    const int last = g.Node("GetVar", "last", 40, 560);
    const int waited = g.Node("Sub", nullptr, 280, 500);
    const int gap = g.Node("GetVar", "gap", 280, 620);
    const int ready = g.Node("Compare", ">=", 500, 540);
    const int br = g.Node("Branch", nullptr, 720, 320);
    const int stamp = g.Node("SetVar", "last", 960, 240);
    const int n = g.Node("AddVar", "shots", 1200, 240);
    g.Def(n, 0, "1");
    const int x = g.Node("GetX", nullptr, 960, 400);
    const int y = g.Node("GetY", nullptr, 960, 500);
    const int z = g.Node("GetZ", nullptr, 960, 600);
    const int half = g.Node("Const", "0.8", 1200, 420);
    const int ahead = g.Node("Add", nullptr, 1200, 340);
    const int sp = g.Node("Spawn", "examples/node_playground/shot.okayprefab", 1440, 240);
    g.Data(waited, 0, now); g.Data(waited, 1, last);
    g.Data(ready, 0, waited); g.Data(ready, 1, gap); g.Data(br, 0, ready);
    g.Data(stamp, 0, now);
    g.Data(ahead, 0, x); g.Data(ahead, 1, half);
    g.Data(sp, 0, ahead); g.Data(sp, 1, y); g.Data(sp, 2, z);
    g.Ex(key, 0, br);
    g.Ex(br, 0, stamp); g.Ex(stamp, 0, n); g.Ex(n, 0, sp);
    return g.s;
}

inline std::string LineOfSight() {
    G g;
    g.Var("speed", "3");
    g.Var("seen", "0");
    g.Note("Looks at Player. Chases only when a straight line reaches them. A wall in between hides them.");
    const int up = g.Node("OnUpdate", nullptr, 40, 160, "OnUpdate");
    const int look = g.Node("LookAt", "Player", 280, 160);
    const int self = g.Node("GetPosition", nullptr, 40, 320);
    const int them = g.Node("ObjectPosition", "Player", 40, 440);
    const int dir = g.Node("Vec3Sub", nullptr, 280, 380);
    const int ray = g.Node("Raycast3D", nullptr, 520, 320);
    g.Def(ray, 2, "30");
    const int who = g.Node("Equals", nullptr, 760, 360);
    const int name = g.Node("Const", "\"Player\"", 760, 480);
    const int br = g.Node("Branch", nullptr, 980, 160);
    const int one = g.Node("Const", "1", 1200, 40);
    const int yes = g.Node("SetVar", "seen", 1200, 140);
    const int sp = g.Node("GetVar", "speed", 1200, 260);
    const int mv = g.Node("MoveTowards", "Player", 1440, 140);
    const int zero = g.Node("Const", "0", 1200, 400);
    const int no = g.Node("SetVar", "seen", 1200, 500);
    g.Data(dir, 0, them); g.Data(dir, 1, self);
    g.Data(ray, 0, self); g.Data(ray, 1, dir);
    g.Data(who, 0, ray, 4); g.Data(who, 1, name); g.Data(br, 0, who);
    g.Data(yes, 0, one); g.Data(mv, 0, sp); g.Data(no, 0, zero);
    g.Ex(up, 0, look); g.Ex(look, 0, br);
    g.Ex(br, 0, yes); g.Ex(yes, 0, mv);
    g.Ex(br, 1, no);
    return g.s;
}

inline std::string HealthBar() {
    G g;
    g.Var("health", "100");
    g.Var("damage", "25");
    g.Note("A hit event lowers health. The bar on this object fills from that number.");
    const int ev = g.Node("OnEvent", "hit", 40, 160, "OnEvent:hit");
    const int hp = g.Node("GetVar", "health", 40, 300);
    const int dmg = g.Node("GetVar", "damage", 40, 400);
    const int left = g.Node("Sub", nullptr, 280, 320);
    const int set = g.Node("SetVar", "health", 500, 160);
    const int now = g.Node("GetVar", "health", 500, 300);
    const int pub = g.Node("SetSceneVar", "health", 740, 160);
    g.Data(left, 0, hp); g.Data(left, 1, dmg); g.Data(set, 0, left); g.Data(pub, 0, now);
    g.Ex(ev, 0, set); g.Ex(set, 0, pub);
    return g.s;
}

inline std::string ClickMove() {
    G g;
    g.Var("speed", "4");
    g.Note("Click the floor. This object walks there. It needs a camera.");
    const int st = g.Node("OnStart", nullptr, 40, 40, "OnStart");
    const int x0 = g.Node("GetX", nullptr, 40, 180);
    const int z0 = g.Node("GetZ", nullptr, 40, 280);
    const int stx = g.Node("SetVar", "tx", 280, 40);
    const int stz = g.Node("SetVar", "tz", 520, 40);
    g.Data(stx, 0, x0); g.Data(stz, 0, z0);
    g.Ex(st, 0, stx); g.Ex(stx, 0, stz);
    const int up = g.Node("OnUpdate", nullptr, 40, 420, "OnUpdate");
    const int down = g.Node("MouseButtonDown", "0", 40, 560);
    const int ground = g.Node("MouseGround", nullptr, 40, 680);
    const int hit = g.Node("And", nullptr, 280, 600);
    const int br = g.Node("Branch", nullptr, 500, 420);
    const int setx = g.Node("SetVar", "tx", 740, 300);
    const int setz = g.Node("SetVar", "tz", 980, 300);
    const int cx = g.Node("GetX", nullptr, 740, 560);
    const int tx = g.Node("GetVar", "tx", 740, 660);
    const int cz = g.Node("GetZ", nullptr, 740, 780);
    const int tz = g.Node("GetVar", "tz", 740, 880);
    const int sp = g.Node("GetVar", "speed", 980, 700);
    const int dt = g.Node("DeltaTime", nullptr, 980, 800);
    const int step = g.Node("Mul", nullptr, 1200, 740);
    const int nx = g.Node("MoveTowardsValue", nullptr, 1440, 560);
    const int nz = g.Node("MoveTowardsValue", nullptr, 1440, 760);
    const int y = g.Node("GetY", nullptr, 1440, 660);
    const int pos = g.Node("SetPosition", nullptr, 1680, 420);
    g.Data(hit, 0, down); g.Data(hit, 1, ground, 3); g.Data(br, 0, hit);
    g.Data(setx, 0, ground, 0); g.Data(setz, 0, ground, 2);
    g.Data(step, 0, sp); g.Data(step, 1, dt);
    g.Data(nx, 0, cx); g.Data(nx, 1, tx); g.Data(nx, 2, step);
    g.Data(nz, 0, cz); g.Data(nz, 1, tz); g.Data(nz, 2, step);
    g.Data(pos, 0, nx); g.Data(pos, 1, y); g.Data(pos, 2, nz);
    g.Ex(up, 0, br);
    g.Ex(br, 0, setx); g.Ex(setx, 0, setz); g.Ex(setz, 0, pos);
    g.Ex(br, 1, pos);
    return g.s;
}

inline std::string CameraSwitch() {
    G g;
    g.Var("close", "0");
    g.Note("Press C to swap the Wide camera and the Close camera. Name them Wide and Close.");
    const int key = g.Node("OnKey", "c", 40, 160, "OnKey:c");
    const int cur = g.Node("GetVar", "close", 40, 300);
    const int zero = g.Node("Const", "0", 40, 400);
    const int wideOn = g.Node("Compare", "==", 260, 320);
    const int br = g.Node("Branch", nullptr, 480, 160);
    const int one = g.Node("Const", "1", 700, 40);
    const int setClose = g.Node("SetVar", "close", 700, 140);
    const int closeOn = g.Node("SetActive", "Close", 940, 40);
    const int wideOff = g.Node("SetActive", "Wide", 1180, 40);
    g.Def(closeOn, 0, "1"); g.Def(wideOff, 0, "0");
    const int setWide = g.Node("SetVar", "close", 700, 320);
    const int wideOnN = g.Node("SetActive", "Wide", 940, 280);
    const int closeOff = g.Node("SetActive", "Close", 1180, 280);
    g.Def(setWide, 0, "0"); g.Def(wideOnN, 0, "1"); g.Def(closeOff, 0, "0");
    g.Data(wideOn, 0, cur); g.Data(wideOn, 1, zero); g.Data(br, 0, wideOn);
    g.Data(setClose, 0, one);
    g.Ex(key, 0, br);
    g.Ex(br, 0, setClose); g.Ex(setClose, 0, closeOn); g.Ex(closeOn, 0, wideOff);
    g.Ex(br, 1, setWide); g.Ex(setWide, 0, wideOnN); g.Ex(wideOnN, 0, closeOff);
    return g.s;
}

inline std::string WinLose() {
    G g;
    g.Var("score", "0");
    g.Var("health", "100");
    g.Var("goal", "10");
    g.Note("Shows You win when score reaches goal, or You lose when health reaches 0.");
    const int up = g.Node("OnUpdate", nullptr, 40, 160, "OnUpdate");
    const int sc = g.Node("GetVar", "score", 40, 300);
    const int goal = g.Node("GetVar", "goal", 40, 400);
    const int winCmp = g.Node("Compare", ">=", 280, 320);
    const int winBr = g.Node("Branch", nullptr, 500, 160);
    const int won = g.Node("SetVar", "won", 740, 40);
    const int winText = g.Node("SetText", "self", 980, 40);
    g.Def(won, 0, "1"); g.Def(winText, 0, "\"You win\"");
    const int hp = g.Node("GetVar", "health", 40, 560);
    const int zero = g.Node("Const", "0", 40, 660);
    const int loseCmp = g.Node("Compare", "<=", 280, 580);
    const int loseBr = g.Node("Branch", nullptr, 500, 420);
    const int lost = g.Node("SetVar", "lost", 740, 420);
    const int loseText = g.Node("SetText", "self", 980, 420);
    g.Def(lost, 0, "1"); g.Def(loseText, 0, "\"You lose\"");
    g.Data(winCmp, 0, sc); g.Data(winCmp, 1, goal); g.Data(winBr, 0, winCmp);
    g.Data(loseCmp, 0, hp); g.Data(loseCmp, 1, zero); g.Data(loseBr, 0, loseCmp);
    g.Ex(up, 0, winBr);
    g.Ex(winBr, 0, won); g.Ex(won, 0, winText);
    g.Ex(winBr, 1, loseBr);
    g.Ex(loseBr, 0, lost); g.Ex(lost, 0, loseText);
    return g.s;
}

inline std::string WinScore() {
    G g;
    g.Var("score", "0");
    g.Var("goal", "10");
    g.Note("Sets won to 1 once score reaches goal.");
    const int up = g.Node("OnUpdate", nullptr, 40, 160, "OnUpdate");
    const int sc = g.Node("GetVar", "score", 40, 300);
    const int goal = g.Node("GetVar", "goal", 40, 400);
    const int cmp = g.Node("Compare", ">=", 280, 320);
    const int br = g.Node("Branch", nullptr, 500, 160);
    const int one = g.Node("Const", "1", 720, 160);
    const int won = g.Node("SetVar", "won", 720, 260);
    g.Data(cmp, 0, sc); g.Data(cmp, 1, goal); g.Data(br, 0, cmp);
    g.Data(won, 0, one);
    g.Ex(up, 0, br); g.Ex(br, 0, won);
    return g.s;
}

} // namespace tpl

inline const std::vector<VsTemplate>& VsTemplates() {
    static const std::vector<VsTemplate> all = {
        {"Movement", "Move with WASD", "Slides with W A S D or the arrow keys. A collider is added so it can open doors.", VsLevel::Basic,
         "A box collider is added on the selected object.", "Solid", tpl::MoveWasd()},
        {"Movement", "Spin", "Turns around at a steady speed.", VsLevel::Basic, "", "", tpl::Spin()},
        {"Movement", "Float", "Bobs up and down.", VsLevel::Basic, "", "", tpl::Float()},
        {"Movement", "Follow a target", "Walks toward an object named Player.", VsLevel::Intermediate,
         "Name the thing to follow Player.", "", tpl::Follow()},
        {"Movement", "Patrol", "Walks back and forth.", VsLevel::Intermediate, "", "",
         tpl::Patrol("Walks from x = 0 to x = span, then back. Change speed and span in the Inspector.")},
        {"Movement", "Jump", "Press Space for an upward push.", VsLevel::Basic,
         "A Rigidbody is added on the selected object.", "Rigidbody", tpl::Jump()},
        {"Interaction", "Door", "Opens once when something enters the trigger.", VsLevel::Basic,
         "A trigger collider is added on the selected object.", "Trigger", tpl::Door()},
        {"Interaction", "Pickup", "Adds to score and disappears when entered. Score is also a scene variable.", VsLevel::Basic,
         "A trigger collider is added on the selected object.", "Trigger", tpl::Pickup()},
        {"Interaction", "Press E", "Press E to switch colour.", VsLevel::Basic, "", "", tpl::PressE()},
        {"Gameplay", "Health", "A hit event hurts it. At 0 it respawns.", VsLevel::Intermediate, "", "", tpl::Health()},
        {"Gameplay", "Checkpoint", "A trigger saves this spot. A respawn event returns.", VsLevel::Intermediate,
         "A trigger collider is added on the selected object.", "Trigger", tpl::Checkpoint()},
        {"Gameplay", "Moving platform", "Slides back and forth. Put the player on it.", VsLevel::Basic, "", "",
         tpl::Patrol("Slides from x = 0 to x = span and back. Change speed and span in the Inspector.")},
        {"Gameplay", "Win at score", "Sets won once score reaches goal.", VsLevel::Basic, "", "", tpl::WinScore()},
        {"Gameplay", "Chase", "Chases the object named Player.", VsLevel::Intermediate,
         "Name the quarry Player.", "", tpl::Chase()},
        {"UI", "Countdown", "Counts down on this object's text, then says you win.", VsLevel::Intermediate,
         "A Text renderer is added on the selected object.", "Text", tpl::Countdown()},
        {"UI", "Score readout", "Shows Score: and the score on this object's text.", VsLevel::Basic,
         "A Text renderer is added on the selected object.", "Text", tpl::ScoreText()},
        {"UI", "Message", "Shows a message on this object's text when play starts.", VsLevel::Basic,
         "A Text renderer is added on the selected object.", "Text", tpl::Message()},
        {"UI", "Hit feedback", "A hit event plays a sound, bursts particles, and shakes the camera.", VsLevel::Intermediate,
         "Sound and particles are added. The scene needs a camera for the shake.", "Audio,Particles", tpl::HitFeedback()},
        {"Camera", "Camera shake", "Shakes the camera when play starts.", VsLevel::Basic,
         "The scene needs a camera.", "", tpl::CamShake()},
        {"Camera", "Camera follow", "The camera eases after the object named Player.", VsLevel::Intermediate,
         "The scene needs a camera, and an object named Player.", "", tpl::CamFollow()},
        {"Camera", "Camera zoom", "Press C to change the camera's field of view.", VsLevel::Intermediate,
         "The scene needs a camera.", "", tpl::CamZoom()},
        {"Logic", "Cooldown ability", "Press Space. It cannot fire again until gap seconds pass.", VsLevel::Intermediate, "", "", tpl::CooldownAbility()},
        {"Logic", "State machine", "Idles, then chases Player once they are in range.", VsLevel::Advanced,
         "Name the quarry Player.", "", tpl::StateMachine()},
        {"Gameplay", "Spawner", "Creates a marker cube every second.", VsLevel::Intermediate,
         "Uses examples/node_playground/marker.okayprefab.", "", tpl::Spawner()},
        {"Gameplay", "Shooter", "Press Space. A shot flies right, then waits.", VsLevel::Intermediate,
         "Uses examples/node_playground/shot.okayprefab.", "", tpl::Shooter()},
        {"Gameplay", "Line of sight", "Chases Player only when nothing blocks the line.", VsLevel::Advanced,
         "Name the quarry Player. Both need a collider; the quarry's is solid.", "Solid", tpl::LineOfSight()},
        {"UI", "Health bar", "A hit lowers health and the bar on this object.", VsLevel::Intermediate,
         "A progress bar is added on the selected object.", "Bar", tpl::HealthBar()},
        {"Movement", "Click to move", "Click the floor and this object walks there.", VsLevel::Intermediate,
         "The scene needs a camera.", "", tpl::ClickMove()},
        {"Camera", "Camera switch", "Press C to swap the cameras named Wide and Close.", VsLevel::Intermediate,
         "The scene needs two cameras, named Wide and Close.", "", tpl::CameraSwitch()},
        {"Gameplay", "Win or lose", "You win at the goal score. You lose at 0 health.", VsLevel::Intermediate,
         "A Text renderer is added on the selected object.", "Text", tpl::WinLose()},
    };
    return all;
}

inline int VsTemplateCount() { return (int)VsTemplates().size(); }

inline const VsTemplate* VsTemplateNamed(const char* name) {
    if (!name) return nullptr;
    for (const auto& t : VsTemplates()) if (std::strcmp(t.name, name) == 0) return &t;
    return nullptr;
}

// Adds the components a template names. A trigger is a box collider that does not block.
inline void VsAddTemplateComponents(GameObject* go, const char* add) {
    if (!go || !add || !add[0]) return;
    const std::string s = add;
    auto has = [&](const char* w) { return s.find(w) != std::string::npos; };
    if (has("Rigidbody") && !go->GetComponent<Rigidbody3D>()) go->AddComponent<Rigidbody3D>();
    if (has("Trigger") && !go->GetComponent<BoxCollider3D>()) {
        auto* box = go->AddComponent<BoxCollider3D>();
        box->isTrigger = true;
    }
    if (has("Text") && !go->GetComponent<TextRenderer>()) go->AddComponent<TextRenderer>();
    if (has("Solid") && !go->GetComponent<BoxCollider3D>()) go->AddComponent<BoxCollider3D>();
    if (has("Bar")) {
        if (!go->GetComponent<UIProgressBar>()) go->AddComponent<UIProgressBar>();
        if (!go->GetComponent<UIBarBind>()) {
            auto* bar = go->AddComponent<UIBarBind>();
            bar->var = "health";
            bar->min = 0.0f;
            bar->max = 100.0f;
        }
    }
    if (has("Audio") && !go->GetComponent<AudioSource>()) {
        auto* au = go->AddComponent<AudioSource>();
        au->clip = AudioClip::Sine(523.0f, 0.18f);
    }
    if (has("Particles") && !go->GetComponent<ParticleSystem>()) go->AddComponent<ParticleSystem>();
}

} // namespace okay::editor::vsed
