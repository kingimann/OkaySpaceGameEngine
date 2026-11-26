#pragma once
// Taking a piece of the OkayUI frame out, to be drawn by someone else (own code).
//
// OkayUI collects the whole frame into one batch and an app draws it once, after everything else. A host that draws OkayUI
// inside another toolkit's window (the editor's Script Editor draws OkayUI::CodeEdit inside a Dear ImGui window) needs that
// widget's shapes at the place in the other toolkit's draw order where the window is, so the other toolkit's popups, tooltips
// and dialogs drawn later cover it. BeginCapture / EndCapture cut the shapes made between them out of the frame and hand them
// over; the rest of the frame is untouched, and input (hover, clicks, keyboard focus) works as before.
//
// GetTexturePixels says what a DrawCmd::texture holds, so the host can upload it as a texture of its own.
#include "okay/UI/OkayUI.hpp"

#include <vector>

namespace OkayUI {

/// Shapes cut out of the frame: the same vertices, indices and commands GetDrawData gives (indices start at 0 in `vertices`).
/// Commands keep their clip rectangle and texture; a command's texture is null (flat colour), or a texture GetTexturePixels knows.
struct CapturedGeometry {
    std::vector<DrawVertex> vertices;
    std::vector<int> indices;
    std::vector<DrawCmd> cmds;
    /// The first command of the overlay layer (popups, open lists, the foreground list); cmds.size() when none was taken.
    int overlayFrom = 0;
    void Clear() { vertices.clear(); indices.clear(); cmds.clear(); overlayFrom = 0; }
    bool Empty() const { return indices.empty(); }
};

/// What BeginCapture takes. Capture_All: the main and the overlay layer (popups, open lists, the foreground list), all of it drawn
/// where the host puts the captured shapes (the Script Editor's code). Capture_KeepOverlay: only the main layer; what the captured
/// windows draw on the overlay layer stays in the frame and is drawn on top of everything as usual (a hosted panel inside a Dear
/// ImGui window: the panel lies in ImGui's order, its own menus, lists and modal dialogs still cover the whole screen).
enum CaptureFlags { Capture_All = 0, Capture_KeepOverlay = 1 };

/// Start taking the shapes drawn from here on out of the frame (`flags`: CaptureFlags). Nested calls are counted: only the
/// outermost pair captures (with the outermost call's flags), an inner EndCapture hands back nothing.
void BeginCapture(int flags = Capture_All);
/// Stop, move the shapes made since BeginCapture into `out` (replacing what it held) and remove them from the frame. Windows begun
/// and ended in between keep their place for the mouse (hover, clicks, scrolling), they just draw nothing in this frame's batch.
/// Without a matching BeginCapture, `out` is emptied and nothing changes.
void EndCapture(CapturedGeometry& out);
/// True between BeginCapture and its EndCapture.
bool Capturing();

/// The pixels behind a DrawCmd::texture: RGBA8, top row first, `w` x `h`. `generation` changes when the pixels change (a font
/// baked new characters), so a copy can be refreshed. False for null and for textures only a backend can resolve (images).
struct TexturePixels { const unsigned char* rgba = nullptr; int w = 0, h = 0; unsigned generation = 0; };
bool GetTexturePixels(const void* texture, TexturePixels& out);
/// True for a texture the host itself gave OkayUI (Image, ImageButton, DrawList::AddImage, PushTexture: an SDL texture or whatever
/// the host's renderer uses), which a host can hand on to its other toolkit as it is (the launcher's Browser page images, drawn in
/// Dear ImGui's order: okayuihost::ImGuiEmbed::SetHostTextures). False for null, the launcher look's atlas and fallback-font atlases
/// (OkayUI's own placeholders: GetTexturePixels gives their pixels).
bool IsHostTexture(const void* texture);

} // namespace OkayUI
