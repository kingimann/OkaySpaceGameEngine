#pragma once
// Vehicle tuning: every number the open-world driving model reads, one table, plus the built-in presets and the
// .okayvehicle text format.
//
// Written from scratch for this engine from general vehicle-dynamics ideas (a bicycle model for steering, a friction
// circle for grip, a torque curve with gear ratios for the engine). No code, constants or text from another project.
// The one table below drives the Inspector rows, scene save/load, SetSetting("name", value) for scripts, presets and the
// .okayvehicle files, so a setting is added in exactly one place.
//
// Header only and free of <mutex>/<thread> (the Windows programs reach it).
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <sstream>
#include <string>
#include <vector>

namespace okay {

struct VehicleTuning {
    // ---- Simple tab ----
    bool  openWorld = false;        ///< true: the open-world driving model; false: the older arcade model (old scenes)
    float handling = 0.7f;          ///< 0 = realistic, 1 = arcade (forgiving)
    float maxSpeed = 22.0f;         ///< top speed (m/s)
    float acceleration = 18.0f;     ///< strongest push of the engine (m/s^2)
    float grip = 7.0f;              ///< how fast sideways slide is killed (higher = sticky)
    float driftAmount = 0.5f;       ///< 0 = hard to drift, 1 = drifts easily
    // ---- Engine ----
    float reverseSpeed = 8.0f;      ///< top speed backwards (m/s)
    float idleRpm = 900.0f;
    float maxRpm = 7000.0f;
    float torquePeak = 0.6f;        ///< where in the rev range the engine pulls hardest (0.2 low .. 0.9 high)
    int   gearCount = 5;
    int   gearbox = 0;              ///< 0 Automatic, 1 Manual, 2 Sequential
    float shiftTime = 0.25f;        ///< seconds of no push while changing gear
    float engineBraking = 4.0f;     ///< extra slowing when you lift off the gas (m/s^2 at full revs)
    float drag = 4.0f;              ///< rolling slowdown when coasting (m/s^2)
    // ---- Drivetrain ----
    int   drivetrain = 0;           ///< 0 Rear-wheel, 1 Front-wheel, 2 All-wheel
    float torqueSplit = 0.4f;       ///< all-wheel: share of the push sent to the front wheels
    float diffLock = 0.3f;          ///< 0 open .. 1 locked: more bite out of corners
    // ---- Steering ----
    float maxSteerAngle = 32.0f;    ///< front wheel lock at standstill (degrees)
    float steerSpeedSensitivity = 0.6f; ///< 0 = same lock at any speed, 1 = much less lock when fast
    float steerResponse = 7.0f;     ///< how quickly the wheels follow the stick
    float autoCentre = 10.0f;       ///< how quickly the wheel straightens when you let go
    float counterSteerAssist = 0.6f;///< help to catch a slide
    bool  steerAssist = true;       ///< never ask for more turn than the tyres can give
    float turnSpeed = 110.0f;       ///< (older model) steering rate, degrees/s
    // ---- Brakes ----
    float brakeForce = 36.0f;       ///< braking power (m/s^2)
    float brakeBias = 0.6f;         ///< share of braking at the front (0.4 .. 0.8 is sane)
    bool  abs = true;               ///< anti-lock brakes
    float handbrakeStrength = 1.0f; ///< how hard the handbrake slows the car
    int   reverseMode = 0;          ///< 0 Hold brake to reverse, 1 Separate reverse button
    // ---- Tyres ----
    float frontGrip = 1.0f;
    float rearGrip = 1.0f;
    float slipLimit = 22.0f;        ///< slip angle (degrees) after which the tyres let go
    float handbrakeGrip = 0.6f;     ///< sideways grip at the rear while the handbrake is held
    float iceGrip = 0.25f;          ///< grip multiplier on a surface tagged Ice
    float mudGrip = 0.55f;          ///< ... Mud
    float grassGrip = 0.7f;         ///< ... Grass
    // ---- Body ----
    float downforce = 0.02f;        ///< extra grip when fast
    float aeroDrag = 0.002f;        ///< air resistance (m/s^2 per (m/s)^2)
    float leanAmount = 1.0f;        ///< how much the body dives, squats and rolls (0 = stiff)
    float antiRoll = 0.5f;          ///< 0 floaty, 1 stiff: keeps the body from rolling over
    // ---- Assists ----
    bool  tractionControl = true;
    float stabilityControl = 0.5f;  ///< calms the car when it starts to spin (0 off .. 1 strong)
    bool  autoFlip = true;          ///< put the car back on its wheels after a flip
    float airControl = 0.5f;        ///< small pitch and roll steering while in the air
    // ---- Boost ----
    bool  boostEnabled = true;
    float boostPower = 10.0f;       ///< extra push while boosting (m/s^2)
    float boostDuration = 3.0f;     ///< seconds a full tank lasts
    float boostRecharge = 0.4f;     ///< tank refilled per second when not boosting (1 = full)
    // ---- Controls ----
    float steerSensitivity = 1.0f;
    float inputSmoothing = 10.0f;   ///< how quickly keys ease in (0 = instant)
    float deadZone = 0.15f;         ///< stick / trigger part that reads 0
    bool  invertSteer = false;
    char  keyThrottle = 'w';
    char  keyBrake = 's';
    char  keySteerLeft = 'a';
    char  keySteerRight = 'd';
    char  keyBoost = 16;            ///< Shift
    char  keyRecover = 'r';
    char  keyHorn = 'h';
    char  keyLights = 'l';
    char  keyCamera = 'c';
    char  keyLookBack = 'b';
    char  keyGearUp = 'x';
    char  keyGearDown = 'z';
    char  keyReverse = 'q';         ///< used when reverseMode is "Separate reverse button"
    int   padHandbrake = 0;         ///< PadA
    int   padBoost = 1;             ///< PadB
    int   padRecover = 4;           ///< PadBack
    int   padHorn = 12;             ///< left stick click
    int   padLights = 8;            ///< D-pad up
    int   padCamera = 13;           ///< right stick click
    int   padGearUp = 7;            ///< RB
    int   padGearDown = 6;          ///< LB
    // ---- Camera ----
    int   camView = 0;              ///< 0 Chase, 1 Far, 2 Hood, 3 Bonnet
    float camFov = 60.0f;
    float camSpeedFov = 14.0f;      ///< extra field of view at top speed
    float camSpeedDistance = 2.5f;  ///< extra distance at top speed
    float camDriftSwing = 0.6f;     ///< how far the camera swings out in a drift
    float camLookAhead = 0.5f;      ///< how far it looks into the turn

    /// What kind a table row is.
    enum Kind { KFloat, KInt, KBool, KEnum, KKey };
    struct Desc {
        const char* name; Kind kind;
        float VehicleTuning::*f; int VehicleTuning::*i; bool VehicleTuning::*b; char VehicleTuning::*k;
        double lo, hi, step;
        const char* group; const char* tip; const char* options;   ///< options: "A|B|C" for an enum
    };

    /// Every setting, in the order the Inspector shows them. Names are plain words.
    static const std::vector<Desc>& Table() {
        static const std::vector<Desc> t = [] {
            std::vector<Desc> v;
            auto F = [&](const char* n, float VehicleTuning::*m, double lo, double hi, double st, const char* g, const char* tip) {
                v.push_back({n, KFloat, m, nullptr, nullptr, nullptr, lo, hi, st, g, tip, ""}); };
            auto I = [&](const char* n, int VehicleTuning::*m, double lo, double hi, const char* g, const char* tip) {
                v.push_back({n, KInt, nullptr, m, nullptr, nullptr, lo, hi, 1, g, tip, ""}); };
            auto B = [&](const char* n, bool VehicleTuning::*m, const char* g, const char* tip) {
                v.push_back({n, KBool, nullptr, nullptr, m, nullptr, 0, 1, 1, g, tip, ""}); };
            auto E = [&](const char* n, int VehicleTuning::*m, const char* opt, const char* g, const char* tip) {
                v.push_back({n, KEnum, nullptr, m, nullptr, nullptr, 0, 0, 1, g, tip, opt}); };
            auto K = [&](const char* n, char VehicleTuning::*m, const char* g, const char* tip) {
                v.push_back({n, KKey, nullptr, nullptr, nullptr, m, 0, 255, 1, g, tip, ""}); };
            B("openWorld", &VehicleTuning::openWorld, "Simple", "On: the open-world driving model (brake to reverse, drifts, gears). Off: the older simple model");
            F("handling", &VehicleTuning::handling, 0, 1, 0.01, "Simple", "Realistic (0) to Arcade (1). Arcade is more forgiving and easier to drift");
            F("maxSpeed", &VehicleTuning::maxSpeed, 1, 200, 0.5, "Simple", "Top speed in metres per second (22 is about 80 km/h)");
            F("acceleration", &VehicleTuning::acceleration, 1, 100, 0.5, "Simple", "How hard the engine pushes (m/s^2)");
            F("grip", &VehicleTuning::grip, 0.1, 30, 0.1, "Simple", "How sticky the tyres are. Low = slidey");
            F("driftAmount", &VehicleTuning::driftAmount, 0, 1, 0.01, "Simple", "How easily the back end slides when you pull the handbrake");
            F("reverseSpeed", &VehicleTuning::reverseSpeed, 0, 100, 0.5, "Engine", "Top speed going backwards (m/s)");
            F("idleRpm", &VehicleTuning::idleRpm, 300, 3000, 50, "Engine", "Engine speed when idling");
            F("maxRpm", &VehicleTuning::maxRpm, 2000, 20000, 100, "Engine", "Engine speed where it hits the limiter");
            F("torquePeak", &VehicleTuning::torquePeak, 0.2, 0.9, 0.01, "Engine", "Where in the rev range the engine pulls hardest (low = pulls from low revs)");
            I("gearCount", &VehicleTuning::gearCount, 1, 8, "Engine", "Number of forward gears");
            E("gearbox", &VehicleTuning::gearbox, "Automatic|Manual|Sequential", "Engine", "Automatic changes for you. Manual and Sequential use the gear up / gear down buttons");
            F("shiftTime", &VehicleTuning::shiftTime, 0, 2, 0.01, "Engine", "Seconds with no push while the gear changes");
            F("engineBraking", &VehicleTuning::engineBraking, 0, 20, 0.1, "Engine", "How much the engine slows you when you lift off the gas");
            F("drag", &VehicleTuning::drag, 0, 100, 0.1, "Engine", "Rolling slowdown when coasting");
            E("drivetrain", &VehicleTuning::drivetrain, "Rear-wheel|Front-wheel|All-wheel", "Drivetrain", "Which wheels the engine drives. All-wheel gets going best on slippery ground");
            F("torqueSplit", &VehicleTuning::torqueSplit, 0, 1, 0.01, "Drivetrain", "All-wheel only: share of the push sent to the front wheels");
            F("diffLock", &VehicleTuning::diffLock, 0, 1, 0.01, "Drivetrain", "0 = open differential, 1 = locked (more bite out of corners)");
            F("maxSteerAngle", &VehicleTuning::maxSteerAngle, 5, 60, 0.5, "Steering", "How far the front wheels turn at a standstill (degrees)");
            F("steerSpeedSensitivity", &VehicleTuning::steerSpeedSensitivity, 0, 1, 0.01, "Steering", "How much the steering lock shrinks as you go faster");
            F("steerResponse", &VehicleTuning::steerResponse, 1, 30, 0.1, "Steering", "How quickly the wheels follow your steering");
            F("autoCentre", &VehicleTuning::autoCentre, 0, 40, 0.1, "Steering", "How quickly the wheel straightens when you let go");
            F("counterSteerAssist", &VehicleTuning::counterSteerAssist, 0, 1, 0.01, "Steering", "Helps you catch a slide by steering into it");
            B("steerAssist", &VehicleTuning::steerAssist, "Steering", "Never turns harder than the tyres can grip");
            F("turnSpeed", &VehicleTuning::turnSpeed, 0, 360, 1, "Steering", "Older model only: steering rate (degrees per second)");
            F("brakeForce", &VehicleTuning::brakeForce, 0, 200, 0.5, "Brakes", "Braking power (m/s^2)");
            F("brakeBias", &VehicleTuning::brakeBias, 0.2, 0.9, 0.01, "Brakes", "Share of braking at the front. About 0.6 is best");
            B("abs", &VehicleTuning::abs, "Brakes", "Anti-lock brakes: stops shorter on slippery ground");
            F("handbrakeStrength", &VehicleTuning::handbrakeStrength, 0, 3, 0.05, "Brakes", "How hard the handbrake slows the car");
            E("reverseMode", &VehicleTuning::reverseMode, "Hold brake to reverse|Separate reverse button", "Brakes", "How you go backwards");
            F("frontGrip", &VehicleTuning::frontGrip, 0.2, 3, 0.01, "Tyres", "Front tyre grip");
            F("rearGrip", &VehicleTuning::rearGrip, 0.2, 3, 0.01, "Tyres", "Rear tyre grip");
            F("slipLimit", &VehicleTuning::slipLimit, 5, 60, 0.5, "Tyres", "How sideways (degrees) the car can slide before the tyres let go");
            F("handbrakeGrip", &VehicleTuning::handbrakeGrip, 0, 30, 0.05, "Tyres", "Sideways grip at the back while the handbrake is held (low = drifty)");
            F("iceGrip", &VehicleTuning::iceGrip, 0.02, 1.5, 0.01, "Tyres", "Grip on a surface tagged Ice (1 = normal)");
            F("mudGrip", &VehicleTuning::mudGrip, 0.02, 1.5, 0.01, "Tyres", "Grip on a surface tagged Mud");
            F("grassGrip", &VehicleTuning::grassGrip, 0.02, 1.5, 0.01, "Tyres", "Grip on a surface tagged Grass");
            F("downforce", &VehicleTuning::downforce, 0, 1, 0.005, "Body", "Extra grip when going fast");
            F("aeroDrag", &VehicleTuning::aeroDrag, 0, 0.1, 0.0005, "Body", "Air resistance");
            F("leanAmount", &VehicleTuning::leanAmount, 0, 2, 0.05, "Body", "How much the body dives, squats and leans (0 = stiff)");
            F("antiRoll", &VehicleTuning::antiRoll, 0, 1, 0.01, "Body", "Keeps the car from rolling over");
            B("tractionControl", &VehicleTuning::tractionControl, "Assists", "Stops wheelspin when you floor it");
            F("stabilityControl", &VehicleTuning::stabilityControl, 0, 1, 0.01, "Assists", "Calms the car when it starts to spin (not while the handbrake is held)");
            B("autoFlip", &VehicleTuning::autoFlip, "Assists", "Puts the car back on its wheels a few seconds after a flip");
            F("airControl", &VehicleTuning::airControl, 0, 1, 0.01, "Assists", "How much you can tilt the car in the air");
            B("boostEnabled", &VehicleTuning::boostEnabled, "Boost", "Allow the boost button");
            F("boostPower", &VehicleTuning::boostPower, 0, 100, 0.5, "Boost", "Extra push while boosting");
            F("boostDuration", &VehicleTuning::boostDuration, 0.1, 60, 0.1, "Boost", "Seconds a full tank lasts");
            F("boostRecharge", &VehicleTuning::boostRecharge, 0, 5, 0.05, "Boost", "How fast the tank refills when not boosting");
            F("steerSensitivity", &VehicleTuning::steerSensitivity, 0.2, 2, 0.05, "Controls", "How strongly the steering reacts to your input");
            F("inputSmoothing", &VehicleTuning::inputSmoothing, 0, 50, 0.5, "Controls", "How quickly keys ease in (0 = instant)");
            F("deadZone", &VehicleTuning::deadZone, 0, 0.9, 0.01, "Controls", "Stick and trigger part that counts as zero");
            B("invertSteer", &VehicleTuning::invertSteer, "Controls", "Swap left and right");
            K("keyThrottle", &VehicleTuning::keyThrottle, "Controls", "Gas key (the Up arrow also works)");
            K("keyBrake", &VehicleTuning::keyBrake, "Controls", "Brake / reverse key (the Down arrow also works)");
            K("keySteerLeft", &VehicleTuning::keySteerLeft, "Controls", "Steer left key (the Left arrow also works)");
            K("keySteerRight", &VehicleTuning::keySteerRight, "Controls", "Steer right key (the Right arrow also works)");
            K("keyBoost", &VehicleTuning::keyBoost, "Controls", "Boost key (16 = Shift)");
            K("keyRecover", &VehicleTuning::keyRecover, "Controls", "Put the car back on its wheels");
            K("keyHorn", &VehicleTuning::keyHorn, "Controls", "Horn");
            K("keyLights", &VehicleTuning::keyLights, "Controls", "Headlights on / off");
            K("keyCamera", &VehicleTuning::keyCamera, "Controls", "Change camera view");
            K("keyLookBack", &VehicleTuning::keyLookBack, "Controls", "Hold to look behind");
            K("keyGearUp", &VehicleTuning::keyGearUp, "Controls", "Gear up (manual and sequential gearbox)");
            K("keyGearDown", &VehicleTuning::keyGearDown, "Controls", "Gear down");
            K("keyReverse", &VehicleTuning::keyReverse, "Controls", "Reverse (only with 'Separate reverse button')");
            I("padHandbrake", &VehicleTuning::padHandbrake, 0, 15, "Gamepad", "Handbrake button (0 = A, 2 = X)");
            I("padBoost", &VehicleTuning::padBoost, 0, 15, "Gamepad", "Boost button (1 = B)");
            I("padRecover", &VehicleTuning::padRecover, 0, 15, "Gamepad", "Recover button (4 = Back)");
            I("padHorn", &VehicleTuning::padHorn, 0, 15, "Gamepad", "Horn button (12 = left stick click)");
            I("padLights", &VehicleTuning::padLights, 0, 15, "Gamepad", "Lights button (8 = D-pad up)");
            I("padCamera", &VehicleTuning::padCamera, 0, 15, "Gamepad", "Change camera button (13 = right stick click)");
            I("padGearUp", &VehicleTuning::padGearUp, 0, 15, "Gamepad", "Gear up button (7 = RB)");
            I("padGearDown", &VehicleTuning::padGearDown, 0, 15, "Gamepad", "Gear down button (6 = LB)");
            E("camView", &VehicleTuning::camView, "Chase|Far|Hood|Bonnet", "Camera", "Which view the camera starts in");
            F("camFov", &VehicleTuning::camFov, 20, 120, 0.5, "Camera", "Field of view (degrees)");
            F("camSpeedFov", &VehicleTuning::camSpeedFov, 0, 60, 0.5, "Camera", "Extra field of view at top speed");
            F("camSpeedDistance", &VehicleTuning::camSpeedDistance, 0, 20, 0.1, "Camera", "Extra distance behind the car at top speed");
            F("camDriftSwing", &VehicleTuning::camDriftSwing, 0, 2, 0.05, "Camera", "How far the camera swings out in a drift");
            F("camLookAhead", &VehicleTuning::camLookAhead, 0, 2, 0.05, "Camera", "How far the camera looks into the turn");
            return v;
        }();
        return t;
    }
    static const Desc* Find(const std::string& name) {
        for (const Desc& d : Table()) if (name == d.name) return &d;
        return nullptr;
    }

    /// Set a setting by name (a number; true/false are 1/0; an enum takes its index). Out of range values are clamped.
    /// Returns false for an unknown name or a NaN.
    bool SetSetting(const std::string& name, double value) {
        const Desc* d = Find(name);
        if (!d || std::isnan(value)) return false;
        switch (d->kind) {
            case KFloat: this->*(d->f) = (float)std::min(d->hi, std::max(d->lo, value)); break;
            case KInt:   this->*(d->i) = (int)std::min(d->hi, std::max(d->lo, std::round(value))); break;
            case KBool:  this->*(d->b) = value != 0.0; break;
            case KEnum:  { int n = 1; for (const char* p = d->options; *p; ++p) if (*p == '|') ++n;
                           this->*(d->i) = (int)std::min((double)(n - 1), std::max(0.0, std::round(value))); break; }
            case KKey:   this->*(d->k) = (char)(int)std::min(255.0, std::max(0.0, std::round(value))); break;
        }
        return true;
    }
    bool GetSetting(const std::string& name, double& out) const {
        const Desc* d = Find(name);
        if (!d) return false;
        switch (d->kind) {
            case KFloat: out = this->*(d->f); break;
            case KInt: case KEnum: out = this->*(d->i); break;
            case KBool: out = (this->*(d->b)) ? 1.0 : 0.0; break;
            case KKey: out = (unsigned char)(this->*(d->k)); break;
        }
        return true;
    }
    /// Every setting back to its default.
    void ResetTuning() { *static_cast<VehicleTuning*>(this) = VehicleTuning(); }

    // ---- Text form (the .okayvehicle file, and one line of a scene) ----
    /// "okayvehicle 1" then one "name value" per line.
    std::string ToFile(const std::string& presetName) const {
        std::ostringstream o;
        o << "okayvehicle 1\nname " << presetName << "\n";
        for (const Desc& d : Table()) { double v = 0; GetSetting(d.name, v); o << d.name << " " << v << "\n"; }
        return o.str();
    }
    /// Reads a file made by ToFile. Starts from the defaults, so a file that lists only some settings is fine. Unknown
    /// names are skipped (a newer file still loads). Returns false if it is not an okayvehicle file.
    bool FromFile(const std::string& text, std::string* presetName = nullptr) {
        std::istringstream in(text);
        std::string head; int ver = 0;
        in >> head >> ver;
        if (head != "okayvehicle" || ver < 1) return false;
        VehicleTuning fresh; fresh.openWorld = true;
        *this = fresh;
        std::string key;
        while (in >> key) {
            if (key == "name") { std::string rest; std::getline(in, rest); if (!rest.empty() && rest[0] == ' ') rest.erase(0, 1);
                                 if (presetName) *presetName = rest; continue; }
            double v = 0; if (!(in >> v)) { in.clear(); std::string skip; std::getline(in, skip); continue; }
            SetSetting(key, v);
        }
        return true;
    }
    /// A single line "k=v k=v ..." with the settings that differ from the defaults (scene files).
    std::string ToLine() const {
        std::ostringstream o; VehicleTuning def;
        for (const Desc& d : Table()) {
            double a = 0, b = 0; GetSetting(d.name, a); def.GetSetting(d.name, b);
            if (a != b) o << d.name << "=" << a << " ";
        }
        return o.str();
    }
    void FromLine(const std::string& line) {
        std::istringstream in(line); std::string tok;
        while (in >> tok) {
            size_t eq = tok.find('=');
            if (eq == std::string::npos) continue;
            SetSetting(tok.substr(0, eq), std::atof(tok.c_str() + eq + 1));
        }
    }

    // ---- Built-in presets ----
    struct Preset { const char* name; const char* values; };   ///< values: "name=value ..." on top of the defaults
    static const std::vector<Preset>& Presets() {
        static const std::vector<Preset> p = {
            {"Sedan",       "maxSpeed=34 acceleration=14 grip=8 driftAmount=0.35 gearCount=5 brakeForce=34 maxSteerAngle=32 handling=0.6"},
            {"Sports Car",  "maxSpeed=48 acceleration=24 grip=9 driftAmount=0.55 gearCount=6 maxRpm=8500 torquePeak=0.7 brakeForce=44 maxSteerAngle=30 frontGrip=1.15 rearGrip=1.15 downforce=0.04 handling=0.7"},
            {"Muscle Car",  "maxSpeed=44 acceleration=26 grip=6.5 driftAmount=0.8 gearCount=4 maxRpm=6500 torquePeak=0.45 brakeForce=36 rearGrip=0.9 engineBraking=5 handling=0.8"},
            {"Pickup",      "maxSpeed=30 acceleration=12 grip=7.5 driftAmount=0.3 gearCount=5 brakeForce=30 maxSteerAngle=34 leanAmount=1.3 handling=0.6"},
            {"Heavy Truck", "maxSpeed=22 acceleration=7 reverseSpeed=6 grip=6.5 driftAmount=0.1 gearCount=6 maxRpm=3500 idleRpm=600 brakeForce=22 maxSteerAngle=28 leanAmount=1.6 antiRoll=0.8 handling=0.4 boostEnabled=0"},
            {"Bus",         "maxSpeed=20 acceleration=6 reverseSpeed=5 grip=6.5 driftAmount=0.1 gearCount=5 maxRpm=3000 idleRpm=600 brakeForce=20 maxSteerAngle=30 leanAmount=1.6 antiRoll=0.8 handling=0.4 boostEnabled=0"},
            {"Buggy",       "maxSpeed=30 acceleration=18 grip=6 driftAmount=0.65 gearCount=4 brakeForce=30 maxSteerAngle=38 drivetrain=2 airControl=0.8 mudGrip=0.8 grassGrip=0.9 handling=0.85"},
            {"Rally Car",   "maxSpeed=40 acceleration=22 grip=7 driftAmount=0.75 gearCount=6 maxRpm=7500 drivetrain=2 brakeForce=40 maxSteerAngle=36 mudGrip=0.85 grassGrip=0.9 iceGrip=0.4 handling=0.8"},
            {"Drift Car",   "maxSpeed=40 acceleration=22 grip=7.5 driftAmount=1.0 gearCount=5 handbrakeGrip=0.25 counterSteerAssist=0.9 slipLimit=40 maxSteerAngle=42 stabilityControl=0.2 handling=0.9"},
        };
        return p;
    }
    /// Defaults, then the named preset. Returns false for an unknown name (and changes nothing). Always the open-world model.
    bool ApplyPreset(const std::string& name) {
        for (const Preset& p : Presets()) {
            if (name != p.name) continue;
            ResetTuning(); openWorld = true; FromLine(p.values);
            return true;
        }
        return false;
    }
};

} // namespace okay
