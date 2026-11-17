// Module ragdoll: its scene records (moved here unchanged from SceneSerializer.cpp, which now finds them through the
// ComponentSerializerRegistry). Linked only when the module is on.
#include "okay/Components/Ragdoll.hpp"
#include "okay/Modules/ModuleRegistry.hpp"
#include "okay/Scene/ComponentSerializer.hpp"
#include "okay/Scene/GameObject.hpp"
#include "okay/Scene/RecordIO.hpp"
#include <algorithm>

namespace okay {
namespace modules {
using rec::Quote; using rec::ReadQuoted; using rec::KvOut; using rec::KvIn;

void Register_ragdoll(ModuleContext&) {
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
