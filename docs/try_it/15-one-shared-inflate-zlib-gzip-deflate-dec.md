---
id: 15
group: switches
row: | 15 | One shared inflate (zlib/gzip) decoder: PNG, `.blend`, FBX, `.x`, `.zgl`, 3MF/AMF, HTTP gzip | open a PNG texture; `examples\own_readers\cube_gzip.blend`, `cube_compressed_arrays.fbx` | PNG always on; the readers need their switch; HTTP gzip is Linux only |
---
### 15. One shared inflate (zlib / gzip / DEFLATE) decoder

**What it is.** Compressed data in PNG pictures, gzip `.blend` files, `.x` MSZIP, `.zgl`, 3MF/AMF zip packages, the arrays
in binary FBX files and gzip answers from our own HTTP client used to be decoded by three separate copies of the same
decoder. Now there is one (`engine/include/okay/Core/Inflate.hpp`) and the others call it. Nothing should look different:
this change is about having less code to get wrong. Plain words: if every picture and model that worked before still
loads, it worked.

**Steps.**
1. **PNG (always on).** In the editor, **File > Import Model...** and choose `examples\obj_texture_paths\cube_ok.obj`. The
   cube shows the grid texture. That texture is `grid.png`, a PNG, and every PNG goes through the shared decoder. Any
   other PNG you drop in as a texture does the same.
2. **gzip `.blend`.** `set OKAY_OWN_BLEND=1` (or `OKAY_OWN_STACK=1`), start the editor, **File > Import Model...**,
   choose `examples\own_readers\cube_gzip.blend` (a Blender file saved with "Compress File", our own test file). A small model
   appears (the `blend_reader` test checks its numbers). (Without Assimp in the build this reader is the only way a `.blend` loads at all.)
3. **FBX with compressed arrays.** `set OKAY_OWN_FBX=1`, start the editor, import
   `examples\own_readers\cube_compressed_arrays.fbx` (a binary FBX 7.4 whose vertex and index arrays are zlib packed).
   A cube should appear. (The About popup should then list `fbx` among the own readers.)
4. **Not shipped as samples:** `.x` with MSZIP, `.zgl`, 3MF and AMF. Their files are only built inside the tests
   (`x_reader`, `xgl_reader`, `3mf_reader`, `amf_reader`). If you have your own `.x` (MSZIP), `.zgl` or `.3mf` file,
   import it with `OKAY_OWN_X=1`, `OKAY_OWN_XGL=1` or `OKAY_OWN_3MF=1`.
5. **gzip HTTP answers.** Only our own HTTP client uses this, and it cannot be tried on Windows (see 10). On Linux:
   build with `-DOKAY_OWN_HTTP=ON`, run with `OKAY_OWN_HTTP=1`, and fetch a URL whose server sends
   `Content-Encoding: gzip`; the program must get the plain bytes (the tests `own_http` and `own_http_net` do this).

**How you know it worked.** There is no log line that names the decoder (it is a function, not a switch). What you can
see: the grid texture on the cube (step 1), the cube from each file (steps 2 and 3), and, on a machine with a
build, the test run `ctest -R inflate_differential` (and `ctest -R png_hint`), which prints how many damaged and valid
streams it compared with an independent slow decoder and ends with `ok:`.

**Status.** Verified here (Linux, headless): `inflate_differential` (about 67,000 streams, ASan and UBSan clean),
`own_http_inflate`, `png_hint`, and the existing `image_decoder`, `blend_reader`, `3mf_reader`, `amf_reader`, `fbx_reader`,
`fbx_hostile`, `x_reader`, `xgl_reader`, `own_http` tests. **Unverified:** every editor step above (nothing here opens a
real editor window), anything on Windows, real-world files other than our own samples.
