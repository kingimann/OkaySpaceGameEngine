#include "okay/Core/ImportSettings.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <locale>
#include <sstream>

namespace okay {

namespace {

std::string Lower(std::string s) {
    for (char& c : s) if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    return s;
}

struct ExtRow { const char* ext; AssetType kind; };
const ExtRow kExtensions[] = {
    {"png", AssetType::Texture}, {"jpg", AssetType::Texture}, {"jpeg", AssetType::Texture},
    {"bmp", AssetType::Texture}, {"tga", AssetType::Texture}, {"gif", AssetType::Texture},
    {"psd", AssetType::Texture}, {"hdr", AssetType::Texture},
    {"glb", AssetType::Model}, {"gltf", AssetType::Model}, {"fbx", AssetType::Model},
    {"obj", AssetType::Model}, {"dae", AssetType::Model},
    {"wav", AssetType::Audio}, {"ogg", AssetType::Audio}, {"mp3", AssetType::Audio}, {"flac", AssetType::Audio},
    {"okaymesh", AssetType::Mesh},
    {"okaymat", AssetType::Material},
    {"okayscene", AssetType::Scene},
    {"okayprefab", AssetType::Prefab},
    {"okayanim", AssetType::Animation}, {"okayclip", AssetType::Animation}, {"bvh", AssetType::Animation},
    {"okaydialog", AssetType::Dialogue},
    {"okayquest", AssetType::Quest},
    {"okaybt", AssetType::BehaviorTree},
    {"okayvs", AssetType::VisualScript},
    {"okayseq", AssetType::Sequence},
    {"cs", AssetType::Script}, {"lua", AssetType::Script},
    {"okaydata", AssetType::Data}, {"okayitems", AssetType::Data}, {"okayloot", AssetType::Data},
    {"okayrecipes", AssetType::Data}, {"okayloc", AssetType::Data}, {"okaynav", AssetType::Data},
    {"json", AssetType::Data}, {"txt", AssetType::Data}, {"bin", AssetType::Data},
    {"ttf", AssetType::Font}, {"otf", AssetType::Font},
};

const char* const kKindNames[] = {
    "unknown", "texture", "model", "audio", "mesh", "material", "scene", "prefab", "animation",
    "dialogue", "quest", "behaviortree", "visualscript", "sequence", "script", "data", "font", "other"};

// Shortest text that reads back as the same float, so hand-edited files stay readable.
std::string FloatText(float v) {
    if (!std::isfinite(v)) return "0";
    char buf[32];
    for (int p = 1; p <= 9; ++p) {
        std::snprintf(buf, sizeof(buf), "%.*g", p, static_cast<double>(v));
        if (std::strtof(buf, nullptr) == v) break;
    }
    std::string s = buf;
    for (char& c : s) if (c == ',') c = '.';   // never a locale decimal comma
    return s;
}

bool ParseBool(const std::string& v, bool& out) {
    const std::string s = Lower(v);
    if (s == "1" || s == "true" || s == "yes" || s == "on") { out = true; return true; }
    if (s == "0" || s == "false" || s == "no" || s == "off") { out = false; return true; }
    return false;
}
bool ParseInt(const std::string& v, int& out) {
    if (v.empty()) return false;
    char* end = nullptr;
    const long n = std::strtol(v.c_str(), &end, 10);
    if (!end || *end != '\0') return false;
    if (n > 1000000000L) out = 1000000000;
    else if (n < -1000000000L) out = -1000000000;
    else out = static_cast<int>(n);
    return true;
}
bool ParseFloat(const std::string& v, float& out) {
    if (v.empty()) return false;
    std::istringstream is(v);
    is.imbue(std::locale::classic());
    float f = 0.0f;
    if (!(is >> f)) return false;
    if (!is.eof()) { char rest = 0; if (is >> rest) return false; }
    if (!std::isfinite(f)) return false;
    out = f;
    return true;
}

void Note(std::vector<std::string>* notes, const std::string& s) { if (notes) notes->push_back(s); }

// Reads one enum from its names; false when `v` is none of them.
template <class E>
bool ParseEnum(const std::string& v, const char* const* names, int count, E& out) {
    const std::string s = Lower(v);
    for (int i = 0; i < count; ++i)
        if (s == names[i]) { out = static_cast<E>(i); return true; }
    return false;
}

const char* const kFilterNames[] = {"point", "bilinear", "trilinear"};
const char* const kWrapNames[] = {"repeat", "clamp", "mirror"};
const char* const kNormalNames[] = {"import", "calculate", "flat"};
const char* const kLoadNames[] = {"decode", "stream"};

// --- Migrations: one step from `v` to `v + 1` --------------------------------------

// Texture v1 (the early layout): `linear` (0/1, the inverse of srgb), `filter` nearest|linear,
// `maxsize`, `mips`. v2 renamed them, added trilinear and `compress`.
ImportSettings::Pairs TextureV1ToV2(const ImportSettings::Pairs& in, std::vector<std::string>* notes) {
    ImportSettings::Pairs out;
    for (const auto& kv : in) {
        const std::string k = Lower(kv.first);
        if (k == "linear") {
            bool lin = false;
            if (ParseBool(kv.second, lin)) out.emplace_back("srgb", lin ? "0" : "1");
            else Note(notes, "texture: dropped unreadable 'linear' value '" + kv.second + "'");
        } else if (k == "filter") {
            const std::string f = Lower(kv.second);
            out.emplace_back("filter", f == "nearest" ? "point" : f == "linear" ? "bilinear" : kv.second);
        } else if (k == "maxsize") {
            out.emplace_back("maxSize", kv.second);
        } else if (k == "mips") {
            out.emplace_back("mipmaps", kv.second);
        } else {
            out.push_back(kv);
        }
    }
    Note(notes, "texture settings migrated from v1 to v2");
    return out;
}

// Audio v1: `stream` 0/1, `mono` 0/1, `quality` as a percent. v2 uses names and 0..1.
ImportSettings::Pairs AudioV1ToV2(const ImportSettings::Pairs& in, std::vector<std::string>* notes) {
    ImportSettings::Pairs out;
    for (const auto& kv : in) {
        const std::string k = Lower(kv.first);
        if (k == "stream") {
            bool s = false;
            if (ParseBool(kv.second, s)) out.emplace_back("loadMode", s ? "stream" : "decode");
            else Note(notes, "audio: dropped unreadable 'stream' value '" + kv.second + "'");
        } else if (k == "mono") {
            out.emplace_back("forceMono", kv.second);
        } else if (k == "quality") {
            float q = 0.0f;
            if (ParseFloat(kv.second, q)) out.emplace_back("quality", FloatText(q / 100.0f));
            else Note(notes, "audio: dropped unreadable 'quality' value '" + kv.second + "'");
        } else {
            out.push_back(kv);
        }
    }
    Note(notes, "audio settings migrated from v1 to v2");
    return out;
}

} // namespace

// --- Kinds ----------------------------------------------------------------------------

const char* AssetTypeName(AssetType kind) {
    const int i = static_cast<int>(kind);
    return i >= 0 && i < static_cast<int>(sizeof(kKindNames) / sizeof(kKindNames[0])) ? kKindNames[i] : "unknown";
}

AssetType AssetTypeFromName(const std::string& name) {
    const std::string s = Lower(name);
    for (int i = 1; i < static_cast<int>(sizeof(kKindNames) / sizeof(kKindNames[0])); ++i)
        if (s == kKindNames[i]) return static_cast<AssetType>(i);
    return AssetType::Unknown;
}

AssetType AssetTypeFromExtension(const std::string& extension) {
    std::string e = Lower(extension);
    if (!e.empty() && e[0] == '.') e.erase(0, 1);
    for (const ExtRow& r : kExtensions) if (e == r.ext) return r.kind;
    return AssetType::Other;
}

std::string AssetFileName(const std::string& path) {
    const std::size_t slash = path.find_last_of("/\\");
    return slash == std::string::npos ? path : path.substr(slash + 1);
}

std::string AssetExtension(const std::string& path) {
    const std::string name = AssetFileName(path);
    const std::size_t dot = name.find_last_of('.');
    if (dot == std::string::npos || dot + 1 >= name.size()) return std::string();
    return Lower(name.substr(dot + 1));
}

AssetType AssetTypeFromPath(const std::string& path) {
    return AssetTypeFromExtension(AssetExtension(path));
}

std::vector<std::string> ExtensionsOfType(AssetType kind) {
    std::vector<std::string> out;
    for (const ExtRow& r : kExtensions) if (r.kind == kind) out.push_back(r.ext);
    return out;
}

// --- Settings ---------------------------------------------------------------------------

int ImportSettings::CurrentVersion(AssetType kind) {
    switch (kind) {
        case AssetType::Texture: return 2;
        case AssetType::Model:   return 1;
        case AssetType::Audio:   return 2;
        default: return 0;
    }
}

ImportSettings ImportSettings::Defaults(AssetType kind) {
    ImportSettings s;
    s.kind = kind;
    s.version = CurrentVersion(kind);
    return s;
}

ImportSettings::Pairs ImportSettings::ToPairs() const {
    Pairs p;
    switch (kind) {
        case AssetType::Texture:
            p.emplace_back("srgb", texture.sRGB ? "1" : "0");
            p.emplace_back("filter", kFilterNames[static_cast<int>(texture.filter)]);
            p.emplace_back("wrap", kWrapNames[static_cast<int>(texture.wrap)]);
            p.emplace_back("maxSize", std::to_string(texture.maxSize));
            p.emplace_back("mipmaps", texture.mipmaps ? "1" : "0");
            p.emplace_back("compress", texture.compress ? "1" : "0");
            break;
        case AssetType::Model:
            p.emplace_back("scale", FloatText(model.scale));
            p.emplace_back("normals", kNormalNames[static_cast<int>(model.normals)]);
            p.emplace_back("importAnimations", model.importAnimations ? "1" : "0");
            p.emplace_back("smoothingAngle", FloatText(model.smoothingAngle));
            break;
        case AssetType::Audio:
            p.emplace_back("loadMode", kLoadNames[static_cast<int>(audio.loadMode)]);
            p.emplace_back("forceMono", audio.forceMono ? "1" : "0");
            p.emplace_back("quality", FloatText(audio.quality));
            break;
        default: break;
    }
    for (const auto& kv : extra) p.push_back(kv);
    return p;
}

ImportSettings::Pairs ImportSettings::Migrate(AssetType kind, int fromVersion, Pairs pairs, std::vector<std::string>* notes) {
    if (fromVersion < 1) fromVersion = 1;
    const int target = CurrentVersion(kind);
    for (int v = fromVersion; v < target; ++v) {
        if (kind == AssetType::Texture && v == 1) pairs = TextureV1ToV2(pairs, notes);
        else if (kind == AssetType::Audio && v == 1) pairs = AudioV1ToV2(pairs, notes);
    }
    return pairs;
}

ImportSettings ImportSettings::FromPairs(AssetType kind, int version, const Pairs& pairsIn, std::vector<std::string>* notes) {
    ImportSettings s = Defaults(kind);
    const int current = CurrentVersion(kind);
    if (current == 0) {
        // No importer options for this kind: keep whatever was stored, untouched.
        s.version = 0;
        for (const auto& kv : pairsIn) s.extra[kv.first] = kv.second;
        return s;
    }
    if (version < 1) version = 1;
    Pairs pairs = pairsIn;
    if (version > current) {
        s.fromNewerVersion = true;
        Note(notes, std::string(AssetTypeName(kind)) + " settings were written by a newer layout (v" +
                    std::to_string(version) + ", this build reads v" + std::to_string(current) + ")");
    } else if (version < current) {
        pairs = Migrate(kind, version, pairs, notes);
    }
    s.version = version > current ? version : current;

    auto bad = [&](const std::string& key, const std::string& value) {
        Note(notes, std::string(AssetTypeName(kind)) + ": bad value '" + value + "' for '" + key + "', using the default");
    };
    for (const auto& kv : pairs) {
        const std::string k = Lower(kv.first);
        const std::string& v = kv.second;
        bool handled = true;
        if (kind == AssetType::Texture) {
            if (k == "srgb") { if (!ParseBool(v, s.texture.sRGB)) bad(kv.first, v); }
            else if (k == "filter") { if (!ParseEnum(v, kFilterNames, 3, s.texture.filter)) bad(kv.first, v); }
            else if (k == "wrap") { if (!ParseEnum(v, kWrapNames, 3, s.texture.wrap)) bad(kv.first, v); }
            else if (k == "maxsize") { if (!ParseInt(v, s.texture.maxSize)) bad(kv.first, v); }
            else if (k == "mipmaps") { if (!ParseBool(v, s.texture.mipmaps)) bad(kv.first, v); }
            else if (k == "compress") { if (!ParseBool(v, s.texture.compress)) bad(kv.first, v); }
            else handled = false;
        } else if (kind == AssetType::Model) {
            if (k == "scale") { if (!ParseFloat(v, s.model.scale)) bad(kv.first, v); }
            else if (k == "normals") { if (!ParseEnum(v, kNormalNames, 3, s.model.normals)) bad(kv.first, v); }
            else if (k == "importanimations") { if (!ParseBool(v, s.model.importAnimations)) bad(kv.first, v); }
            else if (k == "smoothingangle") { if (!ParseFloat(v, s.model.smoothingAngle)) bad(kv.first, v); }
            else handled = false;
        } else if (kind == AssetType::Audio) {
            if (k == "loadmode") { if (!ParseEnum(v, kLoadNames, 2, s.audio.loadMode)) bad(kv.first, v); }
            else if (k == "forcemono") { if (!ParseBool(v, s.audio.forceMono)) bad(kv.first, v); }
            else if (k == "quality") { if (!ParseFloat(v, s.audio.quality)) bad(kv.first, v); }
            else handled = false;
        } else {
            handled = false;
        }
        if (!handled) s.extra[kv.first] = v;
    }
    s.Clamp();
    return s;
}

void ImportSettings::Clamp() {
    if (texture.maxSize != 0) texture.maxSize = std::max(32, std::min(16384, texture.maxSize));
    if (!std::isfinite(model.scale)) model.scale = 1.0f;
    model.scale = std::max(0.0001f, std::min(10000.0f, model.scale));
    if (!std::isfinite(model.smoothingAngle)) model.smoothingAngle = 60.0f;
    model.smoothingAngle = std::max(0.0f, std::min(180.0f, model.smoothingAngle));
    if (!std::isfinite(audio.quality)) audio.quality = 0.7f;
    audio.quality = std::max(0.0f, std::min(1.0f, audio.quality));
}

std::string ImportSettings::Fingerprint() const {
    std::string out = AssetTypeName(kind);
    for (const auto& kv : ToPairs()) { out += ';'; out += kv.first; out += '='; out += kv.second; }
    return out;
}

bool ImportSettings::SameAs(const ImportSettings& other) const {
    return kind == other.kind && ToPairs() == other.ToPairs();
}

std::string ImportSettings::Describe() const {
    std::string out = std::string(AssetTypeName(kind)) + " v" + std::to_string(version);
    for (const auto& kv : ToPairs()) out += " " + kv.first + "=" + kv.second;
    return out;
}

} // namespace okay
