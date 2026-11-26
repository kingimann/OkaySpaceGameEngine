// okay.js maths and meshes in plain Node (node --test, no packages): web/okayjs/src/math.js and geometry.js.
// The expected values are worked out by hand or checked against a second way of computing the same thing (a matrix
// against its quaternion, an inverse against the identity), and the engine's documented behaviour (Euler order Z X Y,
// Lerp clamping t, Round halving away from zero, cameras looking down -Z).
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { Vector2, Vector3, Quaternion, Matrix4, Color, Mathf, floatText, Mesh } from '../../web/okayjs/okay.js';

const near = (a, b, eps = 1e-5, what = '') => assert.ok(Math.abs(a - b) <= eps, `${what} ${a} != ${b}`);
const vnear = (a, b, eps = 1e-5) => { near(a.x, b.x, eps, 'x'); near(a.y, b.y, eps, 'y'); near(a.z, b.z, eps, 'z'); };
// a small deterministic generator (xorshift32) so a failure repeats
let seed = 2463534242;
const rnd = () => { seed ^= seed << 13; seed >>>= 0; seed ^= seed >>> 17; seed ^= seed << 5; seed >>>= 0; return seed / 4294967296; };
const rv = (s = 4) => new Vector3((rnd() - 0.5) * s, (rnd() - 0.5) * s, (rnd() - 0.5) * s);

test('Vector3 works with and without new, helpers return new vectors', () => {
    const a = Vector3(1, 2, 3), b = new Vector3(4, 5, 6);
    assert.ok(a instanceof Vector3);
    vnear(a.add(b), new Vector3(5, 7, 9));
    vnear(b.sub(a), new Vector3(3, 3, 3));
    vnear(a.scale(2), new Vector3(2, 4, 6));
    vnear(a.scale(b), new Vector3(4, 10, 18));
    vnear(b.div(2), new Vector3(2, 2.5, 3));
    vnear(a.negate(), new Vector3(-1, -2, -3));
    vnear(a, new Vector3(1, 2, 3));   // unchanged
    assert.ok(a.clone().equals(a) && a.clone() !== a);
    assert.ok(!a.equals({ x: 1, y: 2, z: 3 }));
    assert.equal(a.set(0, 3, 4).magnitude, 5);
    vnear(a.normalized, new Vector3(0, 0.6, 0.8));
    vnear(new Vector3().normalized, new Vector3());
    assert.throws(() => a.add(5), TypeError);
    assert.throws(() => a.div(new Vector3()), /needs a number/);
});

test('Vector3 statics are fresh copies and match the engine table', () => {
    const up = Vector3.up;
    up.x = 9;
    vnear(Vector3.up, new Vector3(0, 1, 0));
    vnear(Vector3.back, new Vector3(0, 0, -1));
    vnear(Vector3.forward, new Vector3(0, 0, 1));
    near(Vector3.Distance(new Vector3(1, 1, 1), new Vector3(4, 5, 1)), 5);
    near(Vector3.Dot(new Vector3(1, 2, 3), new Vector3(4, 5, 6)), 32);
    vnear(Vector3.Cross(Vector3.right, Vector3.up), Vector3.forward);   // right-handed: x cross y = z
    vnear(Vector3.Lerp(Vector3.zero, new Vector3(10, 0, 0), 2), new Vector3(10, 0, 0));   // t is clamped
    vnear(Vector3.LerpUnclamped(Vector3.zero, new Vector3(10, 0, 0), 2), new Vector3(20, 0, 0));
    vnear(Vector3.MoveTowards(Vector3.zero, new Vector3(10, 0, 0), 3), new Vector3(3, 0, 0));
    vnear(Vector3.MoveTowards(Vector3.zero, new Vector3(1, 0, 0), 3), new Vector3(1, 0, 0));
    near(Vector3.Angle(Vector3.right, Vector3.up), 90);
    vnear(Vector3.Reflect(new Vector3(1, -1, 0), Vector3.up), new Vector3(1, 1, 0));
    vnear(Vector3.ProjectOnPlane(new Vector3(1, 2, 3), Vector3.up), new Vector3(1, 0, 3));
});

test('numbers print the way the engine prints floats', () => {
    assert.equal(floatText(0.1), '0.1');
    assert.equal(floatText(1 / 3), '0.33333334');
    assert.equal(new Vector3(1, 2.5, 0).toString(), '(1, 2.5, 0)');
    assert.equal(String(new Vector3(-0.25, 1e-7, 100)), '(-0.25, 1e-7, 100)');
    assert.equal(new Vector2(1, 2).toString(), '(1, 2)');
});

test('Vector2', () => {
    const v = Vector2(3, 4);
    assert.equal(v.magnitude, 5);
    near(Vector2.Cross(Vector2.right, Vector2.up), 1);
    near(Vector2.Distance(v, Vector2.zero), 5);
    const p = Vector2.Perpendicular(Vector2.right);
    assert.ok(p.x === -0 || p.x === 0); assert.equal(p.y, 1);
});

test('Mathf keeps the engine edge cases', () => {
    assert.equal(Mathf.Round(2.5), 3);
    assert.equal(Mathf.Round(-2.5), -3);   // half away from zero (std::round), not JavaScript's Math.round
    assert.equal(Mathf.Sign(0), 1);
    assert.equal(Mathf.Sign(-0.1), -1);
    assert.equal(Mathf.Sqrt(-4), 0);
    assert.equal(Mathf.Lerp(0, 10, 1.5), 10);
    assert.equal(Mathf.Clamp(5, 0, 3), 3);
    assert.equal(Mathf.Clamp01(-1), 0);
    assert.equal(Mathf.MoveTowards(0, 10, 4), 4);
    assert.equal(Mathf.MoveTowards(9, 10, 4), 10);
    near(Mathf.PI, Math.PI);
});

test('Quaternion.Euler turns in the engine order (Z, then X, then Y) and eulerAngles reads it back', () => {
    vnear(Quaternion.Euler(0, 90, 0).multiplyVector(Vector3.forward), Vector3.right);
    vnear(Quaternion.Euler(90, 0, 0).multiplyVector(Vector3.forward), Vector3.down);
    vnear(Quaternion.Euler(0, 0, 90).multiplyVector(Vector3.right), Vector3.up);
    const q = Quaternion.Euler(30, 40, 50);
    const byParts = Quaternion.AngleAxis(40, Vector3.up).multiply(Quaternion.AngleAxis(30, Vector3.right))
        .multiply(Quaternion.AngleAxis(50, Vector3.forward));
    near(Math.abs(Quaternion.Dot(q, byParts)), 1);
    vnear(q.eulerAngles, new Vector3(30, 40, 50), 1e-4);
    vnear(Quaternion.Euler(new Vector3(-20, 170, 5)).eulerAngles, new Vector3(-20, 170, 5), 1e-4);
    const lock = Quaternion.Euler(90, 30, 0).eulerAngles;   // straight down: the turn goes into the yaw
    near(lock.x, 90, 1e-3); near(lock.y, 30, 1e-3);
});

test('Quaternion maths agrees with its matrix', () => {
    for (let i = 0; i < 50; ++i) {
        const q = Quaternion.Euler(rnd() * 360 - 180, rnd() * 360 - 180, rnd() * 360 - 180), v = rv();
        vnear(q.multiplyVector(v), Matrix4.Rotate(q).multiplyVector(v), 1e-4);
        vnear(q.inverse().multiplyVector(q.multiplyVector(v)), v, 1e-4);
        const r = Quaternion.Euler(rnd() * 90, rnd() * 90, 0);
        vnear(q.multiply(r).multiplyVector(v), q.multiplyVector(r.multiplyVector(v)), 1e-4);   // r first, then q
    }
    near(Quaternion.Angle(Quaternion.identity, Quaternion.Euler(0, 90, 0)), 90, 1e-4);
    const half = Quaternion.Slerp(Quaternion.identity, Quaternion.Euler(0, 90, 0), 0.5);
    vnear(half.multiplyVector(Vector3.forward), new Vector3(Math.SQRT1_2, 0, Math.SQRT1_2));
    near(Quaternion.Angle(Quaternion.Slerp(Quaternion.identity, Quaternion.Euler(0, 90, 0), 7), Quaternion.Euler(0, 90, 0)), 0, 1e-3);
    const nl = Quaternion.Lerp(Quaternion.identity, Quaternion.Euler(0, 60, 0), 0.5);
    near(nl.magnitude, 1);
});

test('LookRotation points +Z along the direction and keeps up', () => {
    for (let i = 0; i < 30; ++i) {
        const d = rv();
        const q = Quaternion.LookRotation(d);
        vnear(q.multiplyVector(Vector3.forward), d.normalized, 1e-4);
        assert.ok(q.multiplyVector(Vector3.up).y >= -1e-6);   // up stays on the upper side
        near(Vector3.Dot(q.multiplyVector(Vector3.right), Vector3.up), 0, 1e-4);   // no roll
    }
    vnear(Quaternion.LookRotation(Vector3.up).multiplyVector(Vector3.forward), Vector3.up, 1e-5);   // along the up hint
    assert.ok(Quaternion.LookRotation(Vector3.zero).equals(Quaternion.identity));
});

test('Matrix4 inverse, determinant, TRS and decompose', () => {
    const I = Matrix4.identity;
    for (let n = 0; n < 50; ++n) {
        const m = new Matrix4(Array.from({ length: 16 }, () => rnd() * 4 - 2));
        const p = m.multiply(m.inverse()).elements;
        for (let k = 0; k < 16; ++k) near(p[k], I.elements[k], 2e-3, 'M * M^-1 at ' + k);
    }
    const t = new Vector3(1, -2, 3), q = Quaternion.Euler(10, 20, 30), s = new Vector3(2, 3, 0.5);
    const trs = Matrix4.TRS(t, q, s);
    near(trs.determinant(), 3, 1e-4);
    const viaParts = Matrix4.Translate(t).multiply(Matrix4.Rotate(q)).multiply(Matrix4.Scale(s));
    for (let k = 0; k < 16; ++k) near(trs.elements[k], viaParts.elements[k], 1e-5);
    const d = trs.decompose();
    vnear(d.position, t); vnear(d.scale, s, 1e-4);
    near(Math.abs(Quaternion.Dot(d.rotation, q)), 1, 1e-5);
    vnear(trs.multiplyPoint(Vector3.zero), t);
    vnear(trs.inverse().multiplyPoint(trs.multiplyPoint(new Vector3(4, 5, 6))), new Vector3(4, 5, 6), 1e-4);
    vnear(Matrix4.Translate(t).multiplyVector(Vector3.one), Vector3.one);   // directions ignore the translation
    assert.ok(new Matrix4(new Float32Array(16)).inverse().elements.every((v, k) => v === I.elements[k]));   // singular -> identity
    const tiny = Matrix4.Scale(new Vector3(1e-4, 1e-4, 1e-4));   // small but well formed: still inverts
    near(tiny.inverse().elements[0], 1e4, 1);
    const tr = trs.transpose();
    assert.equal(tr.get(0, 3), trs.get(3, 0));
});

test('projection and view matrices (clip z from -1 at near to +1 at far, cameras look down -Z)', () => {
    const P = Matrix4.Perspective(60, 16 / 9, 0.5, 100);
    near(P.multiplyPoint(new Vector3(0, 0, -0.5)).z, -1, 1e-4);
    near(P.multiplyPoint(new Vector3(0, 0, -100)).z, 1, 1e-4);
    const edge = Math.tan(30 * Math.PI / 180) * 10;   // the top edge of the view at 10 units
    near(P.multiplyPoint(new Vector3(0, edge, -10)).y, 1, 1e-4);
    const O = Matrix4.Ortho(-4, 4, -3, 3, 1, 11);
    vnear(O.multiplyPoint(new Vector3(4, 3, -11)), new Vector3(1, 1, 1));
    vnear(O.multiplyPoint(new Vector3(-4, -3, -1)), new Vector3(-1, -1, -1));
    const V = Matrix4.LookAt(new Vector3(0, 0, 5), Vector3.zero, Vector3.up);
    vnear(V.multiplyPoint(Vector3.zero), new Vector3(0, 0, -5));
    vnear(V.multiplyPoint(new Vector3(1, 0, 0)), new Vector3(1, 0, -5));
    // a camera object at (0, 0, 5) with no turn: its view (the inverse of its matrix) is the same LookAt
    const cam = Matrix4.TRS(new Vector3(0, 0, 5), Quaternion.identity, Vector3.one).inverse();
    for (let k = 0; k < 16; ++k) near(cam.elements[k], V.elements[k], 1e-6);
    const down = Matrix4.LookAt(new Vector3(0, 10, 0), Vector3.zero, Vector3.up);   // straight down: no broken basis
    assert.ok(down.elements.every(Number.isFinite));
    vnear(down.multiplyPoint(Vector3.zero), new Vector3(0, 0, -10), 1e-5);
});

test('Color', () => {
    const c = Color.Parse('#ff8000');
    near(c.r, 1); near(c.g, 128 / 255); near(c.b, 0); near(c.a, 1);
    near(Color.Parse('#00ff0088').a, 0x88 / 255);
    assert.ok(Color.Parse('0.5, 0.25, 1').equals(new Color(0.5, 0.25, 1, 1)));
    assert.ok(Color.Parse('nope', Color.red).equals(Color.red));
    near(Color.Lerp(Color.black, Color.white, 0.25).g, 0.25);
    near(Color.white.luminance, 1);
    near(Color.FromBytes(255, 0, 0, 51).a, 0.2);
});

/** Every triangle faces away from the middle (closed shapes) and agrees with the stored normals. */
function checkWinding(mesh, outward) {
    const p = mesh.positions, nr = mesh.normals, ix = mesh.indices;
    let bad = 0;
    for (let t = 0; t < ix.length; t += 3) {
        const [a, b, c] = [ix[t], ix[t + 1], ix[t + 2]].map(i => new Vector3(p[i * 3], p[i * 3 + 1], p[i * 3 + 2]));
        const n = Vector3.Cross(b.sub(a), c.sub(a));
        if (n.magnitude < 1e-9) { ++bad; continue; }   // no flat (zero-area) triangles
        const mid = a.add(b).add(c).div(3);
        const stored = new Vector3(nr[ix[t] * 3], nr[ix[t] * 3 + 1], nr[ix[t] * 3 + 2]);
        if (Vector3.Dot(n, outward(mid)) <= 0 || Vector3.Dot(n, stored) <= 0) ++bad;
    }
    return bad;
}

test('Box, Sphere and Plane wind counter-clockwise seen from outside', () => {
    const box = Mesh.Box(2, 1, 3);
    assert.equal(box.vertexCount, 24); assert.equal(box.triangleCount, 12);
    assert.equal(checkWinding(box, m => m), 0);
    assert.deepEqual(box.bounds, { min: [-1, -0.5, -1.5], max: [1, 0.5, 1.5] });
    const sphere = Mesh.Sphere(2, 24, 12);
    assert.equal(checkWinding(sphere, m => m), 0);
    assert.equal(sphere.triangleCount, 24 * 12 * 2 - 2 * 24);
    for (let i = 0; i < sphere.positions.length; i += 3)
        near(Math.hypot(sphere.positions[i], sphere.positions[i + 1], sphere.positions[i + 2]), 2, 1e-5);
    const plane = Mesh.Plane(4, 2, 3);
    assert.equal(plane.vertexCount, 16); assert.equal(plane.triangleCount, 18);
    assert.equal(checkWinding(plane, () => Vector3.up), 0);
    assert.equal(checkWinding(Mesh.Quad(), () => Vector3.forward), 0);
});

test('Mesh from arrays works out normals and refuses bad input', () => {
    const m = Mesh.FromArrays({ positions: [0, 0, 0, 1, 0, 0, 0, 1, 0] });
    assert.deepEqual(Array.from(m.indices), [0, 1, 2]);
    vnear(new Vector3(m.normals[0], m.normals[1], m.normals[2]), Vector3.forward);
    assert.throws(() => Mesh.FromArrays({ positions: [0, 0] }), /3 numbers/);
    assert.throws(() => Mesh.FromArrays({ positions: [0, 0, 0, 1, 0, 0, 0, 1, 0], indices: [0, 1, 3] }), /past the last vertex/);
    assert.throws(() => Mesh.FromArrays({ positions: [0, 0, 0, 1, 0, 0, 0, 1, 0], indices: [0, 1] }), /threes/);
    assert.throws(() => Mesh.FromArrays({ positions: [0, 0, 0, 1, 0, 0, 0, 1, 0], uvs: [0, 0] }), /different vertex counts/);
    const v = m.version; m.MarkChanged(); assert.equal(m.version, v + 1);
});
