// Weapons module: the crosshair scene record.
// The record is still written and read at its old place in the scene file, so a save
// with every module on is byte-identical. The weapon script commands stay in the
// OkayScript VM: those files are under an open claim on another lane.
#include "okay/Modules/ModuleRegistry.hpp"
#include "okay/Scene/ComponentSerializer.hpp"
#include "okay/Scene/GameObject.hpp"
#include "okay/Components/Crosshair.hpp"
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

} // namespace

void Register_weapons(ModuleContext&) {
    ComponentSerializerRegistry::Entry rec;
    rec.name = "crosshair";
    rec.write = &WriteCrosshair;
    rec.read = &ReadCrosshair;
    rec.inChain = true;   // the scene chain calls this at the old crosshair site
    ComponentSerializerRegistry::Register(std::move(rec));
}

} // namespace modules
} // namespace okay
