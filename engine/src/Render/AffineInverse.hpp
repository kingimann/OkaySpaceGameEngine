#pragma once
// The inverse of an affine 4x4 matrix in double precision (last row 0 0 0 1), for bind poses. Internal: not installed.
#include "ReaderCommon.hpp"

namespace okay {
namespace detail {
namespace rc {

// The cofactor matrix of the 3x3 part of `a`, row by row (c[0..2] is the first row). It is the normal matrix of `a`
// up to scale, and its transpose divided by the determinant is the inverse of the 3x3 part.
inline void Cofactors3(const rc::M4& a, double c[9]) {
    const double a00 = a.at(0, 0), a01 = a.at(1, 0), a02 = a.at(2, 0), a10 = a.at(0, 1), a11 = a.at(1, 1), a12 = a.at(2, 1), a20 = a.at(0, 2), a21 = a.at(1, 2), a22 = a.at(2, 2);
    c[0] = a11 * a22 - a12 * a21; c[1] = a12 * a20 - a10 * a22; c[2] = a10 * a21 - a11 * a20;
    c[3] = a02 * a21 - a01 * a22; c[4] = a00 * a22 - a02 * a20; c[5] = a01 * a20 - a00 * a21;
    c[6] = a01 * a12 - a02 * a11; c[7] = a02 * a10 - a00 * a12; c[8] = a00 * a11 - a01 * a10;
}

// Inverse of an affine matrix (last row 0 0 0 1). False when the 3x3 part is singular.
inline bool InvAffine(const rc::M4& a, rc::M4& o) {
    double c[9];
    Cofactors3(a, c);
    const double det = a.at(0, 0) * c[0] + a.at(1, 0) * c[1] + a.at(2, 0) * c[2];
    if (!(std::fabs(det) > 1e-18) || !std::isfinite(det)) return false;
    const double id = 1.0 / det;
    const double r[3][3] = {{c[0] * id, c[3] * id, c[6] * id},
                            {c[1] * id, c[4] * id, c[7] * id},
                            {c[2] * id, c[5] * id, c[8] * id}};
    o = rc::M4();
    for (int row = 0; row < 3; ++row) for (int col = 0; col < 3; ++col) o.at(col, row) = r[row][col];
    const double tx = a.at(3, 0), ty = a.at(3, 1), tz = a.at(3, 2);
    for (int row = 0; row < 3; ++row) o.at(3, row) = -(r[row][0] * tx + r[row][1] * ty + r[row][2] * tz);
    return rc::Finite(o);
}

} // namespace rc
} // namespace detail
} // namespace okay
