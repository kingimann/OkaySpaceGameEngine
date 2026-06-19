#pragma once
#include "okay/Scene/Component.hpp"
#include <string>
#include <vector>

namespace okay {

class GameObject;

/// A "UI Toolkit"-style document: a block of **OkayUI** markup that describes a
/// whole tree of UI widgets as text, which the engine turns into real widget
/// GameObjects. Think Unity's UXML — but line-based, indentation-nested, and
/// built into the engine — so designers can author entire menus and HUDs in one
/// editable document and reuse them, instead of hand-placing every element.
///
/// Each non-blank, non-comment line is one widget:
///
///     panel pos=40,40 size=360,240 color=30,36,52,220
///       text "MY GAME" pos=30,30 size=5
///       button "Play" pos=30,150 size=300,60 anchor=center onclick=load_scene("game")
///       slider pos=30,230 size=300,16 value=0.5
///       toggle "Sound" pos=30,270 size=28,28 on=1
///
/// Indentation nests a widget under the one above it (a button inside a panel).
/// Widget types: panel, text, button, image, slider, toggle, progress. Keys:
/// pos=x,y  size=x,y (or one number for text px)  color=r,g,b[,a] (0-255)
/// anchor=topleft|topcenter|topright|middleleft|center|middleright|bottomleft|
/// bottomcenter|bottomright  value=f  on=0|1  onclick=<okayscript>.
class UIDocument : public Behaviour {
public:
    std::string markup;

    /// Parse `markup` and (re)build the widgets as child GameObjects, replacing
    /// any previously generated ones. Safe to call from the editor or at runtime.
    void Rebuild();

    /// The widget GameObjects produced by the last Rebuild().
    const std::vector<GameObject*>& Generated() const { return m_generated; }

    void Start() override { Rebuild(); }
    void OnDestroy() override { ClearGenerated(); }

private:
    std::vector<GameObject*> m_generated;
    void ClearGenerated();
};

} // namespace okay
