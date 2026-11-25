// okay.js materials and textures. A material says how a mesh is shaded; the renderer (renderer.js) holds the
// GPU side. Change `version` (or call MarkChanged) after replacing a texture's pixels so it is uploaded again.

import { Color } from './math.js';

/** A picture for a material or a sprite: an <img> (loaded), <canvas>, ImageBitmap, ImageData or
 *  { width, height, data: Uint8Array RGBA }. filter 'linear' (smooth) or 'nearest' (pixel art); wrap 'repeat' or
 *  'clamp'. WebGL 1 can only repeat and mipmap pictures whose sides are powers of two; others are clamped there. */
export class Texture {
    constructor(source, { filter = 'linear', wrap = 'clamp', mipmaps = true } = {}) {
        this.source = source;
        this.filter = filter;
        this.wrap = wrap;
        this.mipmaps = mipmaps;
        this.version = 0;
    }
    get width() { return this.source ? this.source.naturalWidth || this.source.width : 0; }
    get height() { return this.source ? this.source.naturalHeight || this.source.height : 0; }
    MarkChanged() { this.version++; }
    /** Load a picture from a URL; resolves once it can be drawn. */
    static Load(url, options) {
        return new Promise((resolve, reject) => {
            const img = new Image();
            img.onload = () => resolve(new Texture(img, options));
            img.onerror = () => reject(new Error('The picture ' + url + " couldn't be loaded"));
            img.src = url;
        });
    }
}

/** Shaded by the scene's lights: ambient + diffuse (Lambert) + a Blinn-Phong highlight, per pixel. */
export class LitMaterial {
    constructor({ color = Color.white, texture = null, specular = new Color(0.25, 0.25, 0.25), shininess = 32,
                  doubleSided = false, transparent = false } = {}) {
        this.color = color; this.texture = texture; this.specular = specular; this.shininess = shininess;
        this.doubleSided = doubleSided; this.transparent = transparent;
    }
    get kind() { return 'lit'; }
}

/** Not shaded: the colour (times the texture) as it is. */
export class UnlitMaterial {
    constructor({ color = Color.white, texture = null, doubleSided = false, transparent = false } = {}) {
        this.color = color; this.texture = texture; this.doubleSided = doubleSided; this.transparent = transparent;
    }
    get kind() { return 'unlit'; }
}
