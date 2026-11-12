#pragma once
// A hanging cable or rope between two points with sag (power lines, bunting, a rope bridge's rail):
// it keeps a Line Renderer on the same object laid along a parabola from this object (or a named
// object) to another named object, so moving either end moves the cable. Purely visual: it has no
// physics; use Joint3D for a rope that must pull on things.
#include "okay/Scene/Component.hpp"
#include "okay/Math/Vec3.hpp"
#include "okay/Render/Color.hpp"
#include <string>
#include <vector>

namespace okay {

class Cable : public Behaviour {
public:
    std::string startObject;        ///< object the cable leaves from (empty = this object)
    std::string endObject;          ///< object it ends on (empty = a point `endOffset` from the start)
    Vec3  startOffset{0, 0, 0};     ///< offsets in each end object's own space
    Vec3  endOffset{0, 0, 0};
    float sag = 1.0f;               ///< how far the middle hangs below the straight line, world units
    int   segments = 24;            ///< pieces the curve is cut into (2..200)
    float width = 0.08f;
    Color color{0.15f, 0.15f, 0.17f, 1.0f};

    /// The points of the curve from `a` to `b`: a parabola that hangs `sag` below the middle.
    static std::vector<Vec3> Curve(const Vec3& a, const Vec3& b, float sag, int segments);
    /// The two ends in world space. False when a named end object does not exist.
    bool Ends(Vec3& a, Vec3& b) const;
    /// Lay the Line Renderer (made if missing) along the curve now. Returns false when an end is missing.
    bool Rebuild();
    /// Total length of the curve in world units.
    float Length() const;

    void Awake() override { Rebuild(); }
    void Update(float) override { Rebuild(); }
};

} // namespace okay
