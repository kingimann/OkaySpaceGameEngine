// okay.js game loop: a Game owns a Scene, a Renderer, an Input and the Time and Debug objects scripts see, and
// runs the frame in the engine's order: input, new scripts (Awake, OnEnable, Start), FixedUpdate at a fixed step
// (1/50 s, at most 5 steps a frame, as the engine's FixedTimestep), Update, LateUpdate, then drawing.
// Our own design; frame pacing from the browser's requestAnimationFrame (HTML Living Standard).

import { Scene, GameObject } from './scene.js';
import { Renderer } from './renderer.js';
import { Input } from './input.js';
import { ScriptComponent } from './script.js';
import { Vector3, Mathf } from './math.js';
import * as okayAll from '../okay.js';   // the whole library, as the script global `okay` (used only at run time)

/** Frame timing, with the engine's Time names. */
export class Time {
    constructor() {
        this.deltaTime = 0;            // seconds this frame, scaled (the fixed step inside FixedUpdate)
        this.unscaledDeltaTime = 0;
        this.fixedDeltaTime = 1 / 50;
        this.timeScale = 1;
        this.sessionTime = 0;          // scaled seconds since the game started
        this.sessionUnscaledTime = 0;
        this.sessionFrameCount = 0;
        this.fps = 0;
        this.paused = false;
    }
    get unscaledTime() { return this.sessionUnscaledTime; }
}

export class Game {
    /** canvas: where to draw (none for a game without pictures, as in the Node tests). renderer: Renderer options.
     *  maxDelta: a longer frame (a stall, a hidden tab) counts as this long. */
    constructor({ canvas = null, scene = new Scene(), renderer = {}, input = {}, maxSteps = 5, maxDelta = 0.25 } = {}) {
        this.scene = scene;
        this.canvas = canvas;
        this.renderer = canvas ? new Renderer(canvas, renderer) : null;
        this.input = new Input(canvas, input);
        this.Time = new Time();
        this.maxSteps = maxSteps;
        this.maxDelta = maxDelta;
        this.log = [];          // every Debug line, newest last (for tests and an on-page console)
        this.onLog = null;      // optional (level, text) => void
        const say = (level, write) => (...parts) => {
            const text = parts.map(String).join(' ');
            this.log.push({ level, text });
            if (this.log.length > 1000) this.log.shift();
            if (this.onLog) this.onLog(level, text);
            write(text);
        };
        this.Debug = Object.freeze({ Log: say('info', t => console.log(t)), LogWarning: say('warning', t => console.warn(t)),
                                     LogError: say('error', t => console.error(t)) });
        this._acc = 0;
        this._running = false;
        this._last = 0;
        this._raf = 0;
        this._fpsFrames = 0; this._fpsTime = 0;
    }

    /** What a script sees as its globals (the engine's names). */
    ScriptGlobals(go) {
        const scene = this.scene;
        const GameObjectApi = Object.freeze({
            Find: name => scene.Find(String(name)),
            FindWithTag: tag => scene.FindWithTag(String(tag)),
            [Symbol.hasInstance]: v => v instanceof GameObject,
        });
        return {
            GameObject: GameObjectApi, Vector3, Input: this.input, Time: this.Time, Debug: this.Debug, Mathf,
            GetComponent: name => go.GetComponent(name),
            Destroy: obj => scene.Destroy(obj),
            gameObject: go, transform: go.transform, okay: okayAll,
        };
    }

    /** Put a script on an object (script text, a class or an object). Returns the ScriptComponent. */
    AddScript(go, source, options) { return go.AddComponent(new ScriptComponent(source, options)); }

    /** Run one frame of `dt` seconds (what start() does each animation frame; tests call it directly). */
    step(dt) {
        dt = Number.isFinite(dt) && dt > 0 ? Math.min(dt, this.maxDelta > 0 ? this.maxDelta : dt) : 0;
        const T = this.Time;
        this.input.BeginFrame();
        const scripts = this.scene.FindComponents(ScriptComponent);
        // new scripts are loaded at once; Awake, OnEnable and Start run together the first frame the script can run
        // (enabled, on an active object), as the engine's ScriptComponent::Start and JavaScriptVM::CallStart do
        for (const s of scripts) if (!s.instance && !s._failed && !s.Load(this)) s._failed = true;
        const live = [];
        for (const s of this.scene.FindComponents(ScriptComponent)) {
            if (!s.instance || !s.gameObject || s.gameObject.destroyed) continue;
            const on = s.enabled && s.gameObject.activeInHierarchy;
            if (!s._started) {
                if (!on) continue;
                s._started = true; s._enabledSeen = true;
                s.Call('Awake'); s.Call('OnEnable'); s.Call('Start');
            } else if (on !== s._enabledSeen) {
                s._enabledSeen = on; s.Call(on ? 'OnEnable' : 'OnDisable');
            }
            if (on) live.push(s);
        }
        const alive = s => s.gameObject && !s.gameObject.destroyed && s.enabled;
        T.unscaledDeltaTime = dt;
        const scaled = T.paused ? 0 : dt * T.timeScale;
        // fixed steps
        this._acc += scaled;
        let steps = 0;
        while (this._acc >= T.fixedDeltaTime && steps < this.maxSteps) {
            this._acc -= T.fixedDeltaTime;
            ++steps;
            T.deltaTime = T.fixedDeltaTime;
            for (const s of live) if (alive(s)) s.Call('FixedUpdate', T.fixedDeltaTime);
        }
        if (steps === this.maxSteps && this._acc >= T.fixedDeltaTime) this._acc %= T.fixedDeltaTime;   // drop what can't be caught up
        T.deltaTime = scaled;
        if (!T.paused) {
            for (const s of live) if (alive(s)) s.Call('Update', scaled);
            for (const s of live) if (alive(s)) s.Call('LateUpdate', scaled);
        }
        T.sessionTime += scaled;
        T.sessionUnscaledTime += dt;
        T.sessionFrameCount++;
        this._fpsFrames++; this._fpsTime += dt;
        if (this._fpsTime >= 0.5) { T.fps = this._fpsFrames / this._fpsTime; this._fpsFrames = 0; this._fpsTime = 0; }
        if (this.renderer) this.renderer.render(this.scene);
    }

    /** Run every animation frame until stop(). */
    start() {
        if (this._running) return this;
        this._running = true;
        this._last = 0;
        const frame = now => {
            if (!this._running) return;
            const dt = this._last ? (now - this._last) / 1000 : 0;
            this._last = now;
            this.step(dt);
            this._raf = requestAnimationFrame(frame);
        };
        this._raf = requestAnimationFrame(frame);
        return this;
    }
    stop() { this._running = false; if (this._raf) cancelAnimationFrame(this._raf); this._raf = 0; return this; }
    get running() { return this._running; }
}
