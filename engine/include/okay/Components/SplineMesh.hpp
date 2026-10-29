#pragma once
// Geometry and objects along a Spline. SplineMesh writes a mesh into its
// object's MeshRenderer: a 2D profile swept along the curve (roads, strips,
// pipes, curbs) or a source mesh repeated and bent along it (fences, pipes,
// rails). SplineInstancer places copies of an object, a prefab or a built-in
// mesh at a spacing (trees, lamp posts, coins). Both rebuild when the spline or
// their settings change; only their settings are saved, never the result.
#include "okay/Scene/Component.hpp"
#include "okay/Render/Mesh.hpp"
#include "okay/Math/Mat4.hpp"
#include "okay/Math/Vec2.hpp"
#include "okay/Math/Vec3.hpp"
#include <cstdint>
#include <string>
#include <unordered_set>
#include <vector>

namespace okay {

class GameObject;
class Spline;

class SplineMesh : public Behaviour {
public:
    enum class Mode : int { Extrude = 0, Repeat = 1 };
    /// Extrude cross-sections. Road: a flat top `width` wide (2 cm above the curve,
    /// so it never flickers with the ground) with sides `height` deep. Strip: a flat ribbon. Tube: a round pipe `width` across. Rail: a solid
    /// bar `width` x `height` standing on the curve (curbs, rails). Custom: your
    /// own points (x across, y up), listed left to right over the top.
    enum class Profile : int { Road = 0, Strip = 1, Tube = 2, Rail = 3, Custom = 4 };
    /// The source mesh's axis that runs along the curve in Repeat mode.
    enum class Axis : int { X = 0, Z = 2 };

    std::string spline;              ///< the object holding the Spline (empty = this object)
    Mode  mode = Mode::Extrude;
    // ---- Extrude ----
    Profile profile = Profile::Road;
    float width = 4.0f;
    float height = 0.25f;
    int   sides = 12;                ///< tube roundness
    std::vector<Vec2> customProfile;
    bool  closedProfile = false;     ///< Custom: join the last point back to the first
    float segmentLength = 0.5f;      ///< spacing of the cross-sections along the curve
    float uvTileLength = 4.0f;       ///< the texture repeats every this many units along the length
    // ---- Repeat ----
    std::string sourceMesh = "Fence";   ///< built-in mesh ("Fence", "Cube"...) or a .obj / .okaymesh file
    Axis  meshAxis = Axis::X;
    float spacing = 0.0f;            ///< gap between copies
    Vec3  meshScale{1.0f, 1.0f, 1.0f};
    bool  stretchToFit = true;       ///< stretch the copies a little so they fill the whole length
    // ---- Both ----
    bool  generateCollider = false;  ///< keep a Mesh Collider on the object (solid to walk on)

    void Awake() override { Rebuild(); }
    void Update(float) override { RebuildIfChanged(); }

    /// Rebuild when the spline, the settings or the placement changed. True if it did.
    bool RebuildIfChanged();
    /// Build into the object's MeshRenderer (added when missing). False when there
    /// is nothing to build (no spline, fewer than two points, no source mesh).
    bool Rebuild();
    /// The geometry for `sp`, in this object's local space, without touching the object.
    bool Build(const Spline& sp, Mesh& out) const;
    /// The Extrude cross-section: points (x across, y up), their texture u, and
    /// whether the last joins back to the first.
    void ProfilePoints(std::vector<Vec2>& pts, std::vector<float>& u, bool& loop) const;
    /// Copies placed by the last Repeat build.
    int Copies() const { return m_copies; }

private:
    std::uint64_t m_signature = 0;
    bool m_built = false;
    mutable int m_copies = 0;
    mutable Mesh m_source;
    mutable std::string m_sourceName;
    mutable bool m_sourceOk = false;
    const Mesh* Source() const;
    Mat4 SplineToLocal(const Spline& sp) const;
    std::uint64_t Signature(const Spline* sp) const;
};

class SplineInstancer : public Behaviour {
public:
    /// Upright: items stand along the object's up axis and face along the curve.
    /// Curve: they also tilt and roll with the curve's frame.
    enum class Align : int { Upright = 0, Curve = 1 };

    std::string spline;              ///< the object holding the Spline (empty = this object)
    std::string templateName;        ///< a scene object to copy (wins over the others)
    std::string prefabPath;          ///< or a .okayprefab file
    std::string meshName = "Tree";   ///< or a built-in mesh
    float spacing = 4.0f;            ///< distance between items along the curve
    float startOffset = 0.0f;        ///< the first item is this far along
    float sideOffset = 0.0f;         ///< across the curve along its `right` (a rider's +X); negative = the other side
    bool  bothSides = false;         ///< mirror every item to the other side
    float jitter = 0.0f;             ///< random offset along and across the curve (units)
    float rotationJitter = 0.0f;     ///< random turn about the up axis (degrees)
    float scaleMin = 1.0f, scaleMax = 1.0f;   ///< random uniform scale range
    std::uint32_t seed = 1;          ///< the same seed places the same items
    Align align = Align::Upright;
    Vec3  rotationOffset{0, 0, 0};   ///< extra turn of every item (degrees)
    int   maxCount = 500;

    void Awake() override { Rebuild(); }
    void Update(float) override { RebuildIfChanged(); }
    void OnDestroy() override { Clear(); }
    void OnObjectsDestroyed(const std::unordered_set<const GameObject*>& dying) override;

    bool RebuildIfChanged();
    /// Remove the old items and place new ones (children of this object, never
    /// saved: they are placed again on load). Returns how many.
    int Rebuild();
    void Clear();
    const std::vector<GameObject*>& Instances() const { return m_items; }
    int Count() const { return (int)m_items.size(); }

    /// Where the items go (relative to this object), in order.
    struct Placement { Vec3 position; Quat rotation; float scale = 1.0f; };
    std::vector<Placement> Placements(const Spline& sp) const;

private:
    std::vector<GameObject*> m_items;
    std::uint64_t m_signature = 0, m_sourceSig = 0;
    bool m_built = false;
    std::uint64_t Signature(const Spline* sp) const;
    std::uint64_t SourceSignature() const;
    GameObject* MakeItem(int index);
};

} // namespace okay
