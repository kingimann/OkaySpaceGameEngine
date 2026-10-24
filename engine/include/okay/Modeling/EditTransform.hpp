#pragma once
// Blender-style modal transforms for mesh edit mode: press G (move), R (rotate)
// or S (scale), move the mouse, lock to an axis with X / Y / Z (Shift = the
// plane of the other two), type an exact value, hold Ctrl to snap, then confirm
// (Enter / click) or cancel (Esc / right-click).
//
// Everything here is plain math on a Mesh, so it's testable without the editor:
// the editor turns mouse input into a world-space matrix and hands it to an
// EditTransformSession, which always works from the vertices' positions at the
// start (so cancelling restores them exactly and typed values are exact).
#include "okay/Math/Mat4.hpp"
#include "okay/Math/Vec3.hpp"
#include "okay/Render/Mesh.hpp"
#include <string>
#include <vector>

namespace okay::modeling {

enum class EditXform { Move, Rotate, Scale };
const char* EditXformName(EditXform k);

/// Which axes a transform may use. Free (axis = -1), one axis, or a plane
/// (every axis but `axis`).
struct AxisLock {
    int  axis  = -1;
    bool plane = false;
    bool Free() const { return axis < 0; }
    bool Allows(int a) const { return axis < 0 || (plane ? a != axis : a == axis); }
    /// X / Y / Z pressed (Shift for the plane). Pressing the same lock again frees it.
    void Press(int a, bool shift);
    /// "X", "Y", "Z", "YZ plane", ... or "" when free.
    std::string Label() const;
};

/// An exact value typed during a transform: digits, one '.', '-' to negate,
/// Backspace to delete. Empty means "use the mouse".
class NumericInput {
public:
    bool Active() const { return !m_text.empty() || m_negative; }
    /// A typed character; returns false if it isn't part of a number.
    bool Type(char c);
    void Backspace();
    void Clear() { m_text.clear(); m_negative = false; }
    /// The typed value, or `fallback` when nothing (or just "-" / ".") is typed.
    float Value(float fallback) const;
    std::string Text() const { return (m_negative ? "-" : "") + m_text; }
private:
    std::string m_text;
    bool m_negative = false;
};

/// Round to the nearest multiple of `step` (step <= 0 leaves it unchanged).
float SnapTo(float v, float step);

// ---- World-space matrices for one transform -----------------------------
/// Move by `offset`, keeping only the axes `lock` allows.
Mat4 MoveMatrix(const Vec3& offset, const AxisLock& lock);
/// Rotate `degrees` about `axisDir` through `pivot`.
Mat4 RotateMatrix(const Vec3& pivot, const Vec3& axisDir, float degrees);
/// Scale by `factor` about `pivot` along the axes `lock` allows (free = uniform).
Mat4 ScaleMatrix(const Vec3& pivot, const AxisLock& lock, float factor);

// ---- Mouse -> transform amount (the editor passes its camera rays) --------
struct Ray { Vec3 origin, dir; };
/// How far the mouse moved the pivot, from the ray at the start of the drag to
/// the ray now: along the locked axis, in the locked plane, or (free) in the
/// plane facing the camera (`viewDir` points from the camera into the scene).
Vec3 MoveFromRays(const AxisLock& lock, const Vec3& pivot, const Vec3& viewDir, const Ray& start, const Ray& now);
/// Signed angle in degrees swept by the mouse around the pivot on screen
/// (screen y points down), wrapped to [-180, 180).
float ScreenAngleDelta(float pivotX, float pivotY, float fromX, float fromY, float toX, float toY);

/// One G / R / S operation on a mesh. Begin() remembers the vertices it moves
/// (the selection, plus the soft-selection falloff and the X-symmetry
/// partners); Apply() sets them from those remembered positions; Cancel() puts
/// them back.
struct EditTransformOptions {
    float softRadius = 0.0f;   ///< proportional editing radius (0 = off)
    bool  mirrorX = false;     ///< also move each vertex's partner across local x = 0, mirrored
};

class EditTransformSession {
public:
    using Options = EditTransformOptions;
    /// `selected`: mesh vertex indices. `toWorld`: the object's local-to-world matrix.
    void Begin(Mesh& mesh, EditXform kind, const std::vector<int>& selected, const Mat4& toWorld, const Options& opt = Options());
    bool Active() const { return m_active; }
    EditXform Kind() const { return m_kind; }
    /// The selection's centre in world space (what Rotate / Scale turn around).
    Vec3 PivotWorld() const { return m_pivotWorld; }
    /// Move every remembered vertex by the world-space matrix `world`.
    void Apply(Mesh& mesh, const Mat4& world);
    /// Restore the remembered positions and end the session.
    void Cancel(Mesh& mesh);
    /// Keep the current positions and end the session.
    void Confirm() { m_active = false; }
    /// Positions at Begin, for undo bookkeeping.
    void Restore(Mesh& mesh) const;
    std::size_t MovedCount() const { return m_verts.size(); }

    AxisLock lock;
    NumericInput typed;
    float accumulatedDegrees = 0.0f;   ///< Rotate: mouse angle summed across frames

private:
    bool m_active = false;
    EditXform m_kind = EditXform::Move;
    Mat4 m_toWorld, m_toLocal;
    Vec3 m_pivotWorld;
    std::vector<int>   m_verts;    // every vertex this session moves
    std::vector<Vec3>  m_orig;     // its position at Begin
    std::vector<float> m_weight;   // 1 for the selection, falloff for soft-selected
    std::vector<char>  m_mirror;   // moved as the mirrored partner of a selected vertex
    std::string        m_name;     // the mesh's primitive name and normals at Begin
    std::vector<Vec3>  m_normals;  // (Apply clears both; Cancel puts them back)
};

} // namespace okay::modeling
