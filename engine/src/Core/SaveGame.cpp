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
#include <functional>
#include <locale>
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

using savefmt::Escape;
using savefmt::Unescape;
std::string Num(float v) { return savefmt::FormatFloat(v); }

std::size_t g_maxBytes = SaveGame::kDefaultMaxFileBytes;
std::size_t g_maxLines = SaveGame::kDefaultMaxLines;
bool g_usedBackup = false;
std::map<int, std::function<bool(std::string&, std::string*)>>& Migrations() {
    static std::map<int, std::function<bool(std::string&, std::string*)>> m;
    return m;
}

// Up to `n` floats from "x y z" or "x,y,z" text (a part that isn't a number keeps `out`'s value).
void ParseFloats(const std::string& text, float* out, int n) {
    std::string q = text;
    std::replace(q.begin(), q.end(), ',', ' ');
    std::istringstream is(q);
    is.imbue(std::locale::classic());
    std::string word;
    for (int i = 0; i < n && (is >> word); ++i) out[i] = savefmt::ParseFloat(word, out[i]);
}

// A script value as "f:1.5", "b:1", "s:text", "v:1,2,3", "j:[...]"; "" for kinds a save can't hold.
std::string Encode(const vs::VsValue& v) {
    if (v.IsNumber()) return "f:" + Num(v.AsFloat());
    if (v.IsBool())   return std::string("b:") + (v.AsBool() ? "1" : "0");
    if (v.IsString()) return "s:" + Escape(v.AsString());
    if (v.IsVec3()) { const Vec3 p = v.AsVec3(); return "v:" + Num(p.x) + "," + Num(p.y) + "," + Num(p.z); }
    // Colours, lists and maps (any nesting) as their literal text, "j:[(1, 2, 3), 4]".
    if (v.IsColor() || v.IsArray() || v.IsMap()) return "j:" + Escape(vs::VsToLiteral(v));
    return "";
}
bool Decode(const std::string& t, vs::VsValue& out) {
    if (t.size() < 2 || t[1] != ':') return false;
    const std::string p = t.substr(2);
    switch (t[0]) {
        case 'f': out = vs::VsValue{savefmt::ParseFloat(p)}; return true;
        case 'b': out = vs::VsValue{p == "1"}; return true;
        case 's': out = vs::VsValue{Unescape(p)}; return true;
        case 'j': return vs::VsParseLiteral(Unescape(p), out);   // a bad one is skipped, not fatal
        case 'v': {
            Vec3 v; float c[3] = {0, 0, 0};
            ParseFloats(p, c, 3);
            v.x = c[0]; v.y = c[1]; v.z = c[2];
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

// One header line (label, scene, time, ...) into the slot's info; unknown lines are ignored,
// so a file from a newer build with more lines still reads.
void ApplyHeaderLine(SaveSlotInfo& info, const std::vector<std::string>& cols) {
    if (cols.size() < 2) return;
    if (cols[0] == "label") info.label = Unescape(cols[1]);
    else if (cols[0] == "scene") info.scene = Unescape(cols[1]);
    else if (cols[0] == "savedAt") { info.savedAt = std::atoll(cols[1].c_str()); info.savedAtMs = info.savedAt * 1000; }
    else if (cols[0] == "playTime") info.playTime = savefmt::ParseFloat(cols[1]);
    else if (cols[0] == "gameVersion") info.gameVersion = Unescape(cols[1]);
    else if (cols[0] == "thumbnail") info.thumbnail = Unescape(cols[1]);
    else if (cols[0] == "field" && cols.size() >= 3) info.fields[Unescape(cols[1])] = Unescape(cols[2]);
}

// A file name with no folder part: it lives in the saves folder next to the slots.
bool IsBareName(const std::string& name) {
    return !name.empty() && name.find_first_of("/\\") == std::string::npos && name != "." && name != ".." &&
           name.find(':') == std::string::npos;
}
std::string SavesFolderFile(const std::string& name) { return Save::Resolve("saves/" + name); }

struct Parsed {
    SaveSlotInfo info;
    std::string sceneText;
    std::vector<std::vector<std::string>> state;   // tab-split "--state--" lines
    std::vector<std::pair<std::string, std::string>> kv;   // the game's Save keys, already unescaped and checked
};

// Where the file's `okaygame N` header says it stands. A header with no number is 1.
bool ParseHeaderLine(const std::string& line, int& version) {
    if (line.rfind("okaygame", 0) != 0) return false;
    version = std::atoi(line.c_str() + 8);
    if (version < 1) version = 1;
    return true;
}

const char kEndLine[] = "--end--\n";
const char kSumKey[] = "checksum\t";

// Version 2+ files end with "--end--\nchecksum\t<16 hex>\n"; the sum covers everything
// before the checksum line. True if present and matching.
bool VerifyChecksum(const std::string& text, std::string* why) {
    const std::string tail = std::string(kEndLine) + kSumKey;
    const std::size_t at = text.rfind(tail);
    if (at == std::string::npos) { *why = "is cut short (no end marker)"; return false; }
    const std::size_t sumStart = at + tail.size();
    std::string hex = text.substr(sumStart);
    while (!hex.empty() && (hex.back() == '\n' || hex.back() == '\r')) hex.pop_back();
    if (hex.size() != 16) { *why = "is cut short (bad checksum line)"; return false; }
    const std::size_t sumLine = at + sizeof(kEndLine) - 1;
    if (savefmt::Hex16(savefmt::Fnv1a(text.data(), sumLine)) != hex) { *why = "doesn't match its checksum (damaged or edited)"; return false; }
    return true;
}

enum class ReadResult { Ok, Missing, Damaged, Newer };

// The header, version and checksum of a slot file's text. Ok means it is a save this build
// reads whole; `version` is what the file says.
ReadResult ClassifyText(const std::string& text, const std::string& slot, int& version, std::string* error) {
    version = 1;
    const std::size_t nl = text.find('\n');
    if (!ParseHeaderLine(text.substr(0, nl), version)) {
        if (error) *error = "Slot \"" + slot + "\" isn't a save game";
        return ReadResult::Damaged;
    }
    if (version > SaveGame::kFormatVersion) {
        if (error) *error = "Slot \"" + slot + "\" was saved by a newer version of the game (save format " +
                            std::to_string(version) + ", this build reads up to " + std::to_string(SaveGame::kFormatVersion) + ")";
        return ReadResult::Newer;
    }
    std::string why;
    if (version >= 2 && !VerifyChecksum(text, &why)) {
        if (error) *error = "Slot \"" + slot + "\" " + why;
        return ReadResult::Damaged;
    }
    return ReadResult::Ok;
}

ReadResult ReadSaveFile(const std::string& path, const std::string& slot, Parsed& out, std::string* error) {
    std::error_code ec;
    if (!fs::exists(path, ec)) { if (error) *error = "No save in slot \"" + slot + "\""; return ReadResult::Missing; }
    std::string text, why;
    if (!savefmt::ReadFileCapped(path, g_maxBytes, text, &why)) {
        if (error) *error = "Slot \"" + slot + "\" can't be read: " + why;
        return ReadResult::Damaged;
    }
    int version = 1;
    const ReadResult classified = ClassifyText(text, slot, version, error);
    if (classified != ReadResult::Ok) return classified;
    // Older saves are brought forward one version at a time (no hook for a step = nothing to change).
    for (int v = version; v < SaveGame::kFormatVersion; ++v) {
        auto it = Migrations().find(v);
        if (it == Migrations().end() || !it->second) continue;
        std::string merr;
        if (!it->second(text, &merr)) {
            if (error) *error = "Slot \"" + slot + "\" couldn't be upgraded from format " + std::to_string(v) + (merr.empty() ? "" : ": " + merr);
            return ReadResult::Damaged;
        }
    }
    out.info.slot = slot; out.info.exists = true; out.info.version = version;
    const std::size_t nl = text.find('\n');   // after the migrations: a step may change the header line
    int section = 0;   // 0 header, 1 scene, 2 state, 3 after the end marker
    std::size_t lines = 0, pos = (nl == std::string::npos) ? text.size() : nl + 1;
    std::string sceneText;
    while (pos < text.size()) {
        if (++lines > g_maxLines) {
            if (error) *error = "Slot \"" + slot + "\" has too many lines";
            return ReadResult::Damaged;
        }
        std::size_t e = text.find('\n', pos);
        if (e == std::string::npos) e = text.size();
        std::string line = text.substr(pos, e - pos);
        pos = e + 1;
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (section == 3) continue;
        if (line == "--scene--") { section = 1; continue; }
        if (line == "--state--") { section = 2; continue; }
        if (version >= 2 && line == "--end--") { section = 3; continue; }
        if (section == 1) { sceneText += line; sceneText += '\n'; continue; }
        auto cols = SplitTabs(line);
        if (section == 0 && cols.size() >= 2) {
            ApplyHeaderLine(out.info, cols);
        } else if (section == 2 && !cols.empty()) {
            if (cols[0] == "kv") {
                // A bad key row makes the whole slot unusable: it would replace the game's save keys.
                if (cols.size() < 3 || cols[2].size() < 2 || cols[2][1] != ':') {
                    if (error) *error = "Slot \"" + slot + "\" is damaged (bad saved-key line)";
                    return ReadResult::Damaged;
                }
                out.kv.emplace_back(Unescape(cols[1]), Unescape(cols[2]));
            } else {
                out.state.push_back(std::move(cols));
            }
        }
    }
    out.sceneText = std::move(sceneText);
    return ReadResult::Ok;
}

// The slot, from its file or else from the .bak left by the last good save.
bool ReadSave(const std::string& slot, Parsed& out, std::string* error) {
    g_usedBackup = false;
    const std::string path = SaveGame::SlotPath(slot);
    std::string err;
    const ReadResult r = ReadSaveFile(path, slot, out, &err);
    if (r == ReadResult::Ok) return true;
    if (r == ReadResult::Damaged) {
        Parsed b;
        std::string berr;
        if (ReadSaveFile(path + ".bak", slot, b, &berr) == ReadResult::Ok) {
            out = std::move(b);
            g_usedBackup = true;
            Log::Warning("Save slot ", slot, " was damaged; loaded its backup instead");
            if (error) *error = "Slot \"" + slot + "\" was damaged (" + err + "); loaded the backup of the previous save instead";
            return true;
        }
    }
    if (error) *error = err;
    return false;
}

// Put back what the scene text doesn't hold, after the loaded objects started.
void ApplyState(Scene& scene, const Parsed& p) {
    const auto index = PathIndex(scene);
    auto find = [&](const std::string& path) -> GameObject* {
        auto it = index.find(path);
        return it == index.end() ? nullptr : it->second;
    };
    for (const auto& c : p.state) {
        const std::string& kind = c[0];
        if (kind == "body3" && c.size() >= 3) {
            if (GameObject* g = find(c[1]))
                if (auto* rb = g->GetComponent<Rigidbody3D>()) {
                    float v[3] = {rb->velocity.x, rb->velocity.y, rb->velocity.z};
                    ParseFloats(c[2], v, 3);
                    rb->velocity.x = v[0]; rb->velocity.y = v[1]; rb->velocity.z = v[2];
                }
        } else if (kind == "body2" && c.size() >= 3) {
            if (GameObject* g = find(c[1]))
                if (auto* rb = g->GetComponent<Rigidbody2D>()) {
                    float v[2] = {rb->velocity.x, rb->velocity.y};
                    ParseFloats(c[2], v, 2);
                    rb->velocity.x = v[0]; rb->velocity.y = v[1];
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
        }
    }
    // The slot was checked whole when it was read, so replacing the game's keys is safe here.
    if (!p.kv.empty()) {
        SaveFile& kv = Save::File(Save::DefaultFile());
        kv.Clear();
        for (const auto& e : p.kv) kv.SetRaw(e.first, e.second);
        Save::Flush(Save::DefaultFile());
    }
    PlayBase() = p.info.playTime;
    PlayBaseAt() = Time::ElapsedTime();
}

} // namespace

bool SaveGame::ValidSlot(const std::string& slot) {
    if (slot.empty() || slot.size() > 64) return false;
    for (char c : slot)
        if (!std::isalnum((unsigned char)c) && c != '-' && c != '_') return false;
    return !savefmt::IsReservedDeviceName(slot);
}

std::string SaveGame::SlotPath(const std::string& slot) {
    return Save::Resolve("saves/" + slot + ".okaygame");
}

std::string SaveGame::ThumbnailPath(const std::string& slot) {
    return SavesFolderFile(slot + ".png");
}

float SaveGame::PlayTime() { return PlayBase() + std::max(0.0f, Time::ElapsedTime() - PlayBaseAt()); }

bool SaveGame::Save(Scene& scene, const std::string& slot, const std::string& label, std::string* error) {
    SaveOptions options;
    options.label = label;
    return Save(scene, slot, options, error);
}

bool SaveGame::Save(Scene& scene, const std::string& slot, const SaveOptions& options, std::string* error) {
    if (!ValidSlot(slot)) { if (error) *error = "\"" + slot + "\" isn't a valid slot name (letters, digits, - and _)"; return false; }
    std::ostringstream out;
    out << "okaygame " << kFormatVersion << "\n";
    out << "label\t" << Escape(options.label) << "\n";
    out << "scene\t" << Escape(scene.Name()) << "\n";
    out << "savedAt\t" << (long long)std::time(nullptr) << "\n";
    out << "playTime\t" << Num(PlayTime()) << "\n";
    if (!options.gameVersion.empty()) out << "gameVersion\t" << Escape(options.gameVersion) << "\n";
    if (!options.thumbnail.empty()) out << "thumbnail\t" << Escape(options.thumbnail) << "\n";
    for (const auto& kv : options.fields) out << "field\t" << Escape(kv.first) << "\t" << Escape(kv.second) << "\n";
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

    out << kEndLine;
    std::string text = out.str();
    // The checksum covers everything up to and including the end marker.
    text += kSumKey + savefmt::Hex16(savefmt::Fnv1a(text.data(), text.size())) + "\n";

    const std::string path = SlotPath(slot);
    std::error_code ec;
    fs::create_directories(fs::path(path).parent_path(), ec);
    // What is there now: a good file becomes the .bak (a damaged one never replaces the last
    // good copy); a file from a newer build is kept as .newer.bak, so going back to that build
    // finds it.
    if (fs::is_regular_file(path, ec)) {
        std::string old, why;
        int v = 1;
        if (savefmt::ReadFileCapped(path, g_maxBytes, old, &why) && ParseHeaderLine(old.substr(0, old.find('\n')), v)) {
            if (v > kFormatVersion) {
                std::string kept;
                const std::string newer = path + ".newer.bak";
                if (!(savefmt::ReadFileCapped(newer, g_maxBytes, kept, nullptr) && kept == old)) savefmt::AtomicWrite(newer, old);
            } else if (v < 2 || VerifyChecksum(old, &why)) {
                savefmt::AtomicWrite(path + ".bak", old);
            }
        }
    }
    // Write beside it, then swap in, so a crash mid-save never leaves a broken slot.
    savefmt::WriteOptions wopt;
    wopt.sync = true;
    std::string werr;
    if (!savefmt::AtomicWriteChecked(path, text, &werr, wopt)) {
        if (error) *error = "Couldn't save slot \"" + slot + "\": " + werr + " (the slot was not changed)";
        return false;
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
    if (!ValidSlot(slot)) return false;
    const SaveSlotInfo info = Info(slot);
    // A thumbnail named without a folder lives in the saves folder with the slot.
    if (info.exists && !info.thumbnail.empty()) {
        // Info resolves a bare name into the saves folder; a path the game gave is left alone.
        const std::string bare = fs::path(info.thumbnail).filename().string();
        if (info.thumbnail == SavesFolderFile(bare) && IsBareName(bare)) fs::remove(info.thumbnail, ec);
    }
    for (const char* suffix : {".bak", ".tmp", ".newer.bak", ".corrupt", ".swap"}) fs::remove(SlotPath(slot) + suffix, ec);
    return fs::remove(SlotPath(slot), ec);
}

namespace {

enum class HeaderResult { Ok, Newer, NotASave };

// Just the header lines of one slot file (label, scene, time, ...), read without the scene.
// The file is read in pieces until its scene or state section starts, so a long label can't
// push the time out of view; at most kMaxHeaderBytes.
HeaderResult ReadHeaderOnly(const std::string& path, const std::string& slot, SaveSlotInfo& info) {
    constexpr std::size_t kMaxHeaderBytes = 1024u * 1024u;
    std::ifstream f(path, std::ios::binary);
    if (!f) return HeaderResult::NotASave;
    std::string text;
    char buf[8192];
    while (text.size() < kMaxHeaderBytes) {
        f.read(buf, sizeof(buf));
        const std::size_t got = (std::size_t)f.gcount();
        if (got == 0) break;
        text.append(buf, got);
        if (text.find("\n--scene--\n") != std::string::npos || text.find("\n--state--\n") != std::string::npos) break;
        if (!f) break;
    }
    int version = 1;
    std::size_t pos = text.find('\n');
    if (!ParseHeaderLine(text.substr(0, pos), version)) return HeaderResult::NotASave;
    info = SaveSlotInfo();
    info.slot = slot; info.exists = true; info.version = version;
    while (pos != std::string::npos && pos + 1 < text.size()) {
        const std::size_t start = pos + 1;
        pos = text.find('\n', start);
        if (pos == std::string::npos) break;   // a line cut by the end of what was read: header is done
        std::string line = text.substr(start, pos - start);
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line == "--scene--" || line == "--state--") break;
        ApplyHeaderLine(info, SplitTabs(line));
    }
    if (version > SaveGame::kFormatVersion) return HeaderResult::Newer;
    return HeaderResult::Ok;
}

std::string NewerMessage(const std::string& slot, int version) {
    return "Slot \"" + slot + "\" was saved by a newer version of the game (save format " +
           std::to_string(version) + ", this build reads up to " + std::to_string(SaveGame::kFormatVersion) + ")";
}

// A thumbnail named without a folder is a file in the saves folder: give its full path.
void ResolveThumbnail(SaveSlotInfo& info) {
    if (!info.thumbnail.empty() && IsBareName(info.thumbnail)) info.thumbnail = SavesFolderFile(info.thumbnail);
}

} // namespace

bool SaveGame::Check(const std::string& slot, std::string* error) {
    if (!ValidSlot(slot)) { if (error) *error = "\"" + slot + "\" isn't a valid slot name"; return false; }
    auto check = [&](const std::string& path, std::string* why) {
        std::error_code ec;
        if (!fs::exists(path, ec)) { *why = "No save in slot \"" + slot + "\""; return ReadResult::Missing; }
        std::string text, readWhy;
        if (!savefmt::ReadFileCapped(path, g_maxBytes, text, &readWhy)) { *why = "Slot \"" + slot + "\" can't be read: " + readWhy; return ReadResult::Damaged; }
        int version = 1;
        return ClassifyText(text, slot, version, why);
    };
    const std::string path = SlotPath(slot);
    std::string why;
    const ReadResult r = check(path, &why);
    if (r == ReadResult::Ok) return true;
    if (r == ReadResult::Damaged) {
        std::string bwhy;
        if (check(path + ".bak", &bwhy) == ReadResult::Ok) {
            if (error) *error = "Slot \"" + slot + "\" was damaged (" + why + "); its backup is fine";
            return true;
        }
    }
    if (error) *error = why;
    return false;
}

SaveSlotInfo SaveGame::Info(const std::string& slot, bool verify) {
    SaveSlotInfo i; i.slot = slot;
    if (!ValidSlot(slot)) return i;
    const std::string path = SlotPath(slot);
    std::error_code ec;
    if (!fs::exists(path, ec)) return i;
    const auto size = fs::file_size(path, ec);
    const long long sizeBytes = ec ? 0 : (long long)size;
    SaveSlotInfo head;
    HeaderResult h = ReadHeaderOnly(path, slot, head);
    if (h == HeaderResult::NotASave) {
        SaveSlotInfo b;
        if (ReadHeaderOnly(path + ".bak", slot, b) == HeaderResult::Ok) {   // main file unreadable: show the backup's
            b.fromBackup = true;
            b.error = "Slot \"" + slot + "\" was damaged; these are the backup's values";
            b.sizeBytes = sizeBytes;
            ResolveThumbnail(b);
            return b;
        }
        i.exists = true; i.corrupt = true; i.sizeBytes = sizeBytes;
        i.error = "Slot \"" + slot + "\" isn't a save game";
        return i;
    }
    head.sizeBytes = sizeBytes;
    ResolveThumbnail(head);
    if (h == HeaderResult::Newer) {
        head.incompatible = true;
        head.error = NewerMessage(slot, head.version);
        return head;
    }
    if (verify) {
        std::string why;
        if (!Check(slot, &why)) { head.corrupt = true; head.error = why; }
        else if (!why.empty()) {   // the main file failed but the backup is fine: show the backup
            SaveSlotInfo b;
            if (ReadHeaderOnly(path + ".bak", slot, b) == HeaderResult::Ok) {
                b.fromBackup = true; b.error = why; b.sizeBytes = sizeBytes;
                ResolveThumbnail(b);
                return b;
            }
        }
    }
    return head;
}

bool SaveGame::LastLoadUsedBackup() { return g_usedBackup; }

void SaveGame::SetLimits(std::size_t maxFileBytes, std::size_t maxLines) {
    g_maxBytes = maxFileBytes ? maxFileBytes : kDefaultMaxFileBytes;
    g_maxLines = maxLines ? maxLines : kDefaultMaxLines;
}

void SaveGame::SetMigration(int fromVersion, Migration fn) {
    if (fn) Migrations()[fromVersion] = std::move(fn);
    else Migrations().erase(fromVersion);
}

std::vector<SaveSlotInfo> SaveGame::List(bool verify) {
    std::vector<SaveSlotInfo> out;
    std::error_code ec;
    const fs::path dir = fs::path(SlotPath("x")).parent_path();
    for (fs::directory_iterator it(dir, ec), end; !ec && it != end; it.increment(ec)) {
        if (it->path().extension() != ".okaygame") continue;
        const std::string slot = it->path().stem().string();
        if (!ValidSlot(slot)) continue;
        SaveSlotInfo i = Info(slot, verify);
        if (i.exists) out.push_back(std::move(i));
    }
    // Newest first; slots without a known time (damaged ones) go last, by name.
    std::sort(out.begin(), out.end(), [](const SaveSlotInfo& a, const SaveSlotInfo& b) {
        if (a.savedAt != b.savedAt) return a.savedAt > b.savedAt;
        return a.slot < b.slot;
    });
    return out;
}

} // namespace okay
