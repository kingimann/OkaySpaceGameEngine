// Custom visual-script nodes (feature map 24.3): the node registry (RegisterNodeType), nodes declared in OkayScript, Lua and
// C# scripts running in a graph (pure and flow nodes, several exits), a body error, hot reload, an old graph whose custom type
// is missing (placeholder), save and load, "Make a custom version" of built-in nodes, and the build shipping the scripts.
#include "test_framework.hpp"
#include "test_sandbox.hpp"
#include "VisualScriptCustomNodes.hpp"
#include "VisualScriptPaletteModel.hpp"
#include "okay/VisualScript/VsCustomNodes.hpp"
#include "okay/VisualScript/VsNodeRegistry.hpp"
#include "okay/VisualScript/Nodes.hpp"
#include "okay/Core/BuildPipeline.hpp"
#include <Okay.hpp>
#include <algorithm>
#include <filesystem>
#include <memory>
#include <sstream>
#include <fstream>
#include <string>
#include <vector>

using namespace okay;
using namespace okay::editor::vsed;
namespace fs = std::filesystem;

namespace {

void Write(const fs::path& p, const std::string& text) {
    std::error_code ec;
    fs::create_directories(p.parent_path(), ec);
    std::ofstream f(p, std::ios::binary);
    f << text;
}

struct Run {
    std::unique_ptr<Scene> scene;   // each run has a scene of its own
    bool loaded = false;
    std::string error;       // load error
    std::string lastError;   // the graph's last runtime error
    VisualScriptComponent* vc = nullptr;
};
// Run a graph file's On Start on an object of a new scene.
Run RunFile(const std::string& path) {
    Run r;
    r.scene = std::make_unique<Scene>("Run");
    Scene& sc = *r.scene;
    r.vc = sc.CreateGameObject("Runner")->AddComponent<VisualScriptComponent>();
    r.loaded = r.vc->LoadFromFile(path, &r.error);
    if (!r.loaded) { std::printf("  load failed: %s\n", r.error.c_str()); return r; }
    sc.Start();
    if (r.vc->Graph()) r.lastError = r.vc->Graph()->LastError();
    return r;
}
std::string Var(const Run& r, const char* name) { return r.vc ? r.vc->GetVariable(name).AsString() : std::string(); }

// On Start -> <flow node 2> -> Set Var result (its value from node 2's output 0). Node 2's inputs get `defaults`.
std::string FlowGraph(const std::string& lib, const std::string& type, const std::vector<std::string>& defaults) {
    std::string t = "nodelib \"" + lib + "\"\nnode 1 OnStart\nentry OnStart 1\nnode 2 " + type + "\n";
    for (std::size_t i = 0; i < defaults.size(); ++i) t += "default 2 " + std::to_string(i) + " " + defaults[i] + "\n";
    t += "node 3 SetVar result\nexec 1 0 2\nexec 2 0 3\ndata 3 0 2 0\n";
    return t;
}
// On Start -> <flow node 2> -> Set Var ran = 1: whether the chain went on past node 2.
std::string ChainGraph(const std::string& lib, const std::string& type) {
    return "nodelib \"" + lib + "\"\nnode 1 OnStart\nentry OnStart 1\nnode 2 " + type +
           "\nnode 3 SetVar ran\ndefault 3 0 1\nexec 1 0 2\nexec 2 0 3\n";
}
// On Start -> Set Var result (its value from the pure node 2's output `out`).
std::string PureGraph(const std::string& lib, const std::string& type, const std::vector<std::string>& defaults, int out = 0) {
    std::string t = "nodelib \"" + lib + "\"\nnode 1 OnStart\nentry OnStart 1\nnode 2 " + type + "\n";
    for (std::size_t i = 0; i < defaults.size(); ++i) t += "default 2 " + std::to_string(i) + " " + defaults[i] + "\n";
    t += "node 3 SetVar result\nexec 1 0 3\ndata 3 0 2 " + std::to_string(out) + "\n";
    return t;
}

// A node made in C++ for the registry test: Out = In + 1.
struct PlusOneNode : vs::VsNode {
    vs::VsValue Eval(int, vs::NodeGraph& g, vs::VsContext& ctx) override { return vs::VsValue(In(0, g, ctx).AsFloat() + 1.0f); }
};

} // namespace

int main() {
    RUN_SUITE("vs_custom_nodes");
    okaytest::Sandbox sandbox("vs_custom_nodes", OKAY_SOURCE_DIR);
    auto sink = Log::sink;
    std::vector<std::string> logged;
    Log::sink = [&logged](Log::Level, const std::string& m) { logged.push_back(m); };
    const fs::path proj = sandbox.Dir() / "proj";
    const fs::path assets = proj / "Assets";
    const fs::path graphs = assets / "Graphs";

    // ---- 1. The registry: a node type with its pins, category and help; MakeNode, pin types, the editor's rows ----
    {
        const int rev0 = vs::VsNodeRegistryRevision();
        vs::VsNodeInfo info;
        info.title = "Plus One";
        info.category = "Math";
        info.help = "Adds one.";
        info.ins = {{"In", vs::PinType::Number, "1"}};
        info.outs = {{"Out", vs::PinType::Number, ""}};
        vs::RegisterNodeType("Custom.plus_one", info, [](const std::vector<std::string>&, std::string*) {
            return std::unique_ptr<vs::VsNode>(new PlusOneNode());
        });
        CHECK(vs::VsNodeRegistryRevision() > rev0);
        const vs::VsNodeEntry* e = vs::FindRegisteredVsNode("Custom.plus_one");
        CHECK(e && e->custom && e->info.title == "Plus One");
        std::string err;
        CHECK(vs::MakeNode("Custom.plus_one", {}, &err) != nullptr);
        const vs::NodeSignature* sig = vs::SignatureFor("Custom.plus_one");
        CHECK(sig && sig->ins == "N" && sig->outs == "N" && !sig->execIn);
        CHECK(vs::InputType("Custom.plus_one", 0) == vs::PinType::Number);
        // The editor reads the same entry: a row, its place (the Custom group), its help, its level.
        const VsDef* d = FindDef("Custom.plus_one");
        CHECK(d && d->custom && std::string(d->title) == "Plus One" && d->dataIn.size() == 1 && d->dataOut.size() == 1);
        const VsPlace place = VsPlaceForNode("Custom.plus_one");
        CHECK(place.category && std::string(place.category) == "Custom");
        CHECK(VsPaletteCategoryIndex("Custom") >= 0);
        const char* help = VsNodeHelp("Custom.plus_one");
        CHECK(help && std::string(help).find("Adds one.") != std::string::npos);
        CHECK(VsVisibleByDefault("Custom.plus_one"));
        CHECK(d && !VsPaletteHidden(*d));
        // Changed in place (hot reload keeps the row), then taken out.
        info.title = "Plus One Again";
        vs::RegisterNodeType("Custom.plus_one", info, [](const std::vector<std::string>&, std::string*) {
            return std::unique_ptr<vs::VsNode>(new PlusOneNode());
        });
        const VsDef* d2 = FindDef("Custom.plus_one");
        CHECK(d2 == d && d2 && std::string(d2->title) == "Plus One Again");
        CHECK(vs::UnregisterNodeType("Custom.plus_one"));
        CHECK(!vs::FindRegisteredVsNode("Custom.plus_one"));
        CHECK(!FindDef("Custom.plus_one"));
        CHECK(d && d->gone && VsPaletteHidden(*d));
        CHECK(vs::MakeNode("Custom.plus_one", {}, &err) == nullptr);
        // The weapon nodes still come from the same registry.
        CHECK(vs::FindRegisteredVsNode("FireWeapon") != nullptr);
    }

    // ---- 2. An old graph whose custom node type is missing: it loads with a placeholder, keeps everything, does not run ----
    {
        const std::string text =
            "nodelib \"Nodes/gone.okay\"\nnode 1 OnStart\nentry OnStart 1\nnode 2 Custom.not_here\ndefault 2 1 7\n"
            "node 3 SetVar result\nnode 4 Const 5\nexec 1 0 2\nexec 2 0 3\ndata 2 0 4 0\ndata 3 0 2 1\n";
        std::string err;
        auto g = vs::NodeGraph::Parse(text, &err);
        CHECK(g != nullptr);
        if (g) {
            CHECK(g->MissingNodes().size() == 1 && g->MissingNodes()[0] == 2);
            vs::VsContext ctx;
            g->RunEntries("OnStart", ctx);
            CHECK(g->LastError().find("Custom.not_here") != std::string::npos && g->LastError().find("missing") != std::string::npos);
            CHECK(!ctx.variables.count("result"));   // the chain stopped at the placeholder
        }
        // The editor keeps the node, its wires, typed value and the nodelib line, and draws a placeholder with those plugs.
        VsEditGraph eg;
        eg.fromText(text);
        const VsEditNode* n = eg.find(2);
        CHECK(n && n->type == "Custom.not_here");
        const VsDef* d = n ? VsDefFor(eg, *n) : nullptr;
        CHECK(d && d->gone && d->dataIn.size() == 2 && d->dataOut.size() == 2 && d->execOut.size() == 1 && d->execIn);
        const std::string back = eg.toText();
        CHECK(back.find("nodelib \"Nodes/gone.okay\"") != std::string::npos);
        CHECK(back.find("node 2 Custom.not_here") != std::string::npos);
        CHECK(back.find("default 2 1 7") != std::string::npos);
        CHECK(back.find("data 3 0 2 1") != std::string::npos && back.find("exec 2 0 3") != std::string::npos);
    }

    // ---- 3. OkayScript: a pure node and a flow node, from a file the graph names ----
    const std::string okayText =
        "// Nodes for the tests.\n"
        "// @node Double It\n"
        "// @category Math\n"
        "// @help Doubles the number you give it.\n"
        "// @in Value: Number = 1\n"
        "// @out Result: Number\n"
        "function double_it(value) {\n"
        "    return value * 2;\n"
        "}\n"
        "\n"
        "// @node Sum And Product\n"
        "// @in A: Number\n"
        "// @in B: Number\n"
        "// @out Sum: Number\n"
        "// @out Product: Number\n"
        "function sum_product(a, b) {\n"
        "    return [a + b, a * b];\n"
        "}\n"
        "\n"
        "// @node Shout\n"
        "// @flow\n"
        "// @in Word: Text\n"
        "// @out Loud: Text\n"
        "function shout(word) {\n"
        "    return word + \"!\";\n"
        "}\n"
        "\n"
        "// @node Big Or Small\n"
        "// @exits Big, Small\n"
        "// @in Value: Number\n"
        "function big_or_small(value) {\n"
        "    return value > 10;\n"
        "}\n"
        "\n"
        "// @node Broken\n"
        "// @flow\n"
        "function broken() {\n"
        "    throw \"it broke\";\n"
        "}\n";
    Write(assets / "Nodes" / "math.okay", okayText);
    {
        const vs::CustomNodeScript parsed = vs::ParseCustomNodeScript(okayText, "okayscript", "math.okay");
        CHECK(parsed.problems.empty());
        CHECK(parsed.nodes.size() == 5);
        if (parsed.nodes.size() == 5) {
            const vs::CustomNodeDecl& d = parsed.nodes[0];
            CHECK(d.type == "Custom.double_it" && d.info.title == "Double It" && d.info.category == "Math");
            CHECK(d.info.help == "Doubles the number you give it." && d.info.line == 7 && !d.info.execIn);
            CHECK(d.info.ins.size() == 1 && d.info.ins[0].name == "Value" && d.info.ins[0].type == vs::PinType::Number && d.info.ins[0].def == "1");
            CHECK(parsed.nodes[2].info.execIn && parsed.nodes[2].info.exits.size() == 1);
            CHECK(parsed.nodes[3].info.exits.size() == 2 && parsed.nodes[3].info.exits[1] == "Small");
        }
        Write(graphs / "pure.okayvs", PureGraph("Nodes/math.okay", "Custom.double_it", {"21"}));
        Run r = RunFile((graphs / "pure.okayvs").generic_string());
        CHECK(r.loaded && Var(r, "result") == "42");
        // A default the script declares is used when nothing is wired or typed in.
        Write(graphs / "pure_default.okayvs", PureGraph("Nodes/math.okay", "Custom.double_it", {}));
        Run r0 = RunFile((graphs / "pure_default.okayvs").generic_string());
        CHECK(r0.loaded && Var(r0, "result") == "2");
        // Several outputs.
        Write(graphs / "two.okayvs", PureGraph("Nodes/math.okay", "Custom.sum_product", {"3", "4"}, 1));
        Run r2 = RunFile((graphs / "two.okayvs").generic_string());
        CHECK(r2.loaded && Var(r2, "result") == "12");
        // A flow node: runs when flow reaches it, its output read afterwards.
        Write(graphs / "flow.okayvs", FlowGraph("Nodes/math.okay", "Custom.shout", {"\"hey\""}));
        Run r3 = RunFile((graphs / "flow.okayvs").generic_string());
        CHECK(r3.loaded && Var(r3, "result") == "hey!");
        CHECK(r3.lastError.empty());
        // Several exits: the body picks one (true = the first, false = the second).
        const std::string exits =
            "nodelib \"Nodes/math.okay\"\nnode 1 OnStart\nentry OnStart 1\nnode 2 Custom.big_or_small\ndefault 2 0 3\n"
            "node 3 SetVar big\nnode 4 SetVar small\ndefault 3 0 1\ndefault 4 0 1\nexec 1 0 2\nexec 2 0 3\nexec 2 1 4\n";
        Write(graphs / "exits.okayvs", exits);
        Run r4 = RunFile((graphs / "exits.okayvs").generic_string());
        CHECK(r4.loaded && Var(r4, "small") == "1" && Var(r4, "big") != "1");   // an unset variable reads 0
        // A body error: the node stops its chain with a message naming the node, the file and the line; the game goes on.
        Write(graphs / "broken.okayvs", ChainGraph("Nodes/math.okay", "Custom.broken"));
        Run r5 = RunFile((graphs / "broken.okayvs").generic_string());
        CHECK(r5.loaded && Var(r5, "ran") != "1");   // the chain stopped at the node
        CHECK(r5.lastError.find("custom node 'Broken'") != std::string::npos);
        CHECK(r5.lastError.find("math.okay") != std::string::npos);
        CHECK(r5.lastError.find("it broke") != std::string::npos);
        CHECK(r5.lastError.find("line") != std::string::npos);
        if (r5.lastError.find("line") == std::string::npos) std::printf("  okayscript error: %s\n", r5.lastError.c_str());
    }

    // ---- 4. Lua ----
    const std::string luaText =
        "-- @node Triple\n"
        "-- @in Value: Number = 2\n"
        "-- @out Result: Number\n"
        "function triple(value)\n"
        "    return value * 3\n"
        "end\n"
        "\n"
        "-- @node Min And Max\n"
        "-- @in A: Number\n"
        "-- @in B: Number\n"
        "-- @out Low: Number\n"
        "-- @out High: Number\n"
        "function low_high(a, b)\n"
        "    if a < b then return {a, b} end\n"
        "    return {b, a}\n"
        "end\n"
        "\n"
        "-- @node Lua Boom\n"
        "-- @flow\n"
        "function lua_boom()\n"
        "    error(\"boom\")\n"
        "end\n";
    Write(assets / "Nodes" / "more.lua", luaText);
    {
        Write(graphs / "lua.okayvs", PureGraph("Nodes/more.lua", "Custom.triple", {"5"}));
        Run r = RunFile((graphs / "lua.okayvs").generic_string());
        CHECK(r.loaded && Var(r, "result") == "15");
        Write(graphs / "lua2.okayvs", PureGraph("Nodes/more.lua", "Custom.low_high", {"9", "4"}, 1));
        Run r2 = RunFile((graphs / "lua2.okayvs").generic_string());
        CHECK(r2.loaded && Var(r2, "result") == "9");
        Write(graphs / "lua3.okayvs", ChainGraph("Nodes/more.lua", "Custom.lua_boom"));
        Run r3 = RunFile((graphs / "lua3.okayvs").generic_string());
        CHECK(r3.loaded && Var(r3, "ran") != "1");
        CHECK(r3.lastError.find("custom node 'Lua Boom'") != std::string::npos && r3.lastError.find("boom") != std::string::npos);
        CHECK(r3.lastError.find(":21:") != std::string::npos);   // the line of error("boom") in the file
        if (r3.lastError.find(":21:") == std::string::npos) std::printf("  lua error: %s\n", r3.lastError.c_str());
    }

    // ---- 5. C# (the typed C#-style language) ----
    const std::string csText =
        "class MathNodes : Behaviour {\n"
        "    [Node(\"Halve\", Category = \"Math\", Help = \"Half of a number.\")]\n"
        "    float Halve(float value = 10) {\n"
        "        return value / 2;\n"
        "    }\n"
        "\n"
        "    [Node(\"Greet\", Inputs = \"Name\", Output = \"Greeting\", Flow = true)]\n"
        "    string Greet(string who) {\n"
        "        return \"Hello \" + who;\n"
        "    }\n"
        "}\n";
    Write(assets / "Nodes" / "MathNodes.cs", csText);
    {
        const vs::CustomNodeScript parsed = vs::ParseCustomNodeScript(csText, "cs", "MathNodes.cs");
        CHECK(parsed.problems.empty());
        for (const auto& p : parsed.problems) std::printf("  cs problem: %s\n", p.c_str());
        CHECK(parsed.nodes.size() == 2);
        if (parsed.nodes.size() == 2) {
            CHECK(parsed.nodes[0].type == "Custom.Halve" && parsed.nodes[0].info.ins.size() == 1 && parsed.nodes[0].info.ins[0].def == "10");
            CHECK(parsed.nodes[0].info.line == 3 && parsed.nodes[0].info.outs.size() == 1);
            CHECK(parsed.nodes[1].info.execIn && parsed.nodes[1].info.ins[0].name == "Name" && parsed.nodes[1].info.outs[0].name == "Greeting");
        }
        CHECK(parsed.vmSource.find("[Node") == std::string::npos && parsed.vmSource.find("= 10") == std::string::npos);
        CHECK(std::count(parsed.vmSource.begin(), parsed.vmSource.end(), '\n') == std::count(csText.begin(), csText.end(), '\n'));
        Write(graphs / "cs.okayvs", PureGraph("Nodes/MathNodes.cs", "Custom.Halve", {"9"}));
        Run r = RunFile((graphs / "cs.okayvs").generic_string());
        CHECK(r.loaded && Var(r, "result") == "4.5");
        if (Var(r, "result") != "4.5") std::printf("  cs: '%s' (%s)\n", Var(r, "result").c_str(), r.lastError.c_str());
        Write(graphs / "cs_default.okayvs", PureGraph("Nodes/MathNodes.cs", "Custom.Halve", {}));
        Run r1 = RunFile((graphs / "cs_default.okayvs").generic_string());
        CHECK(r1.loaded && Var(r1, "result") == "5");
        Write(graphs / "cs2.okayvs", FlowGraph("Nodes/MathNodes.cs", "Custom.Greet", {"\"Sam\""}));
        Run r2 = RunFile((graphs / "cs2.okayvs").generic_string());
        CHECK(r2.loaded && Var(r2, "result") == "Hello Sam");
    }

    // ---- 6. Hot reload: saving the script changes the nodes of a graph that is already running ----
    {
        const fs::path file = assets / "Nodes" / "hot.okay";
        Write(file, "// @node Hot\n// @out Value: Number\nfunction hot() {\n    return 1;\n}\n");
        Write(graphs / "hot.okayvs", PureGraph("Nodes/hot.okay", "Custom.hot", {}));
        Run r = RunFile((graphs / "hot.okayvs").generic_string());
        CHECK(r.loaded && Var(r, "result") == "1");
        vs::NodeGraph* g = r.vc ? r.vc->Graph() : nullptr;
        Write(file, "// @node Hot\n// @out Value: Number\nfunction hot() {\n    return 22;\n}\n");
        CHECK(vs::PollCustomNodeScripts(0) >= 1);
        if (g) {
            vs::VsContext ctx;
            g->RunEntries("OnStart", ctx);
            CHECK(ctx.variables.count("result") && ctx.variables["result"].AsString() == "22");
        }
        // The editor's row follows the new pins too.
        Write(file, "// @node Hot Renamed\n// @in Extra: Number\n// @out Value: Number\nfunction hot(extra) {\n    return 3;\n}\n");
        vs::PollCustomNodeScripts(0);
        const VsDef* d = FindDef("Custom.hot");
        CHECK(d && std::string(d->title) == "Hot Renamed" && d->dataIn.size() == 1);
        // The script deleted: its node goes; the graph keeps working around it (a placeholder on its next load).
        fs::remove(file);
        vs::PollCustomNodeScripts(0);
        CHECK(!vs::FindRegisteredVsNode("Custom.hot"));
        std::string err;
        std::ifstream in(graphs / "hot.okayvs");
        std::stringstream ss; ss << in.rdbuf();
        auto g2 = vs::NodeGraph::Parse(ss.str(), &err, (graphs / "hot.okayvs").generic_string());
        CHECK(g2 && g2->MissingNodes().size() == 1);
    }

    // ---- 7. Save and load in the editor: the node, its nodelib line (relative to Assets) and its values come back ----
    {
        VsEditGraph eg;
        eg.path = (graphs / "saved.okayvs").generic_string();
        const int a = eg.add("OnStart", 0, 0);
        const int b = eg.add("Custom.double_it", 200, 0);
        const int c = eg.add("SetVar", 400, 0);
        eg.find(c)->arg = "result";
        CHECK(eg.find(b) && eg.find(b)->pinDefault.count(0) && eg.find(b)->pinDefault[0] == "1");   // the declared default
        eg.find(b)->pinDefault[0] = "8";
        eg.linkExec(a, 0, c);
        eg.linkData(c, 0, b, 0);
        CHECK(eg.save(eg.path));
        const std::string text = eg.toText();
        CHECK(text.find("nodelib \"Nodes/math.okay\"") != std::string::npos);
        VsEditGraph back;
        CHECK(back.load(eg.path));
        CHECK(back.find(b) && back.find(b)->type == "Custom.double_it" && back.find(b)->pinDefault[0] == "8");
        CHECK(back.toText() == text);
        Run r = RunFile(eg.path);
        CHECK(r.loaded && Var(r, "result") == "16");
    }

    // ---- 8. Make a custom version: a new script with the same pins, the block swapped in place, the same result ----
    {
        // Add (+): OkayScript writes the sum itself.
        VsEditGraph eg;
        eg.path = (graphs / "version.okayvs").generic_string();
        const int a = eg.add("OnStart", 0, 0);
        const int add = eg.add("Add", 200, 0);
        const int set = eg.add("SetVar", 400, 0);
        eg.find(set)->arg = "result";
        eg.find(add)->pinDefault[0] = "2";
        eg.find(add)->pinDefault[1] = "3";
        eg.linkExec(a, 0, set);
        eg.linkData(set, 0, add, 0);
        CHECK(VsCustomVersionRefusal(eg, *eg.find(add)).empty());
        CHECK(!VsCustomVersionRefusal(eg, *eg.find(a)).empty());   // an event can't
        const std::size_t wires = eg.dataLinks.size() + eg.execLinks.size();
        const std::string folder = VsCustomNodeFolder(proj.generic_string(), eg.path);
        CHECK(folder == (assets / "Nodes").generic_string());
        VsCustomVersionResult made = VsMakeCustomVersion(eg, add, "okayscript", folder);
        CHECK(made.ok);
        if (!made.ok) std::printf("  make custom version: %s\n", made.why.c_str());
        CHECK(eg.find(add) && eg.find(add)->type == made.type && made.type.rfind("Custom.", 0) == 0);
        CHECK(eg.dataLinks.size() + eg.execLinks.size() == wires);
        CHECK(eg.find(add)->pinDefault[0] == "2");
        std::ifstream in(made.path);
        std::stringstream ss; ss << in.rdbuf();
        CHECK(ss.str().find("@node Add (+) (custom)") != std::string::npos || ss.str().find("(custom)") != std::string::npos);
        CHECK(ss.str().find("return a + b;") != std::string::npos);
        const VsDef* d = FindDef(made.type);
        CHECK(d && d->dataIn.size() == 2 && d->dataOut.size() == 1);
        CHECK(eg.save(eg.path));
        Run r = RunFile(eg.path);
        CHECK(r.loaded && Var(r, "result") == "5");

        // Compare (a setting, no own code): based on the original, which runs first; the same result as the built-in.
        auto compareRun = [&](const std::string& type, const std::string& language, const std::string& name) {
            VsEditGraph g2;
            g2.path = (graphs / (name + ".okayvs")).generic_string();
            const int s0 = g2.add("OnStart", 0, 0);
            const int cmp = g2.add(type, 200, 0);
            const int sv = g2.add("SetVar", 400, 0);
            g2.find(sv)->arg = "result";
            g2.find(cmp)->arg = ">";
            g2.find(cmp)->pinDefault[0] = "7";
            g2.find(cmp)->pinDefault[1] = "4";
            g2.linkExec(s0, 0, sv);
            g2.linkData(sv, 0, cmp, 0);
            g2.save(g2.path);
            const std::string original = Var(RunFile(g2.path), "result");
            VsCustomVersionResult m2 = VsMakeCustomVersion(g2, cmp, language, folder);
            if (!m2.ok) { std::printf("  %s %s: %s\n", type.c_str(), language.c_str(), m2.why.c_str()); return std::make_pair(original, std::string("(failed)")); }
            g2.save(g2.path);
            Run rr = RunFile(g2.path);
            if (!rr.lastError.empty()) std::printf("  %s %s: %s\n", type.c_str(), language.c_str(), rr.lastError.c_str());
            return std::make_pair(original, Var(rr, "result"));
        };
        for (const char* lang : {"okayscript", "lua"}) {
            const auto res = compareRun("Compare", lang, std::string("cmp_") + lang);
            CHECK(!res.first.empty() && res.first == res.second);
        }
        // C#: Compare's output is a Bool and its inputs Numbers, which C# can take.
        {
            const auto res = compareRun("Compare", "cs", "cmp_cs");
            CHECK(!res.first.empty() && res.first == res.second);
        }
        // Lua and C# write Add's sum themselves too.
        for (const char* lang : {"lua", "cs"}) {
            VsEditGraph g3;
            g3.path = (graphs / (std::string("add_") + lang + ".okayvs")).generic_string();
            const int s0 = g3.add("OnStart", 0, 0);
            const int ad = g3.add("Add", 200, 0);
            const int sv = g3.add("SetVar", 400, 0);
            g3.find(sv)->arg = "result";
            g3.find(ad)->pinDefault[0] = "20";
            g3.find(ad)->pinDefault[1] = "22";
            g3.linkExec(s0, 0, sv);
            g3.linkData(sv, 0, ad, 0);
            VsCustomVersionResult m3 = VsMakeCustomVersion(g3, ad, lang, folder);
            CHECK(m3.ok);
            if (!m3.ok) { std::printf("  add %s: %s\n", lang, m3.why.c_str()); continue; }
            g3.save(g3.path);
            Run rr = RunFile(g3.path);
            CHECK(Var(rr, "result") == "42");
            if (Var(rr, "result") != "42") std::printf("  add %s: '%s' (%s)\n", lang, Var(rr, "result").c_str(), rr.lastError.c_str());
        }
        // A C# version of a node with an Any pin is refused with a reason (Print's Message is Any).
        {
            VsEditGraph g4;
            const int pr = g4.add("Print", 0, 0);
            VsCustomVersionResult m4 = VsMakeCustomVersion(g4, pr, "cs", folder);
            CHECK(!m4.ok && m4.why.find("C#") != std::string::npos);
            // ... and OkayScript takes it (based on the original).
            VsCustomVersionResult m5 = VsMakeCustomVersion(g4, pr, "okayscript", folder);
            CHECK(m5.ok);
        }
    }

    // ---- 9. The build ships the scripts a graph names (and a graph a scene embeds) ----
    {
        buildpipe::WalkSettings ws;
        ws.projectDir = proj.generic_string();
        const buildpipe::WalkResult w = buildpipe::WalkAssets({{"Graphs/pure.okayvs", ""}}, ws);
        bool shipped = false;
        for (const auto& a : w.assets) if (a.dest == "Nodes/math.okay") shipped = true;
        CHECK(shipped);
        Write(assets / "Embed.okayscene",
              "gameobject \"A\"\n  visualscript \"# OkayVS graph\nnodelib \\\"Nodes/more.lua\\\"\nnode 1 OnStart\n\"\n");
        const buildpipe::WalkResult w2 = buildpipe::WalkAssets({{"Embed.okayscene", ""}}, ws);
        bool shipped2 = false;
        for (const auto& a : w2.assets) if (a.dest == "Nodes/more.lua") shipped2 = true;
        CHECK(shipped2);
    }

    // ---- 10. A folder scan finds every script that declares a node ----
    {
        Write(proj / "Other" / "x.lua", "-- @node Scanned\n-- @out V: Number\nfunction scanned()\n    return 1\nend\n");
        Write(proj / "Other" / "plain.lua", "function update(dt) end\n");
        CHECK(vs::ScanCustomNodeFolder((proj / "Other").generic_string()) == 1);
        CHECK(vs::FindRegisteredVsNode("Custom.scanned") != nullptr);
        // Declaration problems are said with their line.
        const vs::CustomNodeScript bad = vs::ParseCustomNodeScript("-- @node X\n-- @in A: Wobble\nfunction x(a) end\n", "lua", "bad.lua");
        CHECK(bad.nodes.empty() && !bad.problems.empty() && bad.problems[0].find("line 2") != std::string::npos);
        const vs::CustomNodeScript loc = vs::ParseCustomNodeScript("-- @node X\nlocal function x() end\n", "lua", "loc.lua");
        CHECK(loc.nodes.empty() && !loc.problems.empty());
    }

    Log::sink = sink;
    CHECK(sandbox.Verify());
    TEST_MAIN_RESULT();
}
