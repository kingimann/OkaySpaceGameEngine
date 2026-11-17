// Module ai_navigation: its scene records (moved here unchanged from SceneSerializer.cpp, which now finds them through the
// ComponentSerializerRegistry). Linked only when the module is on.
#include "okay/Components/NavMeshSurface.hpp"
#include "okay/Components/NavMeshAgent.hpp"
#include "okay/Components/NavMeshModifier.hpp"
#include "okay/Components/NavMeshLink.hpp"
#include "okay/Components/NavMeshObstacle.hpp"
#include "okay/Modules/ModuleRegistry.hpp"
#include "okay/Scene/ComponentSerializer.hpp"
#include "okay/Scene/GameObject.hpp"
#include "okay/Scene/RecordIO.hpp"
#include <algorithm>

namespace okay {
namespace modules {
using rec::Quote; using rec::ReadQuoted; using rec::KvOut; using rec::KvIn;

void Register_ai_navigation(ModuleContext&) {
    {
        ComponentSerializerRegistry::Entry e;
        e.name = "navsurface";
        e.write = [](const GameObject& go, std::ostream& out) {
    for (auto* ns : go.GetComponents<NavMeshSurface>()) {
        out << "  navsurface";
        KvOut k{out};
        k("radius", ns->settings.agentRadius)("height", ns->settings.agentHeight)("slope", ns->settings.maxSlope)
         ("step", ns->settings.maxStep)("cell", ns->settings.cellSize)("cellh", ns->settings.cellHeight)
         .str("path", ns->navPath).b("autob", ns->autoBounds)
         ("bcx", ns->boundsCenter.x)("bcy", ns->boundsCenter.y)("bcz", ns->boundsCenter.z)
         ("bsx", ns->boundsSize.x)("bsy", ns->boundsSize.y)("bsz", ns->boundsSize.z)
         .b("dyn", ns->includeDynamic)("layers", ns->layerMask).b("bakeplay", ns->bakeBeforePlay);
        for (int a = 0; a < kNavAreaCount; ++a) {   // only what differs from the defaults
            if (ns->areaCost[a] != NavAreaDefaultCost(a)) k(("ac" + std::to_string(a)).c_str(), ns->areaCost[a]);
            if (!ns->areaName[a].empty()) k.str(("an" + std::to_string(a)).c_str(), ns->areaName[a]);
        }
        out << "\n";
    }
        };
        e.read = [](GameObject& go, std::istream& in) {
                    auto* ns = go.AddComponent<NavMeshSurface>();
                    KvIn k(in);
                    k.f("radius", ns->settings.agentRadius); k.f("height", ns->settings.agentHeight);
                    k.f("slope", ns->settings.maxSlope); k.f("step", ns->settings.maxStep);
                    k.f("cell", ns->settings.cellSize); k.f("cellh", ns->settings.cellHeight);
                    k.str("path", ns->navPath); k.b("autob", ns->autoBounds);
                    k.f("bcx", ns->boundsCenter.x); k.f("bcy", ns->boundsCenter.y); k.f("bcz", ns->boundsCenter.z);
                    k.f("bsx", ns->boundsSize.x); k.f("bsy", ns->boundsSize.y); k.f("bsz", ns->boundsSize.z);
                    k.b("dyn", ns->includeDynamic); k.u("layers", ns->layerMask); k.b("bakeplay", ns->bakeBeforePlay);
                    for (int a = 0; a < kNavAreaCount; ++a) {
                        k.f(("ac" + std::to_string(a)).c_str(), ns->areaCost[a]);
                        k.str(("an" + std::to_string(a)).c_str(), ns->areaName[a]);
                    }
                    ns->sceneDir = rec::LoadingSceneDir();   // the .okaynav is looked for next to the scene too
        };
        ComponentSerializerRegistry::Register(std::move(e));
    }
    {
        ComponentSerializerRegistry::Entry e;
        e.name = "navagent";
        e.write = [](const GameObject& go, std::ostream& out) {
    for (auto* na : go.GetComponents<NavMeshAgent>()) {
        out << "  navagent";
        KvOut k{out};
        k("radius", na->radius)("height", na->height)("speed", na->speed)("accel", na->acceleration)
         ("angular", na->angularSpeed)("stop", na->stoppingDistance).b("repath", na->autoRepath)
         ("repathT", na->repathInterval)("prio", na->avoidancePriority)("avoid", na->avoidanceRange)
         .b("brake", na->autoBraking).b("face", na->faceMovement).b("anim", na->driveAnimation)("base", na->baseOffset)
         ("mask", na->areaMask);
        out << "\n";
    }
        };
        e.read = [](GameObject& go, std::istream& in) {
                    auto* na = go.AddComponent<NavMeshAgent>();
                    KvIn k(in);
                    k.f("radius", na->radius); k.f("height", na->height); k.f("speed", na->speed);
                    k.f("accel", na->acceleration); k.f("angular", na->angularSpeed); k.f("stop", na->stoppingDistance);
                    k.b("repath", na->autoRepath); k.f("repathT", na->repathInterval); k.i("prio", na->avoidancePriority);
                    k.f("avoid", na->avoidanceRange); k.b("brake", na->autoBraking); k.b("face", na->faceMovement);
                    k.b("anim", na->driveAnimation); k.f("base", na->baseOffset); k.u("mask", na->areaMask);
        };
        ComponentSerializerRegistry::Register(std::move(e));
    }
    {
        ComponentSerializerRegistry::Entry e;
        e.name = "navmod";
        e.write = [](const GameObject& go, std::ostream& out) {
    for (auto* nm : go.GetComponents<NavMeshModifier>()) {
        out << "  navmod";
        KvOut k{out};
        k("area", nm->area).b("kids", nm->applyToChildren);
        out << "\n";
    }
        };
        e.read = [](GameObject& go, std::istream& in) {
                    auto* nm = go.AddComponent<NavMeshModifier>();
                    KvIn k(in);
                    k.i("area", nm->area); k.b("kids", nm->applyToChildren);
                    nm->area = std::clamp(nm->area, 0, kNavAreaCount - 1);
        };
        ComponentSerializerRegistry::Register(std::move(e));
    }
    {
        ComponentSerializerRegistry::Entry e;
        e.name = "navlink";
        e.write = [](const GameObject& go, std::ostream& out) {
    for (auto* nl : go.GetComponents<NavMeshLink>()) {
        out << "  navlink";
        KvOut k{out};
        k("type", (int)nl->type)("sx", nl->startPoint.x)("sy", nl->startPoint.y)("sz", nl->startPoint.z)
         ("ex", nl->endPoint.x)("ey", nl->endPoint.y)("ez", nl->endPoint.z).b("both", nl->bidirectional)
         ("cost", nl->cost)("area", nl->area)("speed", nl->speed)("arc", nl->arcHeight);
        out << "\n";
    }
        };
        e.read = [](GameObject& go, std::istream& in) {
                    auto* nl = go.AddComponent<NavMeshLink>();
                    KvIn k(in);
                    int ty = 0; k.i("type", ty); nl->type = (NavMeshLink::Type)std::clamp(ty, 0, 2);
                    k.f("sx", nl->startPoint.x); k.f("sy", nl->startPoint.y); k.f("sz", nl->startPoint.z);
                    k.f("ex", nl->endPoint.x); k.f("ey", nl->endPoint.y); k.f("ez", nl->endPoint.z);
                    k.b("both", nl->bidirectional); k.f("cost", nl->cost); k.i("area", nl->area);
                    k.f("speed", nl->speed); k.f("arc", nl->arcHeight);
                    nl->area = std::clamp(nl->area, 0, kNavAreaCount - 1);
        };
        ComponentSerializerRegistry::Register(std::move(e));
    }
    {
        ComponentSerializerRegistry::Entry e;
        e.name = "navobstacle";
        e.write = [](const GameObject& go, std::ostream& out) {
    for (auto* no : go.GetComponents<NavMeshObstacle>()) {
        out << "  navobstacle";
        KvOut k{out};
        k("sx", no->size.x)("sy", no->size.y)("sz", no->size.z)("cx", no->center.x)("cy", no->center.y)("cz", no->center.z)
         ("shape", (int)no->shape)("cr", no->capsuleRadius)("ch", no->capsuleHeight).b("carve", no->carve)
         .b("still", no->carveOnlyWhenStationary)("tts", no->timeToStationary)("move", no->moveThreshold);
        out << "\n";
    }
        };
        e.read = [](GameObject& go, std::istream& in) {
                    auto* no = go.AddComponent<NavMeshObstacle>();
                    KvIn k(in);
                    k.f("sx", no->size.x); k.f("sy", no->size.y); k.f("sz", no->size.z);
                    k.f("cx", no->center.x); k.f("cy", no->center.y); k.f("cz", no->center.z);
                    int sh = 0; k.i("shape", sh); no->shape = (NavMeshObstacle::Shape)std::clamp(sh, 0, 1);
                    k.f("cr", no->capsuleRadius); k.f("ch", no->capsuleHeight);
                    k.b("carve", no->carve); k.b("still", no->carveOnlyWhenStationary);
                    k.f("tts", no->timeToStationary); k.f("move", no->moveThreshold);
        };
        ComponentSerializerRegistry::Register(std::move(e));
    }
}
} // namespace modules
} // namespace okay
