// okay.js: our own small 3D and 2D library for the browser, with the engine's script API, so a game script written
// for the engine's JavaScript scripts runs unchanged in a plain web page. Plain ES modules, no build step, no
// dependencies. Design and coverage: docs/scripting/okayjs.md.
//
//   import { Game, Mesh, LitMaterial, MeshRenderer, Camera, Light, Vector3 } from './okay.js';

export { Vector2, Vector3, Quaternion, Matrix4, Color, Mathf, floatText } from './src/math.js';
export { Scene, GameObject, Transform, Component, Camera, Light, MeshRenderer, SpriteRenderer } from './src/scene.js';
export { Mesh } from './src/geometry.js';
export { Texture, LitMaterial, UnlitMaterial } from './src/material.js';
export { Renderer } from './src/renderer.js';
export { Input, keyName, isKnownKeyName } from './src/input.js';
export { ScriptComponent, LIFECYCLE, translateScript, topLevelNames } from './src/script.js';
export { Game, Time } from './src/game.js';

export const version = '0.1.0';
