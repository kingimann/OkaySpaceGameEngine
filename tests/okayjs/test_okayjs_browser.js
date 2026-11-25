// okay.js in headless Chromium (needs playwright and its Chromium; test tools only, nothing of them ships).
//
// usage: node tests/okayjs/test_okayjs_browser.js [--shots DIR]
//   --shots DIR  also save screenshots there (never into the repository); OKAY_SHOT_DIR works too
//
// Serves web/okayjs/ with Node's own http module and checks the examples: the spinning cube is drawn lit in the
// middle of the canvas (two faces of different brightness, the floor, the background, the 2D sprite in its corner),
// Space reaches the game script, the canvas follows a resized page and a pixel ratio of 2, WebGL 1 works when asked
// for and when the browser has no WebGL 2 (the fallback), and the script-API example (the engine test's script,
// unchanged) moves its ball while D is held. Exit code 0 when every check passes.
const http = require('http'), fs = require('fs'), path = require('path');
let chromium;
try { ({ chromium } = require('playwright')); } catch (_) { console.log('skipped: playwright is not installed'); process.exit(77); }

const args = process.argv.slice(2);
const shots = args.includes('--shots') ? args[args.indexOf('--shots') + 1] : process.env.OKAY_SHOT_DIR || '';
const root = path.resolve(__dirname, '../../web/okayjs');
const mime = { '.html': 'text/html', '.js': 'text/javascript', '.mjs': 'text/javascript', '.json': 'application/json' };

const server = http.createServer((req, res) => {
    const want = decodeURIComponent(new URL(req.url, 'http://x').pathname);
    const file = path.join(root, path.normalize(want));
    if (!file.startsWith(root + path.sep) || !fs.existsSync(file) || fs.statSync(file).isDirectory()) {
        res.writeHead(404); res.end(); return;
    }
    res.writeHead(200, { 'content-type': mime[path.extname(file)] || 'application/octet-stream', 'cache-control': 'no-store' });
    fs.createReadStream(file).pipe(res);
});

let failures = 0, passes = 0;
function check(ok, what, detail = '') {
    if (ok) { ++passes; console.log('ok   ' + what); } else { ++failures; console.log('FAIL ' + what + (detail ? ' -- ' + detail : '')); }
}

async function openExample(browser, page, { viewport = { width: 640, height: 400 }, scale = 1, query = '?test', init = null } = {}) {
    const ctx = await browser.newContext({ viewport, deviceScaleFactor: scale });
    if (init) await ctx.addInitScript(init);
    const p = await ctx.newPage();
    const errors = [];
    p.on('pageerror', e => errors.push(String(e)));
    p.on('console', m => { if (m.type() === 'error') errors.push(m.text()); });
    await p.goto(base + '/examples/' + page + query);
    await p.waitForFunction(() => window.okayExample && (window.okayExample.ready || window.okayExample.error), null, { timeout: 20000 });
    const err = await p.evaluate(() => window.okayExample.error || '');
    if (err) errors.push(err);
    await frames(p, 5);
    return { ctx, p, errors };
}
const frames = (p, n) => p.evaluate(n => new Promise(done => {
    const g = window.okayExample.game, until = g.Time.sessionFrameCount + n;
    const wait = () => (g.Time.sessionFrameCount >= until ? done() : requestAnimationFrame(wait));
    wait();
}), n);
const pixel = (p, fx, fy) => p.evaluate(([fx, fy]) => {
    const r = window.okayExample.game.renderer, c = r.canvas;
    return r.readPixel(Math.floor(fx * (c.width - 1)), Math.floor(fy * (c.height - 1)));
}, [fx, fy]);
const shot = async (p, name) => { if (shots) { fs.mkdirSync(shots, { recursive: true }); await p.screenshot({ path: path.join(shots, name) }); } };
const orange = px => px[0] > 60 && px[0] > px[1] * 1.4 && px[1] > px[2];   // the cube's orange, lit or in shade
const bright = px => px[0] + px[1] + px[2];

/** Stop the loop and draw one frame with the cube at a known turn, so the pixel checks do not depend on timing. */
const freeze = p => p.evaluate(() => {
    const ex = window.okayExample;
    ex.game.stop();
    ex.script.Set('yaw', 30);
    ex.game.step(0);
});
const resume = p => p.evaluate(() => { window.okayExample.game.start(); });

/** The lit cube in the middle: orange, and at least two faces of clearly different brightness. */
async function cubeChecks(p, label) {
    await freeze(p);
    const mid = await pixel(p, 0.5, 0.5);
    check(orange(mid), label + ': the middle of the canvas is the orange cube', JSON.stringify(mid));
    const samples = [];
    for (let i = 0; i < 9; ++i) for (let j = 0; j < 9; ++j) samples.push(await pixel(p, 0.42 + i * 0.02, 0.38 + j * 0.03));
    const lit = samples.filter(orange).map(bright);
    const spread = lit.length ? Math.max(...lit) - Math.min(...lit) : 0;
    check(lit.length > 20 && spread > 40, label + ': its faces are lit differently (directional light)', `${lit.length} cube pixels, brightness spread ${spread}`);
    const bg = await pixel(p, 0.98, 0.04);
    check(Math.abs(bg[0] - 49) < 6 && Math.abs(bg[1] - 77) < 6 && Math.abs(bg[2] - 121) < 6, label + ': the corner is the camera background', JSON.stringify(bg));
    await resume(p);
}

let base = '';
(async () => {
    await new Promise(r => server.listen(0, '127.0.0.1', r));
    base = 'http://127.0.0.1:' + server.address().port;
    const browser = await chromium.launch({ args: ['--use-angle=swiftshader', '--enable-unsafe-swiftshader'] });
    try {
        // ---- WebGL 2, pixels, sprite, key, resize
        {
            const { ctx, p, errors } = await openExample(browser, 'spinning_cube.html');
            check(await p.evaluate(() => window.okayExample.game.renderer.webglVersion) === 2, 'WebGL 2 is used when there is one');
            await cubeChecks(p, 'WebGL 2');
            await freeze(p);
            const floor = await pixel(p, 0.5, 0.93);
            check(Math.abs(floor[0] - floor[2]) < 60 && bright(floor) > 60 && !orange(floor), 'the floor is drawn below the cube', JSON.stringify(floor));
            const badge = await pixel(p, 0.06, 0.06), bg = await pixel(p, 0.98, 0.04);
            check(bright(badge) < bright(bg) - 60, 'the 2D sprite (orthographic camera) is drawn in the top-left corner', JSON.stringify(badge));
            await resume(p);
            const log = await p.evaluate(() => window.okayExample.game.log.map(l => l.text));
            check(log.includes('spinning Cube'), 'the script ran Start (Debug.Log reached the game log)', JSON.stringify(log));
            const yaw0 = await p.evaluate(() => window.okayExample.script.Get('yaw'));
            await frames(p, 10);
            const yaw1 = await p.evaluate(() => window.okayExample.script.Get('yaw'));
            check(yaw1 > yaw0, 'Update turns the cube every frame', `${yaw0} -> ${yaw1}`);
            await shot(p, 'okayjs_cube_webgl2.png');

            await p.keyboard.press('Space');
            await frames(p, 3);
            const st = await p.evaluate(() => ({ presses: window.okayExample.script.Get('presses'), speed: window.okayExample.script.Get('speed') }));
            check(st.presses === 1 && st.speed < 0, 'a Space press reaches the script (Input.GetKeyDown("space"))', JSON.stringify(st));
            const scroll = await p.evaluate(() => window.scrollY);
            check(scroll === 0, 'Space does not scroll the page');

            await p.setViewportSize({ width: 900, height: 300 });
            await frames(p, 4);
            const size = await p.evaluate(() => { const c = window.okayExample.game.renderer.canvas; return [c.width, c.height]; });
            check(size[0] === 900 && size[1] === 300, 'the canvas follows a resized page', JSON.stringify(size));
            await freeze(p);
            check(orange(await pixel(p, 0.5, 0.5)), 'after the resize the cube is still in the middle');
            await resume(p);
            const aspect = await p.evaluate(() => { const r = window.okayExample.game.renderer; return r.width / r.height; });
            check(Math.abs(aspect - 3) < 0.01, 'the renderer size matches the new shape', String(aspect));
            await shot(p, 'okayjs_cube_resized.png');
            check(errors.length === 0, 'no page errors (WebGL 2 page)', errors.join(' | '));
            await ctx.close();
        }
        // ---- pixel ratio 2
        {
            const { ctx, p, errors } = await openExample(browser, 'spinning_cube.html', { viewport: { width: 400, height: 300 }, scale: 2 });
            const size = await p.evaluate(() => { const c = window.okayExample.game.renderer.canvas; return [c.width, c.height, window.okayExample.game.renderer.pixelRatio]; });
            check(size[0] === 800 && size[1] === 600 && size[2] === 2, 'at a pixel ratio of 2 the canvas has twice the device pixels', JSON.stringify(size));
            await freeze(p);
            check(orange(await pixel(p, 0.5, 0.5)), 'at a pixel ratio of 2 the cube is in the middle');
            check(errors.length === 0, 'no page errors (pixel ratio 2)', errors.join(' | '));
            await ctx.close();
        }
        // ---- WebGL 1 when asked for
        {
            const { ctx, p, errors } = await openExample(browser, 'spinning_cube.html', { query: '?test&webgl=1' });
            check(await p.evaluate(() => window.okayExample.game.renderer.webglVersion) === 1, 'WebGL 1 is used when asked for (?webgl=1)');
            await cubeChecks(p, 'WebGL 1');
            await shot(p, 'okayjs_cube_webgl1.png');
            check(errors.length === 0, 'no page errors (WebGL 1 page)', errors.join(' | '));
            await ctx.close();
        }
        // ---- the fallback: a browser without WebGL 2
        {
            const noWebGL2 = () => {
                const get = HTMLCanvasElement.prototype.getContext;
                HTMLCanvasElement.prototype.getContext = function (kind, ...rest) { return kind === 'webgl2' ? null : get.call(this, kind, ...rest); };
            };
            const { ctx, p, errors } = await openExample(browser, 'spinning_cube.html', { init: noWebGL2 });
            check(await p.evaluate(() => window.okayExample.game.renderer.webglVersion) === 1, 'without WebGL 2 the renderer falls back to WebGL 1');
            await freeze(p);
            const mid = await pixel(p, 0.5, 0.5);
            check(orange(mid), 'the fallback draws the cube', JSON.stringify(mid));
            check(errors.length === 0, 'no page errors (fallback)', errors.join(' | '));
            await ctx.close();
        }
        // ---- the engine test's script, unchanged, in a web page
        {
            const { ctx, p, errors } = await openExample(browser, 'script_api.html');
            const x0 = await p.evaluate(() => window.okayExample.player.transform.position.x);
            await p.keyboard.down('d');
            await frames(p, 20);
            await p.keyboard.up('d');
            await frames(p, 5);
            const st = await p.evaluate(() => ({ moved: window.okayExample.script.Get('moved'), x: window.okayExample.player.transform.position.x,
                                                 dt: window.okayExample.script.Get('dtSeen') }));
            check(st.moved >= 15 && st.x > x0 + 0.1, 'holding D moves the ball (key_mover.js: Input.GetKey, Translate, Vector3.right.scale)', JSON.stringify(st));
            check(st.dt > 0 && st.dt <= 0.25, 'Time.deltaTime reads the frame time', String(st.dt));
            const moved = st.moved;
            await frames(p, 5);
            check(await p.evaluate(() => window.okayExample.script.Get('moved')) === moved, 'after D is let go the ball stops');
            await shot(p, 'okayjs_script_api.png');
            check(errors.length === 0, 'no page errors (script API page)', errors.join(' | '));
            await ctx.close();
        }
    } catch (e) {
        check(false, 'the browser run finished', e && e.stack || String(e));
    } finally {
        await browser.close();
        server.close();
    }
    console.log(`${passes} passed, ${failures} failed`);
    process.exit(failures ? 1 : 0);
})();
