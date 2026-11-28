// Automation: the visual script graph commands (vs.*). They edit .okayvs files through the Visual Script editor's own
// model (VisualScriptModel.hpp: VsEditGraph load / add / linkExec / linkData / removeNode / toText, the node table
// Registry(), the pin types VsOutPinType / VsInPinType with PinCompatible and ConversionHint), and check them with the
// graph checker the Problems button runs (okay::vs::ValidateGraphText). Every edit is one step of the editor's undo
// history, carried as a file edit (EditorState::AttachUndoFiles), so Undo puts the file back. Our own code.
#include "AutomationEditor.hpp"
#include "VisualScriptModel.hpp"

#include "okay/VisualScript/VsTypes.hpp"

#include <filesystem>

namespace oked::automation {

using okay::automation::Call;
using okay::automation::Command;
using okay::automation::Param;
using okay::automation::ParamType;
using okay::automation::Registry;
using okay::automation::Result;
using okay::json::Value;
namespace code = okay::automation::code;
namespace flag = okay::automation::flag;
using okay::editor::EditorState;
namespace vsed = okay::editor::vsed;
using vsed::VsDef;
using vsed::VsEditGraph;
using vsed::VsEditNode;
using vsed::VsExecLink;
using vsed::VsDataLink;

namespace {

namespace fs = std::filesystem;

const Param kGraph = okay::automation::Arg("graph", ParamType::String, "The .okayvs file (absolute, or inside the project or its Assets folder).", true);

/// The graph file a parameter names (a new one goes in the project's Assets).
std::string GraphPath(Call& c, EditorState& ed) { return ProjectFile(ed, c.Str("graph"), true); }

Value IssuesJson(const std::string& text, const std::string& baseDir, int* errors) {
    Value out = Value::Arr();
    int n = 0;
    for (const okay::vs::VsIssue& i : okay::vs::ValidateGraphText(text, baseDir)) {
        if (i.severity == 0) ++n;
        out.Push(Value::Obj().Set("severity", Value::Str(i.severity == 0 ? "error" : "warning")).Set("node", Value::Num(i.node))
                     .Set("pin", Value::Num(i.pin)).Set("message", Value::Str(i.message)));
    }
    if (errors) *errors = n;
    return out;
}

Value GraphJson(const VsEditGraph& g) {
    Value nodes = Value::Arr();
    for (const VsEditNode& n : g.nodes) {
        Value j = Value::Obj().Set("id", Value::Num(n.id)).Set("type", Value::Str(n.type)).Set("arg", Value::Str(n.arg))
                      .Set("x", Value::Num(n.x)).Set("y", Value::Num(n.y));
        if (const VsDef* d = vsed::VsDefFor(g, n)) j.Set("title", Value::Str(d->title ? d->title : ""));
        if (!n.pinDefault.empty()) {
            Value pd = Value::Obj();
            for (const auto& kv : n.pinDefault) pd.Set(std::to_string(kv.first), Value::Str(kv.second));
            j.Set("pinDefaults", std::move(pd));
        }
        nodes.Push(std::move(j));
    }
    Value exec = Value::Arr(), data = Value::Arr(), vars = Value::Arr();
    for (const VsExecLink& l : g.execLinks) exec.Push(Value::Obj().Set("from", Value::Num(l.from)).Set("pin", Value::Num(l.pin)).Set("to", Value::Num(l.to)));
    for (const VsDataLink& l : g.dataLinks)
        data.Push(Value::Obj().Set("from", Value::Num(l.producer)).Set("pin", Value::Num(l.outPin)).Set("to", Value::Num(l.consumer)).Set("toPin", Value::Num(l.inPin)));
    for (const auto& v : g.vars)
        vars.Push(Value::Obj().Set("name", Value::Str(v.name)).Set("type", Value::Str(v.type)).Set("scope", Value::Str(v.scope)).Set("value", Value::Str(v.value)));
    return Value::Obj().Set("nodes", std::move(nodes)).Set("exec", std::move(exec)).Set("data", std::move(data)).Set("variables", std::move(vars));
}

Value DefJson(const VsDef& d) {
    auto list = [](const std::vector<const char*>& v) { Value a = Value::Arr(); for (const char* s : v) a.Push(Value::Str(s ? s : "")); return a; };
    Value j = Value::Obj().Set("type", Value::Str(d.type)).Set("title", Value::Str(d.title ? d.title : "")).Set("execIn", Value::Boolean(d.execIn))
                  .Set("execOut", list(d.execOut)).Set("dataIn", list(d.dataIn)).Set("dataOut", list(d.dataOut));
    if (d.param) j.Set("arg", Value::Str(d.paramHint ? d.paramHint : (d.param == 2 ? "comparison" : "text")));
    return j;
}

/// Load the graph, let `edit` change it, write it back as one undo step of the editor (the file's old text kept).
Result EditGraph(Call& c, EditorState& ed, bool create, const std::function<Result(VsEditGraph&)>& edit) {
    const std::string path = GraphPath(c, ed);
    std::error_code ec;
    const bool existed = fs::exists(fs::u8path(path), ec);
    if (!existed && !create) return Result::Fail(code::NotFound, "There is no graph file " + path + " (vs.create makes one).");
    if (existed && create) return Result::Fail(code::Refused, "The graph " + path + " already exists.");
    std::string before;
    if (existed && !EditorState::ReadTextFile(path, before)) return Result::Fail(code::Failed, "The graph " + path + " cannot be read.");
    VsEditGraph g;
    if (existed) g.fromText(before);
    Result r = edit(g);
    if (!r.ok) return r;
    const std::string after = g.toText();
    if (existed && after == before) { r.data.Set("changed", Value::Boolean(false)); return r; }
    fs::create_directories(fs::u8path(path).parent_path(), ec);
    if (!g.save(path)) return Result::Fail(code::Failed, "The graph could not be written to " + path + ".");
    EditorState::FileEdit fe;
    fe.path = path;
    fe.hadBefore = existed;
    fe.before = before;
    fe.after = after;
    ed.AttachUndoFiles({fe});
    int errors = 0;
    r.data.Set("graph", Value::Str(path)).Set("changed", Value::Boolean(true));
    r.data.Set("issues", IssuesJson(after, ed.projectDir().empty() ? fs::u8path(path).parent_path().string() : ed.projectDir(), &errors));
    r.data.Set("errors", Value::Num(errors));
    return r;
}

} // namespace

void AddVsCommands(Registry& r) {
    r.Add(EditorCommand("vs.node_types", "List the visual script node types (the Visual Script editor's Add Block table) with their pins.",
              {okay::automation::Arg("filter", ParamType::String, "Only types whose name or title contains this.")},
              "{count, types: [{type, title, execIn, execOut, dataIn, dataOut, arg}]}", 0, [](Call& c, EditorState&) {
                  const std::string f = c.Str("filter");
                  auto low = [](std::string s) { for (char& ch : s) ch = (char)std::tolower((unsigned char)ch); return s; };
                  Value out = Value::Arr();
                  for (const VsDef& d : vsed::Registry()) {
                      if (!f.empty() && low(d.type).find(low(f)) == std::string::npos && low(d.title ? d.title : "").find(low(f)) == std::string::npos) continue;
                      out.Push(DefJson(d));
                  }
                  const double n = (double)out.array.size();
                  return Result::Ok(Value::Obj().Set("count", Value::Num(n)).Set("types", std::move(out)));
              }));
    r.Add(EditorCommand("vs.inspect", "Read a visual script graph: its nodes, flow and data wires, variables, and the checker's problems.",
              {kGraph}, "{graph, nodes, exec, data, variables, issues, errors}", 0, [](Call& c, EditorState& ed) {
                  const std::string path = GraphPath(c, ed);
                  std::string text;
                  if (!EditorState::ReadTextFile(path, text)) return Result::Fail(code::NotFound, "There is no graph file " + path + ".");
                  VsEditGraph g;
                  g.fromText(text);
                  Value v = GraphJson(g);
                  int errors = 0;
                  v.Set("graph", Value::Str(path));
                  v.Set("issues", IssuesJson(text, ed.projectDir().empty() ? fs::u8path(path).parent_path().string() : ed.projectDir(), &errors));
                  v.Set("errors", Value::Num(errors));
                  return Result::Ok(std::move(v));
              }, {code::NotFound}));
    r.Add(EditorCommand("vs.create", "Make a new visual script graph file: empty, or the editor's starter graph (move with WASD).",
              {kGraph, okay::automation::Arg("starter", ParamType::Boolean, "Start from the editor's starter graph instead of an empty one.")},
              "{graph, nodes, issues}", flag::Undoable, [](Call& c, EditorState& ed) {
                  return EditGraph(c, ed, true, [&](VsEditGraph& g) {
                      if (c.Bool("starter", false)) vsed::SeedStarterGraph(g);
                      return Result::Ok(Value::Obj().Set("nodes", Value::Num((double)g.nodes.size())));
                  });
              }, {code::Refused, code::Failed}));
    r.Add(EditorCommand("vs.add_node", "Add a node to a graph (vs.node_types lists the types), with its text field and where it sits.",
              {kGraph, okay::automation::Arg("type", ParamType::String, "The node type (\"OnStart\", \"Print\", \"Translate\" ...).", true),
               okay::automation::Arg("arg", ParamType::String, "Its text field (a key name, a number, an event name ...)."),
               okay::automation::Arg("x", ParamType::Number, "Where it sits on the canvas."), okay::automation::Arg("y", ParamType::Number, "Where it sits on the canvas.")},
              "{node, graph, issues, errors}", flag::Undoable, [](Call& c, EditorState& ed) {
                  if (!vsed::FindDef(c.Str("type"))) return Result::Fail(code::NotFound, "There is no node type '" + c.Str("type") + "' (vs.node_types lists them).");
                  return EditGraph(c, ed, false, [&](VsEditGraph& g) {
                      float y = 40.0f;
                      for (const VsEditNode& n : g.nodes) y = std::max(y, n.y + 120.0f);
                      const int id = g.add(c.Str("type"), (float)c.Num("x", 40.0), (float)c.Num("y", y));
                      if (c.Has("arg")) if (VsEditNode* n = g.find(id)) n->arg = c.Str("arg");
                      return Result::Ok(Value::Obj().Set("node", Value::Num(id)));
                  });
              }, {code::NotFound, code::Failed}));
    r.Add(EditorCommand("vs.connect", "Wire two nodes: a flow wire (exec: from's output pin to the next node) or a data wire (from's output pin into to's input pin; refused when the types cannot fit, with the conversion to use).",
              {kGraph, okay::automation::Arg("kind", ParamType::String, "exec or data.", true), okay::automation::Arg("from", ParamType::Integer, "The node the wire leaves.", true),
               okay::automation::Arg("pin", ParamType::Integer, "Its output pin (0 = the first).", true), okay::automation::Arg("to", ParamType::Integer, "The node the wire goes into.", true),
               okay::automation::Arg("toPin", ParamType::Integer, "data: the input pin of `to` (0 = the first).")},
              "{graph, issues, errors}", flag::Undoable, [](Call& c, EditorState& ed) {
                  const std::string kind = c.Str("kind");
                  if (kind != "exec" && kind != "data") return Result::Fail(code::InvalidParams, "kind must be exec or data.");
                  return EditGraph(c, ed, false, [&](VsEditGraph& g) {
                      const int from = (int)c.Int("from", -1), pin = (int)c.Int("pin", 0), to = (int)c.Int("to", -1), toPin = (int)c.Int("toPin", 0);
                      const VsEditNode* a = g.find(from);
                      const VsEditNode* b = g.find(to);
                      if (!a || !b) return Result::Fail(code::NotFound, "The graph has no node " + std::to_string(!a ? from : to) + ".");
                      if (from == to) return Result::Fail(code::Refused, "A node cannot be wired to itself.");
                      const VsDef* da = vsed::VsDefFor(g, *a);
                      const VsDef* db = vsed::VsDefFor(g, *b);
                      if (kind == "exec") {
                          if (!da || pin < 0 || pin >= (int)da->execOut.size()) return Result::Fail(code::Refused, a->type + " has no flow output " + std::to_string(pin) + ".");
                          if (!db || !db->execIn) return Result::Fail(code::Refused, b->type + " takes no flow input.");
                          g.linkExec(from, pin, to);
                      } else {
                          if (!da || pin < 0 || pin >= (int)da->dataOut.size()) return Result::Fail(code::Refused, a->type + " has no data output " + std::to_string(pin) + ".");
                          if (!db || toPin < 0 || toPin >= (int)db->dataIn.size()) return Result::Fail(code::Refused, b->type + " has no data input " + std::to_string(toPin) + ".");
                          const okay::vs::PinType out = vsed::VsOutPinType(g, from, pin), in = vsed::VsInPinType(g, to, toPin);
                          if (!okay::vs::PinCompatible(out, in)) {
                              const std::string hint = okay::vs::ConversionHint(out, in);
                              return Result::Fail(code::Refused, std::string("A ") + okay::vs::PinTypeName(out) + " cannot go into a " + okay::vs::PinTypeName(in) + " input." + (hint.empty() ? "" : " " + hint));
                          }
                          g.linkData(to, toPin, from, pin);
                      }
                      return Result::Ok();
                  });
              }, {code::NotFound, code::Refused}));
    r.Add(EditorCommand("vs.set_node", "Change a node's text field, a typed value on one of its unwired inputs, or where it sits.",
              {kGraph, okay::automation::Arg("node", ParamType::Integer, "The node.", true), okay::automation::Arg("arg", ParamType::String, "Its new text field."),
               okay::automation::Arg("input", ParamType::Integer, "The input pin whose typed value `value` sets."), okay::automation::Arg("value", ParamType::String, "That value, as typed in the editor."),
               okay::automation::Arg("x", ParamType::Number, "New canvas x."), okay::automation::Arg("y", ParamType::Number, "New canvas y.")},
              "{graph, issues, errors}", flag::Undoable, [](Call& c, EditorState& ed) {
                  return EditGraph(c, ed, false, [&](VsEditGraph& g) {
                      VsEditNode* n = g.find((int)c.Int("node", -1));
                      if (!n) return Result::Fail(code::NotFound, "The graph has no node " + std::to_string(c.Int("node", -1)) + ".");
                      if (c.Has("arg")) n->arg = c.Str("arg");
                      if (c.Has("input")) n->pinDefault[(int)c.Int("input", 0)] = c.Str("value");
                      if (c.Has("x")) n->x = (float)c.Num("x", n->x);
                      if (c.Has("y")) n->y = (float)c.Num("y", n->y);
                      return Result::Ok();
                  });
              }, {code::NotFound}));
    r.Add(EditorCommand("vs.remove_node", "Remove a node and every wire on it.", {kGraph, okay::automation::Arg("node", ParamType::Integer, "The node.", true)},
              "{graph, issues, errors}", flag::Undoable, [](Call& c, EditorState& ed) {
                  return EditGraph(c, ed, false, [&](VsEditGraph& g) {
                      const int id = (int)c.Int("node", -1);
                      if (!g.find(id)) return Result::Fail(code::NotFound, "The graph has no node " + std::to_string(id) + ".");
                      g.removeNode(id);
                      return Result::Ok();
                  });
              }, {code::NotFound}));
}

} // namespace oked::automation
