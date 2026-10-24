#include "okay/VisualScript/VsVariables.hpp"

namespace okay::vs {

namespace {
std::unordered_map<const Scene*, VarMap>& SceneMaps() {
    static std::unordered_map<const Scene*, VarMap> maps;
    return maps;
}
} // namespace

VarMap& SceneVariables(const Scene* scene) { return SceneMaps()[scene]; }
void ForgetSceneVariables(const Scene* scene) { SceneMaps().erase(scene); }

VarMap& GlobalVariables() {
    static VarMap vars;
    return vars;
}
void ClearGlobalVariables() { GlobalVariables().clear(); }

bool VsSameValue(const VsValue& a, const VsValue& b) {
    if (a.IsObject() || b.IsObject()) return a.IsObject() && b.IsObject() && a.AsObjectId() == b.AsObjectId();
    if (a.IsArray() || b.IsArray()) return a.IsArray() && b.IsArray() && a.AsArray() == b.AsArray();
    if (a.IsMap() || b.IsMap()) return a.IsMap() && b.IsMap() && a.AsMap() == b.AsMap();
    if (a.IsString() || b.IsString()) return a.IsString() && b.IsString() && a.AsString() == b.AsString();
    if (a.IsVec3() || b.IsVec3()) {
        if (!a.IsVec3() || !b.IsVec3()) return false;
        const Vec3 x = a.AsVec3(), y = b.AsVec3();
        return x.x == y.x && x.y == y.y && x.z == y.z;
    }
    if (a.IsBool() != b.IsBool()) return false;
    return a.IsBool() ? a.AsBool() == b.AsBool() : a.AsFloat() == b.AsFloat();
}

} // namespace okay::vs
