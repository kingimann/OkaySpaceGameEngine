#include "okay/Input/PadProfiles.hpp"
#include "okay/Core/Prefs.hpp"
#include <algorithm>
#include <cstdio>
#include <deque>

namespace okay {
namespace {
struct State {
    PadProfile pads[Input::kMaxGamepads];
    ControllerSetup setup[Input::kMaxGamepads];
    std::deque<PadEvent> events;
    std::function<void(const std::string&)> toast;
    bool lightPending[Input::kMaxGamepads] = {};
    int light[Input::kMaxGamepads][3] = {};
    bool mirror = false;
    bool trigPending[Input::kMaxGamepads] = {};
    float trig[Input::kMaxGamepads][3] = {};
    bool swapPending = false; int swapA = 0, swapB = 0;
};
State& S() { static State s; return s; }
bool Ok(int slot) { return slot >= 0 && slot < Input::kMaxGamepads; }
const char* PowerName(int p) {
    switch (p) { case 0: return "empty"; case 1: return "low"; case 2: return "medium"; case 3: return "full"; case 4: return "wired"; default: return "unknown"; }
}
std::string Hat(std::uint8_t h) {
    if (!h) return "centre";
    std::string s;
    auto add = [&](bool on, const char* n) { if (on) s += std::string(s.empty() ? "" : "+") + n; };
    add(h & HatUp, "up"); add(h & HatDown, "down"); add(h & HatLeft, "left"); add(h & HatRight, "right");
    return s;
}
std::string MapKey(const std::string& k) { return "padmap." + k; }
void Notify(PadEvent e) {
    State& st = S();
    if (st.events.size() >= 16) st.events.pop_front();
    st.events.push_back(e);
    if (st.toast) st.toast(e.text);
}
}

void PadProfiles::Connect(int slot, int instanceId, const GamepadIdentity& id, const RawPadState& idle, bool builtinMapped, bool notify) {
    if (!Ok(slot)) return;
    PadProfile& p = S().pads[slot];
    p = PadProfile{};
    p.present = true; p.instanceId = instanceId; p.id = id; p.hostMapped = builtinMapped;
    GamepadIdentity ident = id;
    ident.axes = (int)idle.axes.size(); ident.buttons = (int)idle.buttons.size(); ident.hats = (int)idle.hats.size();
    p.info = ClassifyGamepad(ident);
    p.deviceKey = DeviceKey(id.vendor, id.product, ident.axes, ident.buttons, ident.hats);
    p.raw = idle;
    p.generic = BuildGenericMapping(idle, &p.info);
    ControllerMapping saved;
    if (LoadMapping(p.deviceKey, saved)) { p.mapping = saved; p.mapSource = MapSource::User; p.useMapping = true; }
    else if (!builtinMapped) { p.mapping = p.generic; p.mapSource = MapSource::Generic; p.useMapping = true; }
    else p.mapSource = MapSource::Builtin;
    S().setup[slot] = ControllerSetup{};
    PadEvent e; e.connected = true; e.slot = slot; e.name = id.name; e.familyName = p.info.familyName;
    e.text = "Controller " + std::to_string(slot + 1) + " connected: " + (id.name.empty() ? p.info.familyName : id.name);
    if (notify) Notify(e);
}

void PadProfiles::Disconnect(int slot, bool notify) {
    if (!Ok(slot) || !S().pads[slot].present) return;
    PadEvent e; e.connected = false; e.slot = slot; e.name = S().pads[slot].id.name; e.familyName = S().pads[slot].info.familyName;
    e.text = "Controller " + std::to_string(slot + 1) + " disconnected: " + (e.name.empty() ? e.familyName : e.name);
    S().pads[slot] = PadProfile{};
    S().setup[slot] = ControllerSetup{};
    if (notify) Notify(e);
}

void PadProfiles::Update(int slot, const RawPadState& raw, int power) {
    if (!Ok(slot) || !S().pads[slot].present) return;
    PadProfile& p = S().pads[slot];
    p.raw = raw; p.power = power;
    if (S().setup[slot].Active()) S().setup[slot].Feed(raw);
    const ControllerMapping* m = Mapping(slot);
    if (m) p.logical = ApplyMapping(*m, raw);
}
PadProfile& PadProfiles::Edit(int slot) { static PadProfile none; return Ok(slot) ? S().pads[slot] : none; }
void PadProfiles::Reset() { S() = State{}; }
const PadProfile& PadProfiles::Get(int slot) { static const PadProfile none; return Ok(slot) ? S().pads[slot] : none; }

const ControllerMapping* PadProfiles::Mapping(int slot) {
    if (!Ok(slot)) return nullptr;
    const PadProfile& p = S().pads[slot];
    return p.present && p.useMapping ? &p.mapping : nullptr;
}

std::string PadProfiles::ButtonName(int slot, int b) {
    if (Ok(slot) && S().pads[slot].present) return PadButtonName(b, S().pads[slot].info.family);
    // Nothing plugged in: name it for the family the game last told Input about.
    switch (Input::GamepadFamily(Ok(slot) ? slot : 0)) {
        case Input::PadFamily::PlayStation: return PadButtonName(b, ControllerFamily::DualSense);
        case Input::PadFamily::Switch: return PadButtonName(b, ControllerFamily::SwitchPro);
        default: return PadButtonName(b, ControllerFamily::Unknown);
    }
}

std::string PadProfiles::BindingLabel(const InputBinding& b) {
    if (b.kind != InputBinding::Kind::PadButton) return b.Label(Input::GamepadFamily(Input::LastGamepad()));
    int slot = -1;
    if (b.player > 0 && Ok(b.player) && S().pads[b.player].present) slot = b.player;
    else if (Ok(Input::LastGamepad()) && S().pads[Input::LastGamepad()].present) slot = Input::LastGamepad();
    else for (int i = 0; i < Input::kMaxGamepads && slot < 0; ++i) if (S().pads[i].present) slot = i;
    if (slot < 0) return b.Label();
    return PadButtonName(b.code, S().pads[slot].info.family) + (b.player > 0 ? " (Pad " + std::to_string(b.player + 1) + ")" : std::string());
}

std::string PadProfiles::BatteryText(int slot) { return Ok(slot) ? PowerName(S().pads[slot].power) : "unknown"; }

std::string PadProfiles::Describe(int slot) {
    if (!Ok(slot) || !S().pads[slot].present) return "Controller " + std::to_string(slot + 1) + ": none connected";
    const PadProfile& p = S().pads[slot];
    std::string s = "Controller " + std::to_string(slot + 1) + ": " + (p.id.name.empty() ? "(no name)" : p.id.name) + "\n";
    s += "ids " + FormatUsbId(p.id.vendor, p.id.product) + "  sdl type " + std::to_string(p.id.sdlType) + "  family " + p.info.familyName +
         (p.info.recognised ? "" : " (not recognised: generic)") + "  found by " + p.info.matchedBy + "\n";
    s += std::string("mapping ") + MapSourceName(p.mapSource) + "  key " + p.deviceKey + "  raw " + std::to_string(p.raw.axes.size()) + " axes " +
         std::to_string(p.raw.buttons.size()) + " buttons " + std::to_string(p.raw.hats.size()) + " hats  battery " + BatteryText(slot);
    std::string ex;
    auto add = [&](bool on, const char* n) { if (on) ex += std::string(ex.empty() ? "" : ", ") + n; };
    add(p.info.touchpad, "touchpad"); add(p.info.gyro, "gyro"); add(p.info.lightbar, "lightbar"); add(p.info.playerLeds, "player LEDs");
    add(p.info.adaptiveTriggers, "adaptive triggers"); add(p.info.triggerRumble, "trigger rumble"); add(p.info.hdHaptics, "HD haptics");
    s += "\nhas: " + (ex.empty() ? std::string("rumble only") : ex);
    ex.clear();
    add(p.touchpads > 0, "touchpad"); add(p.hostGyro, "gyro"); add(p.hostAccel, "accelerometer"); add(p.hostTriggerRumble, "trigger rumble");
    s += "\nreadable here: " + (ex.empty() ? std::string("sticks and buttons") : "sticks and buttons, " + ex);
    return s;
}

std::string PadProfiles::RawText(int slot) {
    if (!Ok(slot) || !S().pads[slot].present) return "";
    const RawPadState& r = S().pads[slot].raw;
    std::string s = "axes";
    for (std::size_t i = 0; i < r.axes.size(); ++i) { char b[16]; std::snprintf(b, sizeof b, " %.2f", r.axes[i] / 32767.0f); s += b; }
    s += "\nbuttons";
    bool any = false;
    for (std::size_t i = 0; i < r.buttons.size(); ++i) if (r.buttons[i]) { s += (any ? "," : " ") + std::to_string(i); any = true; }
    if (!any) s += " none";
    for (std::size_t i = 0; i < r.hats.size(); ++i) s += "   hat" + std::to_string(i) + " " + Hat(r.hats[i]);
    return s;
}

bool PadProfiles::BeginSetup(int slot) {
    if (!Ok(slot) || !S().pads[slot].present) return false;
    const PadProfile& p = S().pads[slot];
    S().setup[slot].Begin(p.raw.Empty() ? RawPadState{} : p.raw, p.info.family);
    return true;
}
ControllerSetup* PadProfiles::Setup(int slot) { return Ok(slot) ? &S().setup[slot] : nullptr; }
void PadProfiles::CancelSetup(int slot) { if (Ok(slot)) S().setup[slot].Cancel(); }

void PadProfiles::SaveMapping(const std::string& key, const ControllerMapping& m) { Prefs::SetString(MapKey(key), m.Serialize()); }
bool PadProfiles::LoadMapping(const std::string& key, ControllerMapping& out) {
    if (!Prefs::Has(MapKey(key))) return false;
    return ControllerMapping::Parse(Prefs::GetString(MapKey(key)), out);
}
bool PadProfiles::HasSaved(int slot) { ControllerMapping m; return Ok(slot) && S().pads[slot].present && LoadMapping(S().pads[slot].deviceKey, m); }

bool PadProfiles::SaveSetup(int slot) {
    if (!Ok(slot) || !S().pads[slot].present) return false;
    const ControllerSetup& su = S().setup[slot];
    if (su.Result().MappedCount() == 0) return false;
    const std::string key = S().pads[slot].deviceKey;
    SaveMapping(key, su.Result());
    for (PadProfile& p : S().pads)
        if (p.present && p.deviceKey == key) { p.mapping = su.Result(); p.mapSource = MapSource::User; p.useMapping = true; }
    return true;
}

void PadProfiles::ClearSaved(int slot) {
    if (!Ok(slot) || !S().pads[slot].present) return;
    const std::string key = S().pads[slot].deviceKey;
    Prefs::Delete(MapKey(key));
    for (PadProfile& p : S().pads) {
        if (!p.present || p.deviceKey != key) continue;
        if (p.hostMapped) { p.useMapping = false; p.mapSource = MapSource::Builtin; }
        else { p.mapping = p.generic; p.useMapping = true; p.mapSource = MapSource::Generic; }
    }
}

bool PadProfiles::PopEvent(PadEvent& out) {
    State& st = S();
    if (st.events.empty()) return false;
    out = st.events.front(); st.events.pop_front();
    return true;
}
void PadProfiles::SetToastHandler(std::function<void(const std::string&)> fn) { S().toast = std::move(fn); }
void PadProfiles::RequestLightbar(int slot, int r, int g, int b) {
    if (!Ok(slot)) return;
    S().lightPending[slot] = true;
    S().light[slot][0] = std::clamp(r, 0, 255); S().light[slot][1] = std::clamp(g, 0, 255); S().light[slot][2] = std::clamp(b, 0, 255);
}
bool PadProfiles::TakeLightbar(int slot, int& r, int& g, int& b) {
    if (!Ok(slot) || !S().lightPending[slot]) return false;
    S().lightPending[slot] = false; r = S().light[slot][0]; g = S().light[slot][1]; b = S().light[slot][2];
    return true;
}
void PadProfiles::SetMirrorTriggers(bool on) { S().mirror = on; }
bool PadProfiles::MirrorTriggers() { return S().mirror; }
void PadProfiles::RequestTriggerRumble(int slot, float l, float r, float sec) {
    if (!Ok(slot)) return;
    S().trigPending[slot] = true;
    S().trig[slot][0] = std::clamp(l, 0.0f, 1.0f); S().trig[slot][1] = std::clamp(r, 0.0f, 1.0f); S().trig[slot][2] = std::max(0.0f, sec);
}
bool PadProfiles::TakeTriggerRumble(int slot, float& l, float& r, float& sec) {
    if (!Ok(slot) || !S().trigPending[slot]) return false;
    S().trigPending[slot] = false; l = S().trig[slot][0]; r = S().trig[slot][1]; sec = S().trig[slot][2];
    return true;
}
void PadProfiles::RequestSwap(int a, int b) { if (Ok(a) && Ok(b) && a != b) { S().swapPending = true; S().swapA = a; S().swapB = b; } }
bool PadProfiles::TakeSwap(int& a, int& b) {
    if (!S().swapPending) return false;
    S().swapPending = false; a = S().swapA; b = S().swapB; return true;
}

} // namespace okay
