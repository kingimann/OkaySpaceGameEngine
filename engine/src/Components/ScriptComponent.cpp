#include "okay/Components/ScriptComponent.hpp"
#include "okay/Scene/GameObject.hpp"
#include "okay/Core/Log.hpp"
#include <algorithm>
#include "okay/Physics/Collider2D.hpp"
#include "okay/Physics/Collider3D.hpp"
#include "okay/Physics/Physics2D.hpp"
#include "okay/Physics/Physics3D.hpp"

#include <fstream>
#include <sstream>
#include <cctype>
#include <cstdlib>

namespace okay {

// Parse an inspector value string into a VsValue: true/false -> bool, a number
// -> float, new Vector3(x,y,z) -> Vec3, otherwise a string (quotes stripped).
static vs::VsValue ParseFieldValue(const std::string& raw) {
    std::string s = raw;
    auto a = s.find_first_not_of(" \t");
    auto b = s.find_last_not_of(" \t");
    if (a == std::string::npos) return vs::VsValue{std::string{}};
    s = s.substr(a, b - a + 1);
    if (s == "true")  return vs::VsValue{true};
    if (s == "false") return vs::VsValue{false};
    if (s.size() >= 2 && (s.front() == '"' || s.front() == '\'') && s.back() == s.front())
        return vs::VsValue{s.substr(1, s.size() - 2)};
    // Vector3 / Vector2 literal: pull the numbers out of the parentheses.
    if (s.find("Vector3") != std::string::npos || s.find("Vector2") != std::string::npos ||
        s.find("Vec3") != std::string::npos) {
        auto lp = s.find('('), rp = s.rfind(')');
        if (lp != std::string::npos && rp != std::string::npos && rp > lp) {
            std::string inside = s.substr(lp + 1, rp - lp - 1);
            for (char& c : inside) if (c == ',') c = ' ';
            std::istringstream is(inside);
            Vec3 v{0, 0, 0}; is >> v.x >> v.y >> v.z;
            return vs::VsValue{v};
        }
    }
    // Numeric? (allow a leading sign, digits, one dot, and an f/d suffix)
    char* end = nullptr;
    double d = std::strtod(s.c_str(), &end);
    if (end && end != s.c_str()) {
        while (*end == 'f' || *end == 'F' || *end == 'd' || *end == 'D') ++end;
        if (*end == '\0') return vs::VsValue{(float)d};
    }
    return vs::VsValue{s};
}

void ScriptComponent::ApplyFieldOverrides() {
    if (!m_vm) return;
    for (const auto& kv : fields)
        m_vm->SetGlobal(kv.first, ParseFieldValue(kv.second));
}

bool ScriptComponent::LoadSource(const std::string& source, std::string* error) {
    m_source = source;
    m_vm = CreateScriptVM(m_language);
    if (!m_vm) {
        std::string e = "scripting backend '" + m_language + "' is not available in this build";
        if (error) *error = e;
        OKAY_ERROR(e);
        return false;
    }
    bool ok = m_vm->Load(source, error);
    if (ok) ApplyFieldOverrides();   // override the script's defaults with inspector values
    return ok;
}

bool ScriptComponent::LoadFile(const std::string& path, std::string* error) {
    std::ifstream f(path);
    if (!f) { if (error) *error = "cannot open " + path; return false; }
    std::stringstream ss;
    ss << f.rdbuf();
    m_path = path;
    return LoadSource(ss.str(), error);
}

void ScriptComponent::Start() {
    if (!m_vm) return;
    if (!ScriptsRun(gameObject)) { m_startDeferred = true; return; }
    m_startDeferred = false;
    m_host.gameObject = gameObject;
    m_host.transform  = transform;
    m_host.component  = this;
    m_vm->Bind(&m_host);
    m_vm->CallStart();
    m_started = true;
}

void ScriptComponent::Update(float dt) {
    if (!m_vm || !ScriptsRun(gameObject)) return;
    if (m_startDeferred) Start();
    m_host.transform = transform;
    // FixedUpdate(): whole fixed steps for the time that passed (at most 5 per frame
    // so a long hitch can't snowball), before this frame's Update.
    m_fixedAcc = std::min(m_fixedAcc + dt, kFixedStep * 5.0f);
    if (m_fixedAcc >= kFixedStep) {
        m_host.deltaTime = kFixedStep;
        while (m_fixedAcc >= kFixedStep) { m_fixedAcc -= kFixedStep; m_vm->CallEvent("on_fixed_update"); }
    }
    m_host.deltaTime = dt;
    m_vm->CallUpdate(dt);
}

void ScriptComponent::LateUpdate(float) {
    if (m_vm && !m_startDeferred && ScriptsRun(gameObject)) m_vm->CallEvent("on_late_update");
}

void ScriptComponent::OnDestroy() {
    if (!m_vm || m_startDeferred || !ScriptsRun(gameObject)) return;   // never started: nothing to tear down
    // OnDisable once: not again if the object was already switched off.
    if (gameObject && gameObject->active) m_vm->CallEvent("on_disable");
    m_vm->CallEvent("on_destroy");
}

void ScriptComponent::OnEnable() {
    if (m_vm && m_started && ScriptsRun(gameObject)) m_vm->CallEvent("on_enable");
}

void ScriptComponent::OnDisable() {
    if (m_vm && m_started && ScriptsRun(gameObject)) m_vm->CallEvent("on_disable");
}

void ScriptComponent::FireWithObject(const char* event, GameObject* other) {
    if (!m_vm || !ScriptsRun(gameObject)) return;
    vs::VsValue name(other ? other->name : std::string());
    m_vm->SetGlobal("other", name);
    m_vm->CallEventWith(event, name);
}
// Contact details for the handler about to run: contact_point / contact_normal
// (vectors), the same as contact_x/_y/_z and contact_nx/_ny/_nz numbers,
// impact_speed (how hard it hit; 0 for triggers) and other_tag.
void ScriptComponent::SetContact(const Vec3& point, const Vec3& normal, float impactSpeed,
                                 GameObject* other) {
    if (!m_vm) return;
    m_vm->SetGlobal("contact_point", vs::VsValue(point));
    m_vm->SetGlobal("contact_normal", vs::VsValue(normal));
    m_vm->SetGlobal("contact_x", vs::VsValue(point.x));
    m_vm->SetGlobal("contact_y", vs::VsValue(point.y));
    m_vm->SetGlobal("contact_z", vs::VsValue(point.z));
    m_vm->SetGlobal("contact_nx", vs::VsValue(normal.x));
    m_vm->SetGlobal("contact_ny", vs::VsValue(normal.y));
    m_vm->SetGlobal("contact_nz", vs::VsValue(normal.z));
    m_vm->SetGlobal("impact_speed", vs::VsValue(impactSpeed));
    m_vm->SetGlobal("other_tag", vs::VsValue(other ? other->tag : std::string()));
}
void ScriptComponent::FireOther(const char* event, Collider2D* c) {
    const TriggerContact2D* tc = Physics2D::CurrentTrigger();
    SetContact(tc ? Vec3{tc->point} : Vec3::Zero, tc ? Vec3{tc->normal} : Vec3::Zero, 0.0f,
               c ? c->gameObject : nullptr);
    FireWithObject(event, c ? c->gameObject : nullptr);
}
void ScriptComponent::FireOther(const char* event, Collider3D* c) {
    const TriggerContact3D* tc = Physics3D::CurrentTrigger();
    SetContact(tc ? tc->point : Vec3::Zero, tc ? tc->normal : Vec3::Zero, 0.0f,
               c ? c->gameObject : nullptr);
    FireWithObject(event, c ? c->gameObject : nullptr);
}
void ScriptComponent::FireOther(const char* event, const Collision2D& c) {
    SetContact(Vec3{c.point}, Vec3{c.normal}, c.impactSpeed, c.gameObject);
    FireWithObject(event, c.gameObject);
}
void ScriptComponent::FireOther(const char* event, const Collision3D& c) {
    SetContact(c.point, c.normal, c.impactSpeed, c.gameObject);
    FireWithObject(event, c.gameObject);
}

} // namespace okay
