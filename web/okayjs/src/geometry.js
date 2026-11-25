// okay.js meshes: vertex arrays (positions, normals, uvs) and triangle indices, with the shapes games start from.
// Our own generators from basic geometry; front faces wind counter-clockwise seen from outside (WebGL's default
// front face), which tests/okayjs/test_okayjs_math.mjs checks for every shape.

/** A triangle mesh. Arrays are flat: 3 numbers per position and normal, 2 per uv. Call MarkChanged after editing
 *  the arrays in place so the renderer uploads them again. */
export class Mesh {
    constructor({ positions, normals = null, uvs = null, indices = null, name = 'Mesh' } = {}) {
        if (!positions || positions.length % 3 !== 0) throw new TypeError('Mesh needs positions (3 numbers per vertex)');
        this.name = name;
        this.positions = Float32Array.from(positions);
        const n = this.positions.length / 3;
        this.indices = indices ? Uint32Array.from(indices) : Uint32Array.from({ length: n }, (_, i) => i);
        for (const i of this.indices) if (i >= n) throw new RangeError('Mesh index ' + i + ' is past the last vertex (' + (n - 1) + ')');
        if (this.indices.length % 3 !== 0) throw new TypeError('Mesh indices must come in threes (triangles)');
        this.normals = normals ? Float32Array.from(normals) : computeNormals(this.positions, this.indices);
        this.uvs = uvs ? Float32Array.from(uvs) : new Float32Array(n * 2);
        if (this.normals.length !== n * 3 || this.uvs.length !== n * 2) throw new TypeError('Mesh arrays have different vertex counts');
        this.version = 0;
    }
    get vertexCount() { return this.positions.length / 3; }
    get triangleCount() { return this.indices.length / 3; }
    MarkChanged() { this.version++; }
    /** Smallest and largest corner of the positions. */
    get bounds() {
        const p = this.positions, min = [Infinity, Infinity, Infinity], max = [-Infinity, -Infinity, -Infinity];
        for (let i = 0; i < p.length; ++i) { const k = i % 3; if (p[i] < min[k]) min[k] = p[i]; if (p[i] > max[k]) max[k] = p[i]; }
        return { min, max };
    }

    /** A mesh from plain arrays; normals are worked out from the faces when left out. */
    static FromArrays(arrays) { return new Mesh(arrays); }

    /** A box centred on the origin, 24 vertices so each face has its own flat normal. */
    static Box(width = 1, height = 1, depth = 1) {
        const hx = width / 2, hy = height / 2, hz = depth / 2;
        // [normal, u axis, v axis] with u x v = normal, so the corners below run counter-clockwise seen from outside
        const faces = [[[1, 0, 0], [0, 0, -1], [0, 1, 0]], [[-1, 0, 0], [0, 0, 1], [0, 1, 0]],
                       [[0, 1, 0], [1, 0, 0], [0, 0, -1]], [[0, -1, 0], [1, 0, 0], [0, 0, 1]],
                       [[0, 0, 1], [1, 0, 0], [0, 1, 0]], [[0, 0, -1], [-1, 0, 0], [0, 1, 0]]];
        const positions = [], normals = [], uvs = [], indices = [];
        for (const [n, u, v] of faces) {
            const base = positions.length / 3;
            for (const [su, sv] of [[-1, -1], [1, -1], [1, 1], [-1, 1]]) {
                positions.push((n[0] + u[0] * su + v[0] * sv) * hx, (n[1] + u[1] * su + v[1] * sv) * hy, (n[2] + u[2] * su + v[2] * sv) * hz);
                normals.push(...n);
                uvs.push((su + 1) / 2, (sv + 1) / 2);
            }
            indices.push(base, base + 1, base + 2, base, base + 2, base + 3);
        }
        return new Mesh({ positions, normals, uvs, indices, name: 'Box' });
    }

    /** A UV sphere: `segments` around the equator, `rings` from pole to pole. */
    static Sphere(radius = 0.5, segments = 32, rings = 16) {
        segments = Math.max(3, segments | 0); rings = Math.max(2, rings | 0);
        const positions = [], normals = [], uvs = [], indices = [];
        for (let r = 0; r <= rings; ++r) {
            const theta = r / rings * Math.PI, st = Math.sin(theta), ct = Math.cos(theta);
            for (let s = 0; s <= segments; ++s) {
                const phi = s / segments * Math.PI * 2;
                const x = st * Math.sin(phi), y = ct, z = st * Math.cos(phi);
                positions.push(x * radius, y * radius, z * radius);
                normals.push(x, y, z);
                uvs.push(s / segments, 1 - r / rings);
            }
        }
        const row = segments + 1;
        for (let r = 0; r < rings; ++r) {
            for (let s = 0; s < segments; ++s) {
                const a = r * row + s, b = a + row;
                if (r !== 0) indices.push(a, b, a + 1);                 // the top ring has one triangle per segment
                if (r !== rings - 1) indices.push(a + 1, b, b + 1);    // and so has the bottom one
            }
        }
        return new Mesh({ positions, normals, uvs, indices, name: 'Sphere' });
    }

    /** A flat plane on X and Z facing +Y, split into `segments` x `segments` squares. */
    static Plane(width = 1, depth = 1, segments = 1) {
        segments = Math.max(1, segments | 0);
        const positions = [], normals = [], uvs = [], indices = [], row = segments + 1;
        for (let j = 0; j <= segments; ++j) {
            for (let i = 0; i <= segments; ++i) {
                const u = i / segments, v = j / segments;
                positions.push((u - 0.5) * width, 0, (0.5 - v) * depth);   // v runs toward -Z
                normals.push(0, 1, 0);
                uvs.push(u, v);
            }
        }
        for (let j = 0; j < segments; ++j)
            for (let i = 0; i < segments; ++i) {
                const a = j * row + i;
                indices.push(a, a + 1, a + row + 1, a, a + row + 1, a + row);
            }
        return new Mesh({ positions, normals, uvs, indices, name: 'Plane' });
    }

    /** A 1 x 1 square on X and Y facing +Z (what sprites are drawn with). */
    static Quad() {
        return new Mesh({ positions: [-0.5, -0.5, 0, 0.5, -0.5, 0, 0.5, 0.5, 0, -0.5, 0.5, 0], normals: [0, 0, 1, 0, 0, 1, 0, 0, 1, 0, 0, 1],
                          uvs: [0, 0, 1, 0, 1, 1, 0, 1], indices: [0, 1, 2, 0, 2, 3], name: 'Quad' });
    }
}

/** Area-weighted vertex normals from the triangles. */
function computeNormals(p, idx) {
    const out = new Float32Array(p.length);
    for (let t = 0; t < idx.length; t += 3) {
        const a = idx[t] * 3, b = idx[t + 1] * 3, c = idx[t + 2] * 3;
        const ux = p[b] - p[a], uy = p[b + 1] - p[a + 1], uz = p[b + 2] - p[a + 2];
        const vx = p[c] - p[a], vy = p[c + 1] - p[a + 1], vz = p[c + 2] - p[a + 2];
        const nx = uy * vz - uz * vy, ny = uz * vx - ux * vz, nz = ux * vy - uy * vx;
        for (const k of [a, b, c]) { out[k] += nx; out[k + 1] += ny; out[k + 2] += nz; }
    }
    for (let i = 0; i < out.length; i += 3) {
        const m = Math.hypot(out[i], out[i + 1], out[i + 2]);
        if (m > 0) { out[i] /= m; out[i + 1] /= m; out[i + 2] /= m; }
    }
    return out;
}
