#pragma once
// The inverse of an affine 4x4 matrix in double precision (last row 0 0 0 1), for bind poses. Internal: not installed.
#include "ReaderCommon.hpp"

namespace okay {
namespace detail {
namespace rc {

// Inverse of an affine matrix (last row 0 0 0 1). False when the 3x3 part is singular.
inline bool InvAffine(const rc::M4& a, rc::M4& o) {
    const double a00 = a.at(0, 0), a01 = a.at(1, 0), a02 = a.at(2, 0), a10 = a.at(0, 1), a11 = a.at(1, 1), a12 = a.at(2, 1), a20 = a.at(0, 2), a21 = a.at(1, 2), a22 = a.at(2, 2);
    const double c00 = a11 * a22 - a12 * a21, c01 = a12 * a20 - a10 * a22, c02 = a10 * a21 - a11 * a20;
    const double det = a00 * c00 + a01 * c01 + a02 * c02;
    if (!(std::fabs(det) > 1e-18) || !std::isfinite(det)) return false;
    const double id = 1.0 / det;
    const double r[3][3] = {{c00 * id, (a02 * a21 - a01 * a22) * id, (a01 * a12 - a02 * a11) * id},
                            {c01 * id, (a00 * a22 - a02 * a20) * id, (a02 * a10 - a00 * a12) * id},
                            {c02 * id, (a01 * a20 - a00 * a21) * id, (a00 * a11 - a01 * a10) * id}};
    o = rc::M4();
    for (int row = 0; row < 3; ++row) for (int col = 0; col < 3; ++col) o.at(col, row) = r[row][col];
    const double tx = a.at(3, 0), ty = a.at(3, 1), tz = a.at(3, 2);
    for (int row = 0; row < 3; ++row) o.at(3, row) = -(r[row][0] * tx + r[row][1] * ty + r[row][2] * tz);
    return rc::Finite(o);
}

} // namespace rc
} // namespace detail
} // namespace okay
