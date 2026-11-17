// Module ragdoll: its scene records (moved here unchanged from SceneSerializer.cpp, which now finds them through the
// ComponentSerializerRegistry). Linked only when the module is on.
#include "okay/Components/Ragdoll.hpp"
#include "okay/Animation/HumanoidBones.hpp"
#include "okay/Modules/ModuleRegistry.hpp"
#include "okay/Scripting/ScriptApiRegistry.hpp"
#include "okay/Scene/ComponentSerializer.hpp"
#include "okay/Scene/GameObject.hpp"
#include "okay/Scene/RecordIO.hpp"
#include <algorithm>

namespace okay {
namespace modules {
using rec::Quote; using rec::ReadQuoted; using rec::KvOut; using rec::KvIn;

void Register_ragdoll(ModuleContext&) {
    // ragdoll() / ragdoll(0) goes limp / stands up; ragdoll_push(vx, vy, vz) shoves the chest (or the part nearest an optional px, py, pz).
    AddScriptApi("ragdoll", [](GameObject* self, std::vector<vs::VsValue>& a) -> vs::VsValue {
        auto* rd = self ? self->GetComponent<Ragdoll>() : nullptr;
        if (!rd) return vs::VsValue{0.0f};
        if (!a.empty() && a[0].AsFloat() == 0.0f) { rd->Deactivate(); return vs::VsValue{1.0f}; }
        return vs::VsValue{rd->Activate() ? 1.0f : 0.0f};
    });
    AddScriptApi("ragdoll_push", [](GameObject* self, std::vector<vs::VsValue>& a) -> vs::VsValue {
        auto* rd = self ? self->GetComponent<Ragdoll>() : nullptr;
        if (!rd || a.size() < 3) return vs::VsValue{0.0f};
        const Vec3 v{a[0].AsFloat(), a[1].AsFloat(), a[2].AsFloat()};
        const Vec3 at = a.size() >= 6 ? Vec3{a[3].AsFloat(), a[4].AsFloat(), a[5].AsFloat()}
                                      : rd->ParticlePosition(rd->BoneParticle(humanoid::Torso));
        rd->Hit(at, v);
        return vs::VsValue{1.0f};
    });
    AddScriptApi("is_ragdoll", [](GameObject* self, std::vector<vs::VsValue>&) -> vs::VsValue {
        auto* rd = self ? self->GetComponent<Ragdoll>() : nullptr;
        return vs::VsValue{rd && rd->IsActive() ? 1.0f : 0.0f};
    });
    {
        ComponentSerializerRegistry::Entry e;
        e.name = "ragdoll";
        e.write = [](const GameObject& go, std::ostream& out) {
    for (auto* rd : go.GetComponents<Ragdoll>()) {
        out << "  ragdoll";
        KvOut k{out};
        k("grav", rd->gravity)("damp", rd->damping)("fric", rd->friction)("radius", rd->radius)
         ("iters", rd->iterations)("maxSpeed", rd->maxSpeed).b("onDeath", rd->ragdollOnDeath)
         ("deathPush", rd->deathImpulse).b("hits", rd->hitReactions)("hitPush", rd->hitImpulse)
         ("recover", rd->hitRecover);
        out << "\n";
    }
        };
        e.read = [](GameObject& go, std::istream& in) {
                    auto* rd = go.AddComponent<Ragdoll>();
                    KvIn k(in);
                    k.f("grav", rd->gravity); k.f("damp", rd->damping); k.f("fric", rd->friction);
                    k.f("radius", rd->radius); k.i("iters", rd->iterations); k.f("maxSpeed", rd->maxSpeed);
                    k.b("onDeath", rd->ragdollOnDeath); k.f("deathPush", rd->deathImpulse);
                    k.b("hits", rd->hitReactions); k.f("hitPush", rd->hitImpulse); k.f("recover", rd->hitRecover);
        };
        ComponentSerializerRegistry::Register(std::move(e));
    }
}
} // namespace modules
} // namespace okay
