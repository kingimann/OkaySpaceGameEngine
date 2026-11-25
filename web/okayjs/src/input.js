// okay.js input: keyboard, mouse and pointer, touch and gamepads, read once per frame the way the engine's Input
// script type reads them (engine/src/Script/EngineApi_Input.cpp, engine/src/Scripting/ScriptApiCore.cpp,
// engine/include/okay/Input/Input.hpp): key names ("space", "lshift", "f5", "a", "kp7"), mouse buttons 0 left /
// 1 right / 2 middle, GetKeyDown true on the frame of the press, a tap between two frames still seen for one frame,
// the arrows also reading as W A S D, the game actions Move / Look / Jump / Sprint / Crouch / Fire / Aim / Interact /
// Reload / Inventory / Pause. Written from the UI Events KeyboardEvent code values, Pointer Events and Gamepad
// (standard mapping) specifications of the W3C.

import { Vector3 } from './math.js';

// KeyboardEvent.code -> the engine's key names that read it (the first is its own name)
const CODE_NAMES = (() => {
    const m = {};
    for (let c = 65; c <= 90; ++c) m['Key' + String.fromCharCode(c)] = [String.fromCharCode(c + 32)];
    for (let d = 0; d <= 9; ++d) { m['Digit' + d] = [String(d)]; m['Numpad' + d] = ['kp' + d]; }
    for (let f = 1; f <= 12; ++f) m['F' + f] = ['f' + f];
    Object.assign(m, {
        Space: ['space'], Enter: ['enter'], NumpadEnter: ['kpenter', 'enter'], Escape: ['escape'], Backspace: ['backspace'],
        Tab: ['tab'], ArrowUp: ['up', 'w'], ArrowDown: ['down', 's'], ArrowLeft: ['left', 'a'], ArrowRight: ['right', 'd'],
        ShiftLeft: ['lshift', 'shift'], ShiftRight: ['rshift', 'shift'], ControlLeft: ['lctrl', 'ctrl'],
        ControlRight: ['rctrl', 'ctrl'], AltLeft: ['lalt', 'alt'], AltRight: ['ralt', 'alt'], MetaLeft: ['cmd'], MetaRight: ['cmd'],
        Insert: ['insert'], Delete: ['delete'], Home: ['home'], End: ['end'], PageUp: ['pageup'], PageDown: ['pagedown'],
        Minus: ['minus', '-'], Equal: ['equals', '='], BracketLeft: ['leftbracket', '['], BracketRight: ['rightbracket', ']'],
        Backslash: ['backslash', '\\'], Semicolon: ['semicolon', ';'], Quote: ['apostrophe', "'"], Backquote: ['grave', '`'],
        Comma: ['comma', ','], Period: ['period', '.'], Slash: ['slash', '/'], NumpadDivide: ['kpdivide'],
        NumpadMultiply: ['kpmultiply'], NumpadSubtract: ['kpminus'], NumpadAdd: ['kpplus'], NumpadDecimal: ['kpperiod'],
        CapsLock: ['capslock'], PrintScreen: ['printscreen'], ScrollLock: ['scrolllock'], Pause: ['pause'],
        NumLock: ['numlock'], ContextMenu: ['menu'],
    });
    return m;
})();
const KNOWN = new Set(Object.values(CODE_NAMES).flat());
const ALIASES = { return: 'enter', esc: 'escape', control: 'ctrl', command: 'cmd', leftshift: 'lshift', rightshift: 'rshift',
                  leftctrl: 'lctrl', rightctrl: 'rctrl', leftcontrol: 'lctrl', rightcontrol: 'rctrl', leftalt: 'lalt',
                  rightalt: 'ralt' };

/** The engine's key name for what a script wrote: case and spaces, '_' and '-' do not count ("Left Shift",
 *  "page_up", "numpad 7"); one character is taken as it is; an unknown longer name falls back to its first letter,
 *  as the engine does. */
export function keyName(s) {
    s = String(s);
    if (s.length === 1) return s.toLowerCase();
    let n = s.toLowerCase().replace(/[\s_-]/g, '');
    if (ALIASES[n]) n = ALIASES[n];
    const pad = /^numpad(\d)$/.exec(n);
    if (pad) n = 'kp' + pad[1];
    if (KNOWN.has(n)) return n;
    return n ? n[0] : '';
}
/** True for a name keyName reads exactly (one character or a known name), false for one that would fall back. */
export function isKnownKeyName(s) { s = String(s); return s.length === 1 || keyName(s).length > 1; }

/** Buttons that change between frames: press and release events in, one consistent picture per frame out. */
class ButtonSet {
    constructor() { this.live = new Set(); this.pressed = new Set(); this.released = new Set(); this.carry = new Set();
                    this.held = new Set(); this.down = new Set(); this.up = new Set(); }
    press(id) { if (!this.live.has(id)) { this.live.add(id); this.pressed.add(id); } }
    release(id) { if (this.live.delete(id)) this.released.add(id); }
    releaseAll() { for (const id of this.live) this.released.add(id); this.live.clear(); }
    /** Take the events since the last frame. A press and release both in that time (a tap) reads as held and down
     *  this frame and as released the next one. */
    frame() {
        this.down = this.pressed;
        this.up = new Set([...this.released].filter(id => !this.pressed.has(id) || this.live.has(id)));
        for (const id of this.carry) this.up.add(id);
        this.carry = new Set([...this.pressed].filter(id => this.released.has(id) && !this.live.has(id)));
        this.held = new Set([...this.live, ...this.carry]);
        this.pressed = new Set(); this.released = new Set();
    }
}

// Default game actions: keys, mouse buttons and standard-mapping gamepad buttons.
const DEFAULT_ACTIONS = {
    Jump: ['key:space', 'pad:0'], Sprint: ['key:shift', 'pad:10'], Crouch: ['key:c', 'key:ctrl', 'pad:1'],
    Fire: ['mouse:0', 'pad:7'], Aim: ['mouse:1', 'pad:6'], Interact: ['key:e', 'pad:2'], Reload: ['key:r', 'pad:3'],
    Inventory: ['key:tab', 'key:i', 'pad:8'], Pause: ['key:escape', 'pad:9'], Move: [], Look: [],
};
const PAD_DEADZONE = 0.2;
const MOUSE_BUTTON = [0, 2, 1, 3, 4];   // PointerEvent.button (0 left, 1 middle, 2 right) -> the engine's numbering

function deadzone(v) { const a = Math.abs(v); return a < PAD_DEADZONE ? 0 : Math.sign(v) * (a - PAD_DEADZONE) / (1 - PAD_DEADZONE); }
function editable(t) { return !!t && (t.isContentEditable || /^(INPUT|TEXTAREA|SELECT)$/.test(t.tagName || '')); }

/** One game's input. Scripts see it as the global `Input`. `element` is the canvas (pointer events); keys are
 *  read from `keyTarget` (default: window). */
export class Input {
    constructor(element = null, { keyTarget = typeof window !== 'undefined' ? window : null, preventScroll = true } = {}) {
        this.element = element;
        this.keys = new ButtonSet();
        this.mouse = new ButtonSet();
        this.pad = new ButtonSet();
        this._mouse = new Vector3(); this._mouseDelta = [0, 0]; this._frameDelta = new Vector3(); this._wheel = 0; this._frameWheel = 0;
        this._touches = new Map(); this._primaryTouch = null;
        this._padState = null;
        this._actions = {};
        for (const [k, v] of Object.entries(DEFAULT_ACTIONS)) this._actions[k] = v.slice();
        this._off = [];
        const on = (t, ev, fn, opts) => { if (!t) return; t.addEventListener(ev, fn, opts); this._off.push(() => t.removeEventListener(ev, fn, opts)); };
        on(keyTarget, 'keydown', e => {
            if (editable(e.target)) return;   // typing in a text field does not walk the player
            if (CODE_NAMES[e.code]) this.keys.press(e.code);
            if (preventScroll && /^(Space|Arrow|Tab)/.test(e.code)) e.preventDefault();
        });
        on(keyTarget, 'keyup', e => this.keys.release(e.code));
        on(keyTarget, 'blur', () => { this.keys.releaseAll(); this.mouse.releaseAll(); });
        if (element) {
            if (element.style) element.style.touchAction = 'none';
            on(element, 'contextmenu', e => e.preventDefault());
            on(element, 'pointerdown', e => {
                if (element.focus) element.focus();
                if (e.pointerType === 'touch') {
                    this._touches.set(e.pointerId, { id: e.pointerId, position: this.#local(e), phase: 'began' });
                    if (this._primaryTouch !== null) return;   // the first finger down owns the left button
                    this._primaryTouch = e.pointerId;
                }
                this._mouse = this.#local(e);
                this.mouse.press(MOUSE_BUTTON[e.button] ?? e.button);
                if (element.setPointerCapture) try { element.setPointerCapture(e.pointerId); } catch (_) { /* not capturable */ }
            });
            on(element, 'pointermove', e => {
                const p = this.#local(e);
                if (e.pointerType === 'touch') {
                    const t = this._touches.get(e.pointerId);
                    if (t) { t.position = p; if (t.phase !== 'began' || t.seen) t.phase = 'moved'; t.seen = false; }
                    if (e.pointerId !== this._primaryTouch) return;
                }
                this._mouseDelta[0] += e.movementX || 0; this._mouseDelta[1] += e.movementY || 0;
                this._mouse = p;
                // a button pressed or let go while another is held comes as a pointermove (Pointer Events "chorded
                // buttons"): take the state of every button from the `buttons` bit mask
                if (e.pointerType !== 'touch' && typeof e.buttons === 'number') {
                    // bits: 1 left, 2 right, 4 middle, 8 back, 16 forward = the engine's 0, 1, 2, 3, 4
                    for (let b = 0; b < 5; ++b) ((e.buttons >> b) & 1) ? this.mouse.press(b) : this.mouse.release(b);
                }
            });
            const up = e => {
                if (e.pointerType === 'touch') {
                    const t = this._touches.get(e.pointerId);
                    if (t) { t.phase = e.type === 'pointercancel' ? 'canceled' : 'ended'; t.seen = false; }
                    if (e.pointerId !== this._primaryTouch) return;
                    this._primaryTouch = null;
                    this.mouse.release(0);
                    return;
                }
                this.mouse.release(MOUSE_BUTTON[e.button] ?? e.button);
            };
            on(element, 'pointerup', up);
            on(element, 'pointercancel', up);
            on(element, 'wheel', e => { this._wheel += e.deltaY; e.preventDefault(); }, { passive: false });
        }
    }
    #local(e) {
        const r = this.element && this.element.getBoundingClientRect ? this.element.getBoundingClientRect() : { left: 0, top: 0 };
        return new Vector3(e.clientX - r.left, e.clientY - r.top, 0);
    }
    /** Stop listening. */
    dispose() { for (const f of this._off) f(); this._off = []; }

    /** Read the events since the last frame and poll the gamepads (the game loop calls this before the scripts). */
    BeginFrame() {
        this.keys.frame(); this.mouse.frame();
        this._frameDelta = new Vector3(this._mouseDelta[0], this._mouseDelta[1], 0); this._mouseDelta = [0, 0];
        this._frameWheel = this._wheel; this._wheel = 0;
        // a finger keeps the phase an event gave it for one frame: then 'began' / 'moved' become 'stationary' and an
        // 'ended' or 'canceled' finger is gone
        for (const [id, t] of this._touches) {
            if (t.seen) {
                if (t.phase === 'ended' || t.phase === 'canceled') { this._touches.delete(id); continue; }
                t.phase = 'stationary';
            }
            t.seen = true;
        }
        const pads = typeof navigator !== 'undefined' && navigator.getGamepads ? Array.from(navigator.getGamepads()).filter(Boolean) : [];
        this._padState = pads.find(p => p.connected !== false) || null;
        if (this._padState) this._padState.buttons.forEach((b, i) => ((b.pressed || b.value > 0.5) ? this.pad.press(i) : this.pad.release(i)));
        else this.pad.releaseAll();
        this.pad.frame();
    }

    // ---- keys (the engine's names)
    #keyHas(set, name) {
        const n = keyName(name);
        if (!n) return false;
        for (const code of set) if (CODE_NAMES[code] && CODE_NAMES[code].includes(n)) return true;
        return false;
    }
    /** True while the key is held. */
    GetKey(name) { return this.#keyHas(this.keys.held, name); }
    /** True on the frame the key is pressed. */
    GetKeyDown(name) { return this.#keyHas(this.keys.down, name); }
    /** True on the frame the key is released. */
    GetKeyUp(name) { return this.#keyHas(this.keys.up, name); }

    // ---- mouse
    #mouseButton(b) {
        if (!Number.isInteger(b) || b < 0 || b > 2) throw new RangeError('Mouse buttons are 0 (left), 1 (right) and 2 (middle)');
        return b;
    }
    GetMouseButton(b) { return this.mouse.held.has(this.#mouseButton(b)); }
    GetMouseButtonDown(b) { return this.mouse.down.has(this.#mouseButton(b)); }
    GetMouseButtonUp(b) { return this.mouse.up.has(this.#mouseButton(b)); }
    /** The cursor (or the first finger) in CSS pixels from the canvas's top-left corner; z is 0. */
    get mousePosition() { return this._mouse.clone(); }
    /** okay.js only: how far the mouse moved this frame (CSS pixels) and the wheel (positive = down). */
    get mouseDelta() { return this._frameDelta.clone(); }
    get mouseScrollDelta() { return this._frameWheel; }

    // ---- touch (okay.js only: the engine reads gestures, not fingers)
    get touchScreen() { return typeof navigator !== 'undefined' && (navigator.maxTouchPoints || 0) > 0; }
    get touchCount() { return this._touches.size; }
    /** The i-th finger: { id, position (Vector3, CSS pixels), phase 'began' | 'moved' | 'stationary' | 'ended' | 'canceled' }. */
    GetTouch(i) { const t = [...this._touches.values()][i]; return t ? { id: t.id, position: t.position.clone(), phase: t.phase } : null; }

    // ---- gamepad (the first connected pad, standard mapping)
    get gamepadConnected() { return !!this._padState; }
    /** okay.js only: a standard-mapping button by number (0 A / cross ... 15 d-pad right). */
    GetGamepadButton(i) { return this.pad.held.has(i); }
    GetGamepadButtonDown(i) { return this.pad.down.has(i); }
    /** okay.js only: a stick axis (0 left x, 1 left y, 2 right x, 3 right y; up is negative), dead zone applied. */
    GetGamepadAxis(i) { const p = this._padState; return p && p.axes[i] !== undefined ? deadzone(p.axes[i]) : 0; }
    /** Shake the pad: low and high motor strength 0..1, for `seconds`. */
    Rumble(low, high, seconds) {
        const a = this._padState && this._padState.vibrationActuator;
        if (a && a.playEffect) a.playEffect('dual-rumble', { duration: seconds * 1000, weakMagnitude: high, strongMagnitude: low }).catch(() => {});
    }

    // ---- axes and actions
    #dir(pos, neg) { return (pos.some(k => this.GetKey(k)) ? 1 : 0) - (neg.some(k => this.GetKey(k)) ? 1 : 0); }
    /** "Horizontal" (A/D, left/right, stick) or "Vertical" (W/S, up/down, stick): -1 to 1. */
    GetAxis(axis) {
        const clamp = v => Math.max(-1, Math.min(1, v));
        if (axis === 'Horizontal') return clamp(this.#dir(['d', 'right'], ['a', 'left']) + this.GetGamepadAxis(0));
        if (axis === 'Vertical') return clamp(this.#dir(['w', 'up'], ['s', 'down']) - this.GetGamepadAxis(1));
        throw new RangeError('"' + axis + '" isn\'t an axis. Use "Horizontal" or "Vertical"');
    }
    #action(name) {
        const b = this._actions[name];
        if (!b) throw new RangeError('There\'s no input action called "' + name + '" (Move, Look, Jump, Sprint, Crouch, Fire, Aim, ' +
                                     'Interact, Reload, Inventory, Pause, or one the game defined)');
        return b;
    }
    #bindingIn(binding, which) {
        const [kind, arg] = binding.split(':');
        if (kind === 'key') return this.#keyHas(this.keys[which], arg);
        if (kind === 'mouse') return this.mouse[which].has(Number(arg));
        if (kind === 'pad') return this.pad[which].has(Number(arg));
        return false;
    }
    /** True while the action is held (any of its bindings). `player` is accepted for the engine's signature; okay.js
     *  has one player. */
    GetButton(action, player = -1) {
        if (action === 'Move' || action === 'Look') return this.GetActionVector(action).sqrMagnitude > 0;
        return this.#action(action).some(b => this.#bindingIn(b, 'held'));
    }
    GetButtonDown(action, player = -1) { return this.#action(action).some(b => this.#bindingIn(b, 'down')); }
    GetButtonUp(action, player = -1) { return this.#action(action).some(b => this.#bindingIn(b, 'up')); }
    /** Move: W A S D, the arrows and the left stick (x right, y forward). Look: the mouse movement and the right stick. */
    GetActionVector(action) {
        this.#action(action);
        if (action === 'Move') {
            const x = this.GetAxis('Horizontal'), y = this.GetAxis('Vertical'), m = Math.hypot(x, y);
            return m > 1 ? new Vector3(x / m, y / m, 0) : new Vector3(x, y, 0);
        }
        if (action === 'Look') return new Vector3(this._frameDelta.x + this.GetGamepadAxis(2) * 10, -this._frameDelta.y - this.GetGamepadAxis(3) * 10, 0);
        return new Vector3(this.GetButton(action) ? 1 : 0, 0, 0);
    }
    /** Add or replace an action: bindings "key:<name>", "mouse:<0..2>" or "pad:<button>". */
    DefineAction(action, binding, binding2 = '') {
        this._actions[action] = [binding, binding2].filter(Boolean);
    }
}
