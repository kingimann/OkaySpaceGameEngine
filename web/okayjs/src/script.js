// okay.js game scripts: the same script text the engine's JavaScript scripts run (docs/scripting/javascript.md),
// run by the browser's own JavaScript engine.
//
// A script exports its behaviour: `export default class` (its instance fields are the settings), `export default
// { ... }`, or no export at all (its top-level functions are the handlers: `function Update(dt) {}`, also the old
// lower-case `update`). The lifecycle methods are called by name: Awake, OnEnable, Start, FixedUpdate(dt),
// Update(dt), LateUpdate(dt), OnDisable, OnDestroy. `this.gameObject` and `this.transform` are set before the first
// callback, and the globals a script sees are the engine's: GameObject, Vector3, Input, Time, Debug, Mathf,
// GetComponent, Destroy, gameObject, transform, console. Code runs in strict mode, as in the engine.

import { Component } from './scene.js';
import { Vector3 } from './math.js';

/** The handlers, in the engine's names, each with its old lower-case spelling (ScriptLanguageGlue HandlerNamesFor). */
export const LIFECYCLE = {
    Awake: 'awake', OnEnable: 'on_enable', Start: 'start', FixedUpdate: 'fixed_update', Update: 'update',
    LateUpdate: 'late_update', OnDisable: 'on_disable', OnDestroy: 'on_destroy',
};
const HANDLERS = Object.entries(LIFECYCLE).flat();
const ENGINE_GLOBALS = ['GameObject', 'Vector3', 'Input', 'Time', 'Debug', 'Mathf', 'GetComponent', 'Destroy',
                        'gameObject', 'transform', 'okay'];

let scriptCount = 0;

/** Names declared at the top level of a script (var, let, const, function, class), skipping strings, comments,
 *  template text and anything inside brackets. Regular expression literals are not recognised (a `/` that starts
 *  one is read as division), which only matters for a bracket or quote inside one at the top level. */
export function topLevelNames(src) {
    const names = new Set();
    let depth = 0, i = 0;
    const n = src.length;
    while (i < n) {
        const ch = src[i];
        if (ch === '/' && src[i + 1] === '/') { while (i < n && src[i] !== '\n') ++i; continue; }
        if (ch === '/' && src[i + 1] === '*') { const e = src.indexOf('*/', i + 2); i = e < 0 ? n : e + 2; continue; }
        if (ch === '"' || ch === "'" || ch === '`') {
            ++i;
            while (i < n && src[i] !== ch) { if (src[i] === '\\') ++i; ++i; }
            ++i; continue;
        }
        if ('{(['.includes(ch)) { ++depth; ++i; continue; }
        if ('})]'.includes(ch)) { --depth; ++i; continue; }
        if (depth === 0 && /[A-Za-z_$]/.test(ch) && (i === 0 || !/[\w$]/.test(src[i - 1]))) {
            const m = /^(?:var|let|const|function\*?|class)\s+([A-Za-z_$][\w$]*)/.exec(src.slice(i, i + 200));
            if (m) { names.add(m[1]); i += m[0].length; continue; }
            while (i < n && /[\w$]/.test(src[i])) ++i;
            continue;
        }
        ++i;
    }
    return [...names];
}

/** Turn the script text into a function body: `export default X` becomes the value handed back, `export` before a
 *  declaration is dropped, and a getter for the top-level names is added (what the engine's GetGlobal reads).
 *  Lines stay where they are, so error line numbers match the script. */
export function translateScript(src) {
    let defName = null;
    let body = src.replace(/^([ \t]*)export\s+default\s+(class|function\*?)(\s+)([A-Za-z_$][\w$]*)/m, (all, ind, kw, sp, name) => {
        defName = name; return ind + kw + sp + name;
    });
    if (!defName) body = body.replace(/^([ \t]*)export\s+default\s+/m, '$1__okayDefault = ');
    body = body.replace(/^([ \t]*)export\s+(?=(?:async\s+)?(?:class|function|const|let|var)\b)/gm, '$1');
    const names = topLevelNames(body).filter(x => x !== '__okayDefault');
    const handlers = HANDLERS.map(h => `${h}: typeof ${h} === 'function' ? ${h} : undefined`).join(', ');
    const getter = names.length
        ? `(n) => { switch (n) { ${names.map(x => `case ${JSON.stringify(x)}: return ${x};`).join(' ')} } return undefined; }`
        : '() => undefined';
    const setter = names.length
        ? `(n, v) => { switch (n) { ${names.map(x => `case ${JSON.stringify(x)}: ${x} = v; return true;`).join(' ')} } return false; }`
        : '() => false';
    return '"use strict"; let __okayDefault;\n' + body +
        `\n;return { def: ${defName || '__okayDefault'}, handlers: { ${handlers} }, get: ${getter}, set: ${setter} };`;
}

/** Where an error happened in a script, as "script:LINE" (or "script" when the browser does not say). */
function where(err, id) {
    const m = new RegExp('okay-script-' + id + '\\.js:(\\d+)').exec(String(err && err.stack));
    // the body starts on line 3 of the function text the browser builds ("function anonymous(...\n) {\n"), and our
    // first line ("use strict" ...) comes before the script's line 1
    return m ? 'script:' + Math.max(1, Number(m[1]) - 3) : 'script';
}

/** A script on a GameObject. `source` is script text, a class, or an object with handler methods. `fields`
 *  replaces settings after the script is made (what the Inspector does in the engine): numbers in text are read as
 *  numbers, "true" / "false" as booleans. */
export class ScriptComponent extends Component {
    constructor(source, { fields = {}, name = 'script' } = {}) {
        super();
        this.source = source;
        this.fields = fields;
        this.scriptName = name;
        this.instance = null;
        this.errorCount = 0;
        this._started = false;
        this._enabledSeen = false;
        this._module = null;
        this._id = ++scriptCount;
    }
    static get typeName() { return 'ScriptComponent'; }

    /** Make the script's object (the game calls this when the object is in a running game). */
    Load(game) {
        if (this.instance) return true;
        this.game = game;
        const go = this.gameObject;
        const api = game.ScriptGlobals(go);
        let def = this.source, mod = null;
        try {
            if (typeof def === 'string') {
                const fn = new Function(...ENGINE_GLOBALS, translateScript(def) + '\n//# sourceURL=okay-script-' + this._id + '.js');
                mod = fn(...ENGINE_GLOBALS.map(k => api[k]));
                def = mod.def;
            }
            let inst;
            if (typeof def === 'function') inst = new def();
            else if (def && typeof def === 'object') inst = def;
            else {   // no export: the top-level functions are the handlers
                inst = {};
                for (const [k, f] of Object.entries(mod ? mod.handlers : {})) if (f) inst[k] = f;
            }
            this._module = mod;
            inst.gameObject = go;
            inst.transform = go.transform;
            for (const [k, v] of Object.entries(this.fields)) this.Set(k, coerce(v), inst);
            this.instance = inst;
            return true;
        } catch (e) {
            this.#report(e);
            this.instance = null;
            return false;
        }
    }
    /** A value the script holds: a field of its object, or a top-level variable of a script with no export. */
    Get(name) {
        const inst = this.instance;
        if (inst && name in inst) return inst[name];
        return this._module ? this._module.get(name) : undefined;
    }
    Set(name, value, inst = this.instance) {
        if (inst && name in inst) { inst[name] = value; return; }
        let done = false;
        try { done = !!(this._module && this._module.set(name, value)); } catch (_) { /* a const or a class name */ }
        if (!done && inst) inst[name] = value;
    }
    /** The handler a lifecycle name finds (Update, then update). */
    Handler(name) {
        const inst = this.instance;
        if (!inst) return null;
        for (const n of [name, LIFECYCLE[name]]) if (n && typeof inst[n] === 'function') return inst[n];
        return null;
    }
    /** Call a handler by name if the script has it. Errors go to the Console with the line and do not stop the game. */
    Call(name, ...args) {
        const f = this.Handler(name);
        if (!f) return undefined;
        try {
            return f.apply(this.instance, args);
        } catch (e) {
            this.#report(e);
            return undefined;
        }
    }
    #report(e) {
        this.errorCount++;
        const text = where(e, this._id) + ': ' + (e && e.name ? e.name + ': ' + e.message : String(e));
        if (this.game) this.game.Debug.LogError(text); else console.error(text);
    }
    OnDestroyComponent() { if (this.instance) { this.Call('OnDisable'); this.Call('OnDestroy'); } }
}

function coerce(v) {
    if (typeof v !== 'string') return v;
    if (v === 'true') return true;
    if (v === 'false') return false;
    if (v.trim() !== '' && Number.isFinite(Number(v))) return Number(v);
    const vec = /^\s*(?:Vector3)?\(\s*(-?[\d.eE+-]+)\s*,\s*(-?[\d.eE+-]+)\s*,\s*(-?[\d.eE+-]+)\s*\)\s*$/.exec(v);
    if (vec) return new Vector3(Number(vec[1]), Number(vec[2]), Number(vec[3]));
    return v.length >= 2 && v[0] === '"' && v[v.length - 1] === '"' ? v.slice(1, -1) : v;
}
