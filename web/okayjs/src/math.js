// okay.js maths: Vector2, Vector3, Quaternion, Matrix4, Color and Mathf.
//
// Written from general linear algebra (column vectors, column-major 4x4 matrices as WebGL's uniformMatrix4fv reads
// them, unit quaternions for rotations) and matched by behaviour to the engine's own maths (engine/include/okay/Math:
// Vec2, Vec3, Quat, Mat4, Color, Mathf) and to the script API of the engine's JavaScript scripts (docs/scripting/
// javascript.md): the same names, the same Euler order (Z, then X, then Y), the same clamping of Lerp, the same
// right-handed view with cameras looking down their own -Z. Numbers are JavaScript doubles (the engine stores floats),
// so results agree to float precision, not bit for bit.

const DEG = Math.PI / 180;
const EPS = 1e-6;   // Mathf::Epsilon

/** The shortest decimal text that reads back as the same 32-bit float (how the engine prints a float). */
export function floatText(x) {
    const f = Math.fround(x);
    if (!Number.isFinite(f)) return String(f);
    for (let p = 1; p <= 9; ++p) {
        const s = Number(f.toPrecision(p));
        if (Math.fround(s) === f) return String(s);
    }
    return String(f);
}

function isNum(v) { return typeof v === 'number'; }
function need(v, what) {
    if (!(v instanceof Vector3)) throw new TypeError('Vector3 expected for ' + what + ', got ' + (v === null ? 'null' : typeof v));
    return v;
}

// ---------------------------------------------------------------------------------------------------------------
// Vector2 and Vector3 are plain constructor functions so that, as in the engine's scripts, they work with or
// without `new` (`Vector3(1, 2, 3)`). Their helpers return new vectors; `set` changes the vector in place.

export function Vector2(x = 0, y = 0) {
    if (!new.target) return new Vector2(x, y);
    this.x = +x; this.y = +y;
}
Object.assign(Vector2.prototype, {
    add(o) { return new Vector2(this.x + o.x, this.y + o.y); },
    sub(o) { return new Vector2(this.x - o.x, this.y - o.y); },
    scale(s) { return isNum(s) ? new Vector2(this.x * s, this.y * s) : new Vector2(this.x * s.x, this.y * s.y); },
    div(d) { return new Vector2(this.x / d, this.y / d); },
    negate() { return new Vector2(-this.x, -this.y); },
    clone() { return new Vector2(this.x, this.y); },
    set(x, y) { this.x = +x; this.y = +y; return this; },
    equals(o) { return o instanceof Vector2 && o.x === this.x && o.y === this.y; },
    toString() { return '(' + floatText(this.x) + ', ' + floatText(this.y) + ')'; },
});
Object.defineProperties(Vector2.prototype, {
    sqrMagnitude: { get() { return this.x * this.x + this.y * this.y; } },
    magnitude: { get() { return Math.sqrt(this.x * this.x + this.y * this.y); } },
    normalized: { get() { const m = this.magnitude; return m > EPS ? new Vector2(this.x / m, this.y / m) : new Vector2(); } },
});
Object.assign(Vector2, {
    Dot(a, b) { return a.x * b.x + a.y * b.y; },
    Cross(a, b) { return a.x * b.y - a.y * b.x; },
    Distance(a, b) { return Math.hypot(a.x - b.x, a.y - b.y); },
    Lerp(a, b, t) { t = Mathf.Clamp01(t); return new Vector2(a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t); },
    LerpUnclamped(a, b, t) { return new Vector2(a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t); },
    MoveTowards(c, t, maxDelta) {
        const dx = t.x - c.x, dy = t.y - c.y, d = Math.hypot(dx, dy);
        if (d <= maxDelta || d < EPS) return new Vector2(t.x, t.y);
        return new Vector2(c.x + dx / d * maxDelta, c.y + dy / d * maxDelta);
    },
    Reflect(v, n) { const k = 2 * Vector2.Dot(v, n); return new Vector2(v.x - n.x * k, v.y - n.y * k); },
    Perpendicular(v) { return new Vector2(-v.y, v.x); },
});
for (const [n, x, y] of [['zero', 0, 0], ['one', 1, 1], ['up', 0, 1], ['down', 0, -1], ['right', 1, 0], ['left', -1, 0]])
    Object.defineProperty(Vector2, n, { get: () => new Vector2(x, y), enumerable: true });

export function Vector3(x = 0, y = 0, z = 0) {
    if (!new.target) return new Vector3(x, y, z);
    this.x = +x; this.y = +y; this.z = +z;
}
Object.assign(Vector3.prototype, {
    add(o) { need(o, 'add'); return new Vector3(this.x + o.x, this.y + o.y, this.z + o.z); },
    sub(o) { need(o, 'sub'); return new Vector3(this.x - o.x, this.y - o.y, this.z - o.z); },
    /** A number scales every axis; a vector scales axis by axis. */
    scale(s) {
        if (isNum(s)) return new Vector3(this.x * s, this.y * s, this.z * s);
        need(s, 'scale');
        return new Vector3(this.x * s.x, this.y * s.y, this.z * s.z);
    },
    div(d) {
        if (!isNum(d)) throw new TypeError('Vector3.div needs a number');
        return new Vector3(this.x / d, this.y / d, this.z / d);
    },
    negate() { return new Vector3(-this.x, -this.y, -this.z); },
    clone() { return new Vector3(this.x, this.y, this.z); },
    set(x, y, z) {
        if (!isNum(x) || !isNum(y) || !isNum(z)) throw new TypeError('Vector3.set takes numbers (x, y, z)');
        this.x = x; this.y = y; this.z = z; return this;
    },
    equals(o) { return o instanceof Vector3 && o.x === this.x && o.y === this.y && o.z === this.z; },
    toString() { return '(' + floatText(this.x) + ', ' + floatText(this.y) + ', ' + floatText(this.z) + ')'; },
});
Object.defineProperties(Vector3.prototype, {
    sqrMagnitude: { get() { return this.x * this.x + this.y * this.y + this.z * this.z; } },
    magnitude: { get() { return Math.sqrt(this.sqrMagnitude); } },
    normalized: {
        get() { const m = this.magnitude; return m > EPS ? new Vector3(this.x / m, this.y / m, this.z / m) : new Vector3(); },
    },
});
Object.assign(Vector3, {
    Dot(a, b) { return a.x * b.x + a.y * b.y + a.z * b.z; },
    Cross(a, b) { return new Vector3(a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x); },
    Distance(a, b) { return Math.hypot(a.x - b.x, a.y - b.y, a.z - b.z); },
    /** Blend from a (t = 0) to b (t = 1); t is clamped to 0..1, as the engine's Vector3.Lerp does. */
    Lerp(a, b, t) { return Vector3.LerpUnclamped(a, b, Mathf.Clamp01(t)); },
    LerpUnclamped(a, b, t) { return new Vector3(a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t); },
    MoveTowards(c, t, maxDistance) {
        const d = t.sub(c), len = d.magnitude;
        if (len <= maxDistance || len < EPS) return t.clone();
        return c.add(d.scale(maxDistance / len));
    },
    Reflect(v, n) { return v.sub(n.scale(2 * Vector3.Dot(v, n))); },
    Project(a, b) { const s = b.sqrMagnitude; return s < EPS ? new Vector3() : b.scale(Vector3.Dot(a, b) / s); },
    ProjectOnPlane(a, n) { return a.sub(Vector3.Project(a, n)); },
    /** Angle between two directions in degrees (0..180). */
    Angle(a, b) {
        const d = Math.sqrt(a.sqrMagnitude * b.sqrMagnitude);
        if (d < EPS) return 0;
        return Math.acos(Mathf.Clamp(Vector3.Dot(a, b) / d, -1, 1)) / DEG;
    },
    Scale(a, b) { return new Vector3(a.x * b.x, a.y * b.y, a.z * b.z); },
    Min(a, b) { return new Vector3(Math.min(a.x, b.x), Math.min(a.y, b.y), Math.min(a.z, b.z)); },
    Max(a, b) { return new Vector3(Math.max(a.x, b.x), Math.max(a.y, b.y), Math.max(a.z, b.z)); },
});
for (const [n, x, y, z] of [['zero', 0, 0, 0], ['one', 1, 1, 1], ['up', 0, 1, 0], ['down', 0, -1, 0], ['right', 1, 0, 0],
                            ['left', -1, 0, 0], ['forward', 0, 0, 1], ['back', 0, 0, -1]])
    Object.defineProperty(Vector3, n, { get: () => new Vector3(x, y, z), enumerable: true });

// ---------------------------------------------------------------------------------------------------------------
/** A rotation as a unit quaternion (x, y, z, w). */
export class Quaternion {
    constructor(x = 0, y = 0, z = 0, w = 1) { this.x = x; this.y = y; this.z = z; this.w = w; }
    static get identity() { return new Quaternion(); }

    /** `a.multiply(b)` turns by b first and then by a (the engine's a * b). */
    multiply(b) {
        const a = this;
        return new Quaternion(
            a.w * b.x + a.x * b.w + a.y * b.z - a.z * b.y,
            a.w * b.y - a.x * b.z + a.y * b.w + a.z * b.x,
            a.w * b.z + a.x * b.y - a.y * b.x + a.z * b.w,
            a.w * b.w - a.x * b.x - a.y * b.y - a.z * b.z);
    }
    /** Rotate a vector: v' = v + 2w (u x v) + 2 u x (u x v), u the vector part. */
    multiplyVector(v) {
        const ux = this.x, uy = this.y, uz = this.z, w = this.w;
        const tx = 2 * (uy * v.z - uz * v.y), ty = 2 * (uz * v.x - ux * v.z), tz = 2 * (ux * v.y - uy * v.x);
        return new Vector3(v.x + w * tx + (uy * tz - uz * ty), v.y + w * ty + (uz * tx - ux * tz), v.z + w * tz + (ux * ty - uy * tx));
    }
    get magnitude() { return Math.hypot(this.x, this.y, this.z, this.w); }
    get normalized() {
        const m = this.magnitude;
        return m > EPS ? new Quaternion(this.x / m, this.y / m, this.z / m, this.w / m) : new Quaternion();
    }
    conjugate() { return new Quaternion(-this.x, -this.y, -this.z, this.w); }
    inverse() {
        const n = this.x * this.x + this.y * this.y + this.z * this.z + this.w * this.w;
        if (n < EPS) return new Quaternion();
        return new Quaternion(-this.x / n, -this.y / n, -this.z / n, this.w / n);
    }
    clone() { return new Quaternion(this.x, this.y, this.z, this.w); }
    equals(o) { return o instanceof Quaternion && o.x === this.x && o.y === this.y && o.z === this.z && o.w === this.w; }
    toString() { return '(' + [this.x, this.y, this.z, this.w].map(floatText).join(', ') + ')'; }

    /** Euler angles in degrees, Z then X then Y (the inverse of Quaternion.Euler). */
    get eulerAngles() {
        const { x, y, z, w } = this;
        const sx = 2 * (w * x - y * z);
        if (sx > 0.999999 || sx < -0.999999)   // looking straight up or down: put the whole turn in the yaw
            return new Vector3(sx > 0 ? 90 : -90, Math.atan2(2 * (w * y - x * z), 1 - 2 * (y * y + z * z)) / DEG, 0);
        return new Vector3(Math.asin(Mathf.Clamp(sx, -1, 1)) / DEG,
                           Math.atan2(2 * (w * y + x * z), 1 - 2 * (x * x + y * y)) / DEG,
                           Math.atan2(2 * (w * z + x * y), 1 - 2 * (x * x + z * z)) / DEG);
    }

    static Dot(a, b) { return a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w; }
    static AngleAxis(degrees, axis) {
        const a = axis.normalized, h = degrees * DEG * 0.5, s = Math.sin(h);
        return new Quaternion(a.x * s, a.y * s, a.z * s, Math.cos(h));
    }
    /** From Euler angles in degrees (a Vector3 or three numbers), applied Z, then X, then Y. */
    static Euler(x, y, z) {
        if (x instanceof Vector3) ({ x, y, z } = x);
        const qx = Quaternion.AngleAxis(x, Vector3.right), qy = Quaternion.AngleAxis(y, Vector3.up),
              qz = Quaternion.AngleAxis(z, Vector3.forward);
        return qy.multiply(qx).multiply(qz).normalized;
    }
    /** Normalized blend along the shorter way round; t clamped to 0..1. */
    static Lerp(a, b, t) {
        t = Mathf.Clamp01(t);
        const s = Quaternion.Dot(a, b) < 0 ? -1 : 1;
        return new Quaternion(a.x + (s * b.x - a.x) * t, a.y + (s * b.y - a.y) * t, a.z + (s * b.z - a.z) * t,
                              a.w + (s * b.w - a.w) * t).normalized;
    }
    /** Constant-speed blend on the unit sphere; t clamped to 0..1. */
    static Slerp(a, b, t) {
        t = Mathf.Clamp01(t);
        let d = Quaternion.Dot(a, b), bx = b.x, by = b.y, bz = b.z, bw = b.w;
        if (d < 0) { d = -d; bx = -bx; by = -by; bz = -bz; bw = -bw; }
        if (d > 0.9995) return Quaternion.Lerp(a, new Quaternion(bx, by, bz, bw), t);
        const th = Math.acos(d), sn = Math.sin(th), k0 = Math.sin((1 - t) * th) / sn, k1 = Math.sin(t * th) / sn;
        return new Quaternion(a.x * k0 + bx * k1, a.y * k0 + by * k1, a.z * k0 + bz * k1, a.w * k0 + bw * k1);
    }
    /** Angle between two rotations in degrees. */
    static Angle(a, b) { return 2 * Math.acos(Math.min(1, Math.abs(Quaternion.Dot(a.normalized, b.normalized)))) / DEG; }
    /** The rotation whose +Z points along `forward` and whose +Y leans to `up`. */
    static LookRotation(forward, up = Vector3.up) {
        if (forward.sqrMagnitude < EPS) return new Quaternion();
        const f = forward.normalized;
        let r = Vector3.Cross(up, f);
        if (r.sqrMagnitude < EPS) r = Vector3.Cross(Math.abs(f.z) < 0.9 ? Vector3.forward : Vector3.right, f);
        r = r.normalized;
        const u = Vector3.Cross(f, r);
        return Quaternion.FromBasis(r, u, f);
    }
    /** The rotation that takes the x, y and z axes to the three given unit axes (the columns of a rotation matrix). */
    static FromBasis(r, u, f) {
        // Read the quaternion off the matrix with columns r, u, f, using the largest of the four squared parts.
        const m00 = r.x, m10 = r.y, m20 = r.z, m01 = u.x, m11 = u.y, m21 = u.z, m02 = f.x, m12 = f.y, m22 = f.z;
        const tr = m00 + m11 + m22;
        let q;
        if (tr > 0) {
            const s = 2 * Math.sqrt(tr + 1);
            q = new Quaternion((m21 - m12) / s, (m02 - m20) / s, (m10 - m01) / s, s / 4);
        } else if (m00 > m11 && m00 > m22) {
            const s = 2 * Math.sqrt(1 + m00 - m11 - m22);
            q = new Quaternion(s / 4, (m01 + m10) / s, (m02 + m20) / s, (m21 - m12) / s);
        } else if (m11 > m22) {
            const s = 2 * Math.sqrt(1 + m11 - m00 - m22);
            q = new Quaternion((m01 + m10) / s, s / 4, (m12 + m21) / s, (m02 - m20) / s);
        } else {
            const s = 2 * Math.sqrt(1 + m22 - m00 - m11);
            q = new Quaternion((m02 + m20) / s, (m12 + m21) / s, s / 4, (m10 - m01) / s);
        }
        return q.normalized;
    }
}

// ---------------------------------------------------------------------------------------------------------------
/** A 4x4 matrix, column-major in a Float32Array (element (row r, column c) is elements[c * 4 + r]). */
export class Matrix4 {
    constructor(elements) {
        this.elements = new Float32Array(16);
        if (elements) this.elements.set(elements);
        else this.elements[0] = this.elements[5] = this.elements[10] = this.elements[15] = 1;
    }
    static get identity() { return new Matrix4(); }
    get(row, col) { return this.elements[col * 4 + row]; }
    clone() { return new Matrix4(this.elements); }

    /** this * b (b is applied first). */
    multiply(b) {
        const A = this.elements, B = b.elements, out = new Matrix4(), O = out.elements;
        for (let c = 0; c < 4; ++c)
            for (let r = 0; r < 4; ++r)
                O[c * 4 + r] = A[r] * B[c * 4] + A[4 + r] * B[c * 4 + 1] + A[8 + r] * B[c * 4 + 2] + A[12 + r] * B[c * 4 + 3];
        return out;
    }
    multiplyPoint(p) {
        const e = this.elements;
        const x = e[0] * p.x + e[4] * p.y + e[8] * p.z + e[12], y = e[1] * p.x + e[5] * p.y + e[9] * p.z + e[13],
              z = e[2] * p.x + e[6] * p.y + e[10] * p.z + e[14], w = e[3] * p.x + e[7] * p.y + e[11] * p.z + e[15];
        return w !== 0 && w !== 1 ? new Vector3(x / w, y / w, z / w) : new Vector3(x, y, z);
    }
    multiplyVector(v) {
        const e = this.elements;
        return new Vector3(e[0] * v.x + e[4] * v.y + e[8] * v.z, e[1] * v.x + e[5] * v.y + e[9] * v.z,
                           e[2] * v.x + e[6] * v.y + e[10] * v.z);
    }
    transpose() {
        const e = this.elements, out = new Matrix4();
        for (let c = 0; c < 4; ++c) for (let r = 0; r < 4; ++r) out.elements[r * 4 + c] = e[c * 4 + r];
        return out;
    }
    /** The six 2x2 determinants of the two left columns and of the two right columns, which give both the
     *  determinant and the adjugate (Laplace expansion along column pairs). */
    #pairs() {
        const e = this.elements;
        const a = (r0, r1) => e[r0] * e[4 + r1] - e[r1] * e[4 + r0];             // columns 0 and 1
        const b = (r0, r1) => e[8 + r0] * e[12 + r1] - e[8 + r1] * e[12 + r0];   // columns 2 and 3
        return { l: [a(0, 1), a(0, 2), a(0, 3), a(1, 2), a(1, 3), a(2, 3)], r: [b(0, 1), b(0, 2), b(0, 3), b(1, 2), b(1, 3), b(2, 3)] };
    }
    determinant() {
        const { l, r } = this.#pairs();
        return l[0] * r[5] - l[1] * r[4] + l[2] * r[3] + l[3] * r[2] - l[4] * r[1] + l[5] * r[0];
    }
    /** The inverse, or the identity when the matrix is singular. */
    inverse() {
        const e = this.elements, { l, r } = this.#pairs();
        const det = l[0] * r[5] - l[1] * r[4] + l[2] * r[3] + l[3] * r[2] - l[4] * r[1] + l[5] * r[0];
        let scale = 1;
        for (let c = 0; c < 4; ++c) scale *= Math.hypot(e[c * 4], e[c * 4 + 1], e[c * 4 + 2], e[c * 4 + 3]);
        if (!Number.isFinite(det) || Math.abs(det) <= 1e-12 * scale ) return new Matrix4();
        const k = 1 / det, o = new Float32Array(16);
        // m(r, c) = e[c * 4 + r]; out(r, c) = cofactor(c, r) / det
        const m = (row, col) => e[col * 4 + row];
        const [l01, l02, l03, l12, l13, l23] = l, [r01, r02, r03, r12, r13, r23] = r;
        o[0]  = ( m(1, 1) * r23 - m(2, 1) * r13 + m(3, 1) * r12) * k;
        o[4]  = (-m(0, 1) * r23 + m(2, 1) * r03 - m(3, 1) * r02) * k;
        o[8]  = ( m(0, 1) * r13 - m(1, 1) * r03 + m(3, 1) * r01) * k;
        o[12] = (-m(0, 1) * r12 + m(1, 1) * r02 - m(2, 1) * r01) * k;
        o[1]  = (-m(1, 0) * r23 + m(2, 0) * r13 - m(3, 0) * r12) * k;
        o[5]  = ( m(0, 0) * r23 - m(2, 0) * r03 + m(3, 0) * r02) * k;
        o[9]  = (-m(0, 0) * r13 + m(1, 0) * r03 - m(3, 0) * r01) * k;
        o[13] = ( m(0, 0) * r12 - m(1, 0) * r02 + m(2, 0) * r01) * k;
        o[2]  = ( m(1, 3) * l23 - m(2, 3) * l13 + m(3, 3) * l12) * k;
        o[6]  = (-m(0, 3) * l23 + m(2, 3) * l03 - m(3, 3) * l02) * k;
        o[10] = ( m(0, 3) * l13 - m(1, 3) * l03 + m(3, 3) * l01) * k;
        o[14] = (-m(0, 3) * l12 + m(1, 3) * l02 - m(2, 3) * l01) * k;
        o[3]  = (-m(1, 2) * l23 + m(2, 2) * l13 - m(3, 2) * l12) * k;
        o[7]  = ( m(0, 2) * l23 - m(2, 2) * l03 + m(3, 2) * l02) * k;
        o[11] = (-m(0, 2) * l13 + m(1, 2) * l03 - m(3, 2) * l01) * k;
        o[15] = ( m(0, 2) * l12 - m(1, 2) * l02 + m(2, 2) * l01) * k;
        return new Matrix4(o);
    }
    /** Split a matrix made by TRS back into position, rotation and scale (a negative determinant flips x). */
    decompose() {
        const e = this.elements;
        const cx = new Vector3(e[0], e[1], e[2]), cy = new Vector3(e[4], e[5], e[6]), cz = new Vector3(e[8], e[9], e[10]);
        let sx = cx.magnitude;
        const sy = cy.magnitude, sz = cz.magnitude;
        if (this.determinant() < 0) sx = -sx;
        const safe = (v, s) => (Math.abs(s) > EPS ? v.div(s) : new Vector3());
        return { position: new Vector3(e[12], e[13], e[14]),
                 rotation: Quaternion.FromBasis(safe(cx, sx), safe(cy, sy), safe(cz, sz)),
                 scale: new Vector3(sx, sy, sz) };
    }

    static Translate(t) { const m = new Matrix4(); m.elements[12] = t.x; m.elements[13] = t.y; m.elements[14] = t.z; return m; }
    static Scale(s) { const m = new Matrix4(); m.elements[0] = s.x; m.elements[5] = s.y; m.elements[10] = s.z; return m; }
    static Rotate(q) {
        const { x, y, z, w } = q, m = new Matrix4(), e = m.elements;
        e[0] = 1 - 2 * (y * y + z * z); e[1] = 2 * (x * y + w * z);     e[2] = 2 * (x * z - w * y);
        e[4] = 2 * (x * y - w * z);     e[5] = 1 - 2 * (x * x + z * z); e[6] = 2 * (y * z + w * x);
        e[8] = 2 * (x * z + w * y);     e[9] = 2 * (y * z - w * x);     e[10] = 1 - 2 * (x * x + y * y);
        return m;
    }
    /** Translation * Rotation * Scale in one matrix. */
    static TRS(t, q, s) {
        const m = Matrix4.Rotate(q), e = m.elements;
        for (let i = 0; i < 3; ++i) { e[i] *= s.x; e[4 + i] *= s.y; e[8 + i] *= s.z; }
        e[12] = t.x; e[13] = t.y; e[14] = t.z;
        return m;
    }
    /** OpenGL-style perspective (clip z from -1 at near to +1 at far), vertical field of view in degrees. */
    static Perspective(fovDeg, aspect, near, far) {
        const f = 1 / Math.tan(fovDeg * DEG * 0.5), m = new Matrix4(), e = m.elements;
        e[0] = f / aspect; e[5] = f; e[10] = (far + near) / (near - far); e[11] = -1;
        e[14] = 2 * far * near / (near - far); e[15] = 0;
        return m;
    }
    static Ortho(left, right, bottom, top, near, far) {
        const m = new Matrix4(), e = m.elements;
        e[0] = 2 / (right - left); e[5] = 2 / (top - bottom); e[10] = -2 / (far - near);
        e[12] = -(right + left) / (right - left); e[13] = -(top + bottom) / (top - bottom); e[14] = -(far + near) / (far - near);
        return m;
    }
    /** A right-handed view matrix from `eye` toward `center` (the camera looks down its -Z). */
    static LookAt(eye, center, up = Vector3.up) {
        const f = center.sub(eye).normalized;
        let u = up.normalized;
        if (Math.abs(Vector3.Dot(f, u)) > 0.9999) u = Math.abs(f.z) < 0.9 ? Vector3.forward : Vector3.right;
        const s = Vector3.Cross(f, u).normalized, v = Vector3.Cross(s, f), m = new Matrix4(), e = m.elements;
        e[0] = s.x; e[4] = s.y; e[8] = s.z;
        e[1] = v.x; e[5] = v.y; e[9] = v.z;
        e[2] = -f.x; e[6] = -f.y; e[10] = -f.z;
        e[12] = -Vector3.Dot(s, eye); e[13] = -Vector3.Dot(v, eye); e[14] = Vector3.Dot(f, eye);
        return m;
    }
}

// ---------------------------------------------------------------------------------------------------------------
/** A colour with r, g, b, a from 0 to 1. */
export class Color {
    constructor(r = 0, g = 0, b = 0, a = 1) { this.r = r; this.g = g; this.b = b; this.a = a; }
    static FromBytes(r, g, b, a = 255) { return new Color(r / 255, g / 255, b / 255, a / 255); }
    /** "#rgb", "#rrggbb", "#rrggbbaa" (the # is optional) or "r, g, b[, a]" with values from 0 to 1. Unreadable
     *  text gives `fallback`. */
    static Parse(text, fallback = Color.white) {
        const t = String(text).trim();
        const hex = /^#?([0-9a-f]{3}|[0-9a-f]{6}|[0-9a-f]{8})$/i.exec(t);
        if (hex) {
            let h = hex[1];
            if (h.length === 3) h = h.replace(/./g, c => c + c);
            const p = i => parseInt(h.substr(i, 2), 16);
            return Color.FromBytes(p(0), p(2), p(4), h.length === 8 ? p(6) : 255);
        }
        const parts = t.split(',').map(s => Number(s.trim()));
        if ((parts.length === 3 || parts.length === 4) && parts.every(Number.isFinite))
            return new Color(parts[0], parts[1], parts[2], parts.length === 4 ? parts[3] : 1);
        return fallback.clone();
    }
    static Lerp(x, y, t) {
        t = Mathf.Clamp01(t);
        return new Color(x.r + (y.r - x.r) * t, x.g + (y.g - x.g) * t, x.b + (y.b - x.b) * t, x.a + (y.a - x.a) * t);
    }
    get luminance() { return 0.299 * this.r + 0.587 * this.g + 0.114 * this.b; }
    clone() { return new Color(this.r, this.g, this.b, this.a); }
    equals(o) { return o instanceof Color && o.r === this.r && o.g === this.g && o.b === this.b && o.a === this.a; }
    toString() { return 'Color(' + [this.r, this.g, this.b, this.a].map(floatText).join(', ') + ')'; }
}
for (const [n, r, g, b, a] of [['black', 0, 0, 0, 1], ['white', 1, 1, 1, 1], ['red', 1, 0, 0, 1], ['green', 0, 1, 0, 1],
                               ['blue', 0, 0, 1, 1], ['yellow', 1, 0.92, 0.016, 1], ['cyan', 0, 1, 1, 1],
                               ['magenta', 1, 0, 1, 1], ['clear', 0, 0, 0, 0]])
    Object.defineProperty(Color, n, { get: () => new Color(r, g, b, a), enumerable: true });

// ---------------------------------------------------------------------------------------------------------------
/** The engine's Mathf script type: the same names and the same edge cases (Round halves away from zero, Sign of 0
 *  is 1, Sqrt of a negative is 0, Lerp clamps t). */
export const Mathf = Object.freeze({
    PI: Math.PI,
    Deg2Rad: DEG,
    Rad2Deg: 1 / DEG,
    Epsilon: EPS,
    Abs: Math.abs,
    Sqrt: x => Math.sqrt(Math.max(0, x)),
    Sin: Math.sin, Cos: Math.cos, Tan: Math.tan,
    Floor: Math.floor, Ceil: Math.ceil,
    Round: x => (x < 0 ? -Math.round(-x) : Math.round(x)),
    Sign: x => (x < 0 ? -1 : 1),
    Clamp01: x => (x < 0 ? 0 : x > 1 ? 1 : x),
    Clamp: (v, lo, hi) => (v < lo ? lo : v > hi ? hi : v),
    Min: Math.min, Max: Math.max, Pow: Math.pow, Atan2: Math.atan2,
    Lerp: (a, b, t) => a + (b - a) * (t < 0 ? 0 : t > 1 ? 1 : t),
    MoveTowards: (c, t, d) => (Math.abs(t - c) <= d ? t : c + (t > c ? d : -d)),
});
