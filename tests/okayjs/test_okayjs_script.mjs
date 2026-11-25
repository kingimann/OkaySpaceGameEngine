// okay.js scene graph, game loop, input and game scripts in plain Node (node --test, no packages, no browser).
// The game scripts are the ones the engine's JavaScript test (tests/test_js_script.cpp on claude/javascript-1, cases
// 1, 2, 3, 5 and 6) runs on real GameObjects; web/okayjs/examples/scripts/key_mover.js is case 3's text, byte for byte.
import { test } from 'node:test';
import assert from 'node:assert/strict';
import fs from 'node:fs';
import { Game, Scene, GameObject, Transform, Vector3, Camera, ScriptComponent, Input, keyName, isKnownKeyName,
         topLevelNames } from '../../web/okayjs/okay.js';

const DT = 1 / 60;
const near = (a, b, eps = 1e-4) => assert.ok(Math.abs(a - b) <= eps, `${a} != ${b}`);
const vnear = (a, b, eps = 1e-4) => { near(a.x, b.x, eps); near(a.y, b.y, eps); near(a.z, b.z, eps); };
// the scripts' Debug and console lines are checked through game.log, not printed
for (const k of ['log', 'warn', 'error']) console[k] = () => {};
const quiet = g => g;

/** One object with one script, as the engine test's World does. */
function world(src, options) {
    const g = quiet(new Game());
    const player = g.scene.CreateGameObject('Player');
    const sc = g.AddScript(player, src, options);
    return { g, player, sc, frames(n, keys = []) {
        for (let i = 0; i < n; ++i) { for (const k of keys) g.input.keys.press(k); g.step(DT); for (const k of keys) g.input.keys.release(k); }
    } };
}

test('case 1: a class script, fields, Start, Update moving the transform with dt', () => {
    const w = world(`
export default class Mover {
    speed = 2;
    label = "hi";
    started = false;
    Start() {
        this.started = true;
        console.log("start " + this.gameObject.name);
    }
    Update(dt) {
        this.transform.position = this.transform.position.add(new Vector3(this.speed * dt, 0, 0));
    }
}
`);
    w.frames(60);
    assert.equal(w.sc.Get('started'), true);
    near(w.player.transform.position.x, 2, 0.01);
});

test('case 2: settings from outside replace the defaults; an exported object works too', () => {
    const w = world('export default { speed: 2, Update(dt) { this.transform.Translate(this.speed * dt, 0, 0); } };', { fields: { speed: '10' } });
    w.frames(30);
    near(w.player.transform.position.x, 5, 0.02);
    assert.equal(w.sc.Get('speed'), 10);
});

test('case 3: Input and Time through the script globals (examples/scripts/key_mover.js)', () => {
    const src = fs.readFileSync(new URL('../../web/okayjs/examples/scripts/key_mover.js', import.meta.url), 'utf8');
    const w = world(src);
    // held for ten frames, then five frames without
    for (let i = 0; i < 10; ++i) { w.g.input.keys.press('KeyD'); w.g.step(DT); }
    w.g.input.keys.release('KeyD');
    w.frames(5);
    assert.equal(w.sc.Get('moved'), 10);
    near(w.sc.Get('dtSeen'), DT, 1e-9);
    assert.ok(w.player.transform.position.x > 0.1);
});

test('case 5: GetComponent, GameObject.Find, identity and printing', () => {
    const g = quiet(new Game());
    const player = g.scene.CreateGameObject('Player');
    const sc = g.AddScript(player, `
export default class S {
    Start() {
        const tr = this.gameObject.GetComponent("Transform");
        this.sameTransform = tr === this.transform;
        this.noBody = GetComponent("Rigidbody") === null;
        const other = GameObject.Find("Other");
        this.otherName = other ? other.name : "none";
        this.text = String(this.gameObject);
        this.isObject = other instanceof GameObject;
        Debug.Log("found " + this.otherName);
    }
}
`);
    g.scene.CreateGameObject('Other');
    g.step(DT);
    assert.equal(sc.Get('sameTransform'), true);
    assert.equal(sc.Get('noBody'), true);
    assert.equal(sc.Get('otherName'), 'Other');
    assert.equal(sc.Get('text'), 'GameObject(Player)');
    assert.equal(sc.Get('isObject'), true);
    assert.ok(g.log.some(l => l.text === 'found Other'));
});

test('case 6: errors carry the line and do not stop later frames; a destroyed object is an error', () => {
    const w = world(`
export default class E {
    n = 0;
    Update(dt) {
        this.n++;
        if (this.n === 2) {
            const missing = undefined;
            missing.field = 1;
        }
    }
}
`);
    w.frames(4);
    assert.equal(w.sc.Get('n'), 4);
    assert.equal(w.sc.errorCount, 1);
    assert.match(w.g.log.find(l => l.level === 'error').text, /^script:8: TypeError: /);

    const g = quiet(new Game());
    const d = g.AddScript(g.scene.CreateGameObject('Keeper'), `
export default {
    Start() { this.keep = GameObject.Find("Victim"); },
    Update(dt) { if (this.keep) { const n = this.keep.name; } },
};
`);
    const victim = g.scene.CreateGameObject('Victim');
    g.step(DT);
    g.scene.Destroy(victim);
    g.step(DT); g.step(DT);
    assert.ok(g.log.some(l => /destroyed/.test(l.text)));
    assert.ok(d.errorCount >= 1);
});

test('a script with no export: global functions (old lower-case names too) and top-level variables', () => {
    const w = world(`
let count = 0;
var label = "x";
const fixed = [];
function start() { label = "started " + gameObject.name; }
function Update(dt) { count++; }
function FixedUpdate(dt) { fixed.push(dt); }
`);
    w.frames(3);
    assert.equal(w.sc.Get('count'), 3);
    assert.equal(w.sc.Get('label'), 'started Player');
    assert.ok(w.sc.Get('fixed').every(d => d === 1 / 50));
    w.sc.Set('count', 100);
    w.frames(1);
    assert.equal(w.sc.Get('count'), 101);
});

test('lifecycle order and the fixed step', () => {
    const w = world(`
export default class L {
    calls = [];
    fixedCount = 0;
    Awake() { this.calls.push("Awake"); }
    OnEnable() { this.calls.push("OnEnable"); }
    Start() { this.calls.push("Start"); }
    FixedUpdate(dt) { this.fixedCount++; }
    Update(dt) { if (this.calls.length < 6) this.calls.push("Update"); }
    LateUpdate(dt) { if (this.calls.length < 6) this.calls.push("LateUpdate"); }
    OnDisable() { this.calls.push("OnDisable"); }
    OnDestroy() { this.calls.push("OnDestroy"); }
}
`);
    w.frames(60);   // one second at 60 frames: 50 fixed steps
    assert.deepEqual(w.sc.Get('calls'), ['Awake', 'OnEnable', 'Start', 'Update', 'LateUpdate', 'Update']);
    near(w.sc.Get('fixedCount'), 50, 1);
    w.g.step(10);   // a stall: counted as 0.25 s, so at most 5 steps (not 500)
    assert.ok(w.sc.Get('fixedCount') <= 56);
    w.player.active = false;
    w.g.step(DT);
    assert.equal(w.sc.Get('calls').at(-1), 'OnDisable');
    w.player.active = true;
    w.g.step(DT);
    assert.equal(w.sc.Get('calls').at(-1), 'OnEnable');
    w.g.scene.Destroy(w.player);
    assert.deepEqual(w.sc.Get('calls').slice(-2), ['OnDisable', 'OnDestroy']);
});

test('Time.timeScale and pause', () => {
    const w = world('export default class T { t = 0; Update(dt) { this.t += dt; } }');
    w.g.Time.timeScale = 0.5;
    w.frames(60);
    near(w.sc.Get('t'), 0.5, 1e-6);
    near(w.g.Time.unscaledTime, 1, 1e-6);
    w.g.Time.paused = true;
    w.frames(10);
    near(w.sc.Get('t'), 0.5, 1e-6);
});

test('a script that does not compile reports and does not stop the others', () => {
    const g = quiet(new Game());
    const bad = g.AddScript(g.scene.CreateGameObject('Bad'), 'export default class { Update( { }');
    const good = g.AddScript(g.scene.CreateGameObject('Good'), 'export default class { n = 0; Update() { this.n++; } }');
    g.step(DT); g.step(DT);
    assert.equal(bad.instance, null);
    assert.equal(good.Get('n'), 2);
    assert.ok(g.log.some(l => l.level === 'error' && /SyntaxError/.test(l.text)));
});

test('a script class can be handed over directly (no text)', () => {
    const g = quiet(new Game());
    class Bob { y0 = 0; Update(dt) { this.transform.Translate(0, dt, 0); } }
    const go = g.scene.CreateGameObject('Bob');
    const sc = go.AddComponent(ScriptComponent, Bob);
    g.step(0.5);
    near(go.transform.position.y, 0.25);   // 0.5 s is clamped to maxDelta 0.25
    assert.ok(sc.instance instanceof Bob);
});

test('transforms: values are copies, parents carry children, SetParent keeps the world place', () => {
    const s = new Scene();
    const parent = s.CreateGameObject('Parent'), child = s.CreateGameObject('Child', parent);
    parent.transform.position = new Vector3(10, 0, 0);
    parent.transform.localEulerAngles = new Vector3(0, 90, 0);
    parent.transform.localScale = new Vector3(2, 2, 2);
    child.transform.localPosition = new Vector3(0, 0, 1);
    vnear(child.transform.position, new Vector3(12, 0, 0));   // turned +Z becomes +X, scaled by 2
    vnear(child.transform.forward, new Vector3(1, 0, 0));
    const p = child.transform.position;
    p.x = 99;                                                   // a copy: the object does not move
    vnear(child.transform.position, new Vector3(12, 0, 0));
    child.transform.position = new Vector3(10, 4, 0);           // world position under a turned, scaled parent
    vnear(child.transform.localPosition, new Vector3(0, 2, 0));
    child.transform.SetParent(null);                            // keeps its world place
    vnear(child.transform.position, new Vector3(10, 4, 0));
    child.transform.SetParent(parent);
    vnear(child.transform.position, new Vector3(10, 4, 0));
    vnear(child.transform.localScale, new Vector3(1, 1, 1));   // world scale 2 under a parent of scale 2
    assert.equal(parent.transform.childCount, 1);
    assert.equal(parent.transform.Find('Child'), child.transform);
    assert.throws(() => parent.transform.SetParent(child), /own parent/);
    child.transform.LookAt(new Vector3(10, 4, 10));
    vnear(child.transform.forward, new Vector3(0, 0, 1));
    child.transform.Rotate(0, 90, 0);
    vnear(child.transform.forward, new Vector3(1, 0, 0));
    s.Destroy(parent);
    assert.equal(s.objects.length, 0);
    assert.ok(child.destroyed);
    assert.throws(() => child.name, /destroyed/);
    assert.throws(() => child.transform.Translate(1, 0, 0), /destroyed/);
});

test('scene lookups and components', () => {
    const s = new Scene();
    const a = s.CreateGameObject('A');
    a.tag = 'Enemy';
    const cam = a.AddComponent(Camera, { fieldOfView: 50 });
    assert.equal(a.GetComponent('Camera'), cam);
    assert.equal(a.GetComponent(Camera), cam);
    assert.equal(a.GetComponent('Transform'), a.transform);
    assert.ok(a.GetComponent('Transform') instanceof Transform);
    assert.equal(s.FindWithTag('Enemy'), a);
    assert.equal(String(cam), 'Camera(A)');
    a.active = false;
    assert.equal(s.Find('A'), null);   // only active objects, as the engine's Find
    assert.throws(() => a.RemoveComponent(a.transform), /Transform/);
    near(cam.ProjectionMatrix(1).get(1, 1), 1 / Math.tan(25 * Math.PI / 180));
});

test('key names read the way the engine reads them', () => {
    assert.equal(keyName('Left Shift'), 'lshift');
    assert.equal(keyName('page_up'), 'pageup');
    assert.equal(keyName('numpad 7'), 'kp7');
    assert.equal(keyName('ESC'), 'escape');
    assert.equal(keyName('Return'), 'enter');
    assert.equal(keyName('D'), 'd');
    assert.equal(keyName('jump'), 'j');   // an unknown name falls back to its first letter
    assert.ok(isKnownKeyName('space') && isKnownKeyName('x') && !isKnownKeyName('jump'));
});

test('Input: keys from events, a tap between frames, arrows as WASD, mouse, axes and actions', () => {
    const keys = new EventTarget(), el = new EventTarget();
    el.getBoundingClientRect = () => ({ left: 10, top: 20 });
    const inp = new Input(el, { keyTarget: keys });
    const key = (type, code) => keys.dispatchEvent(Object.assign(new Event(type, { cancelable: true }), { code }));
    const ptr = (type, o) => el.dispatchEvent(Object.assign(new Event(type), { pointerType: 'mouse', pointerId: 1, button: 0,
                                                                                clientX: 0, clientY: 0, ...o }));
    key('keydown', 'Space');
    inp.BeginFrame();
    assert.ok(inp.GetKey('space') && inp.GetKeyDown('space') && !inp.GetKeyUp('space'));
    inp.BeginFrame();
    assert.ok(inp.GetKey('space') && !inp.GetKeyDown('space'));
    key('keyup', 'Space');
    inp.BeginFrame();
    assert.ok(!inp.GetKey('space') && inp.GetKeyUp('space'));
    // a tap: down and up before the frame reads them
    key('keydown', 'KeyE'); key('keyup', 'KeyE');
    inp.BeginFrame();
    assert.ok(inp.GetKey('e') && inp.GetKeyDown('e') && !inp.GetKeyUp('e'));
    assert.ok(inp.GetButtonDown('Interact'));
    inp.BeginFrame();
    assert.ok(!inp.GetKey('e') && inp.GetKeyUp('e'));
    // the arrows read as W A S D; ShiftLeft reads as lshift and shift
    key('keydown', 'ArrowUp'); key('keydown', 'ShiftLeft');
    inp.BeginFrame();
    assert.ok(inp.GetKey('w') && inp.GetKey('up') && inp.GetKey('shift') && inp.GetKey('Left Shift') && !inp.GetKey('rshift'));
    assert.equal(inp.GetAxis('Vertical'), 1);
    assert.equal(inp.GetAxis('Horizontal'), 0);
    assert.ok(inp.GetButton('Sprint') && inp.GetButton('Move'));
    vnear(inp.GetActionVector('Move'), new Vector3(0, 1, 0));
    assert.throws(() => inp.GetAxis('Sideways'), /isn't an axis/);
    assert.throws(() => inp.GetButton('Fly'), /no input action called "Fly"/);
    // a blur lets go of everything (no stuck keys after alt-tab)
    keys.dispatchEvent(new Event('blur'));
    inp.BeginFrame();
    assert.ok(!inp.GetKey('w') && inp.GetKeyUp('w'));
    // mouse: right button is 1 in the engine (PointerEvent says 2); the position is from the canvas corner
    ptr('pointerdown', { button: 2, clientX: 110, clientY: 70 });
    inp.BeginFrame();
    assert.ok(inp.GetMouseButtonDown(1) && inp.GetMouseButton(1) && !inp.GetMouseButton(0));
    assert.ok(inp.GetButton('Aim'));
    vnear(inp.mousePosition, new Vector3(100, 50, 0));
    ptr('pointerup', { button: 2 });
    inp.BeginFrame();
    assert.ok(inp.GetMouseButtonUp(1));
    assert.throws(() => inp.GetMouseButton(3), /Mouse buttons/);
    // touch: the first finger is the left button, a second finger is only a touch
    ptr('pointerdown', { pointerType: 'touch', pointerId: 7, clientX: 30, clientY: 40 });
    ptr('pointerdown', { pointerType: 'touch', pointerId: 8, clientX: 60, clientY: 40 });
    inp.BeginFrame();
    assert.ok(inp.GetMouseButtonDown(0));
    assert.equal(inp.touchCount, 2);
    assert.equal(inp.GetTouch(1).phase, 'began');
    vnear(inp.mousePosition, new Vector3(20, 20, 0));
    ptr('pointerup', { pointerType: 'touch', pointerId: 7 });
    inp.BeginFrame();
    assert.ok(inp.GetMouseButtonUp(0));
    assert.equal(inp.GetTouch(0).phase, 'ended');
    inp.BeginFrame();
    assert.equal(inp.touchCount, 1);
    // a defined action
    inp.DefineAction('Dash', 'key:q');
    key('keydown', 'KeyQ');
    inp.BeginFrame();
    assert.ok(inp.GetButtonDown('Dash'));
    // Space and the arrows do not scroll the page
    const ev = Object.assign(new Event('keydown', { cancelable: true }), { code: 'ArrowDown' });
    keys.dispatchEvent(ev);
    assert.ok(ev.defaultPrevented);
    inp.dispose();
    key('keydown', 'KeyZ');
    inp.BeginFrame();
    assert.ok(!inp.GetKey('z'));
});

test('Input: a gamepad through the Gamepad API (standard mapping)', () => {
    const pad = { connected: true, axes: [0.1, -1, 0, 0], buttons: Array.from({ length: 17 }, () => ({ pressed: false, value: 0 })) };
    const saved = Object.getOwnPropertyDescriptor(globalThis, 'navigator');
    Object.defineProperty(globalThis, 'navigator', { value: { getGamepads: () => [null, pad], maxTouchPoints: 0 }, configurable: true });
    try {
        const inp = new Input(null, { keyTarget: new EventTarget() });
        inp.BeginFrame();
        assert.ok(inp.gamepadConnected);
        assert.equal(inp.GetAxis('Horizontal'), 0);   // inside the dead zone
        assert.equal(inp.GetAxis('Vertical'), 1);     // stick up is -1 in the Gamepad API, +1 for the game
        pad.buttons[0] = { pressed: true, value: 1 };
        inp.BeginFrame();
        assert.ok(inp.GetButtonDown('Jump') && inp.GetGamepadButton(0));
        inp.BeginFrame();
        assert.ok(inp.GetButton('Jump') && !inp.GetButtonDown('Jump'));
        pad.buttons[0] = { pressed: false, value: 0 };
        inp.BeginFrame();
        assert.ok(inp.GetButtonUp('Jump'));
    } finally {
        if (saved) Object.defineProperty(globalThis, 'navigator', saved); else delete globalThis.navigator;
    }
});

test('topLevelNames skips strings, comments and blocks', () => {
    assert.deepEqual(topLevelNames(`let a = 1; // let b
const c = "let d"; /* var e */ function f() { let g; } class H { m() { var i; } } var j = { k: 1 }; let \`x\`;`).sort(),
                     ['a', 'c', 'f', 'H', 'j'].sort());
});

test('a GameObject made outside a scene can be added later with its children', () => {
    const s = new Scene();
    const root = new GameObject('Root'), kid = new GameObject('Kid');
    kid.transform.SetParent(root);
    s.Add(root);
    assert.equal(s.Find('Kid'), kid);
    assert.equal(kid.scene, s);
});

// ---- review fixes (PR #1289): each of these failed before its fix

test('review 1: OnDisable on destroy only when the script is enabled (once), OnDestroy only after it started', () => {
    const src = 'export default class D { log = []; OnEnable() { this.log.push("en"); } OnDisable() { this.log.push("dis"); } OnDestroy() { this.log.push("des"); } }';
    const w = world(src);
    w.frames(1);
    w.player.active = false;
    w.frames(1);                 // OnDisable here
    const log = w.sc.Get('log');
    w.g.scene.Destroy(w.player);  // already off: no second OnDisable
    assert.deepEqual(log, ['en', 'dis', 'des']);
    // an object that never ran (inactive from the start) gets neither, as in the engine (ScriptComponent::OnDestroy)
    const g = new Game();
    const off = g.scene.CreateGameObject('Off');
    off.active = false;
    const sc = g.AddScript(off, src);
    g.step(DT);
    g.scene.Destroy(off);
    assert.equal(sc.instance === null ? null : JSON.stringify(sc.Get('log')), '[]');
});

test('review 2: destroying a component (or the same one twice, or a removed one) is safe and runs OnDestroy once', () => {
    const w = world('export default class D { n = 0; OnDestroy() { this.n++; } }');
    w.frames(1);
    w.g.scene.Destroy(w.sc);
    w.g.scene.Destroy(w.sc);
    assert.equal(w.sc.instance.n, 1);
    assert.ok(w.player.destroyed);
    const s = new Scene();
    const a = s.CreateGameObject('A');
    const cam = a.AddComponent(Camera);
    a.RemoveComponent(cam);
    assert.doesNotThrow(() => s.Destroy(cam));
    assert.ok(!a.destroyed);
});

test('review 4: a script may declare its own top-level names that are also engine globals', () => {
    const w = world('const Time = { label: "mine" }; let transform = 5; class Input {}\nexport default class S { Start() { this.t = Time.label + transform; } }');
    w.frames(1);
    assert.equal(w.g.log.filter(l => l.level === 'error').length, 0, JSON.stringify(w.g.log));
    assert.equal(w.sc.Get('t'), 'mine5');
    // error lines still match the script
    const e = world('let a = 1;\nlet b = 2;\nnull.x = 3;');
    e.frames(1);
    assert.match(e.g.log.find(l => l.level === 'error').text, /^script:3: TypeError/);
});

test('review 5: a second mouse button pressed while another is held (a chord: pointermove with buttons) is seen', () => {
    const el = new EventTarget();
    el.getBoundingClientRect = () => ({ left: 0, top: 0 });
    const inp = new Input(el, { keyTarget: new EventTarget() });
    const ptr = (type, o) => el.dispatchEvent(Object.assign(new Event(type), { pointerType: 'mouse', pointerId: 1, clientX: 0, clientY: 0, ...o }));
    ptr('pointerdown', { button: 0, buttons: 1 });
    inp.BeginFrame();
    ptr('pointermove', { button: 2, buttons: 3 });   // right pressed while left is held
    inp.BeginFrame();
    assert.ok(inp.GetMouseButtonDown(1) && inp.GetMouseButton(1) && inp.GetMouseButton(0));
    ptr('pointermove', { button: 2, buttons: 1 });   // right let go, left still held
    inp.BeginFrame();
    assert.ok(inp.GetMouseButtonUp(1) && !inp.GetMouseButton(1) && inp.GetMouseButton(0));
    ptr('pointerup', { button: 0, buttons: 0 });
    inp.BeginFrame();
    assert.ok(inp.GetMouseButtonUp(0));
});

test('review 6: parenting an object under one in a scene puts it (and its children) in that scene', () => {
    const s = new Scene();
    const root = s.CreateGameObject('Root');
    const kid = new GameObject('Kid'), grandkid = new GameObject('Grandkid');
    grandkid.transform.SetParent(kid);
    kid.transform.SetParent(root);
    assert.equal(kid.scene, s);
    assert.equal(s.Find('Grandkid'), grandkid);
    const other = new Scene();
    const host = other.CreateGameObject('Host');
    kid.transform.SetParent(host);   // moved to another scene: listed there, not here
    assert.equal(other.Find('Grandkid'), grandkid);
    assert.equal(s.Find('Kid'), null);
});

test('review 7: export default of a plain function is not a class: its top-level functions are the handlers (as the engine)', () => {
    const w = world('let n = 0;\nexport default function Update(dt) { n++; }');
    w.frames(3);
    assert.equal(w.g.log.filter(l => l.level === 'error').length, 0, JSON.stringify(w.g.log));
    assert.equal(w.sc.Get('n'), 3);
    const g = new Game();
    const go = g.scene.CreateGameObject('F');
    const sc = go.AddComponent(ScriptComponent, function NotAClass() {});
    g.step(DT);
    assert.equal(sc.instance, null);
    assert.ok(g.log.some(l => l.level === 'error' && /a class or an object/.test(l.text)));
});

test('review 8: Awake waits until the object is active, then runs with OnEnable and Start (the engine order)', () => {
    const g = new Game();
    const go = g.scene.CreateGameObject('Later');
    go.active = false;
    const sc = g.AddScript(go, 'export default class A { calls = []; Awake() { this.calls.push("Awake"); } OnEnable() { this.calls.push("OnEnable"); } Start() { this.calls.push("Start"); } }');
    g.step(DT); g.step(DT);
    assert.deepEqual(sc.Get('calls'), []);
    go.active = true;
    g.step(DT);
    assert.deepEqual(sc.Get('calls'), ['Awake', 'OnEnable', 'Start']);
});
