#pragma once
// Custom visual-script nodes written in a script (feature map 24.3, work packages 2 and 3). A script file in the project
// declares one or more nodes in OkayScript, Lua or C# (the typed C#-style language, okayscript-next); each becomes a
// registered node type (VsNodeRegistry.hpp) named "Custom.<function>", shown in the palette's Custom group. Its body runs
// on that language's own VM (CreateScriptVM), so the language's sandbox and instruction budget apply; an error in a body
// stops that node with a message naming the node, the file and the line, and never stops the game.
//
// The declaration, one per language:
//   OkayScript (.okay)                         Lua (.lua)                         C# (.cs, a Behaviour class)
//   // @node Double It                         -- @node Double It                 [Node("Double It", Category = "Math",
//   // @category Math                          -- @category Math                        Help = "Doubles a number.")]
//   // @help Doubles a number.                 -- @help Doubles a number.         float DoubleIt(float value = 1) {
//   // @in Value: Number = 1                   -- @in Value: Number = 1               return value * 2;
//   // @out Result: Number                     -- @out Result: Number             }
//   function double_it(value) {               function double_it(value)
//       return value * 2;                         return value * 2
//   }                                          end
// More tags: `@flow` (a flow node with one exit), `@exits Yes, No` (a flow node with these exits), `@base <Type> [setting]`
// (a "custom version" of a built-in node: the original runs first and its result is passed as the last argument,
// `original`). C# takes the same as attribute fields: Flow = true, Exits = "Yes,No", Base = "Add", Inputs = "A,B" (pin
// labels), Output = "Sum"; a void method is a flow node; parameters are the inputs and the return value the one output.
//
// What a body gets and gives: the input values in pin order (then `original`). It returns: a pure node with one output,
// the value; with several outputs, a list of them (Lua: a table {a, b}); a flow node with several exits, the exit (its
// index from 0, its name, or true / false for the first / second), or a list [exit, outputs...] when it also has
// outputs. `original` has the same shape, so `return original` behaves exactly like the built-in node.
//
// A graph names the scripts it uses with `nodelib "<path>"` lines (NodeGraph::Parse loads them; a missing type keeps a
// placeholder that holds its pins and wires, warns and does not run). The build follows those lines, so the scripts ship
// with the game. Written for this engine from the owner's request of 2026-11-28; no outside code.
#include "okay/VisualScript/VsNodeRegistry.hpp"
#include <memory>
#include <string>
#include <vector>

namespace okay::vs {

class VsNode;

/// The prefix of every custom node type ("Custom.double_it").
inline const char* CustomNodePrefix() { return "Custom."; }
/// True for a "Custom.<name>" type (registered or not).
bool IsCustomNodeType(const std::string& type);
/// The script language of a custom-node file, from its extension: "okayscript" (.okay, .okayscript), "lua", "cs"; ""
/// for anything else.
std::string CustomNodeLanguageFor(const std::string& path);

/// One node a script declares.
struct CustomNodeDecl {
    std::string type;        ///< "Custom.<function>"
    std::string function;    ///< the function / method the body is
    VsNodeInfo info;         ///< pins, title, help, where it is
    std::string baseArg;     ///< the original node's setting, with info.base
};
/// What a script file declares, read without running it.
struct CustomNodeScript {
    std::string path, language;
    std::string vmSource;                 ///< what the VM loads (C#: the attributes and default values blanked out)
    std::vector<CustomNodeDecl> nodes;
    std::vector<std::string> problems;    ///< "line N: ..." for a declaration that could not be read
};
/// Read the node declarations of `text` (language as CustomNodeLanguageFor gives it).
CustomNodeScript ParseCustomNodeScript(const std::string& text, const std::string& language, const std::string& path);

/// Load a custom-node script and register its nodes; a type the file no longer declares is taken out. Loading the same
/// text again does nothing. False with `err` when the file cannot be read or declares nothing.
bool LoadCustomNodeScript(const std::string& path, std::string* err = nullptr);
/// The same with the text given (an unsaved buffer, a test).
bool LoadCustomNodeText(const std::string& path, const std::string& text, std::string* err = nullptr);
/// Forget a loaded file and take its nodes out.
void UnloadCustomNodeScript(const std::string& path);
/// Look at every loaded file again and reload the ones that changed on disk (hot reload). The number reloaded. Cheap to
/// call often: it looks at the files at most every `everySeconds`.
int PollCustomNodeScripts(double everySeconds = 0.5);
/// Load every script under `folder` (recursively; .okay, .okayscript, .lua, .cs) that declares a node. The number of files.
int ScanCustomNodeFolder(const std::string& folder);
/// The files loaded now.
std::vector<std::string> LoadedCustomNodeScripts();
/// Counts up whenever a custom node is added, changed or taken out.
int CustomNodesRevision();

/// The `nodelib` paths of a graph's text, in order, each once.
std::vector<std::string> CustomNodeLibsInGraphText(const std::string& graphText);
/// Where a `nodelib` path is: as written, then beside `graphFile`, then in each folder above it. "" when not found or
/// when the path is not a safe relative one.
std::string ResolveCustomNodeLib(const std::string& lib, const std::string& graphFile);
/// Load each `nodelib` of a graph's text. The number loaded.
int LoadCustomNodeLibs(const std::string& graphText, const std::string& graphFile);

/// The placeholder kept for a custom node whose type is missing: it keeps its pins and wires, says why, and does not run.
std::unique_ptr<VsNode> MakeMissingCustomNode(const std::string& type);
/// True for that placeholder.
bool IsMissingCustomNode(const VsNode* node);

/// "Make a custom version": a new script that declares a custom node with the same pins as a built-in node.
struct CustomVersionSpec {
    std::string baseType;                 ///< the built-in node ("Add", "Transform.Translate/3")
    std::string baseArg;                  ///< its setting (GetVar's name...)
    std::string title;                    ///< its title ("Add (+)"); the new node is "<title> (custom)"
    std::string help, category;
    bool execIn = false;
    std::vector<std::string> exits;
    std::vector<VsPinSpec> ins, outs;
};
/// The script text for `language` ("okayscript", "lua", "cs"), declaring function `function`. Where the language can do
/// the same job with its own code (simple maths and logic; in C#, an engine call on this object), the body does it;
/// otherwise the node is based on the original (`@base`): the original runs first and the body returns its result. False
/// with `why` when that language cannot take these pins (C#: at most one output, no Any / List / Map / Color pins).
bool MakeCustomVersionScript(const CustomVersionSpec& spec, const std::string& language, const std::string& function,
                             std::string& text, std::string* why = nullptr);
/// A function name for a custom version of `title` that is not a registered type yet ("add_custom", "add_custom_2").
std::string CustomVersionFunctionName(const std::string& title, const std::string& language);

} // namespace okay::vs
