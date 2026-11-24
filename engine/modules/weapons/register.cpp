// Weapons module: the crosshair scene record and the OkayScript weapon commands.
// The record is still written and read at its old place in the scene file, so a save
// with every module on is byte-identical. The script commands used to be hand-written
// in the OkayScript VM.
#include "okay/Modules/ModuleRegistry.hpp"
#include "okay/Scene/ComponentSerializer.hpp"
#include "okay/Scripting/ScriptApiRegistry.hpp"
#include "okay/Components/Crosshair.hpp"
#include "okay/Components/MeleeAttacker.hpp"
#include "okay/Components/RangedWeapon.hpp"
#include <iostream>

namespace okay {
namespace modules {

namespace {

void WriteCrosshair(const GameObject& go, std::ostream& out) {
    for (auto* cr : go.GetComponents<Crosshair>()) {
        out << "  crosshair " << cr->position.x << " " << cr->position.y << " "
            << cr->size.x << " " << cr->size.y << " "
            << cr->color.r << " " << cr->color.g << " " << cr->color.b << " " << cr->color.a << " "
            << cr->thickness << " " << cr->gap << " " << cr->length << " "
            << (cr->showLines ? 1 : 0) << " " << (cr->dot ? 1 : 0) << " " << cr->dotSize << " "
            << cr->dotColor.r << " " << cr->dotColor.g << " " << cr->dotColor.b << " " << cr->dotColor.a << " "
            << (cr->outline ? 1 : 0) << " "
            << cr->outlineColor.r << " " << cr->outlineColor.g << " " << cr->outlineColor.b << " " << cr->outlineColor.a
            << " " << (int)cr->anchor;
        out << " " << (cr->lineUp ? 1 : 0) << " " << (cr->lineDown ? 1 : 0)
            << " " << (cr->lineLeft ? 1 : 0) << " " << (cr->lineRight ? 1 : 0) << " " << cr->spread
            << " " << (cr->circle ? 1 : 0) << " " << cr->circleRadius << " " << cr->circleThickness
            << " " << cr->circleColor.r << " " << cr->circleColor.g << " " << cr->circleColor.b << " " << cr->circleColor.a
            << " " << cr->spreadRecover << " " << cr->spreadMax;
        out << "\n";
    }
}

void ReadCrosshair(GameObject& go, std::istream& in) {
    auto* cr = go.AddComponent<Crosshair>();
    Color c, dc, oc; int sl = 1, dt = 0, ol = 1, an = 0;
    in >> cr->position.x >> cr->position.y >> cr->size.x >> cr->size.y
       >> c.r >> c.g >> c.b >> c.a
       >> cr->thickness >> cr->gap >> cr->length
       >> sl >> dt >> cr->dotSize
       >> dc.r >> dc.g >> dc.b >> dc.a
       >> ol >> oc.r >> oc.g >> oc.b >> oc.a >> an;
    cr->color = c; cr->dotColor = dc; cr->outlineColor = oc;
    cr->showLines = (sl != 0); cr->dot = (dt != 0); cr->outline = (ol != 0);
    cr->anchor = (UIAnchor)an;
    in >> std::ws;
    int cpk = in.peek();
    if (std::isdigit(cpk) || cpk == '-' || cpk == '.') {
        int lu = 1, ld = 1, ll = 1, lr = 1, ci = 0; Color qc;
        in >> lu >> ld >> ll >> lr >> cr->spread
           >> ci >> cr->circleRadius >> cr->circleThickness
           >> qc.r >> qc.g >> qc.b >> qc.a;
        cr->lineUp = (lu != 0); cr->lineDown = (ld != 0);
        cr->lineLeft = (ll != 0); cr->lineRight = (lr != 0);
        cr->circle = (ci != 0); cr->circleColor = qc;
    }
    in >> std::ws;
    int rpk = in.peek();
    if (std::isdigit(rpk) || rpk == '-' || rpk == '.')
        in >> cr->spreadRecover >> cr->spreadMax;
}

ScriptApiEntry WeaponFn(const char* name, ScriptApiFn fn) {
    ScriptApiEntry e;
    e.name = name;
    e.module = "weapons";
    e.ret = ScriptApiType::Float;
    e.fn = std::move(fn);
    return e;
}

} // namespace

void Register_weapons(ModuleContext&) {
    ComponentSerializerRegistry::Entry rec;
    rec.name = "crosshair";
    rec.write = &WriteCrosshair;
    rec.read = &ReadCrosshair;
    rec.inChain = true;   // the scene chain calls this at the old crosshair site
    ComponentSerializerRegistry::Register(std::move(rec));

    AddScriptApi(WeaponFn("weapon_swing", [](GameObject* self, std::vector<vs::VsValue>&) {
        auto* m = self ? self->GetComponent<MeleeAttacker>() : nullptr;
        if (!m) return vs::VsValue{0.0f};
        m->Swing();
        return vs::VsValue{1.0f};
    }));
    AddScriptApi(WeaponFn("weapon_fire", [](GameObject* self, std::vector<vs::VsValue>&) {
        auto* g = self ? self->GetComponent<RangedWeapon>() : nullptr;
        return vs::VsValue{g && g->Fire() ? 1.0f : 0.0f};
    }));
    AddScriptApi(WeaponFn("weapon_reload", [](GameObject* self, std::vector<vs::VsValue>&) {
        auto* g = self ? self->GetComponent<RangedWeapon>() : nullptr;
        if (!g) return vs::VsValue{0.0f};
        g->Reload();
        return vs::VsValue{1.0f};
    }));
    AddScriptApi(WeaponFn("weapon_ammo", [](GameObject* self, std::vector<vs::VsValue>&) {
        auto* g = self ? self->GetComponent<RangedWeapon>() : nullptr;
        return vs::VsValue{(float)(g ? g->Rounds() : 0)};
    }));
}

} // namespace modules
} // namespace okay
