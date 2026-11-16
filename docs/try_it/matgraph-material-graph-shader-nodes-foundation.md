---
id: matgraph
row: | matgraph | Material Graph: build a surface from nodes (texture, noise, fresnel, maths), see it on a preview sphere | `Tools\OkayEngine.exe`: File > Examples > Material Graph (shader nodes); Window > Material Graph; `examples\material_graph\` | New in this change; foundation only (no renderer uses it yet), see section matgraph |
---
## matgraph. Material Graph (shader nodes) foundation

What it is: a new asset `.okaymatgraph` that describes a surface as connected nodes (colours, a texture, noise, fresnel rim, time, maths) with an Output node (base colour, emission, roughness, metallic, alpha, normal). A window checks the graph and draws it on a preview sphere with the CPU. **Today this does not change what a game draws** (no renderer reads a graph yet). Full description: [material_graph.md](material_graph.md).

Where: the editor `Tools\OkayEngine.exe`. The example files ship in `examples\material_graph\` (copied by the release zip and installer).

1. Menu **File > Examples > Material Graph (shader nodes) > rim_and_noise**. The **Material Graph** window opens. You should see a preview sphere (warm red-brown noise with a soft blue rim), the text "Graph is valid (0 warnings)", a canvas of boxes joined by blue wires, and a list of nodes below.
2. Tick **Animate Time**: the blue rim glow pulses (the graph multiplies it by a sine of Time).
3. In the node list open **#2 Vec3** (the dark colour) and change its Value with the colour picker: the sphere changes at once.
4. Open the node **#1 Output** and, for the input **Roughness**, pick **(value)** in the drop-down and drag the number to 0.05: the highlight gets small and sharp.
5. Menu **File > Examples > Material Graph (shader nodes) > checker_metal**: a checker pattern (it uses the built-in `proc:checker` texture) with a metal look.
6. Pick **Add Node** > **Math / Multiply** and press **Add Node**: a new node appears; wire something into it with the drop-downs. Make a mistake on purpose: wire a Vec3 node into Output **Roughness**: a red problem line says "cannot wire a Vec3 into input 'Roughness'". Undo that by picking **(value)** again.
7. **Save** (a file dialog asks where for the unsaved example). The file is plain text: open it in any text editor. In the Project panel a `.okaymatgraph` shows as "Material graph"; double-click it to open it again.

What was run: the engine part (`ctest -R matgraph`: file round trip, bad files, the checks, 28 nodes against hand-computed numbers, golden GLSL and HLSL text, preview sphere) on Linux. Headless renders of the preview: [rim_and_noise](images/material_graph/matgraph_rim_and_noise.png), [checker_metal](images/material_graph/matgraph_checker_metal.png) (made by the test with `OKAY_MATGRAPH_SHOT_DIR=<folder>`).

Unverified: the editor window itself (syntax-checked only, never run here: no display), Windows, and the GLSL and HLSL text on a real GPU compiler. Not done: dragging nodes and wires on the canvas, undo, wiring a graph into a renderer or a material.
