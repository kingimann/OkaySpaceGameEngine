// okay.js renderer against a fake WebGL context in plain Node (no browser): GPU object bookkeeping only. Pixels are
// checked in headless Chromium by test_okayjs_browser.js.
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { Renderer, Scene, Camera, SpriteRenderer, Texture } from '../../web/okayjs/okay.js';

/** A WebGL context that accepts every call, counts them, and answers the queries the renderer makes. */
function fakeGL() {
    const counts = {};
    const gl = new Proxy({}, {
        get(_, name) {
            if (typeof name !== 'string') return undefined;
            if (/^[A-Z0-9_]+$/.test(name)) return name.length;   // a constant
            return (...args) => {
                counts[name] = (counts[name] || 0) + 1;
                if (/Parameter$/.test(name)) return true;
                if (name === 'isContextLost') return false;
                if (name === 'getExtension') return {};
                return { made: name, args: args.length };
            };
        },
    });
    return { gl, counts };
}
function fakeCanvas(gl) {
    return { width: 100, height: 100, clientWidth: 100, clientHeight: 100, style: {}, getContext: () => gl, addEventListener() {} };
}

test('review 3 and 9: a picture still loading costs no texture per frame, and nothing needs HTMLImageElement', () => {
    const { gl, counts } = fakeGL();
    const r = new Renderer(fakeCanvas(gl));
    const s = new Scene();
    s.CreateGameObject('Cam').AddComponent(Camera);
    const img = { complete: false, naturalWidth: 0, width: 0, height: 0 };   // an <img> that has not loaded (and no DOM here)
    const tex = new Texture(img);
    s.CreateGameObject('Sprite').AddComponent(SpriteRenderer, { texture: tex });
    const before = counts.createTexture;
    for (let i = 0; i < 5; ++i) r.render(s);
    assert.equal(counts.createTexture, before);   // nothing made while it loads
    img.complete = true; img.naturalWidth = img.width = 2; img.naturalHeight = img.height = 2;
    for (let i = 0; i < 5; ++i) r.render(s);
    assert.equal(counts.createTexture, before + 1);   // made once when it can be drawn, then reused
    assert.equal(counts.texImage2D - 1, 1);           // uploaded once (the other upload is the white 1x1)
});
