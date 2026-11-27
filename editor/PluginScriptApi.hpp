#pragma once
// PluginScriptApi: the script_* calls an editor plugin (an OkayScript file in EditorPlugins/ or <editor>/plugins/) uses to add
// to the Script Editor: commands, completion, lint with quick fixes, a formatter, hover text, snippets and a side panel, plus
// reading and changing the open script. They fill a pluginreg::PluginRegistry (editor/PluginRegistry.hpp) under the plugin's
// owner id, so turning the plugin off or reloading it removes them. PluginsWindow.cpp calls RegisterScriptEditorApi for every
// plugin VM it loads; tests/test_editor_plugins.cpp runs it headless. Documented in docs/editor/editor-plugins.md.
#include "PluginRegistry.hpp"

#include <functional>
#include <string>

namespace okay { class OkayScriptVM; }

namespace oked {

/// What the plugin calls reach in the Script Editor: the open script and a few actions. The Script Editor fills it every
/// frame (DrawScriptEditorApp, editor/ScriptEditorAppHost.cpp); empty functions make the calls do nothing.
struct ScriptEditorHost {
    std::function<std::string()> text, path, language;
    std::function<int()> caretLine;
    std::function<void(const std::string&)> insert;      ///< at the caret (replaces the selection)
    std::function<void(const std::string&)> setText;     ///< the whole text, one undo step
    std::function<void(const std::string&, int)> open;   ///< open a script at a line
};
ScriptEditorHost& ScriptEditorHostApi();

/// Add the script_* builtins to a plugin's VM (before Load). `owner`: the plugin's file path; `name`: its @name.
void RegisterScriptEditorApi(okay::OkayScriptVM& vm, pluginreg::PluginRegistry& reg, const std::string& owner, const std::string& name);

} // namespace oked
