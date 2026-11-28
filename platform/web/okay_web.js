// OkaySpace web player: the page side of the WebAssembly player (our own code, CLAUDE.md rule 19a).
//
// Emscripten puts this file inside okay-player.js (--pre-js), before its own runtime code, so it can use the runtime's
// FS, IDBFS, ENV, addRunDependency and removeRunDependency. Everything the HTML shell (platform/web/shell.html) or a test
// reads is reached by string keys (Module['okay...']), so the Closure minifier never renames it.
//
// What it does:
//   1. Downloads the program (okay-player.wasm, or .wasm.gz) and the game data pack (game.okaypack.gz, written by
//      scripts/web_package.py) at the same time, reports the real byte progress, unpacks gzip with the browser's own
//      DecompressionStream, and writes the game files into the virtual file system before main() runs.
//   2. Keeps saves: a folder backed by the browser's IndexedDB (Emscripten's IDBFS) is mounted, read back before the game
//      starts, and written back shortly after any file in it is closed, renamed or deleted, and when the page is hidden.
//      The player finds the folder in the environment variable OKAY_WEB_SAVE_DIR.
//   3. Browser manners: no context menu on the canvas, no browser pan or zoom on touch, audio unlocked on the first click,
//      key or touch (browsers start audio suspended), and the canvas focused when clicked.
//
// Pack format (scripts/web_package.py writes it; gzip around the whole file):
//   8 bytes "OKWPACK1", u32 little-endian manifest length, manifest JSON {"files":[{"path","offset","size"}...]},
//   then the file bytes; each offset is from the end of the manifest.

var okayWeb = Module['okayWeb'] = Module['okayWeb'] || {};
var okayCfg = Module['okayWebConfig'] || {};
okayWeb['stats'] = okayWeb['stats'] || {};
okayWeb['saveSyncs'] = 0;
okayWeb['saveErrors'] = 0;

function okayStatus(text, done, total) {
    if (Module['okayOnProgress']) Module['okayOnProgress'](text, done || 0, total || 0);
}
function okayFail(text) {
    if (Module['okayOnError']) Module['okayOnError'](text);
    else console.error('[web] ' + text);
}

// ---- 1. downloads with real progress ----------------------------------------------------------------------------
var okayDownloads = {};   // file -> {done, total}
function okayReportDownloads() {
    var done = 0, total = 0;
    for (var k in okayDownloads) { done += okayDownloads[k].done; total += okayDownloads[k].total; }
    okayStatus('Downloading', done, total);
}
function okayLooksGzip(bytes) { return bytes.length > 2 && bytes[0] === 0x1f && bytes[1] === 0x8b; }
// Start a download: the response, or an error in plain words. Progress is counted by okayCounted below.
function okayGet(file, expectBytes) {
    okayDownloads[file] = { done: 0, total: expectBytes || 0, t0: performance.now() };
    okayReportDownloads();
    return fetch(file).then(function (resp) {
        if (!resp.ok) throw new Error('A game file is missing on the server: ' + file + ' (the server said ' + resp.status + ').');
        if (!expectBytes) okayDownloads[file].total = +(resp.headers.get('Content-Length') || 0);
        return resp;
    }, function (e) {
        if (location.protocol === 'file:')
            throw new Error('This game has to be opened from a web server, not straight from a folder on the disk. Use "Run in browser" in the editor, or upload the folder to a host such as itch.io.');
        throw new Error('Could not download ' + file + '. Check the internet connection and reload the page. (' + e.message + ')');
    });
}
// The body as a stream that counts the bytes as they arrive; resolves once the first piece is in, with that piece (to
// tell a gzip file from one the server already unpacked) and the whole stream (the first piece included).
function okayCounted(resp, file) {
    var reader = resp.body.getReader(), d = okayDownloads[file];
    function note(n) {
        d.done += n;
        if (d.total < d.done) d.total = d.done;
        okayReportDownloads();
    }
    return reader.read().then(function (first) {
        var head = first.done ? new Uint8Array(0) : first.value;
        note(head.length);
        var sentHead = false;
        var stream = new ReadableStream({
            pull: function (ctrl) {
                if (!sentHead) { sentHead = true; if (head.length) { ctrl.enqueue(head); return; } }
                if (first.done) { ctrl.close(); return; }
                return reader.read().then(function (r) {
                    if (r.done) {
                        okayWeb['stats'][file] = { 'bytes': d.done, 'ms': Math.round(performance.now() - d.t0) };
                        ctrl.close();
                        return;
                    }
                    note(r.value.length);
                    ctrl.enqueue(r.value);
                });
            }
        });
        return { head: head, stream: stream };
    });
}
// The whole file, unpacked when it is gzip (a server that already sent it with Content-Encoding: gzip hands over plain
// bytes, which are used as they are).
function okayFetch(file, expectBytes) {
    return okayGet(file, expectBytes).then(function (resp) {
        if (!resp.body || !resp.body.getReader)
            return resp.arrayBuffer().then(function (b) { okayDownloads[file].done = b.byteLength; okayReportDownloads(); return new Uint8Array(b); });
        return okayCounted(resp, file).then(function (c) {
            var gz = okayLooksGzip(c.head);
            if (gz && typeof DecompressionStream === 'undefined') throw okayTooOld();
            var s = gz ? c.stream.pipeThrough(new DecompressionStream('gzip')) : c.stream;
            return new Response(s).arrayBuffer().then(function (b) { return new Uint8Array(b); });
        });
    });
}
function okayTooOld() {
    return new Error('This browser is too old to unpack the game files. Please update it (Chrome 80, Firefox 113, Safari 16.4 or newer).');
}

if (typeof WebAssembly !== 'object') okayFail('This browser cannot run the game: it has no WebAssembly. Please use a current Chrome, Edge, Firefox or Safari.');

// The program. Compiled while it downloads and unpacks (instantiateStreaming over the unpacked stream), so a slow line
// and the compile overlap; browsers without streaming compile get the whole file first. Without a config (a plain emcc
// build, as CI makes it) Emscripten's own loader is used.
if (okayCfg['wasm']) {
    var okayWasmFile = okayCfg['wasm'];
    var okayWasmResp = okayGet(okayWasmFile, okayCfg['wasmBytes']);
    Module['instantiateWasm'] = function (imports, receiveInstance) {
        okayWasmResp.then(function (resp) {
            if (typeof WebAssembly.instantiateStreaming !== 'function' || !resp.body || !resp.body.getReader) {
                return okayFetch(okayWasmFile, okayCfg['wasmBytes']).then(function (bytes) { return WebAssembly.instantiate(bytes, imports); });
            }
            return okayCounted(resp, okayWasmFile).then(function (c) {
                var gz = okayLooksGzip(c.head);
                if (gz && typeof DecompressionStream === 'undefined') throw okayTooOld();
                var s = gz ? c.stream.pipeThrough(new DecompressionStream('gzip')) : c.stream;
                return WebAssembly.instantiateStreaming(new Response(s, { headers: { 'Content-Type': 'application/wasm' } }), imports);
            });
        }).then(function (r) {
            okayStatus('Starting', 1, 1);
            receiveInstance(r['instance'], r['module']);
        }, function (e) {
            okayFail(e && e.message ? e.message : String(e));
        });
        return {};   // the exports arrive later, through receiveInstance
    };
}

// The game data packs, unpacked into the file system before main().
function okayUnpack(bytes, name) {
    var magic = 'OKWPACK1';
    for (var i = 0; i < 8; ++i)
        if (bytes[i] !== magic.charCodeAt(i)) throw new Error('The game data file ' + name + ' is damaged (it is not an OkaySpace pack). Reload the page; if it happens again, export the game again.');
    var mlen = bytes[8] | (bytes[9] << 8) | (bytes[10] << 16) | (bytes[11] << 24);
    var manifest = JSON.parse(new TextDecoder().decode(bytes.subarray(12, 12 + mlen)));
    var base = 12 + mlen, files = manifest['files'] || [];
    for (var f = 0; f < files.length; ++f) {
        var e = files[f], path = e['path'];
        if (path.indexOf('..') >= 0 || path.charAt(0) !== '/') continue;   // only plain paths under the root
        var slash = path.lastIndexOf('/');
        if (slash > 0) FS.mkdirTree(path.substring(0, slash));
        FS.writeFile(path, bytes.subarray(base + e['offset'], base + e['offset'] + e['size']));
    }
    return files.length;
}
var okayPacks = (okayCfg['packs'] || []).map(function (p) { return { name: p['file'], bytes: okayFetch(p['file'], p['bytes']) }; });

// ---- 2. saves in IndexedDB -----------------------------------------------------------------------------------------
function okaySlug(s) {
    s = String(s || '').toLowerCase().replace(/[^a-z0-9]+/g, '_').replace(/^_+|_+$/g, '');
    return s.substring(0, 60) || 'game';
}
var okaySaveRoot = '/okaysave/' + okaySlug(okayCfg['gameId'] || 'game');
var okaySaveTimer = 0, okaySaveBusy = false, okaySaveAgain = false;
function okaySyncSaves() {
    if (okaySaveTimer) { clearTimeout(okaySaveTimer); okaySaveTimer = 0; }
    if (okaySaveBusy) { okaySaveAgain = true; return; }
    okaySaveBusy = true;
    FS.syncfs(false, function (err) {
        okaySaveBusy = false;
        if (err) { okayWeb['saveErrors']++; console.warn('[web] could not store the saves in the browser: ' + err); }
        else okayWeb['saveSyncs']++;
        if (okaySaveAgain) { okaySaveAgain = false; okaySyncSaves(); }
    });
}
function okaySaveSoon() {
    if (!okaySaveTimer) okaySaveTimer = setTimeout(okaySyncSaves, 250);
}
function okayInSaves(path) { return typeof path === 'string' && path.indexOf(okaySaveRoot + '/') === 0; }
function okayWatchSaves() {
    // A save is written through fopen / write / fclose (and renamed into place by the safe writers), so a closed
    // writable file, a rename, a delete or a removed folder under the save folder means "store it".
    var close = FS.close, rename = FS.rename, unlink = FS.unlink, rmdir = FS.rmdir;
    FS.close = function (stream) {
        var wrote = stream && (stream.flags & 3) !== 0 && okayInSaves(stream.path);
        var r = close.apply(this, arguments);
        if (wrote) okaySaveSoon();
        return r;
    };
    FS.rename = function (a, b) { var r = rename.apply(this, arguments); if (okayInSaves(a) || okayInSaves(b)) okaySaveSoon(); return r; };
    FS.unlink = function (p) { var r = unlink.apply(this, arguments); if (okayInSaves(p)) okaySaveSoon(); return r; };
    FS.rmdir = function (p) { var r = rmdir.apply(this, arguments); if (okayInSaves(p)) okaySaveSoon(); return r; };
}
function okayFlushNow() {
    if (Module['_okay_web_flush']) { try { Module['_okay_web_flush'](); } catch (e) {} }   // the player writes its pending prefs
    okaySyncSaves();
}
okayWeb['flushSaves'] = okayFlushNow;
document.addEventListener('visibilitychange', function () { if (document.visibilityState === 'hidden') okayFlushNow(); });
window.addEventListener('pagehide', okayFlushNow);

Module['preRun'] = Module['preRun'] || [];
if (typeof Module['preRun'] === 'function') Module['preRun'] = [Module['preRun']];
Module['preRun'].push(function () {
    // The saves: mount, then read what the browser kept before main() opens any file.
    try {
        FS.mkdirTree(okaySaveRoot);
        FS.mount(IDBFS, {}, okaySaveRoot);
        ENV['OKAY_WEB_SAVE_DIR'] = okaySaveRoot + '/';
        addRunDependency('okay-saves');
        FS.syncfs(true, function (err) {
            if (err) { okayWeb['saveErrors']++; console.warn('[web] saved games could not be read from the browser: ' + err); }
            okayWatchSaves();
            removeRunDependency('okay-saves');
        });
    } catch (e) {
        console.warn('[web] saves stay only until the page is closed: ' + e);
    }
    // The game files.
    okayPacks.forEach(function (p) {
        var dep = 'okay-pack-' + p.name;
        addRunDependency(dep);
        p.bytes.then(function (bytes) {
            okayStatus('Unpacking', 1, 1);
            var t0 = performance.now();
            var n = okayUnpack(bytes, p.name);
            okayWeb['stats']['unpack:' + p.name] = { 'files': n, 'bytes': bytes.length, 'ms': Math.round(performance.now() - t0) };
            removeRunDependency(dep);
        }).catch(function (e) { okayFail(e && e.message ? e.message : String(e)); });
    });
});

// ---- 3. browser manners ----------------------------------------------------------------------------------------------
// Audio: browsers create every AudioContext suspended until the person clicks, presses a key or touches. Keep a list of
// the contexts the game makes (the player's sound makes one, engine/src/Platform/WebBackend.cpp) and resume them all on the
// first gesture.
var okayAudioContexts = [];
(function () {
    var names = ['AudioContext', 'webkitAudioContext'];
    names.forEach(function (n) {
        var Real = window[n];
        if (!Real) return;
        var Wrapped = function (opts) {
            var ctx = opts === undefined ? new Real() : new Real(opts);
            okayAudioContexts.push(ctx);
            return ctx;
        };
        Wrapped.prototype = Real.prototype;
        window[n] = Wrapped;
    });
})();
function okayResumeAudio() {
    okayAudioContexts.forEach(function (c) { if (c.state === 'suspended' && c.resume) c.resume(); });
}
okayWeb['audioState'] = function () { return okayAudioContexts.map(function (c) { return c.state; }); };
okayWeb['gestured'] = false;
function okayGesture() {
    okayWeb['gestured'] = true;
    okayResumeAudio();
    if (Module['okayOnGesture']) Module['okayOnGesture']();
}
['pointerdown', 'keydown', 'touchstart', 'mousedown'].forEach(function (t) { window.addEventListener(t, okayGesture, true); });

// Keys the browser scrolls with. The player already blocks the arrows (keydown) and Space (keypress, which is when browsers
// scroll with it; WebBackend.cpp); these four make no text, so blocking their keydown loses nothing and keeps an embedding
// page still.
window.addEventListener('keydown', function (e) {
    if (e.key === 'PageUp' || e.key === 'PageDown' || e.key === 'Home' || e.key === 'End') e.preventDefault();
});

okayWeb['setupCanvas'] = function (canvas) {
    canvas.addEventListener('contextmenu', function (e) { e.preventDefault(); });   // right-click is a game button
    canvas.addEventListener('pointerdown', function () { if (document.activeElement !== canvas) canvas.focus(); });
    canvas.style.touchAction = 'none';   // no browser pan or pinch-zoom over the game
    // A lost WebGL context (a driver reset, too many tabs) cannot be brought back from here: say so instead of a black page.
    canvas.addEventListener('webglcontextlost', function (e) {
        e.preventDefault();
        okayFail('The graphics card stopped the game (the WebGL context was lost). Reload the page to play again.');
    });
};

// The player calls this after it shows its first frame (player/main.cpp); the shell hides the loading screen.
Module['okayFirstFrame'] = function () {
    okayWeb['stats']['firstFrameMs'] = Math.round(performance.now());
    if (Module['okayOnFirstFrame']) Module['okayOnFirstFrame']();
};
var okayPrevAbort = Module['onAbort'];
Module['onAbort'] = function (what) {
    if (okayPrevAbort) okayPrevAbort(what);
    okayFail('The game stopped with an error: ' + what + '. Reload the page to try again.');
};
if (Module['canvas']) okayWeb['setupCanvas'](Module['canvas']);
