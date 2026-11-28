// The Connect to Claude panel (View > Automation Link): the link's switch, "Allow scripts from Automation", setting up
// Claude Desktop and Claude Code in one click (shared/ClaudeConnect.hpp, the one copy of that code), and a status line.
// Drawn with the editor's ImGui-named calls (OkayUI underneath). Our own code.
#include "AutomationEditor.hpp"

#include "ClaudeConnect.hpp"
#include "imgui.h"

namespace oked::automation {

void DrawConnectPanel(okay::editor::EditorState& ed, const std::string& exeDir) {
    bool& open = PanelFlag();
    if (!open) return;
    ImGui::SetNextWindowSize(ImVec2(560, 0), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Connect to Claude", &open)) { ImGui::End(); return; }
    static std::string note;   // what the last button did
    const std::string okayExe = okay::claudeconnect::FindOkayProgram(exeDir);

    ImGui::Checkbox("Automation Link", &LinkFlag());
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Let an AI agent (Claude) work in this editor through okay mcp: its scene, selection, Undo and Play.\n"
                          "This computer only (127.0.0.1), with a new secret token each time. Remembered.");
    ImGui::Checkbox("Allow scripts from Automation", &ScriptsFlag());
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Let the agent run OkayScript, Lua or JavaScript here (script.run). Each run is one Undo step and\n"
                          "writes a line in the Console. Lua and JavaScript have no file, network or program access.");

    ImGui::Separator();
    const std::string status = LinkStatus();
    ImGui::TextUnformatted(status.empty() ? "Automation link off." : status.c_str());
    if (!LinkProject().empty()) ImGui::Text("Project: %s", LinkProject().c_str());
    else if (!ed.projectDir().empty()) ImGui::TextDisabled("Project: %s", ed.projectDir().c_str());
    ImGui::Text("Agents connected: %d", LinkClients());
    ImGui::Text("Last command: %s", LinkLastCommand().empty() ? "none yet" : LinkLastCommand().c_str());

    ImGui::Separator();
    const std::string config = okay::claudeconnect::DesktopConfigPath();
    if (ImGui::Button("Set up Claude Desktop")) {
        const okay::claudeconnect::SetupResult r = okay::claudeconnect::AddToDesktopConfig(config, okayExe);
        note = r.message;
        if (r.ok) LinkFlag() = true;   // Claude needs the link on to find this editor
    }
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Adds (or updates) only the \"okayspace\" server in Claude Desktop's settings file, keeps everything else,\n"
                          "and saves a copy of the old file first (claude_desktop_config.json.bak). It also turns the link on.");
    ImGui::SameLine();
    if (ImGui::Button("Copy Claude Code command")) {
        if (okayExe.empty()) note = "The okay program (okay.exe) was not found beside the editor or in its Tools folder.";
        else {
            const std::string line = okay::claudeconnect::ClaudeCodeCommand(okayExe);
            ImGui::SetClipboardText(line.c_str());
            note = "Copied: " + line + "  (paste it into a terminal, then start Claude Code in any folder).";
        }
    }
    ImGui::TextDisabled("Claude Desktop settings: %s", config.empty() ? "(not found on this computer)" : config.c_str());
    ImGui::TextDisabled("okay program: %s", okayExe.empty() ? "(not found)" : okayExe.c_str());
    if (!note.empty()) { ImGui::Spacing(); ImGui::TextWrapped("%s", note.c_str()); }
    ImGui::End();
}

}  // namespace oked::automation
