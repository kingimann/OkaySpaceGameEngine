#pragma once
// The beginner view of the Modeling window (ModelingEasyUi.cpp): five tabs named by what you want to do, big plain-named
// buttons, a "What do I click next?" line, quick actions for the selection, a recent-tools strip, Undo / Redo / Repeat
// buttons and a switch to the full (advanced) window. The logic lives in ModelingEasy.hpp / ModelingEasyOps.hpp (tested);
// main.cpp only lends this the editor's selection and undo through the Host callbacks below.
#include "ModelingEasyOps.hpp"
#include "okay/Render/Mesh.hpp"
#include <functional>
#include <string>
#include <vector>

namespace oked::easymodel_ui {

struct Host {
    oked::easymodel::EditState state;                         // filled by the caller every frame (cheap: a few numbers)
    okay::Mesh* mesh = nullptr;              // the selected model's mesh, or null (read only here: size line)
    std::function<void(const std::string& shapeName)> addShape;                       // ed.CreateMesh
    std::function<void(const okay::Mesh&, const std::string& name)> addModel;         // a starter, as a new object
    std::function<void(const std::string& label, const std::function<void(okay::Mesh&)>&)> meshOp;   // ModelOp: one undo step
    std::function<oked::easymodel::ToolArgs()> args;          // the current picks
    std::function<void(int)> setGizmo;       // 0 move, 1 turn, 2 bigger
    std::function<void(bool on, int mode)> pickMode;   // edit mode on/off, 0 corners 1 edges 2 faces
    std::function<void()> undo, redo, addSolid, saveAsset;
    std::function<bool()> canUndo, canRedo;
    std::function<void*(const std::string& key, const std::function<okay::Mesh()>& build)> thumb;   // optional small picture
    std::function<void(const std::string&, int)> log;
    std::function<std::vector<std::string>()> loadRecent;                             // the last tools used, from the settings
    std::function<void(const std::vector<std::string>&)> saveRecent;                  // ... and back when they change
    bool mirror = true;                                                               // mirror edits left to right (settings)
    std::function<void(bool)> setMirror;
    std::function<void(const std::vector<int>& faces, const std::vector<int>& corners)> setPicks;   // change what is picked
    int openTab = -1;                        // when 0..4: switch to that tab this frame (screenshots, tests)
};

/// Draw the beginner view inside the Modeling window. `showAdvanced` is the remembered switch.
void DrawEasyModeling(Host& h, bool& showAdvanced);

} // namespace oked::easymodel_ui
