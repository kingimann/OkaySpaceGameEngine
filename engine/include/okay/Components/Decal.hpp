#pragma once
// A decal projector: a picture (a stain, a crack, a sign, bullet holes) projected along this object's
// forward (+Z) axis onto the surfaces inside its box, as a thin mesh laid just above them. The mesh is
// cut from the triangles of the scene's meshes that the box touches, so it follows bumps and corners
// and draws in every renderer. It is built when you press Project (or change a setting in the
// editor), not every frame: move the surfaces and project again.
#include "okay/Scene/Component.hpp"
#include "okay/Math/Vec3.hpp"
#include "okay/Render/Color.hpp"
#include <string>

namespace okay {

class Decal : public Component {
public:
    std::string texture;               ///< picture; empty = a plain tinted patch
    Color tint{1, 1, 1, 1};            ///< multiplies the picture; alpha fades the whole decal
    Vec3  size{2.0f, 2.0f, 1.0f};      ///< the box: width (x), height (y), and how far it reaches along +Z
    float maxAngle = 75.0f;            ///< surfaces turned more than this many degrees away from the projector get none
    float lift = 0.01f;                ///< how far the decal floats above the surface (stops flicker)

    /// Cut the decal mesh from the scene now and put it on this object's Mesh Renderer (made if missing).
    /// Returns how many triangles it covers (0 = nothing in the box).
    int Project();
    int Triangles() const { return triangles_; }
    void Start() override { if (!projected_) Project(); }
    /// True once Project has run for this component.
    bool Projected() const { return projected_; }

private:
    int triangles_ = 0;
    bool projected_ = false;
};

} // namespace okay
