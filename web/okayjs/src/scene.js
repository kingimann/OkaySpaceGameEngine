// okay.js scene graph: Scene, GameObject, Transform, Component and the components the renderer draws (Camera, Light,
// MeshRenderer, SpriteRenderer).
//
// Our own design, matched by name and behaviour to the engine's scene (engine/include/okay/Scene/Transform.hpp,
// Components/Camera.hpp, Light.hpp, MeshRenderer.hpp, SpriteRenderer.hpp) and to what the engine's JavaScript game
// scripts see (docs/scripting/javascript.md): transform values are copies (changing `transform.position.x` does not
// move the object; assign `transform.position = ...`), a destroyed object is an error, GetComponent takes a name.

import { Vector2, Vector3, Quaternion, Matrix4, Color } from './math.js';

const DESTROYED = "This object was destroyed, so it can't be used any more";

function alive(go) {
    if (go._destroyed) throw new Error(DESTROYED);
    return go;
}
function vecArgs(a, b, c, what) {
    if (a instanceof Vector3) return a;
    if (typeof a === 'number' && typeof b === 'number' && typeof c === 'number') return new Vector3(a, b, c);
    throw new TypeError(what + ' takes a Vector3 or three numbers (x, y, z)');
}

/** The base of every component: it belongs to one GameObject. */
export class Component {
    constructor() { this.gameObject = null; this.enabled = true; }
    /** The name GetComponent finds this component by (the class name unless a subclass says otherwise). */
    static get typeName() { return this.name; }
    get transform() { return this.gameObject ? alive(this.gameObject).transform : null; }
    toString() { return this.constructor.typeName + '(' + (this.gameObject ? this.gameObject.name : '') + ')'; }
}

/** Position, rotation and scale, and the object's place in the hierarchy. */
export class Transform extends Component {
    constructor() {
        super();
        this._lp = new Vector3(); this._lr = new Quaternion(); this._ls = new Vector3(1, 1, 1);
        this._parent = null; this._children = [];
    }
    static get typeName() { return 'Transform'; }

    // ---- local space (copies in and out)
    get localPosition() { return this._lp.clone(); }
    set localPosition(v) { this._lp = vecArgs(v, 0, 0, 'localPosition').clone(); }
    get localRotation() { return this._lr.clone(); }
    set localRotation(q) { this._lr = q.normalized; }
    get localScale() { return this._ls.clone(); }
    set localScale(v) { this._ls = vecArgs(v, 0, 0, 'localScale').clone(); }
    get localEulerAngles() { return this._lr.eulerAngles; }
    set localEulerAngles(e) { this._lr = Quaternion.Euler(e); }

    // ---- world space
    get localToWorldMatrix() {
        const local = Matrix4.TRS(this._lp, this._lr, this._ls);
        return this._parent ? this._parent.localToWorldMatrix.multiply(local) : local;
    }
    get worldToLocalMatrix() { return this.localToWorldMatrix.inverse(); }
    get position() {
        alive(this.gameObject);
        const e = this.localToWorldMatrix.elements;
        return new Vector3(e[12], e[13], e[14]);
    }
    set position(p) {
        alive(this.gameObject);
        p = vecArgs(p, 0, 0, 'position');
        this._lp = this._parent ? this._parent.localToWorldMatrix.inverse().multiplyPoint(p) : p.clone();
    }
    get rotation() { return this._parent ? this._parent.rotation.multiply(this._lr).normalized : this._lr.clone(); }
    set rotation(q) { this._lr = (this._parent ? this._parent.rotation.inverse().multiply(q) : q).normalized; }
    get eulerAngles() { return this.rotation.eulerAngles; }
    set eulerAngles(e) { this.rotation = Quaternion.Euler(e); }
    /** The world scale per axis (exact without turned, non-uniformly scaled parents). */
    get lossyScale() { return this._parent ? Vector3.Scale(this._parent.lossyScale, this._ls) : this._ls.clone(); }
    get forward() { return this.rotation.multiplyVector(Vector3.forward); }
    get right() { return this.rotation.multiplyVector(Vector3.right); }
    get up() { return this.rotation.multiplyVector(Vector3.up); }

    // ---- moving and turning (the engine's typed script table)
    /** Move by an offset in the parent's frame (localPosition += offset). */
    Translate(x, y, z) { alive(this.gameObject); this._lp = this._lp.add(vecArgs(x, y, z, 'Translate')); }
    /** Turn by X / Y / Z degrees around the object's own axes. */
    Rotate(x, y, z) {
        alive(this.gameObject);
        this._lr = this._lr.multiply(Quaternion.Euler(vecArgs(x, y, z, 'Rotate'))).normalized;
    }
    /** Turn so forward points at a world position (or at another Transform). */
    LookAt(target, up = Vector3.up) {
        alive(this.gameObject);
        const p = target instanceof Transform ? target.position : target;
        const d = p.sub(this.position);
        if (d.magnitude > 1e-6) this.rotation = Quaternion.LookRotation(d, up);
    }
    TransformPoint(p) { return this.localToWorldMatrix.multiplyPoint(p); }
    InverseTransformPoint(p) { return this.worldToLocalMatrix.multiplyPoint(p); }
    TransformDirection(d) { return this.rotation.multiplyVector(d); }
    InverseTransformDirection(d) { return this.rotation.inverse().multiplyVector(d); }

    // ---- hierarchy
    get parent() { return this._parent; }
    set parent(p) { this.SetParent(p); }
    get childCount() { return this._children.length; }
    GetChild(i) { return this._children[i] || null; }
    get children() { return this._children.slice(); }
    /** Find a direct child by name ("a/b" walks down). */
    Find(path) {
        let t = this;
        for (const part of String(path).split('/')) {
            t = t._children.find(c => c.gameObject.name === part) || null;
            if (!t) return null;
        }
        return t;
    }
    /** Put this object under `parent` (a Transform, a GameObject or null). With `keepWorld` it stays where it is. */
    SetParent(parent, keepWorld = true) {
        if (parent && !(parent instanceof Transform)) parent = parent.transform;
        for (let p = parent; p; p = p._parent) if (p === this) throw new Error("A transform can't be its own parent");
        const world = keepWorld ? this.localToWorldMatrix : null;
        if (this._parent) this._parent._children.splice(this._parent._children.indexOf(this), 1);
        this._parent = parent || null;
        if (parent) parent._children.push(this);
        const scene = parent && parent.gameObject && parent.gameObject.scene;
        if (scene && this.gameObject && this.gameObject.scene !== scene) scene.Add(this.gameObject);   // with its children
        if (world) {
            const local = parent ? parent.localToWorldMatrix.inverse().multiply(world) : world;
            const d = local.decompose();
            this._lp = d.position; this._lr = d.rotation; this._ls = d.scale;
        }
    }
}

/** An object in the scene: a name, a Transform and components. */
export class GameObject {
    constructor(name = 'GameObject') {
        this._name = String(name);
        this.tag = 'Untagged';
        this.layer = 0;          // 0..31; a camera draws the layers in its cullingMask
        this.active = true;
        this.scene = null;
        this._destroyed = false;
        this._components = [];
        this.transform = this.AddComponent(Transform);
    }
    get name() { return alive(this)._name; }
    set name(v) { alive(this)._name = String(v); }
    /** Active here and in every parent. */
    get activeInHierarchy() {
        for (let t = this.transform; t; t = t._parent) if (!t.gameObject.active) return false;
        return true;
    }
    get destroyed() { return this._destroyed; }

    /** Add a component: a class (made with `args`) or an instance. Returns it. */
    AddComponent(type, ...args) {
        alive(this);
        const c = typeof type === 'function' ? new type(...args) : type;
        if (!(c instanceof Component)) throw new TypeError('AddComponent needs a Component class or instance');
        c.gameObject = this;
        this._components.push(c);
        if (this.scene && c.OnAddedToScene) c.OnAddedToScene(this.scene);
        return c;
    }
    /** The first component of a type: a class, or a name ("Transform", "Camera", "ScriptComponent"). null if none. */
    GetComponent(type) {
        alive(this);
        return this._components.find(c => matches(c, type)) || null;
    }
    GetComponents(type) { return type === undefined ? this._components.slice() : this._components.filter(c => matches(c, type)); }
    RemoveComponent(c) {
        if (c instanceof Transform) throw new Error("The Transform can't be removed");
        const i = this._components.indexOf(c);
        if (i >= 0) { this._components.splice(i, 1); c.gameObject = null; }
    }
    toString() { return 'GameObject(' + this._name + ')'; }
}
function matches(c, type) {
    if (typeof type === 'string') {
        for (let k = c.constructor; k && k !== Object; k = Object.getPrototypeOf(k)) if (k.typeName === type) return true;
        return false;
    }
    return c instanceof type;
}

/** The camera: perspective (fieldOfView is vertical, in degrees) or orthographic (orthographicSize is half the
 *  height in world units). It looks down its own -Z, as the engine's cameras do. */
export class Camera extends Component {
    constructor(options = {}) {
        super();
        this.projection = 'perspective';
        this.fieldOfView = 60;
        this.orthographicSize = 5;
        this.nearClip = 0.3;
        this.farClip = 4000;
        this.backgroundColor = Color.FromBytes(49, 77, 121);   // the engine's default blue-grey
        this.clearFlags = 'solidColor';                        // 'solidColor', 'depthOnly' or 'dontClear'
        this.depth = 0;                                         // cameras draw in order of depth
        this.cullingMask = ~0;                                  // the GameObject layers this camera draws (bit per layer)
        this.rect = { x: 0, y: 0, w: 1, h: 1 };                 // the viewport as parts of the canvas
        Object.assign(this, options);
    }
    get viewMatrix() { return this.transform.localToWorldMatrix.inverse(); }
    ProjectionMatrix(aspect) {
        if (this.projection === 'orthographic') {
            const h = this.orthographicSize, w = h * aspect;
            return Matrix4.Ortho(-w, w, -h, h, this.nearClip, this.farClip);
        }
        return Matrix4.Perspective(this.fieldOfView, aspect, this.nearClip, this.farClip);
    }
}

/** A light. 'directional' shines along its transform's forward; every light adds `ambient` x ambientColor x color as
 *  an unlit floor (the engine takes the floor from its first light; okay.js adds them, which is the same for one). */
export class Light extends Component {
    constructor(options = {}) {
        super();
        this.type = 'directional';
        this.color = Color.white;
        this.intensity = 1;
        this.ambient = 0.3;
        this.ambientColor = Color.white;
        Object.assign(this, options);
    }
    get direction() { return this.transform.forward; }
}

/** Draws a Mesh with a material (LitMaterial or UnlitMaterial from material.js). */
export class MeshRenderer extends Component {
    constructor(mesh = null, material = null) {
        super();
        this.mesh = mesh;
        this.material = material;
    }
}

/** A flat picture in the world, facing the camera's -Z (the 2D path: pair it with an orthographic camera).
 *  `size` is in world units; `texture` is a Texture (material.js) or null for a plain coloured quad. */
export class SpriteRenderer extends Component {
    constructor(options = {}) {
        super();
        this.texture = null;
        this.size = Vector2.one;
        this.color = Color.white;
        this.sortOrder = 0;
        this.flipX = false;
        this.flipY = false;
        Object.assign(this, options);
    }
}

/** A scene: every GameObject in it, the ambient colour and the light-independent background. */
export class Scene {
    constructor(name = 'Scene') {
        this.name = name;
        this.objects = [];
    }
    /** Make an object in this scene (under `parent` when given). */
    CreateGameObject(name = 'GameObject', parent = null) {
        const go = new GameObject(name);
        this.Add(go);
        if (parent) go.transform.SetParent(parent, false);
        return go;
    }
    /** Put an existing object (and its children) into this scene. */
    Add(go) {
        const visit = g => {
            if (g.scene === this) return;
            if (g.scene) { const i = g.scene.objects.indexOf(g); if (i >= 0) g.scene.objects.splice(i, 1); }   // moved here
            g.scene = this;
            this.objects.push(g);
            for (const c of g._components) if (c.OnAddedToScene) c.OnAddedToScene(this);
            for (const t of g.transform._children) visit(t.gameObject);
        };
        visit(go);
        return go;
    }
    /** The first active object with this name, or null. */
    Find(name) { return this.objects.find(g => !g._destroyed && g._name === name && g.activeInHierarchy) || null; }
    FindWithTag(tag) { return this.objects.find(g => !g._destroyed && g.tag === tag && g.activeInHierarchy) || null; }
    /** Every live component of a type in the scene. */
    FindComponents(type) {
        const out = [];
        for (const g of this.objects) if (!g._destroyed) for (const c of g._components) if (matches(c, type)) out.push(c);
        return out;
    }
    /** Remove an object and its children. Components with OnDestroy hear about it first. */
    Destroy(go) {
        if (go instanceof Component) go = go.gameObject;   // a component destroys its object (a removed one: nothing)
        if (!go || go._destroyed) return;
        for (const t of go.transform._children.slice()) this.Destroy(t.gameObject);
        for (const c of go._components) if (c.OnDestroyComponent) c.OnDestroyComponent();
        go.transform.SetParent(null, false);
        go._destroyed = true;
        const i = this.objects.indexOf(go);
        if (i >= 0) this.objects.splice(i, 1);
    }
}
