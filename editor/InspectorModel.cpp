#include "InspectorModel.hpp"
#include <algorithm>
#include <cctype>
#include <filesystem>
#include <sstream>

using namespace okay;

namespace oked {

namespace {

std::string Lower(std::string s) {
    for (char& c : s) c = (char)std::tolower((unsigned char)c);
    return s;
}

bool Contains(const std::string& hay, const std::string& needle) {
    return hay.find(needle) != std::string::npos;
}

std::vector<std::string> SplitWords(const std::string& text) {
    std::vector<std::string> out;
    std::istringstream in(text);
    std::string w;
    while (in >> w) out.push_back(w);
    return out;
}

} // namespace

// ---- Hierarchy search -------------------------------------------------------------------

std::string HierKindWords(const GameObject& go) {
    std::string h;
    if (go.GetComponent<Camera>())          h += " camera";
    if (go.GetComponent<MeshRenderer>())    h += " mesh 3d";
    if (go.GetComponent<SpriteRenderer>())  h += " sprite 2d";
    if (go.GetComponent<Light>())           h += " light";
    if (go.GetComponent<TextRenderer>())    h += " text ui";
    if (go.GetComponent<ParticleSystem>())  h += " particles fx";
    if (go.GetComponent<AudioSource>())     h += " sound audio";
    if (!go.GetComponents<ScriptComponent>().empty()) h += " script";
    if (go.GetComponent<Rigidbody3D>() || go.GetComponent<Rigidbody2D>()) h += " rigidbody physics";
    if (go.GetComponent<Collider3D>() || go.GetComponent<Collider2D>())   h += " collider";
    if (go.GetComponent<NPCController>())   h += " npc ai enemy";
    if (go.GetComponent<Spawner>())         h += " spawner waves";
    if (go.GetComponent<Terrain>())         h += " terrain ground";
    if (go.GetComponent<Character>())       h += " character humanoid";
    if (go.GetComponent<FirstPersonController>() || go.GetComponent<ThirdPersonController>() ||
        go.GetComponent<CharacterController3D>() || go.GetComponent<ClickToMoveController>())
        h += " player controller";
    if (go.GetComponent<UIButton>() || go.GetComponent<UIPanel>() ||
        go.GetComponent<UIImage>()  || go.GetComponent<UIRawImage>() || go.GetComponent<UISlider>() ||
        go.GetComponent<UIToggle>() || go.GetComponent<UIProgressBar>())
        h += " ui widget canvas";
    return h;
}

std::string HierComponentWords(const GameObject& go) {
    std::string h = HierKindWords(go);
    for (const auto& up : go.Components()) {
        if (!up) continue;
        if (auto info = PropertyRegistry::FindFor(*up)) {
            h += " " + info->name;
            if (!info->recordName.empty()) h += " " + info->recordName;
        }
    }
    return Lower(h);
}

HierFilter HierFilter::Parse(const std::string& text) {
    HierFilter f;
    std::size_t a = text.find_first_not_of(" \t");
    if (a == std::string::npos) return f;
    const std::string trimmed = text.substr(a, text.find_last_not_of(" \t") - a + 1);
    if (trimmed.find('/') != std::string::npos) {   // a path keeps its spaces
        f.terms.push_back({Kind::Path, trimmed, false});
        return f;
    }
    for (std::string w : SplitWords(trimmed)) {
        Term t;
        if (w.size() > 1 && w[0] == '-') { t.negate = true; w.erase(0, 1); }
        std::string lw = Lower(w);
        const std::size_t colon = lw.find(':');
        t.kind = Kind::Word;
        t.text = lw;
        if (colon != std::string::npos && colon > 0) {
            const std::string key = lw.substr(0, colon), val = lw.substr(colon + 1);
            Kind k = Kind::Word;
            bool known = true;
            if (key == "t" || key == "tag")                              k = Kind::Tag;
            else if (key == "c" || key == "comp" || key == "component")  k = Kind::Component;
            else if (key == "l" || key == "layer")                       k = Kind::Layer;
            else if (key == "is")                                        k = Kind::State;
            else if (key == "n" || key == "name")                        k = Kind::Word;
            else known = false;
            if (known) {
                if (val.empty()) continue;   // "t:" while typing: no restriction yet
                t.kind = k;
                t.text = val;
            }
        }
        f.terms.push_back(t);
    }
    return f;
}

std::string HierFilter::PathText() const {
    for (const Term& t : terms) if (t.kind == Kind::Path) return t.text;
    return {};
}

bool HierFilter::Matches(const GameObject& go, const std::unordered_set<const GameObject*>* pathHits) const {
    for (const Term& t : terms) {
        bool hit = false;
        switch (t.kind) {
            case Kind::Path: {
                const std::string hay = Lower(go.name) + HierKindWords(go);
                hit = (pathHits && pathHits->count(&go)) || Contains(hay, Lower(t.text));
                break;
            }
            case Kind::Word:
                hit = Contains(Lower(go.name), t.text) || Contains(HierKindWords(go), t.text) ||
                      (!go.tag.empty() && Contains(Lower(std::string(go.tag)), t.text));
                break;
            case Kind::Tag:
                hit = Contains(go.tag.empty() ? std::string("untagged") : Lower(std::string(go.tag)), t.text);
                break;
            case Kind::Component:
                hit = Contains(HierComponentWords(go), t.text);
                break;
            case Kind::Layer:
                hit = Contains(Lower(Layers::Name(go.layer)), t.text);
                break;
            case Kind::State: {
                const std::string& s = t.text;
                if (s == "off" || s == "inactive" || s == "disabled") hit = !go.active;
                else if (s == "on" || s == "active")                  hit = go.active;
                else if (s == "locked")                               hit = go.editorLocked;
                else if (s == "static")                               hit = go.isStatic;
                else if (s == "prefab")                               hit = PrefabTools::IsPrefabInstance(go);
                else if (s == "ui")                                   hit = Contains(HierKindWords(go), "ui");
                else hit = false;
                break;
            }
        }
        if (hit == t.negate) return false;
    }
    return true;
}

// ---- Drag and drop ----------------------------------------------------------------------------

std::vector<GameObject*> HierarchyOrder(Scene& scene) {
    std::vector<GameObject*> out;
    std::function<void(GameObject*)> walk = [&](GameObject* g) {
        out.push_back(g);
        for (Transform* c : g->transform->Children())
            if (c && c->gameObject) walk(c->gameObject);
    };
    for (const auto& up : scene.Objects())
        if (up && up->transform && !up->transform->Parent()) walk(up.get());
    return out;
}

std::vector<GameObject*> PlanDrop(Scene& scene, const std::vector<GameObject*>& dragged,
                                  const GameObject* target, DropWhere where) {
    std::vector<GameObject*> moved;
    for (GameObject* g : dragged) {
        if (!g || !g->transform || g == target) continue;
        if (std::find(moved.begin(), moved.end(), g) != moved.end()) continue;
        // Never into itself or under its own child.
        bool cycle = false;
        if (target)
            for (Transform* t = target->transform; t; t = t->Parent())
                if (t == g->transform) { cycle = true; break; }
        if (cycle) continue;
        // A dragged parent already carries this one.
        bool covered = false;
        for (Transform* t = g->transform->Parent(); t && !covered; t = t->Parent())
            if (std::find(dragged.begin(), dragged.end(), t->gameObject) != dragged.end()) covered = true;
        if (covered) continue;
        moved.push_back(g);
    }
    if (moved.size() > 1) {
        const std::vector<GameObject*> order = HierarchyOrder(scene);
        auto rank = [&](GameObject* g) {
            return (int)(std::find(order.begin(), order.end(), g) - order.begin());
        };
        std::stable_sort(moved.begin(), moved.end(), [&](GameObject* a, GameObject* b) { return rank(a) < rank(b); });
    }
    (void)where;
    return moved;
}

int ApplyDrop(Scene& scene, const std::vector<GameObject*>& dragged, GameObject* target,
              DropWhere where, const DropHooks& hooks) {
    if (!target && where != DropWhere::Inside) return 0;
    const std::vector<GameObject*> moved = PlanDrop(scene, dragged, target, where);
    int n = 0;
    GameObject* afterAnchor = target;
    for (GameObject* mv : moved) {
        if (hooks.before) hooks.before(mv);
        if (where == DropWhere::Before) {
            scene.ReorderSibling(mv, target, /*after=*/false);   // each lands right before the target: group order kept
        } else if (where == DropWhere::After) {
            scene.ReorderSibling(mv, afterAnchor, /*after=*/true);
            afterAnchor = mv;                                    // chain, or the group would come out reversed
        } else {
            Transform* np = target ? target->transform : nullptr;
            mv->transform->SetParent(np, /*worldPositionStays=*/true, std::numeric_limits<int>::max());
        }
        if (hooks.after) hooks.after(mv);
        ++n;
    }
    return n;
}

// ---- Several objects at once ------------------------------------------------------------------------

std::string ComponentPath(const std::string& typeName, int index) {
    return index > 0 ? typeName + "#" + std::to_string(index) : typeName;
}

int ComponentIndexOf(const GameObject& go, const Component& c) {
    auto info = PropertyRegistry::FindFor(c);
    if (!info) return 0;
    int n = 0;
    for (const auto& up : go.Components()) {
        if (up.get() == &c) return n;
        if (up && PropertyRegistry::FindFor(*up) == info) ++n;
    }
    return 0;
}

static bool AxisEqual(const PropertyValue& a, const PropertyValue& b, int i) {
    auto get = [i](const PropertyValue& v) -> float {
        switch (v.Type()) {
            case PropertyType::Vec2:  { Vec2 x = v.AsVec2(); return i == 0 ? x.x : x.y; }
            case PropertyType::Vec3:  { Vec3 x = v.AsVec3(); return i == 0 ? x.x : i == 1 ? x.y : x.z; }
            case PropertyType::Vec4:  { Vec4 x = v.AsVec4(); return i == 0 ? x.x : i == 1 ? x.y : i == 2 ? x.z : x.w; }
            case PropertyType::Quat:  { Quat x = v.AsQuat(); return i == 0 ? x.x : i == 1 ? x.y : i == 2 ? x.z : x.w; }
            case PropertyType::Color: { Color x = v.AsColor(); return i == 0 ? x.r : i == 1 ? x.g : i == 2 ? x.b : x.a; }
            default: return 0.0f;
        }
    };
    return get(a) == get(b);
}

FieldState ReadField(const std::vector<GameObject*>& objs, const std::string& typeName, int index,
                     const PropertyDesc& desc) {
    FieldState st;
    const int comps = PropertyTypeComponentCount(desc.type);
    st.axisMixed.assign((std::size_t)comps, false);
    const std::string path = ComponentPath(typeName, index) + "." + desc.name;
    for (GameObject* g : objs) {
        if (!g) continue;
        PropertyValue v;
        if (!PropertyRegistry::TryGetProperty(*g, path, v)) continue;
        if (st.have == 0) st.value = v;
        else {
            if (v != st.value) st.mixed = true;
            for (int i = 0; i < comps; ++i)
                if (!AxisEqual(v, st.value, i)) st.axisMixed[(std::size_t)i] = true;
        }
        ++st.have;
    }
    return st;
}

int WriteField(const std::vector<GameObject*>& objs, const std::string& typeName, int index,
               const PropertyDesc& desc, const PropertyValue& value) {
    const std::string path = ComponentPath(typeName, index) + "." + desc.name;
    int n = 0;
    for (GameObject* g : objs) {
        if (!g) continue;
        if (PropertyRegistry::WriteProperty(*g, path, value).changed) ++n;
    }
    return n;
}

static PropertyValue WithAxes(const PropertyValue& base, const PropertyValue& edit, unsigned mask) {
    auto pick = [&](int i, float b, float e) { return (mask >> i) & 1u ? e : b; };
    switch (base.Type()) {
        case PropertyType::Vec2: { Vec2 b = base.AsVec2(), e = edit.AsVec2();
            return PropertyValue::MakeVec2({pick(0, b.x, e.x), pick(1, b.y, e.y)}); }
        case PropertyType::Vec3: { Vec3 b = base.AsVec3(), e = edit.AsVec3();
            return PropertyValue::MakeVec3({pick(0, b.x, e.x), pick(1, b.y, e.y), pick(2, b.z, e.z)}); }
        case PropertyType::Vec4: { Vec4 b = base.AsVec4(), e = edit.AsVec4();
            return PropertyValue::MakeVec4({pick(0, b.x, e.x), pick(1, b.y, e.y), pick(2, b.z, e.z), pick(3, b.w, e.w)}); }
        case PropertyType::Quat: { Quat b = base.AsQuat(), e = edit.AsQuat();
            return PropertyValue::MakeQuat({pick(0, b.x, e.x), pick(1, b.y, e.y), pick(2, b.z, e.z), pick(3, b.w, e.w)}); }
        case PropertyType::Color: { Color b = base.AsColor(), e = edit.AsColor();
            return PropertyValue::MakeColor({pick(0, b.r, e.r), pick(1, b.g, e.g), pick(2, b.b, e.b), pick(3, b.a, e.a)}); }
        default: return edit;
    }
}

int WriteFieldAxes(const std::vector<GameObject*>& objs, const std::string& typeName, int index,
                   const PropertyDesc& desc, const PropertyValue& value, unsigned axisMask) {
    const std::string path = ComponentPath(typeName, index) + "." + desc.name;
    int n = 0;
    for (GameObject* g : objs) {
        if (!g) continue;
        PropertyValue cur;
        if (!PropertyRegistry::TryGetProperty(*g, path, cur)) continue;
        if (PropertyRegistry::WriteProperty(*g, path, WithAxes(cur, value, axisMask)).changed) ++n;
    }
    return n;
}

ComponentEdit BeginComponentEdit(const std::vector<GameObject*>& selection, const GameObject* primary, const Component& comp) {
    ComponentEdit e;
    if (selection.size() < 2 || !primary || std::find(selection.begin(), selection.end(), primary) == selection.end()) return e;
    if (!PropertyRegistry::FindFor(comp)) return e;
    e.before = SnapshotComponent(comp);
    e.active = !e.before.Empty();
    return e;
}

int PropagateComponentEdit(const std::vector<GameObject*>& selection, GameObject* primary, Component& comp,
                           const ComponentEdit& edit) {
    if (!edit.active || !primary) return 0;
    auto info = PropertyRegistry::FindFor(comp);
    if (!info) return 0;
    const std::vector<PropertyDiff> diff = DiffProperties(edit.before, SnapshotComponent(comp));
    if (diff.empty()) return 0;
    const int idx = ComponentIndexOf(*primary, comp);
    int written = 0;
    for (GameObject* g : selection) {
        if (!g || g == primary) continue;
        Component* other = nullptr;
        int n = 0;
        for (const auto& up : g->Components())
            if (up && PropertyRegistry::FindFor(*up) == info) { if (n++ == idx) { other = up.get(); break; } }
        if (!other) continue;
        for (const PropertyDiff& d : diff)
            if (d.kind == PropertyDiffKind::Changed && PropertyRegistry::SetComponentProperty(*other, d.property, d.after)) ++written;
    }
    return written;
}

// ---- Copy, paste, reset ----------------------------------------------------------------------------------

bool HasRegisteredProperties(const Component& c) {
    auto info = PropertyRegistry::FindFor(c);
    return info && !info->properties.empty();
}

ComponentClip CopyComponentValues(const Component& c) {
    ComponentClip clip;
    auto info = PropertyRegistry::FindFor(c);
    if (!info) return clip;
    clip.type = info->name;
    clip.values = SnapshotComponent(c);
    return clip;
}

int PasteComponentValues(const ComponentClip& clip, Component& target) {
    auto info = PropertyRegistry::FindFor(target);
    if (!clip.Valid() || !info || info->name != clip.type) return 0;
    int n = 0;
    for (const auto& e : clip.values.entries) {
        PropertyValue before = PropertyRegistry::GetComponentProperty(target, e.property);
        if (before == e.value) continue;
        if (PropertyRegistry::SetComponentProperty(target, e.property, e.value)) ++n;
    }
    return n;
}

int ResetComponentValues(Component& c) {
    auto info = PropertyRegistry::FindFor(c);
    if (!info || !info->add) return 0;
    // A fresh component of the same type, on a scratch object, is the reference for "default".
    Scene scratch;
    GameObject* g = scratch.CreateGameObject("scratch");
    Component* fresh = info->add(*g);
    if (!fresh) return 0;
    return PasteComponentValues(CopyComponentValues(*fresh), c);
}

// ---- Field labels and asset kinds ---------------------------------------------------------------------------------

std::string PrettyName(const std::string& n) {
    // Names that would read as jargon ("Ao Map", "Uv Min") get a plain label. The saved name never changes.
    static const struct { const char* name; const char* label; } kPlain[] = {
        {"fovAxisHorizontal", "Field Of View Along Width"}, {"aoMap", "Crease Shadow Map"}, {"aoStrength", "Crease Shadow Strength"},
        {"uvScroll", "Texture Scroll Speed"}, {"uvMin", "Picture Start"}, {"uvMax", "Picture End"},
    };
    for (const auto& r : kPlain) if (n == r.name) return r.label;
    std::string out;
    for (std::size_t i = 0; i < n.size(); ++i) {
        const char c = n[i];
        if (c == '_') { if (!out.empty() && out.back() != ' ') out += ' '; continue; }
        const bool upper = std::isupper((unsigned char)c) != 0;
        if (i > 0 && upper) {
            const bool prevLower = std::islower((unsigned char)n[i - 1]) || std::isdigit((unsigned char)n[i - 1]);
            const bool acronymEnd = std::isupper((unsigned char)n[i - 1]) && i + 1 < n.size() && std::islower((unsigned char)n[i + 1]);
            if ((prevLower || acronymEnd) && !out.empty() && out.back() != ' ') out += ' ';
        }
        out += (out.empty() || out.back() == ' ') ? (char)std::toupper((unsigned char)c) : c;
    }
    return out.empty() ? n : out;
}

std::vector<std::string> AssetKindExtensions(const std::string& kind) {
    const std::string k = Lower(kind);
    if (k == "image" || k == "texture") return {".png", ".jpg", ".jpeg", ".bmp", ".tga"};
    if (k == "audio" || k == "sound")   return {".wav", ".ogg", ".mp3", ".flac"};
    if (k == "model" || k == "mesh")    return {".obj", ".fbx", ".gltf", ".glb", ".dae", ".stl", ".ply", ".3ds"};
    if (k == "font")                    return {".ttf", ".otf"};
    if (k == "prefab")                  return {".okayprefab"};
    if (k == "script")                  return {".okay", ".okayvs"};
    if (k == "material")                return {".okaymat"};
    if (k == "scene")                   return {".okayscene"};
    return {};
}

bool AssetKindAccepts(const std::string& kind, const std::string& path) {
    const std::vector<std::string> exts = AssetKindExtensions(kind);
    if (exts.empty()) return !path.empty();
    const std::size_t dot = path.find_last_of('.');
    if (dot == std::string::npos) return false;
    const std::string ext = Lower(path.substr(dot));
    return std::find(exts.begin(), exts.end(), ext) != exts.end();
}

std::vector<std::string> ListAssetsOfKind(const std::string& root, const std::string& kind, std::size_t limit) {
    namespace fs = std::filesystem;
    std::vector<std::string> out;
    std::error_code ec;
    if (root.empty() || !fs::is_directory(root, ec)) return out;
    for (fs::recursive_directory_iterator it(root, fs::directory_options::skip_permission_denied, ec), end;
         !ec && it != end && out.size() < limit; it.increment(ec)) {
        if (it->path().filename().string().rfind('.', 0) == 0) {          // hidden folders and files
            if (it->is_directory(ec)) it.disable_recursion_pending();
            continue;
        }
        if (!it->is_regular_file(ec)) continue;
        const std::string p = it->path().string();
        if (AssetKindAccepts(kind, p)) out.push_back(p);
    }
    std::sort(out.begin(), out.end(), [](const std::string& a, const std::string& b) {
        return a.size() != b.size() ? a.size() < b.size() : a < b;
    });
    return out;
}

namespace {
struct TipRow { const char* key; const char* text; };
const TipRow kTips[] = {
    {"Transform.position", "Where the object is, relative to its parent."},
    {"Transform.rotation", "How the object is turned, in degrees."},
    {"Transform.scale", "How big the object is on each axis."},
    {"Camera.projection", "Perspective looks like the real world (things shrink with distance). Orthographic has no depth shrink, good for 2D."},
    {"Camera.nearClip", "Closest distance the camera draws. Things nearer are cut off."},
    {"Camera.farClip", "Farthest distance the camera draws."},
    {"Camera.clearFlags", "What fills the screen before drawing: the sky, a flat colour, or nothing."},
    {"Camera.backgroundColor", "The flat colour behind everything when the camera clears to a colour."},
    {"Rigidbody2D.sleeping", "True while physics has put it to rest."},
    {"Rigidbody3D.sleeping", "True while physics has put it to rest."},
    {"Rigidbody3D.groundedOnTerrain", "True while it stands on terrain."},
    {"Camera.lensShiftX", "Slides the picture sideways without turning the camera."},
    {"Camera.lensShiftY", "Slides the picture up or down without turning the camera."},
    {"Camera.pixelsPerUnit", "How many screen pixels make one world unit (pixel-perfect 2D)."},
    {"Camera.shakeAmount", "Strength of the current camera shake."},
    {"Camera.shakeAngle", "How much the shake also rolls the picture, in degrees."},
    {"Camera.shakeFrequency", "How fast the shake wobbles."},
    {"Camera.shakeRecovery", "How quickly the shake dies down."},
    {"Camera.zoom", "Extra zoom on top of the field of view."},
    {"Camera.rectY", "Where the camera's picture starts from the bottom (0 to 1). Use with Width and Height for split screen."},
    {"Camera.rectW", "Width of the camera's picture on screen (0 to 1)."},
    {"Camera.rectH", "Height of the camera's picture on screen (0 to 1)."},
    {"Camera.pixelPerfect", "Snap the 2D view to whole pixels so pixel art stays sharp."},
    {"Light.type", "Directional is the sun, Point is a bulb, Spot is a cone, Area is a soft panel."},
    {"Light.color", "The colour of the light."},
    {"Light.intensity", "How bright the light is."},
    {"Light.spotAngle", "How wide the cone is, in degrees (spot lights)."},
    {"Light.spotSoftness", "How soft the edge of the cone is."},
    {"Light.falloff", "How fast the light fades with distance."},
    {"Light.castShadows", "Objects in front of this light throw shadows."},
    {"Light.shadowStrength", "How dark the shadows are."},
    {"Light.shadowBias", "Nudges shadows to remove speckles. Raise it if you see stripes."},
    {"Light.flickerSpeed", "How fast the flicker changes."},
    {"SpriteRenderer.color", "Tint multiplied with the picture. White leaves it unchanged."},
    {"SpriteRenderer.texture", "The picture to draw. Drop an image from the Project panel."},
    {"SpriteRenderer.uvMin", "Start of the part of the picture that is used (0 to 1)."},
    {"SpriteRenderer.uvMax", "End of the part of the picture that is used (0 to 1)."},
    {"SpriteRenderer.flipX", "Mirror the picture left to right."},
    {"SpriteRenderer.flipY", "Mirror the picture upside down."},
    {"SpriteRenderer.texFilter", "Smooth blends pixels, Point keeps them sharp (pixel art)."},
    {"MeshRenderer.wireframe", "Draw only the edges of the triangles."},
    {"MeshRenderer.doubleSided", "Draw both sides of every face (for thin things like leaves)."},
    {"MeshRenderer.shader", "The look: standard, toon, gradient and so on."},
    {"MeshRenderer.emissive", "Makes the surface glow with its own light."},
    {"MeshRenderer.specular", "How strong the shiny highlight is."},
    {"MeshRenderer.shininess", "How tight the highlight is. Higher is smaller and sharper."},
    {"MeshRenderer.metallic", "0 is plastic or stone, 1 is metal."},
    {"MeshRenderer.reflectivity", "How much the surface mirrors its surroundings."},
    {"MeshRenderer.texture", "The picture on the surface. Drop an image from the Project panel."},
    {"MeshRenderer.tiling", "How many times the picture repeats across the surface."},
    {"MeshRenderer.texOffset", "Slides the picture across the surface."},
    {"MeshRenderer.texFilter", "Smooth blends pixels, Point keeps them sharp."},
    {"MeshRenderer.normalMap", "A picture that fakes small bumps."},
    {"MeshRenderer.normalStrength", "How strong the fake bumps are."},
    {"MeshRenderer.specularMap", "A picture that says which parts are shiny."},
    {"MeshRenderer.aoMap", "A picture that darkens creases."},
    {"MeshRenderer.aoStrength", "How dark the creases are."},
    {"MeshRenderer.triplanar", "Project the picture from three sides so it does not stretch."},
    {"MeshRenderer.rimStrength", "Glow around the edge seen at an angle."},
    {"MeshRenderer.rimPower", "How thin the edge glow is."},
    {"MeshRenderer.rimColor", "Colour of the edge glow."},
    {"MeshRenderer.outline", "Draw an outline around the object."},
    {"MeshRenderer.outlineColor", "Colour of the outline."},
    {"MeshRenderer.outlineWidth", "Thickness of the outline."},
    {"MeshRenderer.castShadows", "This object throws shadows."},
    {"MeshRenderer.receiveShadows", "Shadows fall on this object."},
    {"MeshRenderer.groundShadowY", "Height of the ground the blob shadow sits on."},
    {"MeshRenderer.groundShadowStrength", "How dark the blob shadow is."},
    {"MeshRenderer.gradientTop", "Colour at the top for the gradient look."},
    {"MeshRenderer.gradientBottom", "Colour at the bottom for the gradient look."},
    {"Rigidbody2D.bodyType", "Dynamic moves with physics, Kinematic moves only by script, Static never moves."},
    {"Rigidbody2D.mass", "How heavy it is. Heavier things are harder to push."},
    {"Rigidbody2D.gravityScale", "How much gravity pulls on it. 0 floats, 1 is normal."},
    {"Rigidbody2D.angularDrag", "How quickly spinning slows down."},
    {"Rigidbody2D.bounciness", "How much it bounces (0 none, 1 full)."},
    {"Rigidbody2D.friction", "How much it grips what it touches."},
    {"Rigidbody2D.freezeRotation", "Never rotate from physics."},
    {"Rigidbody2D.allowSleep", "Stop simulating it when it comes to rest, to save time."},
    {"Rigidbody2D.continuousCollision", "Catch fast objects so they do not pass through thin walls."},
    {"Rigidbody3D.bodyType", "Dynamic moves with physics, Kinematic moves only by script, Static never moves."},
    {"Rigidbody3D.mass", "How heavy it is. Heavier things are harder to push."},
    {"Rigidbody3D.gravityScale", "How much gravity pulls on it. 0 floats, 1 is normal."},
    {"Rigidbody3D.angularDrag", "How quickly spinning slows down."},
    {"Rigidbody3D.bounciness", "How much it bounces (0 none, 1 full)."},
    {"Rigidbody3D.friction", "How much it grips what it touches."},
    {"Rigidbody3D.freezeX", "Do not move along X."},
    {"Rigidbody3D.freezeY", "Do not move along Y."},
    {"Rigidbody3D.freezeZ", "Do not move along Z."},
    {"Rigidbody3D.freezeRotation", "Never rotate from physics."},
    {"Rigidbody3D.allowSleep", "Stop simulating it when it comes to rest, to save time."},
    {"Rigidbody3D.continuousCollision", "Catch fast objects so they do not pass through thin walls."},
    {"BoxCollider2D.size", "Width and height of the box."},
    {"BoxCollider3D.size", "Width, height and depth of the box."},
    {"MeshCollider3D.size", "Fallback box size, used only when there is no mesh with triangles. Otherwise static meshes use the exact triangles and a dynamic body uses the convex hull."},
    {"CircleCollider2D.radius", "How big the circle is."},
    {"SphereCollider3D.radius", "How big the sphere is."},
    {"CapsuleCollider2D.size", "Width and height of the capsule."},
    {"CapsuleCollider2D.direction", "Which way the capsule stands."},
    {"CapsuleCollider3D.radius", "How wide the capsule is."},
    {"CylinderCollider3D.radius", "How wide the cylinder is."},
    {"CylinderCollider3D.height", "How tall the cylinder is."},
    {"CylinderCollider3D.axis", "Which way the cylinder stands."},
    {"AudioSource.volume", "How loud it is (0 to 1)."},
    {"AudioSource.loop", "Start again when it ends."},
    {"AudioSource.playOnAwake", "Start playing when the game starts."},
    {"AudioSource.spatial", "Sound gets quieter and pans with distance and direction (3D sound)."},
    {"AudioSource.minDistance", "Within this distance the sound is at full volume."},
    {"AudioSource.maxDistance", "Beyond this distance the sound is silent."},
    {"AudioSource.rolloff", "How fast the sound fades between the two distances."},
    // ---- Particle System (written from ParticleSystem.hpp: what each value does to a live particle) ----
    {"ParticleSystem.playing", "Turn on to emit particles; turn off to stop making new ones (those alive finish their life)."},
    {"ParticleSystem.maxParticles", "Most particles alive at once (a count). New ones wait when this is full."},
    {"ParticleSystem.loop", "When on, the emission cycle starts again after Duration ends."},
    {"ParticleSystem.burstTime", "Seconds into the cycle when the one-shot burst fires."},
    {"ParticleSystem.shape", "Where particles are born: Point, a Circle ring, a Sphere disc, a Box, a Cone spray, or an Edge line."},
    {"ParticleSystem.shapeRadius", "Size of the Circle, Sphere or Edge area, in world units (metres)."},
    {"ParticleSystem.boxSize", "Width, height and depth of the Box area, in world units (metres)."},
    {"ParticleSystem.startLifetime", "How long each particle lives, in seconds."},
    {"ParticleSystem.startLifetimeRandom", "Random extra or less life per particle, plus or minus this many seconds."},
    {"ParticleSystem.startSize", "How big each particle is when born, in world units (metres)."},
    {"ParticleSystem.startSizeRandom", "Random size difference per particle, plus or minus this many world units."},
    {"ParticleSystem.startColor", "The colour of each particle when born."},
    {"ParticleSystem.startVelocity", "The launch speed and direction of each particle, in metres per second on each axis."},
    {"ParticleSystem.velocityRandom", "Random speed added to each axis of the launch speed, plus or minus this many metres per second."},
    {"ParticleSystem.speedRandom", "Random change of the launch speed for the shaped emitters, plus or minus this fraction (0 to 1 is none to full)."},
    {"ParticleSystem.texture", "The picture drawn for each particle. Empty draws a soft round puff."},
    {"ParticleSystem.renderMode", "Billboard always faces the camera. Stretch draws each particle longer the faster it moves."},
    {"ParticleSystem.stretchScale", "Stretch mode: how much longer a particle is drawn for each metre per second of speed."},
    {"ParticleSystem.startRotation", "How far each particle is turned when born, in degrees."},
    {"ParticleSystem.startRotationRandom", "Random start turn, plus or minus this many degrees."},
    {"ParticleSystem.rotationSpeed", "How fast each particle spins, in degrees per second."},
    {"ParticleSystem.rotationSpeedRandom", "Random spin speed, plus or minus this many degrees per second."},
    {"ParticleSystem.endColor", "The colour at the end of life. Used only when Colour Over Life is on."},
    {"ParticleSystem.colorOverLife", "Blend each particle from Start Colour to End Colour as it ages."},
    {"ParticleSystem.endSize", "The size at the end of life, in world units (metres). Used only when Size Over Life is on."},
    {"ParticleSystem.sizeOverLife", "Blend each particle from its start size to End Size as it ages."},
    {"ParticleSystem.fadeOverLife", "Fade each particle out as it nears the end of its life."},
    {"ParticleSystem.speedOverLife", "Change the speed over a particle's life using the speed curve (it has no effect with no curve set)."},
    {"ParticleSystem.gravity", "Constant pull on every particle, in metres per second squared on each axis. Default pulls down."},
    {"ParticleSystem.gravityModifier", "Multiplies Gravity. 0 switches it off, 1 is as set, negative pulls up."},
    {"ParticleSystem.damping", "Air drag: the fraction of speed lost each second (0 is none)."},
    {"ParticleSystem.simulationSpace", "World: particles stay where they were born when the object moves. Local: they travel with the object."},
    {"ParticleSystem.inheritVelocity", "How much of the emitter's own speed new particles start with (0 none, 1 all of it)."},
    {"ParticleSystem.noiseStrength", "Turbulence: how far particles wobble, in metres per second. 0 is off."},
    {"ParticleSystem.noiseFrequency", "Turbulence: how tight the wobble is (swirls per metre). Higher is busier."},
    {"ParticleSystem.noiseScroll", "Turbulence: how fast the wobble pattern changes over time (cycles per second)."},
    {"ParticleSystem.orbitSpeed", "Spin particles around the Orbit Axis through the emitter, in degrees per second. 0 is off."},
    {"ParticleSystem.orbitAxis", "The direction of the line the particles orbit and swirl around (default up)."},
    {"ParticleSystem.vortexStrength", "Whirlwind: sideways push around the Orbit Axis, in metres per second squared. 0 is off."},
    {"ParticleSystem.vortexPull", "Whirlwind: pull towards the Orbit Axis, in metres per second squared. Negative pushes outward."},
    {"ParticleSystem.collision", "Particles bounce off a flat floor at the height below."},
    {"ParticleSystem.collisionY", "Height of the floor the particles bounce off, in metres."},
    {"ParticleSystem.bounce", "How much speed a particle keeps when it hits the floor (0 sticks, 1 bounces fully)."},
    {"ParticleSystem.collisionFriction", "How much sideways speed is lost at each bounce (0 to 1)."},
    {"ParticleSystem.collisionLifeLoss", "How much of its remaining life a particle loses at each bounce (0 to 1)."},
    // ---- UI Panel and Canvas (written from UIPanel.hpp and Canvas.hpp) ----
    {"UIPanel.size", "Width and height of the panel, in pixels."},
    {"UIPanel.anchor", "Which part of the screen (or of the parent) the panel is placed against."},
    {"UIPanel.color", "The fill colour of the panel (its alpha is how see-through it is)."},
    {"UIPanel.shape", "The outline shape of the panel: rounded rectangle, circle, pill, arrow and so on."},
    {"UIPanel.cornerRadius", "How round the corners are, in pixels (Rounded and Pill shapes)."},
    {"UIPanel.borderWidth", "Thickness of a border drawn inside the panel edge, in pixels. 0 is no border."},
    {"UIPanel.borderColor", "Colour of the inside border."},
    {"UIPanel.useGradient", "Fade the fill from Colour to Bottom Colour."},
    {"UIPanel.gradientDir", "Which way the fade runs: top to bottom, left to right, or diagonal."},
    {"UIPanel.colorBottom", "The colour the fill fades to when Use Gradient is on."},
    {"UIPanel.outlineColor", "Colour of the ring drawn outside the panel edge."},
    {"UIPanel.highlightColor", "Colour of the light band along the inside top edge (Top Highlight)."},
    {"UIPanel.shadow", "Draw a drop shadow behind the panel."},
    {"UIPanel.shadowColor", "Colour of the drop shadow."},
    {"UIPanel.shadowOffset", "How far the shadow is shifted right and down, in pixels."},
    {"UIPanel.shadowSoftness", "How blurry the shadow edge is, in pixels. 0 is crisp."},
    {"Canvas.scaleMode", "ConstantPixelSize keeps widgets the same size in pixels. ScaleWithScreenSize grows them with the window."},
    {"Canvas.screenMatch", "When scaling with the screen: follow the width or height, fit everything inside (Expand), or fill the screen (Shrink)."},
    {"Canvas.visible", "Show or hide every widget under this canvas."},
    {"Canvas.opacity", "Fade everything on this canvas (0 to 1: invisible to solid)."},
    {"Canvas.designResolution", "The canvas's own size in pixels while it is placed in the 3D world."},
    {"Canvas.worldPixelsPerUnit", "World canvas: how many design pixels make one metre in the world. Bigger makes it smaller in the world."},
    // ---- Camera rigs (written from the registry notes and VirtualCamera.hpp) ----
    {"VirtualCamera.rotationDamping", "How quickly the camera turns to its aim. Higher is snappier; 0 turns at once."},
    {"VirtualCamera.deadZoneH", "Height of the middle area where the target can move without the camera moving (0 to 1 of the screen)."},
    {"VirtualCamera.softZoneH", "Height of the area past which the camera catches up at once (0 to 1 of the screen)."},
    {"VirtualCamera.zoneDamping", "How quickly the camera catches up when the target leaves the middle area. Higher is snappier."},
    {"VirtualCamera.lookaheadMax", "Most the camera may look ahead of a moving target, in metres."},
    {"VirtualCamera.roomBlendTime", "Seconds the camera takes to slide when the target enters another room."},
    {"VirtualCamera.collisionRadius", "How fat the camera is for wall tests, in metres."},
    {"VirtualCamera.collisionSkin", "Gap kept between the camera and a wall, in metres."},
    {"VirtualCamera.collisionRecover", "How quickly the camera moves back out after a wall stops blocking it. Higher is faster."},
    {"VirtualCamera.occlusionAlpha", "How see-through things in the way become (0 invisible, 1 solid)."},
    {"VirtualCamera.occlusionFadeSpeed", "How fast things in the way fade. Higher is faster."},
    {"CinemachineBrain.blendStyle", "The curve of a blend between shots: Cut jumps, Linear is even, the Ease options start or end gently."},
    // ---- Small gameplay and UI parts (written from each component's header comments) ----
    {"UIDamageNumbers.textSize", "Text size of a normal number, in pixels."},
    {"UIDamageNumbers.critScale", "How much bigger a critical hit is drawn (1.6 means 60 percent bigger)."},
    {"UIDamageNumbers.popScale", "Every number starts this many times bigger and settles down to normal size."},
    {"UIDamageNumbers.maxActive", "Most numbers on screen at once (a count). The oldest goes first when there are more."},
    {"UIDamageNumbers.normalColor", "Colour of a normal damage number."},
    {"UIDamageNumbers.critColor", "Colour of a critical hit."},
    {"UIDamageNumbers.healColor", "Colour of healing numbers."},
    {"UIDamageNumbers.missColor", "Colour of a miss."},
    {"UIDamageNumbers.infoColor", "Colour of information text."},
    {"DayNightCycle.paused", "Freeze the time of day where it is."},
    {"DayNightCycle.dayIntensity", "Brightness of the sun in the middle of the day."},
    {"DayNightCycle.nightIntensity", "Brightness of the sun (or moon light) at night."},
    {"DayNightCycle.dayAmbient", "How bright the shadowy parts are by day (0 to 1)."},
    {"DayNightCycle.nightAmbient", "How bright the shadowy parts are at night (0 to 1)."},
    {"Draggable.axis", "Limit dragging to sideways only, up and down only, or free."},
    {"Draggable.gridX", "Snap the dragged item sideways to steps of this many world units (metres). 0 is no snap."},
    {"Draggable.gridY", "Snap the dragged item up and down to steps of this many world units (metres). 0 is no snap."},
    {"DropZone.acceptTag", "Only items with this tag can be dropped here. Empty accepts any."},
    {"FollowTarget2D.speed", "How fast it moves towards the target, in metres per second."},
    {"Collectible.respawnDelay", "Seconds until it comes back after being picked up (only when it respawns)."},
    {"UIFrame.anchorMin", "Lower-left corner of the area it is stretched across in its parent (0 to 1 of the parent). Equal Min and Max pin it to one point."},
    {"UIFrame.anchorMax", "Upper-right corner of the area it is stretched across in its parent (0 to 1 of the parent)."},
    {"UIFrame.pivot", "The point the frame turns and resizes around (0 to 1 across its own size)."},
    {"UIFlexItem.baseSize", "Its size in pixels before growing or shrinking. 0 uses the widget's own size."},
    {"UILayoutGroup.direction", "How the children are lined up: a column, a row, a grid or a wrapping flex line."},
    {"UIRadialProgress.clockwise", "Fill clockwise. Off fills anti-clockwise."},
    {"UIRadialProgress.spin", "Keep the arc turning (a loading spinner)."},
    {"UIRadialProgress.spinSpeed", "How fast the spinner turns, in degrees per second."},
    {"UIScrollView.axis", "Scroll up and down, sideways, or both."},
    {"UIScrollView.autoContent", "Measure the content from its children automatically."},
    {"UIScrollView.inertia", "How long a wheel scroll keeps gliding. 0 stops at once; higher stops sooner (per second)."},
    {"UIDraggable.axis", "Limit dragging to sideways only, up and down only, or free."},
    {"UIDraggable.dragThreshold", "Pixels the pointer must move before a drag starts, so a small click does not drag. 0 starts at once."},
    {"EdgeCollider2D.oneWayNormal", "Which way is the solid side of a one-way edge (a direction, not a distance)."},
};

// Explanations that replace a registry tooltip which names no unit or no range. Looked up first.
const TipRow kTipOverrides[] = {
    {"FollowTarget2D.stopDistance", "It stops this many metres from the target (0 = touch it)."},
    {"UIScrollView.contentHeight", "Total height of the scrollable content, in pixels."},
    {"UIScrollView.contentWidth", "Total width of the scrollable content, in pixels (sideways scrolling)."},
    {"Camera.focalLength", "Lens length in millimetres. Longer zooms in (physical camera only)."},
    {"Camera.sensorHeight", "Height of the camera sensor in millimetres. A taller sensor sees a wider view (physical camera only)."},
    {"Rigidbody2D.bounciness", "How much speed it keeps after a hit (0 to 1: 0 stops dead, 1 bounces back as fast)."},
    {"Rigidbody3D.bounciness", "How much speed it keeps after a hit (0 to 1: 0 stops dead, 1 bounces back as fast)."},
    {"Rigidbody2D.friction", "How much it grips what it touches (0 to 10: 0 is ice, 1 is a normal grip)."},
    {"Rigidbody3D.friction", "How much it grips what it touches (0 to 10: 0 is ice, 1 is a normal grip)."},
    {"Rigidbody2D.rollingResistance", "How quickly a rolling ball or wheel slows down (0 to 1). 0 rolls on, 0.03 stops a ball in about 15 seconds. Needs the Improved physics quality."},
    {"Rigidbody3D.rollingResistance", "How quickly a rolling ball or wheel slows down (0 to 1). 0 rolls on, 0.03 stops a ball in about 15 seconds. Needs the Improved physics quality."},
    {"VirtualCamera.followOffset", "Where the camera stands relative to what it follows, in metres on each axis."},
    {"VirtualCamera.lookAtOffset", "Aim above or beside the target, in metres (for example at the head)."},
    {"VirtualCamera.occlusionAlpha", "How see-through things in the way become (0 to 1: 0 invisible, 1 solid)."},
    {"Spawner.spawnRadius", "Spawns land within this many metres of the spawner."},
    {"Spawner.startDelay", "Seconds to wait before the first spawn."},
    {"Spawner.waveDelay", "Seconds to pause between waves (0 = just the Interval)."},
    {"TriggerZone.action", "What happens when something enters: set or add to a variable, turn the target on or off, win, lose or quit."},
    {"NavMeshAgent.radius", "Body radius used to steer around others, in metres."},
    {"NavMeshAgent.height", "Body height in metres (information only: the baked navigation mesh sets the real clearance)."},
    {"NavMeshAgent.stoppingDistance", "It counts as arrived when this close to the destination, in metres."},
    {"NavMeshAgent.avoidanceRange", "How far it looks for other agents and obstacles to steer around, in metres (0 is off)."},
    {"NavMeshAgent.baseOffset", "How far the object's origin is above its feet, in metres (a cube's centre is half its height)."},
    {"UIButton.borderWidth", "Thickness of the border, in pixels (0 is no border)."},
    {"UIImage.borderWidth", "Thickness of a frame drawn around the picture, in pixels (0 is no frame)."},
    {"Light.range", "How far a point, spot or area light reaches, in metres."},
    {"MeshRenderer.texOffset", "Slides the picture across the surface (in repeats of the picture: 1 is one full picture)."},
    {"MeshRenderer.uvScroll", "Makes the picture crawl across the surface, in repeats of the picture per second."},
    {"MeshRenderer.outlineWidth", "Thickness of the outline, in metres."},
    {"AudioSource.pitch", "Speed and pitch together (1 is normal, 2 is twice as fast and an octave higher)."},
    {"AudioSource.minDistance", "Within this many metres the sound is at full volume."},
    {"AudioSource.maxDistance", "Beyond this many metres the sound is silent."},
    {"AudioSource.occlusionVolume", "How loud it stays when a wall is in the way (0 to 1: 1 is no quieter)."},
    {"ParticleSystem.noiseScroll", "Turbulence: how fast the wobble pattern changes over time, in seconds of drift per second (0 freezes it)."},
    {"CameraFollow.offset", "Where the camera sits relative to the target, in metres on each axis."},
    {"UIPanel.outlineWidth", "Thickness of a ring drawn outside the panel edge, in pixels. 0 is off."},
};
}

std::string TooltipFor(const std::string& typeName, const PropertyDesc& d) {
    const std::string key = typeName + "." + d.name;
    for (const TipRow& r : kTipOverrides) if (key == r.key) return r.text;
    if (!d.tooltip.empty()) return d.tooltip;
    for (const TipRow& r : kTips) if (key == r.key) return r.text;
    return {};
}

std::string FieldTooltip(const std::string& typeName, const std::string& label) {
    auto info = PropertyRegistry::FindByName(typeName);
    if (!info) return {};
    std::string l = label.substr(0, label.find("##"));
    while (!l.empty() && l.back() == ' ') l.pop_back();
    l = Lower(l);
    for (const PropertyDesc& d : info->properties)
        if (Lower(PrettyName(d.name)) == l || Lower(d.name) == l) return TooltipFor(typeName, d);
    return {};
}

std::vector<std::string> MixedPropertyNames(const std::vector<GameObject*>& objs, const std::string& typeName, int index) {
    std::vector<std::string> out;
    auto info = PropertyRegistry::FindByName(typeName);
    if (!info || objs.size() < 2) return out;
    for (const PropertyDesc& d : info->properties) {
        if (d.IsHidden() || d.IsRuntimeOnly() || d.name == "enabled") continue;
        const FieldState st = ReadField(objs, typeName, index, d);
        if (st.have > 1 && st.mixed) out.push_back(PrettyName(d.name));
    }
    return out;
}

RootSlot CaptureRootSlot(Scene& scene, const GameObject& go) {
    RootSlot slot;
    if (!go.transform || go.transform->Parent()) return slot;
    const GameObject* before = nullptr;
    bool found = false;
    for (const auto& up : scene.Objects()) {
        if (!up || up->IsDestroyed() || !up->transform || up->transform->Parent()) continue;
        if (up.get() == &go) { found = true; if (before) { slot.valid = true; slot.neighbourGuid = before->Guid(); slot.neighbourIsBefore = true; } continue; }
        if (!found) before = up.get();
        else if (!slot.valid) { slot.valid = true; slot.neighbourGuid = up->Guid(); slot.neighbourIsBefore = false; }
    }
    return slot;
}

void RestoreRootSlot(Scene& scene, GameObject* go, const RootSlot& slot) {
    if (!go || !slot.valid || !go->transform || go->transform->Parent()) return;
    GameObject* n = scene.FindByGuid(slot.neighbourGuid);
    if (!n || n == go || n->IsDestroyed() || !n->transform || n->transform->Parent()) return;
    scene.ReorderSibling(go, n, /*after=*/slot.neighbourIsBefore);
}

// ---- Prefab overrides ----------------------------------------------------------------------------------------

std::string RecordNameOf(const Component& c) {
    auto info = PropertyRegistry::FindFor(c);
    return info ? info->recordName : std::string();
}

std::vector<PrefabChange> PrefabChangesOf(GameObject& root, std::string* error) {
    std::vector<PrefabChange> out;
    PrefabDiff d;
    if (root.prefabSource.empty() || !PrefabTools::Diff(root, d, error)) return out;
    std::unordered_set<const GameObject*> added;
    bool anyRemoved = false;
    for (const auto& o : d.objects) {
        if (o.kind == PrefabObjectDiff::Kind::Added && o.live) added.insert(o.live);
        else if (o.kind == PrefabObjectDiff::Kind::Removed) anyRemoved = true;
    }
    // The prefab's objects in file order, as they sit in the live instance.
    std::vector<GameObject*> live;
    std::function<void(GameObject*)> walk = [&](GameObject* g) {
        if (added.count(g)) return;
        live.push_back(g);
        for (Transform* c : g->transform->Children())
            if (c && c->gameObject) walk(c->gameObject);
    };
    walk(&root);
    for (const auto& r : d.records) {
        PrefabChange ch;
        ch.record = r.record;
        ch.occurrence = r.occurrence;
        ch.prefabObject = r.object;
        ch.added = r.kind == PrefabRecordDiff::Kind::Added;
        ch.removed = r.kind == PrefabRecordDiff::Kind::Removed;
        ch.description = r.Describe();
        ch.objectName = r.objectName;
        ch.fields = r.fields;
        ch.wholeRecordOnly = r.wholeRecordOnly;
        GameObject* g = (r.object >= 0 && r.object < (int)live.size()) ? live[(std::size_t)r.object] : nullptr;
        // Positions drift when objects were lost, so then trust the name. Without lost objects the position is exact, also for
        // an object (or the instance root) that was renamed.
        if (g && anyRemoved && r.object != 0 && g->name != r.objectName) {
            GameObject* byName = nullptr;
            for (GameObject* l : live) if (l->name == r.objectName) { byName = l; break; }
            g = byName;
        }
        ch.object = g;
        out.push_back(std::move(ch));
    }
    return out;
}

const PrefabChange* FindChange(const std::vector<PrefabChange>& changes, const GameObject& go, const Component& c) {
    const std::string rec = RecordNameOf(c);
    if (rec.empty()) return nullptr;
    // The n-th component of this record type on the object is the n-th occurrence of the record.
    int occurrence = 0;
    for (const auto& up : go.Components()) {
        if (up.get() == &c) break;
        if (up && RecordNameOf(*up) == rec) ++occurrence;
    }
    for (const PrefabChange& ch : changes)
        if (ch.object == &go && ch.record == rec && ch.occurrence == occurrence) return &ch;
    return nullptr;
}

bool IsChanged(const std::vector<PrefabChange>& changes, const GameObject& go, const Component& c) {
    return FindChange(changes, go, c) != nullptr;
}

} // namespace oked
