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
    g.Note("Opens once, when something enters this trigger.");
    const int ev = g.Node("OnTrigger", nullptr, 40, 160, "OnTrigger");
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
    g.Data(shut, 0, open); g.Data(shut, 1, zero); g.Data(br, 0, shut);
    g.Data(set, 0, one);
    g.Data(pos, 0, gx); g.Data(pos, 1, lift); g.Data(pos, 2, gz);
    g.Ex(ev, 0, br);
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
    const int off = g.Node("SetActive", "self", 560, 160);
    g.Def(off, 0, "0");
    g.Data(add, 0, pts);
    g.Ex(ev, 0, add); g.Ex(add, 0, off);
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
    const int near = g.Node("Compare", "<", 280, 320);
    const int br = g.Node("Branch", nullptr, 500, 160);
    const int one = g.Node("Const", "1", 720, 40);
    const int chase = g.Node("SetVar", "state", 720, 140);
    const int sp = g.Node("GetVar", "speed", 720, 260);
    const int mv = g.Node("MoveTowards", "Player", 960, 140);
    const int zero = g.Node("Const", "0", 720, 400);
    const int idle = g.Node("SetVar", "state", 720, 500);
    g.Data(near, 0, dist); g.Data(near, 1, range); g.Data(br, 0, near);
    g.Data(chase, 0, one); g.Data(mv, 0, sp); g.Data(idle, 0, zero);
    g.Ex(up, 0, br);
    g.Ex(br, 0, chase); g.Ex(chase, 0, mv);
    g.Ex(br, 1, idle);
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
        {"Movement", "Move with WASD", "Slides with W A S D or the arrow keys.", VsLevel::Basic, "", "", tpl::MoveWasd()},
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
        {"Interaction", "Pickup", "Adds to score and disappears when entered.", VsLevel::Basic,
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
    if (has("Audio") && !go->GetComponent<AudioSource>()) {
        auto* au = go->AddComponent<AudioSource>();
        au->clip = AudioClip::Sine(523.0f, 0.18f);
    }
    if (has("Particles") && !go->GetComponent<ParticleSystem>()) go->AddComponent<ParticleSystem>();
}

} // namespace okay::editor::vsed
