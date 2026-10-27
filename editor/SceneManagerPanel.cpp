// Scene Manager: list the project's .okayscene files and open, create, rename,
// duplicate or delete them, and set the build order. The first ticked scene is
// the one a build starts on. The list is <project>/.okay/scene_build.txt and is
// copied into Build Settings so File > Build Settings ships the same order.
#include "SceneManagerPanel.hpp"
#include "EditorCommon.hpp"
#include "ConsolePanel.hpp"

#include <unordered_set>

namespace oked {

namespace {
namespace fs = std::filesystem;

struct Row {
    std::string path;
    bool on = false;
    bool exists = false;
};

std::string s_proj = "\x01";
std::vector<SceneBuildEntry> s_written;
bool s_forceScan = false;
std::string s_scanRoot;
std::uint32_t s_scanAt = 0;
std::vector<std::string> s_disk;

std::string CanonDir(const std::string& d) {
    if (d.empty()) return {};
    std::error_code ec;
    fs::path c = fs::weakly_canonical(fs::path(d), ec);
    return ec ? d : c.generic_string();
}

std::string CanonFile(const std::string& p) {
    if (p.empty()) return {};
    std::error_code ec;
    fs::path in(p);
    fs::path c = fs::weakly_canonical(in, ec);
    if (!ec) return c.generic_string();
    c = fs::absolute(in, ec);
    if (ec) return in.generic_string();
    return c.lexically_normal().generic_string();
}

bool ExistsFile(const fs::path& p) {
    std::error_code ec;
    return fs::is_regular_file(p, ec);
}

fs::path AssetsRoot(EditorState& ed) {
    std::error_code ec;
    if (ed.projectDir().empty()) return fs::absolute(".", ec);
    return fs::path(ed.projectDir()) / "Assets";
}

bool UnderDir(const fs::path& root, const std::string& file) {
    std::string r = CanonDir(root.string());
    std::string f = CanonFile(file);
    if (r.empty() || f.size() < r.size()) return false;
    if (f.compare(0, r.size(), r) != 0) return false;
    return f.size() == r.size() || f[r.size()] == '/';
}

std::string FileStem(const std::string& path) {
    return fs::path(path).stem().string();
}

bool SameList(const std::vector<SceneBuildEntry>& a, const std::vector<SceneBuildEntry>& b) {
    if (a.size() != b.size()) return false;
    for (std::size_t i = 0; i < a.size(); ++i)
        if (a[i].on != b[i].on || a[i].path != b[i].path) return false;
    return true;
}

std::string BuildFile(EditorState& ed) {
    if (ed.projectDir().empty()) return {};
    return (fs::path(ed.projectDir()) / ".okay" / "scene_build.txt").string();
}

std::vector<SceneBuildEntry> LoadFile(EditorState& ed) {
    std::vector<SceneBuildEntry> out;
    std::string path = BuildFile(ed);
    if (path.empty()) return out;
    std::ifstream f(path);
    if (!f) return out;
    std::string line;
    while (std::getline(f, line)) {
        while (!line.empty() && (line.back() == '\r' || line.back() == '\n')) line.pop_back();
        if (line.size() < 3 || line[0] == '#' || line[1] != '|') continue;
        if (line[0] != '0' && line[0] != '1') continue;
        std::string stored = line.substr(2);
        fs::path p(stored);
        if (!p.is_absolute()) p = fs::path(ed.projectDir()) / p;
        std::string abs = CanonFile(p.string());
        if (!abs.empty()) out.push_back(SceneBuildEntry{abs, line[0] == '1'});
    }
    return out;
}

void SaveFile(EditorState& ed, const std::vector<SceneBuildEntry>& entries) {
    std::string path = BuildFile(ed);
    if (path.empty()) return;
    std::error_code ec;
    fs::create_directories(fs::path(path).parent_path(), ec);
    std::ofstream f(path, std::ios::trunc);
    if (!f) {
        ConsoleLog("Couldn't save the scene build list.", 2);
        return;
    }
    f << "# OkaySpace scene build list. First ticked scene is the start scene.\n";
    for (const SceneBuildEntry& e : entries) {
        std::error_code rec;
        fs::path rel = fs::relative(fs::path(e.path), fs::path(ed.projectDir()), rec);
        std::string stored = (!rec && !rel.empty()) ? rel.generic_string() : e.path;
        f << (e.on ? '1' : '0') << '|' << stored << '\n';
    }
}

const std::vector<std::string>& DiskScenes(EditorState& ed) {
    fs::path root = AssetsRoot(ed);
    std::string key = root.generic_string();
    std::uint32_t now = SDL_GetTicks();
    if (!s_forceScan && key == s_scanRoot && now - s_scanAt < 1000) return s_disk;
    s_forceScan = false;
    s_scanRoot = key;
    s_scanAt = now;
    s_disk.clear();
    std::error_code ec;
    if (!fs::exists(root, ec)) return s_disk;
    int seen = 0;
    for (auto it = fs::recursive_directory_iterator(root, fs::directory_options::skip_permission_denied, ec);
         !ec && it != fs::recursive_directory_iterator(); it.increment(ec)) {
        if (++seen > 20000) break;
        if (!it->is_regular_file(ec)) continue;
        if (it->path().extension() == ".okayscene") s_disk.push_back(CanonFile(it->path().string()));
    }
    std::sort(s_disk.begin(), s_disk.end());
    s_disk.erase(std::unique(s_disk.begin(), s_disk.end()), s_disk.end());
    return s_disk;
}

void Publish(EditorState& ed) {
    std::vector<std::string> want;
    for (const SceneBuildEntry& e : SceneBuildSnapshot())
        if (e.on && !e.path.empty()) want.push_back(e.path);
    if (want.empty() && !ed.projectDir().empty()) want = DiskScenes(ed);
    const std::vector<std::string>& cur = SceneManager::Scenes();
    if (cur.size() == want.size() && std::equal(cur.begin(), cur.end(), want.begin())) return;
    SceneManager::ClearScenes();
    for (const std::string& p : want) SceneManager::AddScene(p);
}

std::vector<Row> Compose(EditorState& ed) {
    std::vector<Row> rows;
    std::unordered_set<std::string> seen;
    for (const SceneBuildEntry& e : SceneBuildSnapshot()) {
        std::string p = CanonFile(e.path);
        if (p.empty() || !seen.insert(p).second) continue;
        rows.push_back(Row{p, e.on, ExistsFile(p)});
    }
    for (const std::string& p : DiskScenes(ed))
        if (seen.insert(p).second) rows.push_back(Row{p, false, true});
    return rows;
}

std::vector<SceneBuildEntry> ToEntries(const std::vector<Row>& rows) {
    std::vector<SceneBuildEntry> out;
    out.reserve(rows.size());
    for (const Row& r : rows) out.push_back(SceneBuildEntry{r.path, r.on});
    return out;
}

void Commit(EditorState& ed, std::vector<Row> rows) {
    std::vector<SceneBuildEntry> entries = ToEntries(rows);
    SceneBuildApply(ed, entries);
    SaveFile(ed, entries);
    s_written = std::move(entries);
    Publish(ed);
}

bool ValidName(const std::string& name, std::string& err) {
    if (name.empty() || name == "." || name == "..") { err = "Enter a name."; return false; }
    if (name.size() > 80) { err = "That name is too long."; return false; }
    for (unsigned char c : name) {
        if (c < 32 || c == '/' || c == '\\' || c == ':' || c == '*' || c == '?' ||
            c == '"' || c == '<' || c == '>' || c == '|') {
            err = "That name has a character files can't use.";
            return false;
        }
    }
    return true;
}

std::string StripExt(std::string name) {
    const std::string ext = ".okayscene";
    if (name.size() >= ext.size()) {
        std::string tail = name.substr(name.size() - ext.size());
        for (char& c : tail) c = (char)std::tolower((unsigned char)c);
        if (tail == ext) name.erase(name.size() - ext.size());
    }
    return name;
}

bool WriteTemplate(const std::string& path, const std::string& title, int kind) {
    Scene sc(title);
    sc.runScripts = false;
    GameObject* camObj = sc.CreateGameObject("Main Camera");
    Camera* cam = camObj->AddComponent<Camera>();
    cam->main = true;
    sc.mainCamera = cam;
    if (kind == 1) {
        cam->projection = Camera::Projection::Orthographic;
        camObj->transform->localPosition = {0, 0, 10};
        GameObject* sp = sc.CreateGameObject("Sprite");
        sp->AddComponent<SpriteRenderer>()->color = Color::Green;
    } else if (kind == 2) {
        cam->projection = Camera::Projection::Perspective;
        camObj->transform->localPosition = {0, 2, 10};
        GameObject* ground = sc.CreateGameObject("Ground");
        MeshRenderer* gmr = ground->AddComponent<MeshRenderer>();
        gmr->mesh = Mesh::Plane(20.0f);
        gmr->color = Color::FromBytes(150, 154, 150);
        GameObject* cube = sc.CreateGameObject("Cube");
        MeshRenderer* cmr = cube->AddComponent<MeshRenderer>();
        cmr->mesh = Mesh::Cube(1.0f);
        cmr->color = Color::FromBytes(196, 128, 96);
        cube->transform->localPosition = {0, 0.5f, 0};
        GameObject* sun = sc.CreateGameObject("Sun");
        Light* L = sun->AddComponent<Light>();
        L->type = Light::Type::Directional;
        L->intensity = 1.15f;
        sun->transform->localRotation = Quat::Euler(Vec3{50.0f, -30.0f, 0.0f});
    } else {
        cam->projection = Camera::Projection::Perspective;
        camObj->transform->localPosition = {0, 2, 10};
    }
    return SceneSerializer::SaveToFile(sc, path);
}

void ReplacePath(std::vector<Row>& rows, const std::string& from, const std::string& to) {
    std::string a = CanonFile(from), b = CanonFile(to);
    for (Row& r : rows)
        if (r.path == a) { r.path = b; r.exists = ExistsFile(b); }
}

bool NameTaken(const fs::path& dest) { return ExistsFile(dest); }

} // namespace

void AddRecent(const std::string& path); // defined in main.cpp

void SceneBuildSync(EditorState& ed) {
    const std::string proj = CanonDir(ed.projectDir());
    if (proj != s_proj) {
        s_proj = proj;
        s_forceScan = true;
        std::vector<SceneBuildEntry> loaded;
        if (!proj.empty()) loaded = LoadFile(ed);
        SceneBuildApply(ed, loaded);
        s_written = std::move(loaded);
    } else if (!proj.empty()) {
        std::vector<SceneBuildEntry> live = SceneBuildSnapshot();
        if (!SameList(live, s_written)) {
            SaveFile(ed, live);
            s_written = std::move(live);
        }
    }
    Publish(ed);
}

void DrawSceneManager(EditorState& ed) {
    ImGui::SetNextWindowSize(ImVec2(560, 480), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Scene Manager###Scenes", &g_showScenes)) { ImGui::End(); return; }

    const bool hasProject = !ed.projectDir().empty();
    const bool playing = ed.isPlaying();
    static char filter[64] = "";
    static char createBuf[96] = "";
    static int createKind = 0;
    static char renameBuf[96] = "";
    static std::string renameFrom;
    static std::string deletePath;
    static std::string createErr, renameErr;
    bool openRename = false, openDelete = false;

    ImGui::BeginDisabled(!hasProject || playing);
    if (ImGui::Button("Create")) {
        createBuf[0] = '\0';
        createKind = 0;
        createErr.clear();
        ImGui::OpenPopup("Create Scene");
    }
    ImGui::EndDisabled();
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
        ImGui::SetTooltip(!hasProject ? "Open a project first." : playing ? "Stop Play first." : "Make a new scene file in Assets/Scenes.");
    ImGui::SameLine();
    if (ImGui::Button("Refresh")) s_forceScan = true;
    ImGui::SameLine();
    ImGui::SetNextItemWidth(180);
    ImGui::InputTextWithHint("##scenefilter", "Filter scenes", filter, sizeof(filter));

    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
    if (!hasProject)
        ImGui::TextWrapped("No project is open. Scenes in the working folder are listed; open a project to create and edit them.");
    else
        ImGui::TextWrapped("Ticked scenes ship in this order. The first ticked scene is where the game starts. "
                           "With none ticked, a build ships the open scene only.");
    ImGui::PopStyleColor();
    if (playing) ImGui::TextDisabled("Stop Play to create, rename, duplicate or delete.");

    std::vector<Row> rows = Compose(ed);
    std::string startName;
    for (const Row& r : rows) if (r.on) { startName = FileStem(r.path); break; }
    if (startName.empty()) ImGui::TextDisabled("Start scene: (none — the open scene)");
    else ImGui::Text("Start scene: %s", startName.c_str());

    ImGui::BeginDisabled(!hasProject || rows.empty());
    if (ImGui::SmallButton("Add all to build")) {
        for (Row& r : rows) r.on = true;
        Commit(ed, rows);
        rows = Compose(ed);
    }
    ImGui::SameLine();
    if (ImGui::SmallButton("Clear build list")) {
        for (Row& r : rows) r.on = false;
        Commit(ed, rows);
        rows = Compose(ed);
    }
    ImGui::EndDisabled();
    ImGui::Separator();

    auto lc = [](std::string s) {
        for (char& c : s) c = (char)std::tolower((unsigned char)c);
        return s;
    };
    const std::string q = lc(filter);
    const std::string openPath = CanonFile(ed.path());
    const bool filtering = !q.empty();

    ImGui::BeginChild("##scenelist", ImVec2(0, -ImGui::GetFrameHeightWithSpacing() * 2.2f), true);
    if (rows.empty()) ImGui::TextDisabled("No .okayscene files yet. Create one to start.");
    int shown = 0;
    for (int i = 0; i < (int)rows.size(); ++i) {
        const Row& r = rows[i];
        std::string stem = FileStem(r.path);
        if (!q.empty() && lc(stem).find(q) == std::string::npos && lc(r.path).find(q) == std::string::npos)
            continue;
        ++shown;
        ImGui::PushID(r.path.c_str());
        bool on = r.on;
        if (ImGui::Checkbox("##inbuild", &on)) {
            rows[i].on = on;
            Commit(ed, rows);
        }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Include this scene in the build.");
        ImGui::SameLine();
        if (r.on) {
            int idx = 0;
            for (int j = 0; j < i; ++j) if (rows[j].on) ++idx;
            if (idx == 0) ImGui::TextColored(ImVec4(0.55f, 0.85f, 0.55f, 1), "#0 start");
            else ImGui::Text("#%d", idx);
        } else ImGui::TextDisabled("  -");
        ImGui::SameLine();
        if (SDL_Texture* th = r.exists ? GetSceneThumb(r.path) : nullptr) {
            ImGui::Image((ImTextureID)th, ImVec2(28, 28));
            ImGui::SameLine();
        }
        const bool isOpen = !openPath.empty() && openPath == r.path;
        if (!r.exists) ImGui::TextColored(ImVec4(0.95f, 0.45f, 0.42f, 1), "%s (missing)", stem.c_str());
        else if (isOpen) ImGui::TextColored(ImVec4(0.55f, 0.80f, 1.0f, 1), "%s", stem.c_str());
        else ImGui::TextUnformatted(stem.c_str());
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", r.path.c_str());

        ImGui::SameLine();
        ImGui::BeginDisabled(!r.exists);
        if (ImGui::SmallButton("Open")) {
            std::string err;
            if (ed.Load(r.path, &err)) { ConsoleLog("Opened " + stem); AddRecent(r.path); }
            else ConsoleLog("Open failed: " + err, 2);
        }
        ImGui::EndDisabled();
        ImGui::SameLine();
        ImGui::BeginDisabled(playing || !hasProject);
        if (ImGui::SmallButton("Rename")) {
            renameFrom = r.path;
            std::snprintf(renameBuf, sizeof(renameBuf), "%s", stem.c_str());
            renameErr.clear();
            openRename = true;
        }
        ImGui::SameLine();
        if (ImGui::SmallButton("Duplicate")) {
            if (!r.exists) ConsoleLog("Can't duplicate a missing scene.", 1);
            else if (!UnderDir(AssetsRoot(ed), r.path)) ConsoleLog("That scene is outside the project.", 2);
            else if (isOpen && ed.dirty && !ed.Save(ed.path())) ConsoleLog("Save the open scene before duplicating it.", 2);
            else {
                fs::path src(r.path);
                fs::path dest = src.parent_path() / (src.stem().string() + " copy" + src.extension().string());
                for (int n = 2; ExistsFile(dest) && n < 1000; ++n)
                    dest = src.parent_path() / (src.stem().string() + " copy " + std::to_string(n) + src.extension().string());
                std::error_code ec;
                fs::copy_file(src, dest, fs::copy_options::none, ec);
                if (ec) ConsoleLog("Duplicate failed: " + ec.message(), 2);
                else { s_forceScan = true; Publish(ed); ConsoleLog("Duplicated " + stem); }
            }
        }
        ImGui::SameLine();
        if (ImGui::SmallButton("Delete")) { deletePath = r.path; openDelete = true; }
        ImGui::EndDisabled();
        ImGui::SameLine();
        ImGui::BeginDisabled(filtering || i == 0);
        if (ImGui::ArrowButton("##up", ImGuiDir_Up)) {
            std::swap(rows[i], rows[i - 1]);
            Commit(ed, rows);
        }
        ImGui::EndDisabled();
        ImGui::SameLine(0, 2);
        ImGui::BeginDisabled(filtering || i + 1 >= (int)rows.size());
        if (ImGui::ArrowButton("##dn", ImGuiDir_Down)) {
            std::swap(rows[i], rows[i + 1]);
            Commit(ed, rows);
        }
        ImGui::EndDisabled();
        if (filtering && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
            ImGui::SetTooltip("Clear the filter to reorder.");
        ImGui::SameLine();
        bool isStart = on;
        if (isStart) for (int j = 0; j < i; ++j) if (rows[j].on) isStart = false;
        ImGui::BeginDisabled(isStart);
        if (ImGui::SmallButton("Set start")) {
            Row moved = rows[i];
            moved.on = true;
            rows.erase(rows.begin() + i);
            rows.insert(rows.begin(), std::move(moved));
            Commit(ed, rows);
        }
        ImGui::EndDisabled();
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
            ImGui::SetTooltip(isStart ? "This scene already starts the game." : "Ship this scene first.");
        ImGui::PopID();
    }
    if (shown == 0 && !rows.empty()) ImGui::TextDisabled("No scenes match that filter.");
    ImGui::EndChild();

    if (openRename) ImGui::OpenPopup("Rename Scene");
    if (openDelete) ImGui::OpenPopup("Delete Scene");
    if (ImGui::BeginPopupModal("Rename Scene", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextUnformatted("New name");
        ImGui::SetNextItemWidth(260);
        bool go = ImGui::InputText("##rn", renameBuf, sizeof(renameBuf), ImGuiInputTextFlags_EnterReturnsTrue);
        if (!renameErr.empty()) ImGui::TextColored(ImVec4(0.95f, 0.45f, 0.42f, 1), "%s", renameErr.c_str());
        if (ImGui::Button("Rename", ImVec2(120, 0)) || go) {
            std::string name = StripExt(renameBuf);
            std::string err;
            fs::path dest = fs::path(renameFrom).parent_path() / (name + ".okayscene");
            if (!ValidName(name, err)) renameErr = err;
            else if (!UnderDir(AssetsRoot(ed), renameFrom)) renameErr = "That scene is outside the project.";
            else if (CanonFile(dest.string()) != CanonFile(renameFrom) && NameTaken(dest))
                renameErr = "A scene with that name already exists.";
            else if (CanonFile(dest.string()) == CanonFile(renameFrom)) ImGui::CloseCurrentPopup();
            else {
                bool ok = false;
                std::error_code ec;
                if (CanonFile(ed.path()) == CanonFile(renameFrom)) {
                    if (ed.Save(dest.string())) {
                        fs::remove(renameFrom, ec);
                        AddRecent(dest.string());
                        ok = true;
                    } else ConsoleLog("Rename failed: couldn't save the scene.", 2);
                } else {
                    fs::rename(renameFrom, dest, ec);
                    ok = !ec;
                    if (ec) ConsoleLog("Rename failed: " + ec.message(), 2);
                }
                if (ok) {
                    ReplacePath(rows, renameFrom, dest.string());
                    s_forceScan = true;
                    Commit(ed, rows);
                    ConsoleLog("Renamed to " + name);
                    ImGui::CloseCurrentPopup();
                }
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel##rn", ImVec2(120, 0))) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
    if (ImGui::BeginPopupModal("Delete Scene", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        std::string stemDel = FileStem(deletePath);
        bool openDel = !deletePath.empty() && CanonFile(ed.path()) == CanonFile(deletePath);
        bool missing = deletePath.empty() || !ExistsFile(deletePath);
        if (missing)
            ImGui::TextWrapped("\"%s\" is already missing. Remove it from the list?", stemDel.c_str());
        else if (openDel && ed.dirty)
            ImGui::TextWrapped("Delete \"%s\"? This scene is open and has unsaved changes, which will be discarded. This cannot be undone.", stemDel.c_str());
        else if (openDel)
            ImGui::TextWrapped("Delete \"%s\"? This scene is open, so it will close. This cannot be undone.", stemDel.c_str());
        else
            ImGui::TextWrapped("Delete \"%s\"? The file is removed from the project. This cannot be undone.", stemDel.c_str());
        ImGui::Separator();
        if (ImGui::Button("Delete", ImVec2(120, 0))) {
            bool ok = missing;
            if (!missing) {
                if (hasProject && !UnderDir(AssetsRoot(ed), deletePath)) {
                    ConsoleLog("Refusing to delete a file outside the project Assets folder.", 2);
                } else {
                    std::error_code ec;
                    fs::remove(deletePath, ec);
                    if (ec) ConsoleLog("Delete failed: " + ec.message(), 2);
                    else ok = true;
                }
            }
            if (ok) {
                if (openDel) ed.NewScene();
                const std::string gone = CanonFile(deletePath);
                rows.erase(std::remove_if(rows.begin(), rows.end(), [&](const Row& row) {
                    return row.path == gone;
                }), rows.end());
                s_forceScan = true;
                Commit(ed, rows);
                ConsoleLog(missing ? "Removed " + stemDel + " from the list" : "Deleted " + stemDel);
                deletePath.clear();
                ImGui::CloseCurrentPopup();
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel##del", ImVec2(120, 0))) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }

    if (ImGui::BeginPopupModal("Create Scene", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextDisabled("Saved in Assets/Scenes. The open scene stays as it is.");
        ImGui::SetNextItemWidth(260);
        bool go = ImGui::InputTextWithHint("##cn", "Scene name", createBuf, sizeof(createBuf), ImGuiInputTextFlags_EnterReturnsTrue);
        const char* kinds[] = {"Empty", "2D", "3D"};
        ImGui::SetNextItemWidth(160);
        ImGui::Combo("Template", &createKind, kinds, 3);
        if (!createErr.empty()) ImGui::TextColored(ImVec4(0.95f, 0.45f, 0.42f, 1), "%s", createErr.c_str());
        if (!hasProject) ImGui::TextDisabled("Open a project first.");
        ImGui::BeginDisabled(!hasProject);
        if (ImGui::Button("Create", ImVec2(120, 0)) || (go && hasProject)) {
            std::string name = StripExt(createBuf);
            std::string err;
            fs::path dest = fs::path(ed.projectDir()) / "Assets" / "Scenes" / (name + ".okayscene");
            if (!ValidName(name, err)) createErr = err;
            else if (NameTaken(dest)) createErr = "A scene with that name already exists.";
            else {
                std::error_code ec;
                fs::create_directories(dest.parent_path(), ec);
                if (ec) createErr = "Couldn't create Assets/Scenes.";
                else if (!WriteTemplate(dest.string(), name, createKind)) createErr = "Couldn't write the scene file.";
                else {
                    s_forceScan = true;
                    Publish(ed);
                    ConsoleLog("Created " + dest.generic_string());
                    ImGui::CloseCurrentPopup();
                }
            }
        }
        ImGui::EndDisabled();
        ImGui::SameLine();
        if (ImGui::Button("Cancel##cr", ImVec2(120, 0))) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }

    ImGui::TextDisabled("Open loads a scene. Build order is saved with the project and used by Build Settings.");
    ImGui::End();
}

} // namespace oked
