#include "okay/Core/SaveGame.hpp"
#include "okay/Core/SaveData.hpp"
#include "okay/Core/Time.hpp"
#include "okay/Core/Log.hpp"
#include "okay/Scene/Scene.hpp"
#include "okay/Scene/GameObject.hpp"
#include "okay/Scene/Transform.hpp"
#include "okay/Scene/SceneSerializer.hpp"
#include "okay/Physics/Rigidbody2D.hpp"
#include "okay/Physics/Rigidbody3D.hpp"
#include "okay/Components/ScriptComponent.hpp"
#include "okay/Components/VisualScriptComponent.hpp"
#include <algorithm>
#include <cctype>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <map>
#include <sstream>
#include <unordered_map>

namespace okay {

namespace {

namespace fs = std::filesystem;

float& PlayBase() { static float t = 0.0f; return t; }        // play time of the loaded save
float& PlayBaseAt() { static float t = 0.0f; return t; }      // Time::ElapsedTime() when it was loaded

// "Root/Child#1": names from the root down, with #n for the nth same-named sibling
// (from 0; left off for the first), so an object is found again after a load.
std::string ObjectPath(GameObject* go, Scene& scene) {
    std::vector<std::string> parts;
    for (Transform* t = go->transform; t && t->gameObject; t = t->Parent()) {
        GameObject* g = t->gameObject;
        int nth = 0;
        if (Transform* p = t->Parent()) {
            for (Transform* c : p->Children()) {
                if (c == t) break;
                if (c && c->gameObject && c->gameObject->name == g->name) ++nth;
            }
        } else {
            for (const auto& o : scene.Objects()) {
                if (o.get() == g) break;
                if (o && !o->transform->Parent() && o->name == g->name) ++nth;
            }
        }
        parts.push_back(nth ? g->name + "#" + std::to_string(nth) : g->name);
    }
    std::string path;
    for (auto it = parts.rbegin(); it != parts.rend(); ++it) { if (!path.empty()) path += '/'; path += *it; }
    return path;
}

std::unordered_map<std::string, GameObject*> PathIndex(Scene& scene) {
    std::unordered_map<std::string, GameObject*> m;
    for (const auto& o : scene.Objects()) if (o) m.emplace(ObjectPath(o.get(), scene), o.get());
    return m;
}

std::string Escape(const std::string& s) {
    std::string o;
    for (char c : s) {
        if (c == '\\') o += "\\\\";
        else if (c == '\n') o += "\\n";
        else if (c == '\t') o += "\\t";
        else o += c;
    }
    return o;
}
std::string Unescape(const std::string& s) {
    std::string o;
    for (std::size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '\\' && i + 1 < s.size()) {
            const char n = s[++i];
            o += n == 'n' ? '\n' : n == 't' ? '\t' : n;
        } else o += s[i];
    }
    return o;
}
std::string Num(float v) { std::ostringstream o; o.precision(9); o << v; return o.str(); }

// A script value as "f:1.5", "b:1", "s:text", "v:1,2,3"; "" for kinds a save can't hold.
std::string Encode(const vs::VsValue& v) {
    if (v.IsNumber()) return "f:" + Num(v.AsFloat());
    if (v.IsBool())   return std::string("b:") + (v.AsBool() ? "1" : "0");
    if (v.IsString()) return "s:" + Escape(v.AsString());
    if (v.IsVec3()) { const Vec3 p = v.AsVec3(); return "v:" + Num(p.x) + "," + Num(p.y) + "," + Num(p.z); }
    return "";
}
bool Decode(const std::string& t, vs::VsValue& out) {
    if (t.size() < 2 || t[1] != ':') return false;
    const std::string p = t.substr(2);
    switch (t[0]) {
        case 'f': out = vs::VsValue{(float)std::atof(p.c_str())}; return true;
        case 'b': out = vs::VsValue{p == "1"}; return true;
        case 's': out = vs::VsValue{Unescape(p)}; return true;
        case 'v': {
            Vec3 v; std::string q = p; std::replace(q.begin(), q.end(), ',', ' ');
            std::istringstream is(q); is >> v.x >> v.y >> v.z;
            out = vs::VsValue{v}; return true;
        }
    }
    return false;
}

std::vector<std::string> SplitTabs(const std::string& line) {
    std::vector<std::string> out;
    std::size_t start = 0;
    for (std::size_t i = 0; i <= line.size(); ++i)
        if (i == line.size() || line[i] == '\t') { out.push_back(line.substr(start, i - start)); start = i + 1; }
    return out;
}

struct Parsed {
    SaveSlotInfo info;
    std::string sceneText;
    std::vector<std::vector<std::string>> state;   // tab-split "--state--" lines
};

bool ReadSave(const std::string& slot, Parsed& out, std::string* error) {
    std::ifstream f(SaveGame::SlotPath(slot), std::ios::binary);
    if (!f) { if (error) *error = "No save in slot \"" + slot + "\""; return false; }
    std::string line;
    if (!std::getline(f, line) || line.rfind("okaygame", 0) != 0) {
        if (error) *error = "Slot \"" + slot + "\" isn't a save game";
        return false;
    }
    out.info.slot = slot; out.info.exists = true;
    int section = 0;   // 0 header, 1 scene, 2 state
    std::string sceneText;
    while (std::getline(f, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line == "--scene--") { section = 1; continue; }
        if (line == "--state--") { section = 2; continue; }
        if (section == 1) { sceneText += line; sceneText += '\n'; continue; }
        auto cols = SplitTabs(line);
        if (section == 0 && cols.size() >= 2) {
            if (cols[0] == "label") out.info.label = Unescape(cols[1]);
            else if (cols[0] == "scene") out.info.scene = Unescape(cols[1]);
            else if (cols[0] == "savedAt") out.info.savedAt = std::atoll(cols[1].c_str());
            else if (cols[0] == "playTime") out.info.playTime = (float)std::atof(cols[1].c_str());
        } else if (section == 2 && !cols.empty()) {
            out.state.push_back(std::move(cols));
        }
    }
    out.sceneText = std::move(sceneText);
    return true;
}

// Put back what the scene text doesn't hold, after the loaded objects started.
void ApplyState(Scene& scene, const Parsed& p) {
    const auto index = PathIndex(scene);
    auto find = [&](const std::string& path) -> GameObject* {
        auto it = index.find(path);
        return it == index.end() ? nullptr : it->second;
    };
    SaveFile& kv = Save::File(Save::DefaultFile());
    bool kvTouched = false;
    for (const auto& c : p.state) {
        const std::string& kind = c[0];
        if (kind == "body3" && c.size() >= 3) {
            if (GameObject* g = find(c[1]))
                if (auto* rb = g->GetComponent<Rigidbody3D>()) {
                    std::istringstream is(c[2]); is >> rb->velocity.x >> rb->velocity.y >> rb->velocity.z;
                }
        } else if (kind == "body2" && c.size() >= 3) {
            if (GameObject* g = find(c[1]))
                if (auto* rb = g->GetComponent<Rigidbody2D>()) {
                    std::istringstream is(c[2]); is >> rb->velocity.x >> rb->velocity.y;
                }
        } else if ((kind == "script" || kind == "graph") && c.size() >= 5) {
            GameObject* g = find(c[1]);
            vs::VsValue v;
            if (!g || !Decode(c[4], v)) continue;
            const std::size_t nth = (std::size_t)std::atoi(c[2].c_str());
            if (kind == "script") {
                auto scripts = g->GetComponents<ScriptComponent>();
                if (nth < scripts.size() && scripts[nth]->VM()) scripts[nth]->VM()->SetGlobal(c[3], v);
            } else {
                auto graphs = g->GetComponents<VisualScriptComponent>();
                if (nth < graphs.size()) graphs[nth]->SetVariable(c[3], v);
            }
        } else if (kind == "kv" && c.size() >= 3) {
            if (!kvTouched) { kv.Clear(); kvTouched = true; }
            kv.SetRaw(Unescape(c[1]), Unescape(c[2]));
        }
    }
    if (kvTouched) Save::Flush(Save::DefaultFile());
    PlayBase() = p.info.playTime;
    PlayBaseAt() = Time::ElapsedTime();
}

} // namespace

bool SaveGame::ValidSlot(const std::string& slot) {
    if (slot.empty() || slot.size() > 64) return false;
    for (char c : slot)
        if (!std::isalnum((unsigned char)c) && c != '-' && c != '_') return false;
    return true;
}

std::string SaveGame::SlotPath(const std::string& slot) {
    return Save::Resolve("saves/" + slot + ".okaygame");
}

float SaveGame::PlayTime() { return PlayBase() + std::max(0.0f, Time::ElapsedTime() - PlayBaseAt()); }

bool SaveGame::Save(Scene& scene, const std::string& slot, const std::string& label, std::string* error) {
    if (!ValidSlot(slot)) { if (error) *error = "\"" + slot + "\" isn't a valid slot name (letters, digits, - and _)"; return false; }
    std::ostringstream out;
    out << "okaygame 1\n";
    out << "label\t" << Escape(label) << "\n";
    out << "scene\t" << Escape(scene.Name()) << "\n";
    out << "savedAt\t" << (long long)std::time(nullptr) << "\n";
    out << "playTime\t" << Num(PlayTime()) << "\n";
    out << "--scene--\n" << SceneSerializer::Serialize(scene);
    out << "--state--\n";
    for (const auto& up : scene.Objects()) {
        GameObject* g = up.get();
        if (!g) continue;
        std::string path;
        auto pathOf = [&]() -> const std::string& { if (path.empty()) path = ObjectPath(g, scene); return path; };
        if (auto* rb = g->GetComponent<Rigidbody3D>())
            out << "body3\t" << pathOf() << "\t" << Num(rb->velocity.x) << " " << Num(rb->velocity.y) << " " << Num(rb->velocity.z) << "\n";
        if (auto* rb = g->GetComponent<Rigidbody2D>())
            out << "body2\t" << pathOf() << "\t" << Num(rb->velocity.x) << " " << Num(rb->velocity.y) << "\n";
        std::size_t k = 0;
        for (auto* sc : g->GetComponents<ScriptComponent>()) {
            if (IScriptVM* vm = sc->VM())
                for (const std::string& name : vm->GlobalNames()) {
                    const std::string t = Encode(vm->GetGlobal(name));
                    if (!t.empty()) out << "script\t" << pathOf() << "\t" << k << "\t" << name << "\t" << t << "\n";
                }
            ++k;
        }
        k = 0;
        for (auto* vc : g->GetComponents<VisualScriptComponent>()) {
            for (const auto& kv : vc->Variables()) {
                const std::string t = Encode(kv.second);
                if (!t.empty()) out << "graph\t" << pathOf() << "\t" << k << "\t" << kv.first << "\t" << t << "\n";
            }
            ++k;
        }
    }
    for (const auto& kv : Save::File(Save::DefaultFile()).Raw())
        out << "kv\t" << Escape(kv.first) << "\t" << Escape(kv.second) << "\n";

    const std::string path = SlotPath(slot);
    std::error_code ec;
    fs::create_directories(fs::path(path).parent_path(), ec);
    // Write beside it, then swap in, so a crash mid-save never leaves a broken slot.
    const std::string tmp = path + ".tmp";
    {
        std::ofstream f(tmp, std::ios::binary);
        if (!f || !(f << out.str()) || !f.flush()) {
            if (error) *error = "Couldn't write " + path;
            return false;
        }
    }
    fs::rename(tmp, path, ec);
    if (ec) {
        fs::remove(path, ec);
        fs::rename(tmp, path, ec);
        if (ec) { if (error) *error = "Couldn't write " + path; return false; }
    }
    Log::Info("Saved game to slot ", slot);
    return true;
}

bool SaveGame::Load(Scene& scene, const std::string& slot, std::string* error) {
    if (!ValidSlot(slot)) { if (error) *error = "\"" + slot + "\" isn't a valid slot name"; return false; }
    auto p = std::make_shared<Parsed>();
    if (!ReadSave(slot, *p, error)) return false;
    std::string text = std::move(p->sceneText);
    scene.RequestLoadText(std::move(text), [p](Scene& s) { ApplyState(s, *p); });
    return true;
}

bool SaveGame::LoadNow(Scene& scene, const std::string& slot, std::string* error) {
    if (!ValidSlot(slot)) { if (error) *error = "\"" + slot + "\" isn't a valid slot name"; return false; }
    Parsed p;
    if (!ReadSave(slot, p, error)) return false;
    std::string err;
    if (!SceneSerializer::Deserialize(scene, p.sceneText, &err)) {
        if (error) *error = "Slot \"" + slot + "\" is damaged: " + err;
        return false;
    }
    scene.Start();
    ApplyState(scene, p);
    return true;
}

bool SaveGame::Exists(const std::string& slot) {
    std::error_code ec;
    return ValidSlot(slot) && fs::exists(SlotPath(slot), ec);
}

bool SaveGame::Delete(const std::string& slot) {
    std::error_code ec;
    return ValidSlot(slot) && fs::remove(SlotPath(slot), ec);
}

SaveSlotInfo SaveGame::Info(const std::string& slot) {
    Parsed p;
    if (!ValidSlot(slot) || !ReadSave(slot, p, nullptr)) { SaveSlotInfo i; i.slot = slot; return i; }
    return p.info;
}

std::vector<SaveSlotInfo> SaveGame::List() {
    std::vector<SaveSlotInfo> out;
    std::error_code ec;
    const fs::path dir = fs::path(SlotPath("x")).parent_path();
    for (fs::directory_iterator it(dir, ec), end; !ec && it != end; it.increment(ec)) {
        if (it->path().extension() != ".okaygame") continue;
        const std::string slot = it->path().stem().string();
        if (!ValidSlot(slot)) continue;
        SaveSlotInfo i = Info(slot);
        if (i.exists) out.push_back(std::move(i));
    }
    std::sort(out.begin(), out.end(), [](const SaveSlotInfo& a, const SaveSlotInfo& b) { return a.savedAt > b.savedAt; });
    return out;
}

} // namespace okay
