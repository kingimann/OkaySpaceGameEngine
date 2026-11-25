// okay.js renderer: draws a Scene with WebGL 2, or WebGL 1 when the browser has no WebGL 2 (or when asked).
//
// Written from the WebGL 2.0 and WebGL 1.0 specifications (Khronos) and the GLSL ES 3.00 / 1.00 language
// specifications; the shading is the textbook Lambert diffuse term plus a Blinn-Phong highlight. One shader text
// serves both versions: a short header maps its words (ATTR, VARY, TEX, FRAG_COLOR) onto each version's keywords.

import { Mesh } from './geometry.js';
import { Camera, Light, MeshRenderer, SpriteRenderer } from './scene.js';
import { Matrix4, Vector3 } from './math.js';

const MAX_LIGHTS = 4;
const A_POSITION = 0, A_NORMAL = 1, A_UV = 2;

const VERTEX = `
ATTR vec3 aPosition;
ATTR vec3 aNormal;
ATTR vec2 aUv;
uniform mat4 uModel;
uniform mat4 uViewProj;
uniform mat3 uNormalMatrix;
uniform vec4 uUvRect;
VARY vec3 vWorldPos;
VARY vec3 vNormal;
VARY vec2 vUv;
void main() {
    vec4 world = uModel * vec4(aPosition, 1.0);
    vWorldPos = world.xyz;
    vNormal = uNormalMatrix * aNormal;
    vUv = uUvRect.xy + aUv * uUvRect.zw;
    gl_Position = uViewProj * world;
}`;

const FRAGMENT = `
uniform vec4 uColor;
uniform sampler2D uTexture;
uniform float uTextured;
VARY vec3 vWorldPos;
VARY vec3 vNormal;
VARY vec2 vUv;
#ifdef LIT
uniform vec3 uAmbient;
uniform vec3 uCameraPos;
uniform vec3 uSpecular;
uniform float uShininess;
uniform float uLightCount;
uniform vec3 uLightDir[${MAX_LIGHTS}];
uniform vec3 uLightColor[${MAX_LIGHTS}];
#endif
void main() {
    vec4 base = uColor;
    if (uTextured > 0.5) base *= TEX(uTexture, vUv);
#ifdef LIT
    vec3 n = normalize(vNormal);
    if (!gl_FrontFacing) n = -n;
    vec3 toEye = normalize(uCameraPos - vWorldPos);
    vec3 diffuse = uAmbient;
    vec3 highlight = vec3(0.0);
    for (int i = 0; i < ${MAX_LIGHTS}; ++i) {
        if (float(i) >= uLightCount) break;
        vec3 toLight = -uLightDir[i];
        float lambert = max(dot(n, toLight), 0.0);
        diffuse += uLightColor[i] * lambert;
        if (lambert > 0.0) {
            vec3 halfway = normalize(toLight + toEye);
            highlight += uLightColor[i] * pow(max(dot(n, halfway), 0.0), uShininess);
        }
    }
    FRAG_COLOR = vec4(base.rgb * diffuse + uSpecular * highlight, base.a);
#else
    FRAG_COLOR = base;
#endif
}`;

function header(version2, fragment, defines) {
    const d = defines.map(n => '#define ' + n + '\n').join('');
    if (version2) {
        return '#version 300 es\n' + d + (fragment
            ? 'precision highp float;\n#define VARY in\n#define TEX texture\nout vec4 okayFragColor;\n#define FRAG_COLOR okayFragColor\n'
            : '#define ATTR in\n#define VARY out\n');
    }
    return d + (fragment
        ? '#ifdef GL_FRAGMENT_PRECISION_HIGH\nprecision highp float;\n#else\nprecision mediump float;\n#endif\n' +
          '#define VARY varying\n#define TEX texture2D\n#define FRAG_COLOR gl_FragColor\n'
        : '#define ATTR attribute\n#define VARY varying\n');
}

/** The 3x3 inverse transpose of a model matrix's upper-left part (how normals move), column-major. */
function normalMatrix(e, out) {
    const a = e[0], b = e[4], c = e[8], d = e[1], f = e[5], g = e[9], h = e[2], i = e[6], j = e[10];
    // cofactors of the row-major matrix [a b c; d f g; h i j]
    const A = f * j - g * i, B = -(d * j - g * h), C = d * i - f * h;
    const D = -(b * j - c * i), E = a * j - c * h, F = -(a * i - b * h);
    const G = b * g - c * f, H = -(a * g - c * d), I = a * f - b * d;
    const det = a * A + b * B + c * C, k = Math.abs(det) > 1e-20 ? 1 / det : 0;
    // inverse transpose = cofactor matrix / det; stored column-major
    out[0] = A * k; out[1] = D * k; out[2] = G * k;
    out[3] = B * k; out[4] = E * k; out[5] = H * k;
    out[6] = C * k; out[7] = F * k; out[8] = I * k;
    return out;
}

/** Draws scenes into a canvas. Options: webgl 'auto' (2 then 1), 1 or 2; pixelRatio (default devicePixelRatio);
 *  autoResize (default true: follow the canvas's CSS size each frame); antialias; preserveDrawingBuffer. */
export class Renderer {
    constructor(canvas, { webgl = 'auto', pixelRatio, autoResize = true, antialias = true, preserveDrawingBuffer = false,
                          alpha = false } = {}) {
        this.canvas = canvas;
        this.pixelRatio = pixelRatio || (typeof devicePixelRatio === 'number' ? devicePixelRatio : 1);
        this.autoResize = autoResize;
        const attrs = { antialias, preserveDrawingBuffer, alpha, depth: true, premultipliedAlpha: false };
        this.gl = null;
        if (webgl === 'auto' || webgl === 2) { this.gl = canvas.getContext('webgl2', attrs); this.webglVersion = 2; }
        if (!this.gl && (webgl === 'auto' || webgl === 1)) {
            this.gl = canvas.getContext('webgl', attrs) || canvas.getContext('experimental-webgl', attrs);
            this.webglVersion = 1;
        }
        if (!this.gl) throw new Error(webgl === 'auto' ? 'This browser has no WebGL, so okay.js cannot draw'
                                                      : 'This browser has no WebGL ' + webgl);
        this.stats = { drawCalls: 0, triangles: 0, frame: 0 };
        this.contextLost = false;
        canvas.addEventListener('webglcontextlost', e => { e.preventDefault(); this.contextLost = true; });
        canvas.addEventListener('webglcontextrestored', () => { this.contextLost = false; this.#init(); });
        this.#init();
        this.width = 0; this.height = 0;
        this.resize();
    }

    #init() {
        const gl = this.gl;
        this.uintIndices = this.webglVersion === 2 || !!gl.getExtension('OES_element_index_uint');
        this.programs = new Map();
        this.meshes = new WeakMap();
        this.textures = new WeakMap();
        this.white = gl.createTexture();
        gl.bindTexture(gl.TEXTURE_2D, this.white);
        gl.texImage2D(gl.TEXTURE_2D, 0, gl.RGBA, 1, 1, 0, gl.RGBA, gl.UNSIGNED_BYTE, new Uint8Array([255, 255, 255, 255]));
        this.quad = Mesh.Quad();
        gl.enable(gl.DEPTH_TEST);
        gl.depthFunc(gl.LEQUAL);
        gl.frontFace(gl.CCW);
        gl.cullFace(gl.BACK);
        gl.blendFunc(gl.SRC_ALPHA, gl.ONE_MINUS_SRC_ALPHA);
    }

    /** Set the drawing size in CSS pixels; the canvas gets width x pixelRatio device pixels. */
    setSize(width, height, updateStyle = false) {
        const w = Math.max(1, Math.round(width * this.pixelRatio)), h = Math.max(1, Math.round(height * this.pixelRatio));
        if (updateStyle) { this.canvas.style.width = width + 'px'; this.canvas.style.height = height + 'px'; }
        this.width = width; this.height = height;
        if (this.canvas.width !== w || this.canvas.height !== h) { this.canvas.width = w; this.canvas.height = h; }
    }
    setPixelRatio(r) { this.pixelRatio = r; this.setSize(this.width, this.height); }
    /** Follow the canvas's CSS size (what autoResize does every frame). True when the size changed. */
    resize() {
        const c = this.canvas, w = c.clientWidth || c.width / this.pixelRatio, h = c.clientHeight || c.height / this.pixelRatio;
        const pw = Math.max(1, Math.round(w * this.pixelRatio)), ph = Math.max(1, Math.round(h * this.pixelRatio));
        if (pw === c.width && ph === c.height && w === this.width && h === this.height) return false;
        this.setSize(w, h);
        return true;
    }

    // ---- GPU objects
    #program(kind) {
        let p = this.programs.get(kind);
        if (p) return p;
        const gl = this.gl, v2 = this.webglVersion === 2, defs = kind === 'lit' ? ['LIT'] : [];
        const compile = (type, src) => {
            const s = gl.createShader(type);
            gl.shaderSource(s, src);
            gl.compileShader(s);
            if (!gl.getShaderParameter(s, gl.COMPILE_STATUS) && !gl.isContextLost())
                throw new Error('okay.js shader did not compile: ' + gl.getShaderInfoLog(s));
            return s;
        };
        const prog = gl.createProgram();
        gl.attachShader(prog, compile(gl.VERTEX_SHADER, header(v2, false, defs) + VERTEX));
        gl.attachShader(prog, compile(gl.FRAGMENT_SHADER, header(v2, true, defs) + FRAGMENT));
        gl.bindAttribLocation(prog, A_POSITION, 'aPosition');
        gl.bindAttribLocation(prog, A_NORMAL, 'aNormal');
        gl.bindAttribLocation(prog, A_UV, 'aUv');
        gl.linkProgram(prog);
        if (!gl.getProgramParameter(prog, gl.LINK_STATUS) && !gl.isContextLost())
            throw new Error('okay.js shader did not link: ' + gl.getProgramInfoLog(prog));
        const u = {};
        for (const n of ['uModel', 'uViewProj', 'uNormalMatrix', 'uUvRect', 'uColor', 'uTexture', 'uTextured', 'uAmbient',
                         'uCameraPos', 'uSpecular', 'uShininess', 'uLightCount', 'uLightDir', 'uLightColor'])
            u[n] = gl.getUniformLocation(prog, n);
        p = { prog, u };
        this.programs.set(kind, p);
        return p;
    }

    #meshBuffers(mesh) {
        const gl = this.gl;
        let b = this.meshes.get(mesh);
        if (b && b.version === mesh.version) return b;
        if (!b) b = { pos: gl.createBuffer(), nrm: gl.createBuffer(), uv: gl.createBuffer(), idx: gl.createBuffer() };
        const upload = (buf, data) => { gl.bindBuffer(gl.ARRAY_BUFFER, buf); gl.bufferData(gl.ARRAY_BUFFER, data, gl.STATIC_DRAW); };
        upload(b.pos, mesh.positions); upload(b.nrm, mesh.normals); upload(b.uv, mesh.uvs);
        const big = mesh.vertexCount > 65535;
        if (big && !this.uintIndices) throw new Error('This mesh has more than 65535 vertices, which this WebGL 1 can not draw');
        gl.bindBuffer(gl.ELEMENT_ARRAY_BUFFER, b.idx);
        gl.bufferData(gl.ELEMENT_ARRAY_BUFFER, big ? mesh.indices : Uint16Array.from(mesh.indices), gl.STATIC_DRAW);
        b.type = big ? gl.UNSIGNED_INT : gl.UNSIGNED_SHORT;
        b.count = mesh.indices.length;
        b.version = mesh.version;
        this.meshes.set(mesh, b);
        return b;
    }

    #texture(tex) {
        const gl = this.gl;
        if (!tex || !tex.source) return this.white;
        let t = this.textures.get(tex);
        if (t && t.version === tex.version) return t.handle;
        const src = tex.source;
        // an <img> that has not loaded yet: draw plain, and make nothing until it can be drawn ('complete' rather than
        // instanceof HTMLImageElement, which does not exist in a worker)
        if ('complete' in src && !src.complete) return t ? t.handle : this.white;
        if (!t) { t = { handle: gl.createTexture() }; this.textures.set(tex, t); }
        gl.bindTexture(gl.TEXTURE_2D, t.handle);
        gl.pixelStorei(gl.UNPACK_FLIP_Y_WEBGL, true);   // picture rows run top-down; uv (0, 0) is the bottom-left
        const w = tex.width, h = tex.height;
        if (src.data && !(typeof ImageData !== 'undefined' && src instanceof ImageData))
            gl.texImage2D(gl.TEXTURE_2D, 0, gl.RGBA, w, h, 0, gl.RGBA, gl.UNSIGNED_BYTE, src.data);
        else gl.texImage2D(gl.TEXTURE_2D, 0, gl.RGBA, gl.RGBA, gl.UNSIGNED_BYTE, src);
        gl.pixelStorei(gl.UNPACK_FLIP_Y_WEBGL, false);
        const pow2 = (w & (w - 1)) === 0 && (h & (h - 1)) === 0;
        const full = this.webglVersion === 2 || pow2;   // WebGL 1: no repeat or mipmaps on other sizes
        const near = tex.filter === 'nearest';
        const mips = tex.mipmaps && full;
        if (mips) gl.generateMipmap(gl.TEXTURE_2D);
        gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MAG_FILTER, near ? gl.NEAREST : gl.LINEAR);
        gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MIN_FILTER,
                         mips ? (near ? gl.NEAREST_MIPMAP_NEAREST : gl.LINEAR_MIPMAP_LINEAR) : (near ? gl.NEAREST : gl.LINEAR));
        const wrap = tex.wrap === 'repeat' && full ? gl.REPEAT : gl.CLAMP_TO_EDGE;
        gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_S, wrap);
        gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_T, wrap);
        t.version = tex.version;
        this.textures.set(tex, t);
        return t.handle;
    }

    // ---- drawing
    /** Draw the scene: through `camera` (a Camera component or its GameObject), or through every enabled camera in
     *  the scene in order of depth. */
    render(scene, camera = null) {
        if (this.contextLost) return;
        if (this.autoResize) this.resize();
        const gl = this.gl;
        this.stats.drawCalls = 0; this.stats.triangles = 0; this.stats.frame++;
        if (camera && !(camera instanceof Camera)) camera = camera.GetComponent(Camera);
        const cams = camera ? [camera] : scene.FindComponents(Camera).filter(c => c.enabled && c.gameObject.activeInHierarchy)
            .sort((a, b) => a.depth - b.depth);
        // world matrices once per frame, parents first
        const world = new Map();
        const worldOf = t => {
            let m = world.get(t);
            if (!m) {
                const local = Matrix4.TRS(t._lp, t._lr, t._ls);
                m = t._parent ? worldOf(t._parent).multiply(local) : local;
                world.set(t, m);
            }
            return m;
        };
        if (!cams.length) {
            gl.viewport(0, 0, this.canvas.width, this.canvas.height);
            gl.clearColor(0, 0, 0, 1);
            gl.clear(gl.COLOR_BUFFER_BIT | gl.DEPTH_BUFFER_BIT);
            return;
        }
        const lights = scene.FindComponents(Light).filter(l => l.enabled && l.gameObject.activeInHierarchy);
        const ambient = [0, 0, 0], dirs = new Float32Array(MAX_LIGHTS * 3), colors = new Float32Array(MAX_LIGHTS * 3);
        let count = 0;
        for (const l of lights) {
            ambient[0] += l.ambient * l.ambientColor.r * l.color.r;
            ambient[1] += l.ambient * l.ambientColor.g * l.color.g;
            ambient[2] += l.ambient * l.ambientColor.b * l.color.b;
            if (l.type !== 'directional' || count >= MAX_LIGHTS) continue;
            const e = worldOf(l.gameObject.transform).elements;
            const d = new Vector3(e[8], e[9], e[10]).normalized;   // the light's +Z (forward) in the world
            dirs.set([d.x, d.y, d.z], count * 3);
            colors.set([l.color.r * l.intensity, l.color.g * l.intensity, l.color.b * l.intensity], count * 3);
            ++count;
        }
        const items = [];
        for (const r of scene.FindComponents(MeshRenderer))
            if (r.enabled && r.mesh && r.material && r.gameObject.activeInHierarchy) items.push(r);
        for (const s of scene.FindComponents(SpriteRenderer))
            if (s.enabled && s.gameObject.activeInHierarchy) items.push(s);

        for (const cam of cams) {
            const W = this.canvas.width, H = this.canvas.height, rc = cam.rect;
            const vx = Math.round(rc.x * W), vy = Math.round(rc.y * H), vw = Math.max(1, Math.round(rc.w * W)), vh = Math.max(1, Math.round(rc.h * H));
            gl.viewport(vx, vy, vw, vh);
            if (cam.clearFlags !== 'dontClear') {
                gl.enable(gl.SCISSOR_TEST);
                gl.scissor(vx, vy, vw, vh);
                const bg = cam.backgroundColor;
                gl.clearColor(bg.r, bg.g, bg.b, bg.a);
                gl.depthMask(true);
                gl.clear((cam.clearFlags === 'depthOnly' ? 0 : gl.COLOR_BUFFER_BIT) | gl.DEPTH_BUFFER_BIT);
                gl.disable(gl.SCISSOR_TEST);
            }
            const camWorld = worldOf(cam.gameObject.transform);
            const view = camWorld.inverse();
            const viewProj = cam.ProjectionMatrix(vw / vh).multiply(view);
            const eye = [camWorld.elements[12], camWorld.elements[13], camWorld.elements[14]];
            // opaque first (front to back), then see-through and sprites (back to front, sprites by sortOrder first)
            const depthOf = r => { const e = worldOf(r.gameObject.transform).elements, v = view.elements;
                                   return v[2] * e[12] + v[6] * e[13] + v[10] * e[14] + v[14]; };
            const isClear = r => r instanceof SpriteRenderer || r.material.transparent || r.material.color.a < 1;
            const seen = items.filter(r => (cam.cullingMask >>> (r.gameObject.layer & 31)) & 1);
            const opaque = seen.filter(r => !isClear(r)), clear = seen.filter(isClear);
            const keyed = list => list.map(r => ({ r, z: depthOf(r) }));
            const front = keyed(opaque).sort((a, b) => b.z - a.z), back = keyed(clear).sort((a, b) =>
                ((a.r.sortOrder || 0) - (b.r.sortOrder || 0)) || (a.z - b.z));
            gl.disable(gl.BLEND); gl.depthMask(true);
            for (const { r } of front) this.#draw(r, worldOf, viewProj, eye, ambient, dirs, colors, count);
            gl.enable(gl.BLEND); gl.depthMask(false);
            for (const { r } of back) this.#draw(r, worldOf, viewProj, eye, ambient, dirs, colors, count);
            gl.depthMask(true); gl.disable(gl.BLEND);
        }
    }

    #draw(r, worldOf, viewProj, eye, ambient, dirs, colors, lightCount) {
        const gl = this.gl;
        const sprite = r instanceof SpriteRenderer;
        const mat = sprite ? null : r.material;
        const kind = sprite ? 'unlit' : mat.kind;
        const { prog, u } = this.#program(kind);
        gl.useProgram(prog);
        let model = worldOf(r.gameObject.transform);
        if (sprite) model = model.multiply(Matrix4.Scale(new Vector3(r.size.x, r.size.y, 1)));
        const mesh = sprite ? this.quad : r.mesh;
        const b = this.#meshBuffers(mesh);
        gl.uniformMatrix4fv(u.uModel, false, model.elements);
        gl.uniformMatrix4fv(u.uViewProj, false, viewProj.elements);
        gl.uniformMatrix3fv(u.uNormalMatrix, false, normalMatrix(model.elements, new Float32Array(9)));
        if (sprite) gl.uniform4f(u.uUvRect, r.flipX ? 1 : 0, r.flipY ? 1 : 0, r.flipX ? -1 : 1, r.flipY ? -1 : 1);
        else gl.uniform4f(u.uUvRect, 0, 0, 1, 1);
        const c = sprite ? r.color : mat.color;
        gl.uniform4f(u.uColor, c.r, c.g, c.b, c.a);
        const tex = sprite ? r.texture : mat.texture;
        gl.activeTexture(gl.TEXTURE0);
        gl.bindTexture(gl.TEXTURE_2D, this.#texture(tex));
        gl.uniform1i(u.uTexture, 0);
        gl.uniform1f(u.uTextured, tex && tex.source ? 1 : 0);
        if (kind === 'lit') {
            gl.uniform3f(u.uAmbient, ambient[0], ambient[1], ambient[2]);
            gl.uniform3f(u.uCameraPos, eye[0], eye[1], eye[2]);
            gl.uniform3f(u.uSpecular, mat.specular.r, mat.specular.g, mat.specular.b);
            gl.uniform1f(u.uShininess, Math.max(1, mat.shininess));
            gl.uniform1f(u.uLightCount, lightCount);
            gl.uniform3fv(u.uLightDir, dirs);
            gl.uniform3fv(u.uLightColor, colors);
        }
        if (sprite || mat.doubleSided) gl.disable(gl.CULL_FACE); else gl.enable(gl.CULL_FACE);
        const attr = (loc, buf, size) => {
            gl.bindBuffer(gl.ARRAY_BUFFER, buf);
            gl.enableVertexAttribArray(loc);
            gl.vertexAttribPointer(loc, size, gl.FLOAT, false, 0, 0);
        };
        attr(A_POSITION, b.pos, 3); attr(A_NORMAL, b.nrm, 3); attr(A_UV, b.uv, 2);
        gl.bindBuffer(gl.ELEMENT_ARRAY_BUFFER, b.idx);
        gl.drawElements(gl.TRIANGLES, b.count, b.type, 0);
        this.stats.drawCalls++;
        this.stats.triangles += b.count / 3;
    }

    /** The colour of one device pixel of the last frame (x, y from the top-left), as bytes [r, g, b, a]. Needs
     *  preserveDrawingBuffer, or a call in the same task as render(). */
    readPixel(x, y) {
        const gl = this.gl, out = new Uint8Array(4);
        gl.readPixels(x, this.canvas.height - 1 - y, 1, 1, gl.RGBA, gl.UNSIGNED_BYTE, out);
        return Array.from(out);
    }
}
