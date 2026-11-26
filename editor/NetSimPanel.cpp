#include "NetSimPanel.hpp"
#include "OkayPanels.hpp"   // DrawNetSimOkayUI: the window is drawn by OkayUI only
#include "okay/Net/NetSim.hpp"

namespace oked {

bool g_showNet = false;

// The simulator's switches (edited in place through the model's pointers) and the last full second's traffic per object.
void DrawNetPanel(bool* open) {
    okay::net::SimSettings& sim = okay::net::Simulator();
    okaypanels::NetSimModel model;
    model.enabled = &sim.enabled;
    model.latencyMs = &sim.latencyMs;
    model.jitterMs = &sim.jitterMs;
    model.loss = &sim.loss;
    model.duplicate = &sim.duplicate;
    const okay::net::TrafficSample& t = okay::net::Traffic();
    model.wireInPerSec = t.wireInPerSec;
    model.wireOutPerSec = t.wireOutPerSec;
    model.rows.reserve(t.rows.size());
    for (const okay::net::TrafficRow& r : t.rows) model.rows.push_back({r.name, r.messagesPerSec, r.bytesPerSec});
    okaypanels::NetSimIntent intent;
    DrawNetSimOkayUI(open, model, intent);
}

} // namespace oked
