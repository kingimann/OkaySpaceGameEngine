#pragma once
// The Material Graph window (MaterialGraphWindow.cpp): opens a `.okaymatgraph` (engine: Render/MaterialGraph.hpp),
// shows the graph as a read-only canvas plus an editable node list, the checks, and a preview sphere drawn by the
// CPU evaluator. Nothing here changes what the game draws yet. Written from our own design.
#include <string>
#include <vector>

struct SDL_Renderer;

namespace oked {

extern bool g_showMaterialGraph;   // the window's visibility flag (Window menu)

/// Draw the window (no-op while hidden). `renderer` makes the preview texture.
void DrawMaterialGraphWindow(SDL_Renderer* renderer);
/// Open a .okaymatgraph in the window and show it. Returns false (and logs to the Console) when the file is bad.
bool OpenMaterialGraphFile(const std::string& path);
/// Open the built-in example graph (unsaved) in the window.
void OpenMaterialGraphExample();
/// The example graphs shipped in examples/material_graph, found next to the program or the current folder.
std::vector<std::string> MaterialGraphExampleFiles();

} // namespace oked
