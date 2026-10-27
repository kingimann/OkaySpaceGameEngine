#include "NetSimPanel.hpp"
#include "okay/Net/NetSim.hpp"
#include "imgui.h"

namespace oked {

bool g_showNet = false;

void DrawNetPanel(bool* open) {
    if (!ImGui::Begin("Net###Net", open)) { ImGui::End(); return; }

    okay::net::SimSettings& sim = okay::net::Simulator();
    ImGui::Checkbox("Simulate", &sim.enabled);
    ImGui::SameLine();
    ImGui::TextDisabled("off = packets go straight to the socket");
    ImGui::SliderFloat("Latency (ms)", &sim.latencyMs, 0.0f, 500.0f, "%.0f");
    ImGui::SliderFloat("Jitter (ms)", &sim.jitterMs, 0.0f, 200.0f, "%.0f");
    float lossPct = sim.loss * 100.0f;
    float dupPct = sim.duplicate * 100.0f;
    if (ImGui::SliderFloat("Loss (%)", &lossPct, 0.0f, 50.0f, "%.1f"))
        sim.loss = lossPct / 100.0f;
    if (ImGui::SliderFloat("Duplicate (%)", &dupPct, 0.0f, 50.0f, "%.1f"))
        sim.duplicate = dupPct / 100.0f;
    if (sim.latencyMs < 0.0f) sim.latencyMs = 0.0f;
    if (sim.jitterMs < 0.0f) sim.jitterMs = 0.0f;
    if (sim.loss < 0.0f) sim.loss = 0.0f;
    if (sim.loss > 1.0f) sim.loss = 1.0f;
    if (sim.duplicate < 0.0f) sim.duplicate = 0.0f;
    if (sim.duplicate > 1.0f) sim.duplicate = 1.0f;

    ImGui::Separator();
    const okay::net::TrafficSample& t = okay::net::Traffic();
    ImGui::Text("Wire  in %.1f KB/s    out %.1f KB/s",
                t.wireInPerSec / 1024.0f, t.wireOutPerSec / 1024.0f);
    ImGui::TextDisabled("Last full second. Objects are synced ids, RPC names, or channels.");
    if (ImGui::BeginTable("nettraffic", 3, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_ScrollY,
                          ImVec2(0, 220))) {
        ImGui::TableSetupColumn("Object");
        ImGui::TableSetupColumn("Msg/s", ImGuiTableColumnFlags_WidthFixed, 70.0f);
        ImGui::TableSetupColumn("B/s", ImGuiTableColumnFlags_WidthFixed, 80.0f);
        ImGui::TableHeadersRow();
        for (const okay::net::TrafficRow& row : t.rows) {
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::TextUnformatted(row.name.c_str());
            ImGui::TableSetColumnIndex(1);
            ImGui::Text("%.1f", row.messagesPerSec);
            ImGui::TableSetColumnIndex(2);
            ImGui::Text("%.0f", row.bytesPerSec);
        }
        ImGui::EndTable();
    }
    ImGui::End();
}

} // namespace oked
