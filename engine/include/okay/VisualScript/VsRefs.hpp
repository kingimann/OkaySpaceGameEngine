#pragma once
// Object and asset references stored in visual-script pin defaults and node
// text fields. Old graphs keep plain names and paths; these still resolve.
// A reference written by the editor looks like:
//   @self  @other  @guid:<32 hex>  @prefab:<path>  @material:  @audio:
//   @anim:  @scene:  @graph:  @asset:
#include "okay/Scene/GameObject.hpp"
#include "okay/Scene/Scene.hpp"
#include <cctype>
#include <string>

namespace okay::vs {

enum class VsRefKind { None, Object, Prefab, Material, Audio, Animation, Scene, Graph, Asset };

inline bool VsStartsWith(const std::string& s, const char* p) {
    for (std::size_t i = 0; p[i]; ++i) if (i >= s.size() || s[i] != p[i]) return false;
    return true;
}

inline const char* VsRefPrefix(VsRefKind kind) {
    switch (kind) {
        case VsRefKind::Prefab:    return "@prefab:";
        case VsRefKind::Material:  return "@material:";
        case VsRefKind::Audio:     return "@audio:";
        case VsRefKind::Animation: return "@anim:";
        case VsRefKind::Scene:     return "@scene:";
        case VsRefKind::Graph:     return "@graph:";
        case VsRefKind::Asset:     return "@asset:";
        default: return "";
    }
}

inline bool VsIsObjectRef(const std::string& s) {
    return s == "@self" || s == "@other" || VsStartsWith(s, "@guid:");
}

inline bool VsIsAssetRef(const std::string& s) {
    return VsStartsWith(s, "@prefab:") || VsStartsWith(s, "@material:") || VsStartsWith(s, "@audio:")
        || VsStartsWith(s, "@anim:") || VsStartsWith(s, "@scene:") || VsStartsWith(s, "@graph:")
        || VsStartsWith(s, "@asset:");
}

inline std::string VsGuidRef(const std::string& guid) { return "@guid:" + guid; }

inline std::string VsAssetRef(VsRefKind kind, const std::string& path) {
    const char* p = VsRefPrefix(kind == VsRefKind::None ? VsRefKind::Asset : kind);
    if (!p[0]) p = "@asset:";
    return std::string(p) + path;
}

/// The file path inside an asset reference, or `stored` when it is a plain path.
inline std::string VsBareAsset(const std::string& stored) {
    const char* prefs[] = {"@prefab:", "@material:", "@audio:", "@anim:", "@scene:", "@graph:", "@asset:"};
    for (const char* p : prefs)
        if (VsStartsWith(stored, p)) return stored.substr(std::char_traits<char>::length(p));
    return stored;
}

/// A clip node stores a name. A dropped animation file uses the file's name.
inline std::string VsClipToken(const std::string& stored) {
    std::string p = VsBareAsset(stored);
    auto slash = p.find_last_of("/\\");
    std::string file = slash == std::string::npos ? p : p.substr(slash + 1);
    auto dot = file.find_last_of('.');
    if (dot != std::string::npos && dot > 0) {
        std::string ext = file.substr(dot);
        for (char& c : ext) c = (char)std::tolower((unsigned char)c);
        if (ext == ".okayanim" || ext == ".anim") return file.substr(0, dot);
    }
    return p;
}

inline const char* VsRefKindName(VsRefKind kind) {
    switch (kind) {
        case VsRefKind::Object:    return "an object";
        case VsRefKind::Prefab:    return "a prefab (.okayprefab)";
        case VsRefKind::Material:  return "a material (.okaymat)";
        case VsRefKind::Audio:     return "a sound (.wav, .ogg, .mp3)";
        case VsRefKind::Animation: return "an animation (.okayanim)";
        case VsRefKind::Scene:     return "a scene (.okayscene)";
        case VsRefKind::Graph:     return "a graph (.okayvs or .okaybt)";
        case VsRefKind::Asset:     return "a file";
        default: return "a value";
    }
}

inline std::string VsExtOf(const std::string& path) {
    auto dot = path.find_last_of('.');
    if (dot == std::string::npos) return {};
    std::string e = path.substr(dot);
    for (char& c : e) c = (char)std::tolower((unsigned char)c);
    return e;
}

inline bool VsExtOk(VsRefKind kind, const std::string& path) {
    const std::string e = VsExtOf(path);
    switch (kind) {
        case VsRefKind::Prefab:    return e == ".okayprefab";
        case VsRefKind::Material:  return e == ".okaymat";
        case VsRefKind::Audio:     return e == ".wav" || e == ".ogg" || e == ".mp3";
        case VsRefKind::Animation: return e == ".okayanim" || e == ".anim";
        case VsRefKind::Scene:     return e == ".okayscene";
        case VsRefKind::Graph:     return e == ".okayvs" || e == ".okaybt";
        case VsRefKind::Asset:     return !e.empty() || !path.empty();
        default: return false;
    }
}

/// Rewrite a legacy name or path into a reference when the scene (or the file
/// extension) tells us what it is. Returns true when `text` changed.
inline bool VsUpgradeRefString(std::string& text, VsRefKind kind, Scene* scene) {
    if (text.empty() || kind == VsRefKind::None || VsIsObjectRef(text) || VsIsAssetRef(text)) return false;
    if (kind == VsRefKind::Object) {
        if (text == "self") { text = "@self"; return true; }
        if (text == "other") { text = "@other"; return true; }
        if (scene) if (GameObject* o = scene->Find(text)) if (!o->Guid().empty()) {
            text = VsGuidRef(o->Guid());
            return true;
        }
        return false;
    }
    if (!VsExtOk(kind, text) && text.find('/') == std::string::npos && text.find('\\') == std::string::npos)
        return false;
    text = VsAssetRef(kind, text);
    return true;
}

} // namespace okay::vs
