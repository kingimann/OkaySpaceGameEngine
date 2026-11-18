---
id: tex1
row: | tex1 | Texture tools (our own): mipmaps, atlas, BC compression, DDS | Windows release: `examples\texture\try_texture.bat` (needs `Tools\okay-texture.exe`); editor: Project panel > pick a picture > Import Settings > "Generate mipmaps / compress" | Unverified on Windows; Linux tests and CLI run |
---
## tex1. Texture tools: mipmaps, atlas, compression

What it is: our own texture library (`docs/own_texture_library.md`). It makes mip chains (box, Lanczos, Kaiser,
colour-correct), packs sprites into an atlas with a manifest, compresses pictures to BC1/BC3/BC4/BC5 and writes
`.dds` files you can open in a DDS viewer, and plans which mip each texture needs for a memory budget. It only adds
tools: nothing existing is switched off or replaced, so there is no switch to turn on.

**Try it from the release (command line)**
1. Unzip the Windows release. Open `examples\texture` and double-click `try_texture.bat`.
2. A console runs six steps and a new `out` folder appears with: `checker_0.png` ... `checker_7.png` (the mip
   levels, each half the size), `checker.dds`, `fence.dds`, `bumps.dds`, `icons.png` and `icons.json`.
3. What you should see in the console: `wrote 8 mip level(s)`, then `BC1 128x128, 8 mip level(s)` with a list of
   level sizes (128x128 down to 1x1), `BC3` for the fence (it has see-through parts), `BC5` for the normal map,
   `packed 3 picture(s)`, and the last line says which mip the streaming planner keeps at 100 pixels on screen.
4. Open `out\icons.png` in any picture viewer: three icons side by side, one turned sideways (`"rotated": true` in
   `icons.json`). Open `out\checker_3.png`: a smaller, smooth checker, not a blocky one.
5. Your own pictures: `Tools\okay-texture.exe compress my.png` writes `my.dds`; run `okay-texture.exe` alone for
   every command and option (`--filter box|lanczos|kaiser`, `--srgb`, `--normal`, `--cutout`, `--format bc1|bc3|bc4|bc5`).

**Try it in the editor**
1. Open the editor (OkayEngine.exe), open any project, click a `.png` in the Project panel.
2. In the details strip, Import Settings, scroll to "MIPMAPS AND COMPRESSION". Pick "Mipmaps + compressed",
   a filter, tick "Colour picture" for photos and colours, then click "Generate mipmaps / compress".
3. You should see a line "Wrote <your picture>.dds" under the button, and the `.dds` file next to the picture
   (open it in a DDS viewer; the Project panel may need a refresh to list it). The picture and its import settings
   are not changed. The game and editor renderers do not load `.dds` yet (see the doc, "Not done").

Verified: unit tests `tex_mip`, `tex_atlas`, `tex_convert`, `tex_bc`, `tex_dds_stream` and the six commands above
run on Linux (headless). Not verified: the
Windows build of `okay-texture.exe`, the `.bat`, the editor section in a real window, and any DDS viewer.
