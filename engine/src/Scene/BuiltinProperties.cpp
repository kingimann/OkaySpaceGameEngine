// The property registrations of the built-in components. They are described here,
// from outside, through each component's public fields, so the component headers
// and the scene serializer stay as they are. See docs/properties.md for what is
// covered and what is not.
#include "BuiltinPropertiesShared.hpp"
#include "okay/Scene/Scene.hpp"
#include "okay/Scene/Transform.hpp"
#include "okay/Components/AudioSource.hpp"
#include "okay/Components/Camera.hpp"
#include "okay/Components/CameraFollow.hpp"
#include "okay/Components/Canvas.hpp"
#include "okay/Components/Lifetime.hpp"
#include "okay/Components/Light.hpp"
#include "okay/Components/MeshRenderer.hpp"
#include "okay/Components/Mover.hpp"
#include "okay/Components/ParticleSystem.hpp"
#include "okay/Components/Spinner.hpp"
#include "okay/Components/SpriteRenderer.hpp"
#include "okay/Components/UIPanel.hpp"
#include "okay/Physics/Collider2D.hpp"
#include "okay/Physics/Collider3D.hpp"
#include "okay/Physics/Rigidbody2D.hpp"
#include "okay/Physics/Rigidbody3D.hpp"

namespace okay {

namespace detail {
const std::vector<std::string>& AnchorNames() {
    static const std::vector<std::string> n = {"TopLeft", "TopCenter", "TopRight", "MiddleLeft", "Center", "MiddleRight",
                                               "BottomLeft", "BottomCenter", "BottomRight",
                                               "StretchHorizontal", "StretchVertical", "StretchFull"};
    return n;
}
const std::vector<std::string>& ShapeNames() {
    static const std::vector<std::string> n = {"Rectangle", "Rounded", "Circle", "Pill", "Triangle", "Diamond", "Hexagon", "Octagon",
                                               "Parallelogram", "Trapezoid", "Pentagon", "Squircle", "TabTop",
                                               "ArrowRight", "ArrowLeft", "ArrowUp", "ArrowDown"};
    return n;
}
} // namespace detail

namespace {

using detail::kBig;
using detail::AnchorNames;
using detail::ShapeNames;

// ---- GameObject (the object's own fields) ---------------------------------------

void RegisterGameObject() {
    PropertyBuilder<GameObject> b("GameObject", "", "Core", false);
    b.AddString("name", &GameObject::name).Tooltip("Name shown in the hierarchy")
     .Custom("active", PropertyType::Bool,
             [](const GameObject& g) { return PropertyValue::MakeBool(g.active); },
             [](GameObject& g, const PropertyValue& v) { g.active = v.AsBool(); return true; })
        .Tooltip("Inactive objects are skipped by Update and Render").Default(PropertyValue::MakeBool(true))
     .Custom("tag", PropertyType::String,
             [](const GameObject& g) { return PropertyValue::MakeString(g.tag); },
             [](GameObject& g, const PropertyValue& v) { return g.SetTag(v.AsString()); })
        .Tooltip("Tag for lookups (refused when the project only allows listed tags)").Default(PropertyValue::MakeString(""))
     .AddInt("layer", &GameObject::layer).Range(0, 31).Tooltip("Render and physics layer").Default(PropertyValue::MakeInt(0))
     .AddBool("isStatic", &GameObject::isStatic).Tooltip("Marked as not moving at runtime").Default(PropertyValue::MakeBool(false))
     .AddBool("editorLocked", &GameObject::editorLocked).Tooltip("Cannot be picked in the Scene view").Hidden().Default(PropertyValue::MakeBool(false))
     .AddInt("uiDrawOrder", &GameObject::uiDrawOrder).Range(-100000, 100000).Tooltip("UI layering override (0 = the widget's default)").Default(PropertyValue::MakeInt(0));
    b.Commit();
}

// ---- Transform -------------------------------------------------------------------------

void RegisterTransform() {
    PropertyBuilder<Transform> b("Transform", "transform", "Core", false);
    b.NoAdd();
    b.AddVec3("position", &Transform::localPosition).Tooltip("Position relative to the parent")
     .AddQuat("rotation", &Transform::localRotation).Tooltip("Rotation relative to the parent (a unit quaternion)")
     .AddVec3("scale", &Transform::localScale).Tooltip("Scale relative to the parent")
     .Custom("eulerAngles", PropertyType::Vec3,
             [](const Transform& t) { return PropertyValue::MakeVec3(t.localRotation.ToEuler()); },
             [](Transform& t, const PropertyValue& v) {
                 // Writing back the angles just read must not nudge the rotation by rounding.
                 if ((t.localRotation.ToEuler() - v.AsVec3()).Magnitude() < 1e-5f) return true;
                 t.localRotation = Quat::Euler(v.AsVec3());
                 return true;
             })
        .Tooltip("The local rotation as Euler angles in degrees (another view of rotation)").Derived()
     .Custom("worldPosition", PropertyType::Vec3,
             [](const Transform& t) { return PropertyValue::MakeVec3(t.Position()); },
             [](Transform& t, const PropertyValue& v) { t.SetPosition(v.AsVec3()); return true; })
        .Tooltip("Position in the world").Derived().Hidden()
     .Custom("parent", PropertyType::ObjectRef,
             [](const Transform& t) {
                 const Transform* p = t.Parent();
                 return PropertyValue::MakeObjectRef(p && p->gameObject ? p->gameObject->Guid() : std::string());
             },
             [](Transform& t, const PropertyValue& v) {
                 const std::string& guid = v.AsString();
                 if (guid.empty()) return t.SetParent(nullptr, false);
                 Scene* scene = t.gameObject ? t.gameObject->scene() : nullptr;
                 GameObject* p = scene ? scene->FindByGuid(guid) : nullptr;
                 if (!p || !p->transform) return false;
                 return t.SetParent(p->transform, false);   // keep the local pose: reparenting stays its own change
             })
        .Tooltip("The parent object, by id (empty = a root object). The local pose is kept.").Structural();
    // Placed after the chain so the 'Transform' object keeps its position at the front.
    b.Commit();
}

// ---- Camera ------------------------------------------------------------------------------

void RegisterCamera() {
    PropertyBuilder<Camera> b("Camera", "camera", "Rendering");
    b.Section("Projection")
     .AddEnum("projection", &Camera::projection, {"Orthographic", "Perspective"})
     .AddFloat("orthographicSize", &Camera::orthographicSize).Range(0.01, kBig, 0.1).Tooltip("Half the viewing height in world units (orthographic)")
     .AddFloat("fieldOfView", &Camera::fieldOfView).Range(1, 179, 0.5).Tooltip("Field of view in degrees (perspective)")
     .AddBool("fovAxisHorizontal", &Camera::fovAxisHorizontal).Tooltip("Measure the field of view along the horizontal axis")
     .AddFloat("nearClip", &Camera::nearClip).Range(0.001, kBig, 0.01)
     .AddFloat("farClip", &Camera::farClip).Range(0.01, 10000000.0, 1.0)
     .Section("Physical camera")
     .AddBool("physicalCamera", &Camera::physicalCamera).Tooltip("Derive the field of view from a lens and sensor size")
     .AddFloat("focalLength", &Camera::focalLength).Range(1, 2000, 1).Tooltip("Millimetres")
     .AddFloat("sensorHeight", &Camera::sensorHeight).Range(1, 200, 0.5).Tooltip("Millimetres")
     .Section("Output")
     .AddInt("cullingMask", &Camera::cullingMask).Tooltip("Bit n set = layer n is drawn")
     .AddFloat("rectX", &Camera::rectX).Range(0, 1, 0.01).Tooltip("Viewport left edge (0..1, from the bottom-left)")
     .AddFloat("rectY", &Camera::rectY).Range(0, 1, 0.01)
     .AddFloat("rectW", &Camera::rectW).Range(0, 1, 0.01)
     .AddFloat("rectH", &Camera::rectH).Range(0, 1, 0.01)
     .AddEnum("clearFlags", &Camera::clearFlags, {"Skybox", "SolidColor", "DepthOnly", "DontClear"})
     .AddColor("backgroundColor", &Camera::backgroundColor).Range(0, 1, 0.01)
     .AddFloat("depth", &Camera::depth).Range(-100000, 100000, 1).Tooltip("Render order: lower draws first")
     .AddBool("main", &Camera::main).Tooltip("Whether this camera registers as the scene's main camera")
     .Section("Framing")
     .AddFloat("zoom", &Camera::zoom).Range(0.05, 1000, 0.05)
     .AddFloat("dutch", &Camera::dutch).Range(-180, 180, 0.5).Tooltip("Roll around the view direction, degrees")
     .AddFloat("lensShiftX", &Camera::lensShiftX).Range(-1, 1, 0.01)
     .AddFloat("lensShiftY", &Camera::lensShiftY).Range(-1, 1, 0.01)
     .AddBool("pixelPerfect", &Camera::pixelPerfect).Tooltip("Snap to whole pixels (2D)")
     .AddFloat("pixelsPerUnit", &Camera::pixelsPerUnit).Range(1, 4096, 1)
     .Section("Shake")
     .AddFloat("shakeAmount", &Camera::shakeAmount).Range(0, 1000, 0.05)
     .AddFloat("shakeAngle", &Camera::shakeAngle).Range(0, 180, 0.1)
     .AddFloat("shakeFrequency", &Camera::shakeFrequency).Range(0, 1000, 0.5)
     .AddFloat("shakeRecovery", &Camera::shakeRecovery).Range(0, 1000, 0.1);
    b.Commit();
}

// ---- Light -----------------------------------------------------------------------------------

void RegisterLight() {
    PropertyBuilder<Light> b("Light", "light", "Rendering");
    b.AddEnum("type", &Light::type, {"Directional", "Point", "Spot", "Area"})
     .AddColor("color", &Light::color).Range(0, 16, 0.01)
     .AddFloat("intensity", &Light::intensity).Range(0, 1000, 0.05)
     .AddFloat("ambient", &Light::ambient).Range(0, 1, 0.01).Tooltip("Unlit floor, taken from the first light")
     .AddColor("ambientColor", &Light::ambientColor).Range(0, 16, 0.01).Tooltip("Tint of the ambient floor")
     .Section("Shape")
     .AddFloat("range", &Light::range).Range(0, kBig, 0.5).Tooltip("Reach of a point, spot or area light")
     .AddFloat("spotAngle", &Light::spotAngle).Range(1, 179, 0.5).Tooltip("Spot cone, full angle in degrees")
     .AddFloat("spotSoftness", &Light::spotSoftness).Range(0, 1, 0.01)
     .AddEnum("falloff", &Light::falloff, {"Linear", "InverseSquare"})
     .Section("Temperature")
     .AddBool("useTemperature", &Light::useTemperature).Tooltip("Drive the color from a Kelvin temperature")
     .AddFloat("temperature", &Light::temperature).Range(1000, 40000, 100).Tooltip("Kelvin")
     .Section("Shadows")
     .AddBool("castShadows", &Light::castShadows)
     .AddFloat("shadowStrength", &Light::shadowStrength).Range(0, 1, 0.01)
     .AddFloat("shadowBias", &Light::shadowBias).Range(0, 100, 0.05)
     .Section("Flicker")
     .AddFloat("flicker", &Light::flicker).Range(0, 1, 0.01).Tooltip("How far the intensity may dim")
     .AddFloat("flickerSpeed", &Light::flickerSpeed).Range(0, 1000, 0.5);
    b.Commit();
}

// ---- SpriteRenderer -----------------------------------------------------------------------

void RegisterSpriteRenderer() {
    PropertyBuilder<SpriteRenderer> b("SpriteRenderer", "sprite", "Rendering");
    b.AddVec2("size", &SpriteRenderer::size).Range(0, kBig, 0.1).Tooltip("Size in world units before the Transform scale")
     .AddColor("color", &SpriteRenderer::color).Range(0, 1, 0.01)
     .AddAssetPath("texture", &SpriteRenderer::texture, "image")
     .AddInt("glyph", &SpriteRenderer::glyph).Range(32, 126).Tooltip("Character the console renderer draws (ASCII code)")
     .Section("Texture region")
     .AddVec2("uvMin", &SpriteRenderer::uvMin).Range(-1000, 1000, 0.01)
     .AddVec2("uvMax", &SpriteRenderer::uvMax).Range(-1000, 1000, 0.01)
     .AddBool("flipX", &SpriteRenderer::flipX)
     .AddBool("flipY", &SpriteRenderer::flipY)
     .AddEnum("texFilter", &SpriteRenderer::texFilter, {"Smooth", "Pixel"})
     .Section("Sorting")
     .AddInt("sortingLayer", &SpriteRenderer::sortingLayer).Range(-1000, 1000).Tooltip("A coarse band that always wins over the order")
     .AddInt("sortOrder", &SpriteRenderer::sortOrder).Range(-1000000, 1000000).Tooltip("Draw order inside the sorting layer");
    b.Commit();
}

// ---- MeshRenderer ----------------------------------------------------------------------------
// The mesh geometry itself (primitive, edited vertices, modifier results) is not a
// property: setting it would silently throw away edits. The surface settings are.

void RegisterMeshRenderer() {
    PropertyBuilder<MeshRenderer> b("MeshRenderer", "mesh", "Rendering");
    b.AddBool("meshEnabled", &MeshRenderer::enabled).Tooltip("When off the mesh is not drawn (the renderer's own flag, kept as the mesh record)")
     .AddColor("color", &MeshRenderer::color).Range(0, 1, 0.01).Tooltip("Base (albedo) color")
     .AddAssetPath("meshPath", &MeshRenderer::meshPath, "model").Tooltip("A model file that replaces the mesh when the scene loads")
     .AddBool("wireframe", &MeshRenderer::wireframe)
     .AddBool("doubleSided", &MeshRenderer::doubleSided)
     .Section("Shading")
     .AddEnum("shader", &MeshRenderer::shader, {"Standard", "Unlit", "Toon", "Gradient", "Fresnel", "Iridescent", "Hologram", "Posterize", "Velvet"})
     .AddInt("toonBands", &MeshRenderer::toonBands).Range(1, 16).Tooltip("Cel bands for the Toon shader")
     .AddColor("gradientTop", &MeshRenderer::gradientTop).Range(0, 1, 0.01)
     .AddColor("gradientBottom", &MeshRenderer::gradientBottom).Range(0, 1, 0.01)
     .AddBool("unlit", &MeshRenderer::unlit).Tooltip("Draw the flat base color without lighting")
     .Section("Material")
     .AddColor("emissive", &MeshRenderer::emissive).Range(0, 16, 0.01).Tooltip("Self-illumination")
     .AddFloat("specular", &MeshRenderer::specular).Range(0, 1, 0.01)
     .AddFloat("shininess", &MeshRenderer::shininess).Range(1, 1024, 1)
     .AddFloat("metallic", &MeshRenderer::metallic).Range(0, 1, 0.01)
     .AddFloat("reflectivity", &MeshRenderer::reflectivity).Range(0, 1, 0.01)
     .Section("Textures")
     .AddAssetPath("texture", &MeshRenderer::texture, "image")
     .AddVec2("tiling", &MeshRenderer::tiling).Range(-1000, 1000, 0.1)
     .AddVec2("texOffset", &MeshRenderer::texOffset).Range(-1000, 1000, 0.01)
     .AddEnum("texFilter", &MeshRenderer::texFilter, {"Smooth", "Pixel"})
     .AddAssetPath("normalMap", &MeshRenderer::normalMap, "image")
     .AddFloat("normalStrength", &MeshRenderer::normalStrength).Range(0, 8, 0.05)
     .AddAssetPath("specularMap", &MeshRenderer::specularMap, "image")
     .AddAssetPath("aoMap", &MeshRenderer::aoMap, "image")
     .AddFloat("aoStrength", &MeshRenderer::aoStrength).Range(0, 1, 0.01)
     .AddVec2("uvScroll", &MeshRenderer::uvScroll).Range(-100, 100, 0.01).Tooltip("Texture offset change per second")
     .AddBool("triplanar", &MeshRenderer::triplanar)
     .Section("Rim")
     .AddFloat("rimStrength", &MeshRenderer::rimStrength).Range(0, 10, 0.05)
     .AddFloat("rimPower", &MeshRenderer::rimPower).Range(0.1, 32, 0.1)
     .AddColor("rimColor", &MeshRenderer::rimColor).Range(0, 1, 0.01)
     .Section("Outline")
     .AddBool("outline", &MeshRenderer::outline)
     .AddColor("outlineColor", &MeshRenderer::outlineColor).Range(0, 1, 0.01)
     .AddFloat("outlineWidth", &MeshRenderer::outlineWidth).Range(0, 1, 0.005)
     .Section("Shadows")
     .AddBool("castShadows", &MeshRenderer::castShadows)
     .AddBool("receiveShadows", &MeshRenderer::receiveShadows)
     .AddBool("groundShadow", &MeshRenderer::groundShadow).Tooltip("A flat blob shadow while real shadow maps are off")
     .AddFloat("groundShadowY", &MeshRenderer::groundShadowY).Range(-kBig, kBig, 0.1)
     .AddFloat("groundShadowStrength", &MeshRenderer::groundShadowStrength).Range(0, 1, 0.01);
    b.Commit();
}

// ---- Rigidbody2D / Rigidbody3D -------------------------------------------------------------------

void RegisterRigidbody2D() {
    PropertyBuilder<Rigidbody2D> b("Rigidbody2D", "rigidbody2d", "Physics 2D");
    b.AddEnum("bodyType", &Rigidbody2D::bodyType, {"Dynamic", "Kinematic", "Static"})
     .AddFloat("mass", &Rigidbody2D::mass).Range(0.0001, 1000000, 0.1)
     .AddFloat("gravityScale", &Rigidbody2D::gravityScale).Range(-100, 100, 0.1)
     .AddFloat("drag", &Rigidbody2D::drag).Range(0, 1000, 0.01).Tooltip("Linear damping per second")
     .AddFloat("angularDrag", &Rigidbody2D::angularDrag).Range(0, 1000, 0.01)
     .AddFloat("bounciness", &Rigidbody2D::bounciness).Range(0, 1, 0.01)
     .AddFloat("friction", &Rigidbody2D::friction).Range(0, 10, 0.01)
     .AddBool("freezeRotation", &Rigidbody2D::freezeRotation)
     .AddBool("allowSleep", &Rigidbody2D::allowSleep)
     .AddBool("continuousCollision", &Rigidbody2D::continuousCollision)
     .Section("Runtime")
     .AddVec2("velocity", &Rigidbody2D::velocity).RuntimeOnly().Tooltip("Current velocity (live state, not saved)")
     .AddFloat("angularVelocity", &Rigidbody2D::angularVelocity).RuntimeOnly().Tooltip("Degrees per second (live state, not saved)")
     .AddBool("sleeping", &Rigidbody2D::sleeping).ReadOnly().RuntimeOnly();
    b.Commit();
}

void RegisterRigidbody3D() {
    PropertyBuilder<Rigidbody3D> b("Rigidbody3D", "rigidbody3d", "Physics 3D");
    b.AddEnum("bodyType", &Rigidbody3D::bodyType, {"Dynamic", "Kinematic", "Static"})
     .AddFloat("mass", &Rigidbody3D::mass).Range(0.0001, 1000000, 0.1)
     .AddFloat("gravityScale", &Rigidbody3D::gravityScale).Range(-100, 100, 0.1)
     .AddFloat("drag", &Rigidbody3D::drag).Range(0, 1000, 0.01).Tooltip("Linear damping per second")
     .AddFloat("angularDrag", &Rigidbody3D::angularDrag).Range(0, 1000, 0.01)
     .AddFloat("bounciness", &Rigidbody3D::bounciness).Range(0, 1, 0.01)
     .AddFloat("friction", &Rigidbody3D::friction).Range(0, 10, 0.01)
     .AddFloat("maxFallSpeed", &Rigidbody3D::maxFallSpeed).Range(0, kBig, 0.5).Tooltip("Terminal fall speed (0 = no limit)")
     .AddBool("freezeX", &Rigidbody3D::freezeX)
     .AddBool("freezeY", &Rigidbody3D::freezeY)
     .AddBool("freezeZ", &Rigidbody3D::freezeZ)
     .AddBool("freezeRotation", &Rigidbody3D::freezeRotation)
     .AddBool("allowSleep", &Rigidbody3D::allowSleep)
     .AddBool("continuousCollision", &Rigidbody3D::continuousCollision)
     .Section("Runtime")
     .AddVec3("velocity", &Rigidbody3D::velocity).RuntimeOnly().Tooltip("Current velocity (live state, not saved)")
     .AddVec3("angularVelocity", &Rigidbody3D::angularVelocity).RuntimeOnly().Tooltip("Radians per second (live state, not saved)")
     .AddBool("sleeping", &Rigidbody3D::sleeping).ReadOnly().RuntimeOnly()
     .AddBool("groundedOnTerrain", &Rigidbody3D::groundedOnTerrain).ReadOnly().RuntimeOnly();
    b.Commit();
}

// ---- Colliders ----------------------------------------------------------------------------------------

// `autoFit` only means something for the box, circle and capsule (the scene file
// keeps it for those three), so edge and polygon colliders leave it out.
template <class C>
void Collider2DCommon(PropertyBuilder<C>& b, bool withAutoFit = true) {
    b.Section("Collider")
     .AddVec2("offset", &Collider2D::offset).Tooltip("Offset from the Transform, in local units")
     .AddBool("isTrigger", &Collider2D::isTrigger).Tooltip("Detects overlaps without pushing objects apart")
     .AddInt("layer", &Collider2D::layer).Range(0, 31).Tooltip("Collision layer");
    if (withAutoFit) b.AddBool("autoFit", &Collider2D::autoFit).Tooltip("Keep the collider matched to the sprite bounds");
    b.Section("Trigger filter")
     .AddString("triggerTag", &Collider2D::triggerTag).Tooltip("A trigger reacts only to objects with this tag (empty = any)")
     .AddInt("triggerLayers", &Collider2D::triggerLayers).Tooltip("Bit n set = layer n sets the trigger off");
}

template <class C>
void Collider3DCommon(PropertyBuilder<C>& b) {
    b.Section("Collider")
     .AddVec3("offset", &Collider3D::offset).Tooltip("Offset from the Transform, in local units")
     .AddBool("isTrigger", &Collider3D::isTrigger).Tooltip("Detects overlaps without pushing objects apart")
     .AddInt("layer", &Collider3D::layer).Range(0, 31).Tooltip("Collision layer")
     .AddBool("autoFit", &Collider3D::autoFit).Tooltip("Keep the collider matched to the mesh bounds")
     .Section("Trigger filter")
     .AddString("triggerTag", &Collider3D::triggerTag).Tooltip("A trigger reacts only to objects with this tag (empty = any)")
     .AddInt("triggerLayers", &Collider3D::triggerLayers).Tooltip("Bit n set = layer n sets the trigger off");
}

void RegisterColliders2D() {
    {
        PropertyBuilder<BoxCollider2D> b("BoxCollider2D", "boxcollider2d", "Physics 2D");
        b.AddVec2("size", &BoxCollider2D::size).Range(0, kBig, 0.1);
        Collider2DCommon(b);
        b.Commit();
    }
    {
        PropertyBuilder<CircleCollider2D> b("CircleCollider2D", "circlecollider2d", "Physics 2D");
        b.AddFloat("radius", &CircleCollider2D::radius).Range(0, kBig, 0.05);
        Collider2DCommon(b);
        b.Commit();
    }
    {
        PropertyBuilder<CapsuleCollider2D> b("CapsuleCollider2D", "capsulecollider2d", "Physics 2D");
        b.AddVec2("size", &CapsuleCollider2D::size).Range(0, kBig, 0.1)
         .AddEnum("direction", &CapsuleCollider2D::direction, {"Vertical", "Horizontal"});
        Collider2DCommon(b);
        b.Commit();
    }
    // Edge and polygon point lists are not properties (they are edited in the Scene view).
    {
        PropertyBuilder<EdgeCollider2D> b("EdgeCollider2D", "edgecollider2d", "Physics 2D");
        b.AddBool("oneWay", &EdgeCollider2D::oneWay).Tooltip("Blocks only bodies approaching from the normal's side")
         .AddVec2("oneWayNormal", &EdgeCollider2D::oneWayNormal);
        Collider2DCommon(b, false);
        b.Commit();
    }
    {
        PropertyBuilder<PolygonCollider2D> b("PolygonCollider2D", "polygoncollider2d", "Physics 2D");
        Collider2DCommon(b, false);
        b.Commit();
    }
}

void RegisterColliders3D() {
    {
        PropertyBuilder<BoxCollider3D> b("BoxCollider3D", "boxcollider3d", "Physics 3D");
        b.AddVec3("size", &BoxCollider3D::size).Range(0, kBig, 0.1);
        Collider3DCommon(b);
        b.Commit();
    }
    {
        PropertyBuilder<SphereCollider3D> b("SphereCollider3D", "spherecollider3d", "Physics 3D");
        b.AddFloat("radius", &SphereCollider3D::radius).Range(0, kBig, 0.05);
        Collider3DCommon(b);
        b.Commit();
    }
    {
        PropertyBuilder<CapsuleCollider3D> b("CapsuleCollider3D", "capsulecollider3d", "Physics 3D");
        b.AddFloat("radius", &CapsuleCollider3D::radius).Range(0, kBig, 0.05)
         .AddFloat("height", &CapsuleCollider3D::height).Range(0, kBig, 0.1).Tooltip("Tip to tip")
         .AddEnum("axis", &CapsuleCollider3D::axis, {"X", "Y", "Z"}).Tooltip("The long axis");
        Collider3DCommon(b);
        b.Commit();
    }
    {
        PropertyBuilder<CylinderCollider3D> b("CylinderCollider3D", "cylindercollider3d", "Physics 3D");
        b.AddFloat("radius", &CylinderCollider3D::radius).Range(0, kBig, 0.05)
         .AddFloat("height", &CylinderCollider3D::height).Range(0, kBig, 0.1)
         .AddEnum("axis", &CylinderCollider3D::axis, {"X", "Y", "Z"}).Tooltip("The long axis");
        Collider3DCommon(b);
        b.Commit();
    }
    {
        PropertyBuilder<MeshCollider3D> b("MeshCollider3D", "meshcollider3d", "Physics 3D");
        b.AddVec3("size", &MeshCollider3D::size).Range(0, kBig, 0.1)
         .Tooltip("Fallback box, used only when the object has no mesh with triangles");
        Collider3DCommon(b);
        b.Commit();
    }
}

// ---- AudioSource ---------------------------------------------------------------------------------------------
// The loaded clip data (`clip`) is not a property; the clip file path is.

void RegisterAudioSource() {
    PropertyBuilder<AudioSource> b("AudioSource", "audio", "Audio");
    b.AddAssetPath("clipPath", &AudioSource::clipPath, "audio").Tooltip("Audio file to play (WAV, OGG, MP3 or FLAC)")
     .AddFloat("volume", &AudioSource::volume).Range(0, 1, 0.01)
     .AddFloat("pitch", &AudioSource::pitch).Range(0.01, 8, 0.01).Tooltip("Speed and pitch together")
     .AddFloat("pan", &AudioSource::pan).Range(-1, 1, 0.01).Tooltip("-1 left, 0 centre, 1 right")
     .AddEnum("group", &AudioSource::group, {"SFX", "Music", "UI", "Voice"}).Tooltip("Mixer group")
     .AddBool("loop", &AudioSource::loop)
     .AddBool("playOnAwake", &AudioSource::playOnAwake)
     .AddBool("stream", &AudioSource::stream).Tooltip("Play the file in small pieces as it plays instead of loading it whole: for long music")
     .AddFloat("lowPass", &AudioSource::lowPass).Range(10, AudioSource::kLowPassOff, 10).Tooltip("Cutoff in Hz; the top of the range leaves the sound untouched")
     .Section("3D sound")
     .AddBool("spatial", &AudioSource::spatial)
     .AddFloat("minDistance", &AudioSource::minDistance).Range(0, kBig, 0.5)
     .AddFloat("maxDistance", &AudioSource::maxDistance).Range(0, kBig, 0.5)
     .AddEnum("rolloff", &AudioSource::rolloff, {"Linear", "Inverse", "Logarithmic"})
     .AddBool("doppler", &AudioSource::doppler).Tooltip("Raise the pitch when this sound or the listener is moving closer, and lower it when moving apart")
     .AddFloat("dopplerScale", &AudioSource::dopplerScale).Range(0, 5, 0.05).Tooltip("1 matches real speed. 0 is no shift. Higher exaggerates it")
     .AddBool("occlude", &AudioSource::occlude).Tooltip("A wall between this and the listener makes the sound quieter and muffled")
     .AddFloat("occlusionVolume", &AudioSource::occlusionVolume).Range(0, 1, 0.01).Tooltip("How loud it stays when a wall is in the way. 1 is no quieter")
     .AddFloat("occlusionLowPass", &AudioSource::occlusionLowPass).Range(10, AudioSource::kLowPassOff, 10).Tooltip("How muffled it is behind a wall, in Hz. Lower is duller")
     .AddBool("distanceLowPass", &AudioSource::distanceLowPass).Tooltip("Far sounds get muffled. Off keeps the tone the same at every distance")
     .AddFloat("distanceLowPassHz", &AudioSource::distanceLowPassHz).Range(10, AudioSource::kLowPassOff, 10).Tooltip("Cutoff at the far edge, in Hz. The muffling follows a curve that stays bright up close");
    b.Commit();
}

// ---- ParticleSystem -----------------------------------------------------------------------------------------------
// The color gradient and the size and speed curves are not properties (they are
// lists of keys); everything else the scene file keeps is.

void RegisterParticleSystem() {
    using PS = ParticleSystem;
    PropertyBuilder<PS> b("ParticleSystem", "particles", "Rendering");
    b.Section("Emission")
     .AddBool("playing", &PS::playing)
     .AddFloat("emissionRate", &PS::emissionRate).Range(0, kBig, 1).Tooltip("Particles per second")
     .AddInt("maxParticles", &PS::maxParticles).Range(0, 100000)
     .AddFloat("duration", &PS::duration).Range(0, kBig, 0.1).Tooltip("Seconds of emission per cycle (0 = forever)")
     .AddBool("loop", &PS::loop)
     .AddInt("burstCount", &PS::burstCount).Range(0, 100000).Tooltip("Particles in the one-shot burst (0 = none)")
     .AddFloat("burstTime", &PS::burstTime).Range(0, kBig, 0.1)
     .AddInt("seed", &PS::seed).Tooltip("Random seed: the same seed gives the same effect")
     .Section("Shape")
     .AddEnum("shape", &PS::shape, {"Point", "Circle", "Sphere", "Box", "Cone", "Edge"})
     .AddFloat("shapeRadius", &PS::shapeRadius).Range(0, kBig, 0.05)
     .AddFloat("shapeAngle", &PS::shapeAngle).Range(0, 180, 0.5).Tooltip("Cone half-angle in degrees")
     .AddVec3("boxSize", &PS::boxSize).Range(0, kBig, 0.1)
     .Section("Start values")
     .AddFloat("startLifetime", &PS::startLifetime).Range(0, 10000, 0.05)
     .AddFloat("startLifetimeRandom", &PS::startLifetimeRandom).Range(0, 10000, 0.05)
     .AddFloat("startSize", &PS::startSize).Range(0, 10000, 0.01)
     .AddFloat("startSizeRandom", &PS::startSizeRandom).Range(0, 10000, 0.01)
     .AddColor("startColor", &PS::startColor).Range(0, 1, 0.01)
     .AddVec3("startVelocity", &PS::startVelocity).Range(-kBig, kBig, 0.1)
     .AddFloat("velocityRandom", &PS::velocityRandom).Range(0, kBig, 0.1)
     .AddFloat("speedRandom", &PS::speedRandom).Range(0, kBig, 0.05)
     .Section("Appearance")
     .AddAssetPath("texture", &PS::texture, "image")
     .AddEnum("renderMode", &PS::renderMode, {"Billboard", "Stretch"})
     .AddFloat("stretchScale", &PS::stretchScale).Range(0, 1000, 0.01)
     .AddFloat("startRotation", &PS::startRotation).Range(-3600, 3600, 1)
     .AddFloat("startRotationRandom", &PS::startRotationRandom).Range(0, 3600, 1)
     .AddFloat("rotationSpeed", &PS::rotationSpeed).Range(-36000, 36000, 1)
     .AddFloat("rotationSpeedRandom", &PS::rotationSpeedRandom).Range(0, 36000, 1)
     .Section("Over lifetime")
     .AddColor("endColor", &PS::endColor).Range(0, 1, 0.01)
     .AddBool("colorOverLife", &PS::colorOverLife)
     .AddFloat("endSize", &PS::endSize).Range(0, 10000, 0.01)
     .AddBool("sizeOverLife", &PS::sizeOverLife)
     .AddBool("fadeOverLife", &PS::fadeOverLife)
     .AddBool("speedOverLife", &PS::speedOverLife)
     .Section("Forces")
     .AddVec3("gravity", &PS::gravity).Range(-kBig, kBig, 0.1)
     .AddFloat("gravityModifier", &PS::gravityModifier).Range(-1000, 1000, 0.05)
     .AddFloat("damping", &PS::damping).Range(0, 1000, 0.01)
     .AddFloat("windInfluence", &PS::windInfluence).Range(0, 100, 0.05)
     .Section("Motion")
     .AddEnum("simulationSpace", &PS::simulationSpace, {"World", "Local"})
     .AddFloat("inheritVelocity", &PS::inheritVelocity).Range(0, 100, 0.05)
     .AddFloat("noiseStrength", &PS::noiseStrength).Range(0, 10000, 0.05)
     .AddFloat("noiseFrequency", &PS::noiseFrequency).Range(0, 10000, 0.05)
     .AddFloat("noiseScroll", &PS::noiseScroll).Range(-10000, 10000, 0.05)
     .AddFloat("orbitSpeed", &PS::orbitSpeed).Range(-36000, 36000, 1)
     .AddVec3("orbitAxis", &PS::orbitAxis).Range(-1, 1, 0.01)
     .AddFloat("vortexStrength", &PS::vortexStrength).Range(-10000, 10000, 0.05)
     .AddFloat("vortexPull", &PS::vortexPull).Range(-10000, 10000, 0.05)
     .Section("Ground collision")
     .AddBool("collision", &PS::collision)
     .AddFloat("collisionY", &PS::collisionY).Range(-kBig, kBig, 0.1)
     .AddFloat("bounce", &PS::bounce).Range(0, 1, 0.01)
     .AddFloat("collisionFriction", &PS::collisionFriction).Range(0, 1, 0.01)
     .AddFloat("collisionLifeLoss", &PS::collisionLifeLoss).Range(0, 1, 0.01);
    b.Commit();
}

// ---- Small gameplay helpers ---------------------------------------------------------------------------

void RegisterSimpleBehaviours() {
    {
        PropertyBuilder<Mover> b("Mover", "mover", "Gameplay");
        b.AddVec3("velocity", &Mover::velocity).Range(-kBig, kBig, 0.1).Tooltip("Units per second, read in the chosen space")
         .AddEnum("space", &Mover::space, {"Local", "World", "Self"}).Tooltip("Local: the parent's frame. World: scene axes. Self: the object's own axes");
        b.Commit();
    }
    {
        PropertyBuilder<Spinner> b("Spinner", "spinner", "Gameplay");
        b.AddVec3("angularVelocity", &Spinner::angularVelocity).Range(-36000, 36000, 1).Tooltip("Degrees per second: the vector is the axis of spin, its length the speed")
         .AddEnum("space", &Spinner::space, {"Local", "World", "Self"}).Tooltip("Self: the object's own axes. Local: the parent's. World: the scene's");
        b.Commit();
    }
    {
        PropertyBuilder<Lifetime> b("Lifetime", "lifetime", "Gameplay");
        b.AddFloat("seconds", &Lifetime::seconds).Range(0, 1000000, 0.1).Tooltip("Seconds until the object is destroyed");
        b.Commit();
    }
    {
        PropertyBuilder<CameraFollow> b("CameraFollow", "camerafollow", "Camera");
        b.AddString("targetName", &CameraFollow::targetName).Tooltip("Name of the object to follow")
         .AddVec3("offset", &CameraFollow::offset).Range(-kBig, kBig, 0.1).Tooltip("Position relative to the target")
         .AddFloat("smoothing", &CameraFollow::smoothing).Range(0, 1000, 0.1).Tooltip("Higher is snappier; 0 snaps at once");
        b.Commit();
    }
}

// ---- UI: Canvas and UIPanel ----------------------------------------------------------------------------------
// The other widgets (button, text, image, slider ...) are in BuiltinPropertiesUI.cpp.

void RegisterCanvas() {
    PropertyBuilder<Canvas> b("Canvas", "canvas", "UI");
    b.AddEnum("scaleMode", &Canvas::scaleMode, {"ConstantPixelSize", "ScaleWithScreenSize"})
     .AddVec2("referenceResolution", &Canvas::referenceResolution).Range(1, 100000, 1).Tooltip("The resolution the layout was designed at")
     .AddEnum("screenMatch", &Canvas::screenMatch, {"MatchWidthOrHeight", "Expand", "Shrink"})
     .AddFloat("matchWidthOrHeight", &Canvas::matchWidthOrHeight).Range(0, 1, 0.01).Tooltip("0 follows the width, 1 the height")
     .AddFloat("scaleFactor", &Canvas::scaleFactor).Range(0.01, 100, 0.05).Tooltip("An extra UI zoom")
     .AddInt("sortOrder", &Canvas::sortOrder).Range(-100000, 100000).Tooltip("Higher draws on top")
     .AddBool("visible", &Canvas::visible)
     .AddFloat("opacity", &Canvas::opacity).Range(0, 1, 0.01)
     .Section("World space")
     .AddBool("worldSpace", &Canvas::worldSpace).Tooltip("Lives on a plane in the 3D world instead of the screen")
     .AddVec2("designResolution", &Canvas::designResolution).Range(1, 100000, 1)
     .AddFloat("worldPixelsPerUnit", &Canvas::worldPixelsPerUnit).Range(0.01, 100000, 1)
     .AddBool("billboard", &Canvas::billboard).Tooltip("Always face the camera");
    b.Commit();
}

void RegisterUIPanel() {
    PropertyBuilder<UIPanel> b("UIPanel", "uipanel", "UI");
    b.AddVec2("position", &UIPanel::position).Range(-kBig, kBig, 1).Tooltip("Pixels from the anchor (margins for a stretch anchor)")
     .AddVec2("size", &UIPanel::size).Range(-kBig, kBig, 1)
     .AddEnum("anchor", &UIPanel::anchor, AnchorNames())
     .AddColor("color", &UIPanel::color).Range(0, 1, 0.01)
     .AddEnum("shape", &UIPanel::shape, ShapeNames())
     .AddFloat("cornerRadius", &UIPanel::cornerRadius).Range(0, 10000, 1)
     .AddInt("cornerMask", &UIPanel::cornerMask).Range(0, 15).Tooltip("Bits for the corners that are rounded: 1 top-left, 2 top-right, 4 bottom-right, 8 bottom-left")
     .Section("Border")
     .AddFloat("borderWidth", &UIPanel::borderWidth).Range(0, 1000, 0.5)
     .AddColor("borderColor", &UIPanel::borderColor).Range(0, 1, 0.01)
     .Section("Gradient")
     .AddBool("useGradient", &UIPanel::useGradient)
     .AddEnum("gradientDir", &UIPanel::gradientDir, {"Vertical", "Horizontal", "DiagonalDown", "DiagonalUp"})
     .AddColor("colorBottom", &UIPanel::colorBottom).Range(0, 1, 0.01)
     .Section("Outline")
     .AddFloat("outlineWidth", &UIPanel::outlineWidth).Range(0, 1000, 0.5).Tooltip("A ring drawn outside the panel edge")
     .AddColor("outlineColor", &UIPanel::outlineColor).Range(0, 1, 0.01)
     .Section("Highlight")
     .AddBool("topHighlight", &UIPanel::topHighlight).Tooltip("A light band along the inside top edge")
     .AddColor("highlightColor", &UIPanel::highlightColor).Range(0, 1, 0.01)
     .Section("Shadow")
     .AddBool("shadow", &UIPanel::shadow)
     .AddColor("shadowColor", &UIPanel::shadowColor).Range(0, 1, 0.01)
     .AddVec2("shadowOffset", &UIPanel::shadowOffset).Range(-10000, 10000, 1)
     .AddFloat("shadowSoftness", &UIPanel::shadowSoftness).Range(0, 1000, 0.5)
     .Section("Input")
     .AddBool("blocksPointer", &UIPanel::blocksPointer).Tooltip("Widgets drawn under this panel cannot be hovered or clicked through it");
    b.Commit();
}

} // namespace

namespace detail {
void RegisterBuiltinPropertyTable() {
    RegisterGameObject();
    RegisterTransform();
    RegisterCamera();
    RegisterLight();
    RegisterSpriteRenderer();
    RegisterMeshRenderer();
    RegisterRigidbody2D();
    RegisterRigidbody3D();
    RegisterColliders2D();
    RegisterColliders3D();
    RegisterAudioSource();
    RegisterParticleSystem();
    RegisterSimpleBehaviours();
    RegisterCanvas();
    RegisterUIPanel();
    RegisterGameplayProperties();
    RegisterUIProperties();
}
} // namespace detail

} // namespace okay
