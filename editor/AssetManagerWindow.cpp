// The Asset Manager window (AssetManagerWindow.hpp): what the launcher's Marketplace downloaded, listed by type and
// imported into the open project. Moved out of main.cpp unchanged (docs/editor/main_split_plan.md, move 5).
#include "EditorCommon.hpp"
#include "AssetManagerWindow.hpp"
#include "MainShared.hpp"          // g_showAssetManager, g_showProject, g_pingAsset, OkayExeDir, IsModelExt, SysNoWindow, GetThumb, extide (main.cpp)
#include "ConsolePanel.hpp"        // ConsoleLog
#include "EditorWidgets.hpp"       // IconButton, AccentToggleButton, SearchField, TbIcon
#include "okay/Platform/Platform.hpp"   // okay::platform
#include "okay/Core/FileCopy.hpp"   // CopyFileOver: copy_file with overwrite_existing fails over an existing file on Windows (MinGW)

namespace oked {

// ---- Asset Manager -------------------------------------------------------------
// Everything the user downloaded from the Marketplace in the launcher lands in
// <launcher>/downloads (the editor ships beside the launcher), each file with a
// "<file>.okayasset" sidecar the launcher writes (title / author / kind). This
// window lists them by type and imports them into the open project's Assets
// (archives are extracted into their own folder), then pings the result in the
// Project panel.
struct DlAsset {
    std::string path, file, title, author, kind, ext;
    int cat = 0;                 // 1 model, 2 texture, 3 audio, 4 script, 5 package, 6 scene, 7 font, 0 other
    long long size = 0;
    long long when = 0;          // unix seconds (sidecar time, else file time)
};
static const char* kAssetCats[] = {"Other", "Models", "Textures", "Audio", "Scripts", "Packages", "Scenes", "Fonts"};
static int AssetCategoryOf(const std::string& extLower) {
    if (IsModelExt(extLower)) return 1;
    for (const char* e : {".png", ".jpg", ".jpeg", ".tga", ".bmp", ".gif", ".hdr", ".psd"}) if (extLower == e) return 2;
    for (const char* e : {".wav", ".ogg", ".mp3", ".flac"}) if (extLower == e) return 3;
    for (const char* e : {".okay", ".lua", ".cs", ".okayvs"}) if (extLower == e) return 4;
    for (const char* e : {".zip", ".okaypkg", ".7z", ".tar", ".gz"}) if (extLower == e) return 5;
    if (extLower == ".okayscene" || extLower == ".okayprefab") return 6;
    if (extLower == ".ttf" || extLower == ".otf") return 7;
    return 0;
}
static std::string AssetDownloadsDir() {
    if (const char* e = std::getenv("OKAY_DOWNLOADS_DIR"); e && *e) return e;
    return (std::filesystem::path(OkayExeDir()) / "downloads").string();
}
static std::vector<DlAsset> ScanDownloadedAssets() {
    namespace fs = std::filesystem;
    std::vector<DlAsset> out;
    std::error_code ec;
    fs::path dir = AssetDownloadsDir();
    if (!fs::is_directory(dir, ec)) return out;
    for (const auto& de : fs::directory_iterator(dir, ec)) {
        if (!de.is_regular_file(ec)) continue;
        std::string ext = de.path().extension().string();
        for (auto& c : ext) c = (char)std::tolower((unsigned char)c);
        if (ext == ".part" || ext == ".okayasset") continue;
        DlAsset a;
        a.path = de.path().string(); a.file = de.path().filename().string(); a.ext = ext;
        a.size = (long long)de.file_size(ec);
        a.title = de.path().stem().string();
        {
            auto ft = fs::last_write_time(de.path(), ec);
            if (!ec) a.when = (long long)std::chrono::duration_cast<std::chrono::seconds>(
                ft.time_since_epoch() - fs::file_time_type::clock::now().time_since_epoch() +
                std::chrono::system_clock::now().time_since_epoch()).count();
        }
        std::ifstream meta(a.path + ".okayasset");
        std::string line;
        while (std::getline(meta, line)) {
            auto eq = line.find('=');
            if (eq == std::string::npos) continue;
            std::string k = line.substr(0, eq), v = line.substr(eq + 1);
            if (k == "title" && !v.empty()) a.title = v;
            else if (k == "author") a.author = v;
            else if (k == "kind") a.kind = v;
            else if (k == "time") a.when = std::atoll(v.c_str());
        }
        a.cat = AssetCategoryOf(ext);
        out.push_back(std::move(a));
    }
    std::sort(out.begin(), out.end(), [](const DlAsset& x, const DlAsset& y) { return x.when > y.when; });
    return out;
}
// Where an asset is (or would be) imported inside the project.
static std::filesystem::path AssetImportTarget(EditorState& ed, const DlAsset& a) {
    namespace fs = std::filesystem;
    fs::path assets = ed.projectDir().empty() ? fs::path("Assets") : fs::path(ed.projectDir()) / "Assets";
    fs::path base = assets / "Downloaded" / kAssetCats[a.cat];
    if (a.cat == 5) {   // archives extract into a folder named after the item
        std::string nm;
        for (char c : a.title) nm += (std::isalnum((unsigned char)c) || c == '-' || c == '_' || c == ' ') ? c : '_';
        if (nm.empty()) nm = fs::path(a.file).stem().string();
        return base / nm;
    }
    return base / a.file;
}
static bool ImportDownloadedAsset(EditorState& ed, const DlAsset& a, std::string& err) {
    namespace fs = std::filesystem;
    std::error_code ec;
    fs::path dst = AssetImportTarget(ed, a);
    if (a.cat == 5) {
        fs::create_directories(dst, ec);
        if (ec) { err = "Couldn't create " + dst.string(); return false; }
        std::string z = a.path, d = dst.string();
#if defined(_WIN32)
        bool ok = SysNoWindow("tar -xf \"" + z + "\" -C \"" + d + "\"") == 0 ||
                  SysNoWindow("powershell -NoProfile -Command \"Expand-Archive -Force -LiteralPath '" + z +
                              "' -DestinationPath '" + d + "'\"") == 0;
#else
        bool ok = SysNoWindow("unzip -o \"" + z + "\" -d \"" + d + "\" >/dev/null 2>&1") == 0 ||
                  SysNoWindow("tar -xf \"" + z + "\" -C \"" + d + "\" 2>/dev/null") == 0;
#endif
        if (!ok) { err = "Couldn't extract the archive."; return false; }
    } else {
        fs::create_directories(dst.parent_path(), ec);
        okay::CopyFileOver(a.path, dst, ec);
        if (ec) { err = ec.message(); return false; }
    }
    g_pingAsset = dst.string();
    g_showProject = true;
    return true;
}
void DrawAssetManager(EditorState& ed) {
    namespace fs = std::filesystem;
    if (ImGuiWindow* con = ImGui::FindWindowByName("###Console"))
        if (con->DockId) ImGui::SetNextWindowDockID(con->DockId, ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Asset Manager", &g_showAssetManager)) { ImGui::End(); return; }
    static std::vector<DlAsset> s_items;
    static Uint32 s_nextScan = 0;
    static char s_search[96] = "";
    static int s_cat = -1;          // -1 = all
    static bool s_grid = false;
    if (okay::platform::TicksMs32() >= s_nextScan) { s_items = ScanDownloadedAssets(); s_nextScan = okay::platform::TicksMs32() + 3000; }
    const ImGuiStyle& st = ImGui::GetStyle();
    const float fh = ImGui::GetFrameHeight();

    // Toolbar: [search............] [view] [refresh] [open folder]
    SearchField("##amsearch", s_search, sizeof(s_search), "Search downloads", -(fh * 3.0f + st.ItemSpacing.x * 3.0f));
    ImGui::SameLine();
    if (IconButton("##amgrid", s_grid ? TbIcon::List : TbIcon::Grid, s_grid ? "List view" : "Grid view")) s_grid = !s_grid;
    ImGui::SameLine();
    if (IconButton("##amrefresh", TbIcon::Rotate, "Rescan the downloads folder")) s_nextScan = 0;
    ImGui::SameLine();
    if (IconButton("##amfolder", TbIcon::Folder, "Open the downloads folder")) {
        std::error_code ec; fs::create_directories(AssetDownloadsDir(), ec);
        extide::RevealInFiles(AssetDownloadsDir());
    }
    // Category chips with counts.
    int counts[8] = {0};
    for (const auto& a : s_items) counts[a.cat]++;
    auto chip = [&](const char* label, int cat, int n) {
        char b[48]; std::snprintf(b, sizeof(b), "%s  %d##amc%d", label, n, cat);
        if (AccentToggleButton(b, s_cat == cat)) s_cat = cat;
        ImGui::SameLine(0, 4);
    };
    chip("All", -1, (int)s_items.size());
    for (int c : {1, 2, 3, 4, 5, 6, 7, 0}) if (counts[c]) chip(kAssetCats[c], c, counts[c]);
    ImGui::NewLine();
    ImGui::Separator();

    if (s_items.empty()) {
        ImGui::Spacing();
        ImGui::TextUnformatted("No downloaded assets yet.");
        ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
        ImGui::TextWrapped("Get models, textures, sounds and scripts from the Marketplace in the OkaySpace launcher "
                           "and they show up here, ready to import into this project.");
        ImGui::TextWrapped("Folder: %s", AssetDownloadsDir().c_str());
        ImGui::PopStyleColor();
        ImGui::End();
        return;
    }
    std::string needle = s_search;
    for (auto& c : needle) c = (char)std::tolower((unsigned char)c);
    std::vector<const DlAsset*> vis;
    for (const auto& a : s_items) {
        if (s_cat >= 0 && a.cat != s_cat) continue;
        if (!needle.empty()) {
            std::string hay = a.title + " " + a.author + " " + a.kind + " " + a.file;
            for (auto& c : hay) c = (char)std::tolower((unsigned char)c);
            if (hay.find(needle) == std::string::npos) continue;
        }
        vis.push_back(&a);
    }
    auto fmtSize = [](long long b) {
        char s[32];
        if (b >= (1LL << 30)) std::snprintf(s, sizeof(s), "%.1f GB", b / 1073741824.0);
        else if (b >= (1LL << 20)) std::snprintf(s, sizeof(s), "%.1f MB", b / 1048576.0);
        else if (b >= 1024) std::snprintf(s, sizeof(s), "%.0f KB", b / 1024.0);
        else std::snprintf(s, sizeof(s), "%lld B", b);
        return std::string(s);
    };
    auto fmtAgo = [](long long when) {
        long long d = (long long)std::time(nullptr) - when;
        char s[32];
        if (when <= 0) return std::string("-");
        if (d < 3600) std::snprintf(s, sizeof(s), "%lldm ago", d / 60 < 1 ? 1 : d / 60);
        else if (d < 86400) std::snprintf(s, sizeof(s), "%lldh ago", d / 3600);
        else std::snprintf(s, sizeof(s), "%lldd ago", d / 86400);
        return std::string(s);
    };
    static const ImU32 kCatCol[] = {IM_COL32(150, 155, 165, 255), IM_COL32(110, 170, 255, 255), IM_COL32(235, 160, 90, 255),
                                    IM_COL32(120, 210, 140, 255), IM_COL32(230, 205, 90, 255), IM_COL32(175, 135, 240, 255),
                                    IM_COL32(90, 200, 210, 255), IM_COL32(220, 130, 180, 255)};
    // Type tile: a thumbnail for images, else a coloured badge with the extension.
    auto tile = [&](const DlAsset& a, float sz) {
        ImVec2 p = ImGui::GetCursorScreenPos();
        ImDrawList* dl = ImGui::GetWindowDrawList();
        SDL_Texture* th = a.cat == 2 && a.size < 32ll * 1024 * 1024 ? GetThumb(a.path) : nullptr;
        if (th) ImGui::Image((ImTextureID)th, ImVec2(sz, sz));
        else {
            ImGui::Dummy(ImVec2(sz, sz));
            ImU32 c = kCatCol[a.cat];
            dl->AddRectFilled(p, ImVec2(p.x + sz, p.y + sz), (c & 0x00FFFFFF) | 0x38000000, 4.0f);
            dl->AddRect(p, ImVec2(p.x + sz, p.y + sz), (c & 0x00FFFFFF) | 0x90000000, 4.0f);
            std::string e = a.ext.size() > 1 ? a.ext.substr(1) : "?";
            for (auto& ch : e) ch = (char)std::toupper((unsigned char)ch);
            ImVec2 ts = ImGui::CalcTextSize(e.c_str());
            dl->AddText(ImVec2(p.x + (sz - ts.x) * 0.5f, p.y + (sz - ts.y) * 0.5f), c, e.c_str());
        }
    };
    static std::string s_err;
    auto actions = [&](const DlAsset& a, bool compact) {
        std::error_code ec;
        bool inProject = fs::exists(AssetImportTarget(ed, a), ec);
        ImGui::BeginDisabled(ed.projectDir().empty());
        const char* lbl = inProject ? "Re-import" : "Import";
        if (compact ? ImGui::SmallButton(lbl) : ImGui::Button(lbl)) {
            std::string err;
            if (ImportDownloadedAsset(ed, a, err)) ConsoleLog("Imported " + a.title + " into Assets/Downloaded/" + kAssetCats[a.cat]);
            else { s_err = a.title + ": " + err; ConsoleLog("Import failed - " + s_err, 2); }
        }
        ImGui::EndDisabled();
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
            ImGui::SetTooltip(ed.projectDir().empty() ? "Open a project first" :
                              a.cat == 5 ? "Extract into Assets/Downloaded/Packages" : "Copy into Assets/Downloaded");
        if (inProject) {
            ImGui::SameLine();
            ImGui::TextColored(ImVec4(0.45f, 0.85f, 0.55f, 1.0f), "in project");
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Click to show it in the Project panel");
            if (ImGui::IsItemClicked()) { g_pingAsset = AssetImportTarget(ed, a).string(); g_showProject = true; }
        }
    };
    auto rowMenu = [&](const DlAsset& a) {
        if (ImGui::BeginPopupContextItem("##amctx")) {
            if (ImGui::MenuItem("Show in folder")) extide::RevealInFiles(fs::path(a.path).parent_path().string());
            if (ImGui::MenuItem("Delete download (permanent, no Undo)")) {
                std::error_code ec; fs::remove(a.path, ec); fs::remove(a.path + ".okayasset", ec);
                s_nextScan = 0;
            }
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Deletes the downloaded file from disk for good. It does not go to the trash and Ctrl+Z cannot bring it back; you can download it again.");
            ImGui::EndPopup();
        }
    };
    ImGui::BeginChild("##amlist", ImVec2(0, 0));
    if (!s_grid) {
        if (ImGui::BeginTable("##amtable", 5, ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_PadOuterX)) {
            ImGui::TableSetupColumn("Asset",  ImGuiTableColumnFlags_WidthStretch, 3.0f);
            ImGui::TableSetupColumn("Type",   ImGuiTableColumnFlags_WidthStretch, 1.0f);
            ImGui::TableSetupColumn("Size",   ImGuiTableColumnFlags_WidthStretch, 0.8f);
            ImGui::TableSetupColumn("Added",  ImGuiTableColumnFlags_WidthStretch, 0.8f);
            ImGui::TableSetupColumn("",       ImGuiTableColumnFlags_WidthStretch, 1.6f);
            ImGui::TableHeadersRow();
            for (const DlAsset* ap : vis) {
                const DlAsset& a = *ap;
                ImGui::PushID(a.path.c_str());
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                float tsz = fh * 1.5f;
                tile(a, tsz);
                rowMenu(a);
                ImGui::SameLine();
                ImGui::BeginGroup();
                ImGui::TextUnformatted(a.title.c_str());
                if (!a.author.empty()) ImGui::TextDisabled("by %s", a.author.c_str());
                else                   ImGui::TextDisabled("%s", a.file.c_str());
                ImGui::EndGroup();
                ImGui::TableNextColumn();
                ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(kCatCol[a.cat]), "%s", a.kind.empty() ? kAssetCats[a.cat] : a.kind.c_str());
                ImGui::TableNextColumn(); ImGui::TextDisabled("%s", fmtSize(a.size).c_str());
                ImGui::TableNextColumn(); ImGui::TextDisabled("%s", fmtAgo(a.when).c_str());
                ImGui::TableNextColumn(); actions(a, false);
                ImGui::PopID();
            }
            ImGui::EndTable();
        }
    } else {
        float cell = fh * 5.0f;
        int cols = std::max(1, (int)((ImGui::GetContentRegionAvail().x + st.ItemSpacing.x) / (cell + st.ItemSpacing.x)));
        int i = 0;
        for (const DlAsset* ap : vis) {
            const DlAsset& a = *ap;
            ImGui::PushID(a.path.c_str());
            if (i % cols) ImGui::SameLine();
            ImGui::BeginGroup();
            tile(a, cell);
            rowMenu(a);
            std::string t = a.title;
            while (t.size() > 3 && ImGui::CalcTextSize(t.c_str()).x > cell) t = t.substr(0, t.size() - 4) + "...";
            ImGui::TextUnformatted(t.c_str());
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s\n%s  -  %s", a.title.c_str(), kAssetCats[a.cat], fmtSize(a.size).c_str());
            actions(a, true);
            ImGui::EndGroup();
            ImGui::PopID();
            ++i;
        }
    }
    if (vis.empty()) ImGui::TextDisabled("Nothing matches.");
    ImGui::EndChild();
    ImGui::End();
}

} // namespace oked
