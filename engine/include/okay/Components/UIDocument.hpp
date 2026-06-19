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
/// Widget types: panel, text, button, image, slider, toggle, progress, input,
/// dropdown, scroll, layout. Common keys:
///   pos=x,y  size=x,y (or one number for text px)  color=r,g,b[,a] (0-255)
///   anchor=topleft|topcenter|topright|middleleft|center|middleright|bottomleft|
///   bottomcenter|bottomright  corner=<radius>  tooltip="hint" [tipdelay=s]
///   name=<id> (addressable by the ui_* script API)  active=0|1
/// Per-type keys:
///   panel:    border=w bordercolor=r,g,b,a gradient=r,g,b,a
///   button:   hover= pressed= textcolor= font=<scale> border= onclick=<script>
///   text:     align=left|center|right  outline=r,g,b,a  shadow=r,g,b,a
///             bind="Score: {score}"  (live data binding to Prefs values)
///   slider:   value= min= max= fill= knob= showvalue=1 onchange=<script>
///   toggle:   on=1 check=r,g,b,a ontoggle=<script>
///   progress: value= fill= percent=1
///   image:    texture=path nineslice=1 border=px fill=left|right|up|down amount=f
///   input:    placeholder="..." max=N onsubmit=<script>  (label = initial text)
///   dropdown: options=A|B|C value=i onchange=<script>
///   scroll:   content=<px> bar=r,g,b,a   (parent widgets to it to clip+scroll)
///   layout:   dir=vertical|horizontal spacing= padding=  (auto-arranges kids)
/// Event handlers (on*) take the rest of the line, so put them last.
///
/// Two "advanced" constructs make UIs reusable (UI-Toolkit-like):
///   * style <name> <keys...>   — a USS-style class. A widget pulls it in with
///     `class=<name>`; the widget's own keys override the style.
///   * define <name>            — a custom widget. Its indented block is a
///     template; an instance line `<name> pos=x,y` expands a copy of the block
///     shifted to that position. Build your own controls once, reuse them.
class UIDocument : public Behaviour {
public:
    std::string markup;

    /// Parse `markup` and (re)build the widgets as child GameObjects, replacing
    /// any previously generated ones. Safe to call from the editor or at runtime.
    void Rebuild();

    /// The widget GameObjects produced by the last Rebuild().
    const std::vector<GameObject*>& Generated() const { return m_generated; }

    /// Human-readable warnings from the last Rebuild() — unknown widget types or
    /// unrecognized keys, each prefixed with its 1-based line number. Empty when
    /// the markup is clean. The editor surfaces these so you can fix typos.
    const std::vector<std::string>& Diagnostics() const { return m_diagnostics; }

    void Start() override { Rebuild(); }
    void OnDestroy() override { ClearGenerated(); }

private:
    std::vector<GameObject*> m_generated;
    std::vector<std::string> m_diagnostics;
    void ClearGenerated();
};

} // namespace okay
