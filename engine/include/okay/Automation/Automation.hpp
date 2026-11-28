#pragma once
// Automation: the engine's one table of commands that an outside program (a test script, the okay command line, an AI
// agent through MCP) runs to operate the engine and the editor. Design note: docs/engine/automation.md.
//
//   Registry   every command, by name ("scene.create_object"), with its summary, its parameters (written out as JSON
//              Schema for MCP's tools/list), what it returns, what can go wrong, and how it may run (Flags).
//   Host       where commands act: the scene, the project folder, the editor's undo and Play. The okay command line's
//              headless host and the running editor are both an EditorHost (editor/AutomationEditor.hpp) around the
//              editor's own EditorState, so a command behaves the same in both.
//   Call       runs a command: unknown names, parameters that do not fit the schema, play-mode refusals, engine errors
//              and C++ exceptions all come back as a structured Error, never as a throw. A command that changes the
//              scene is ONE step of the host's undo history (Host::BeginEdit / EndEdit around the handler).
//
// Commands are written next to the system they drive and handed over with AddProvider(system, fn) (the engine's own
// providers are in Automation.cpp's list), so a new engine feature becomes reachable by adding its commands where its
// code lives; MCP (Mcp.hpp), `okay automation` and the tests pick them up from here. Nothing in this header needs
// <thread> or <mutex> (CLAUDE.md rule 21a): the registry is filled and used on one thread.
//
// Written for this engine from the JSON Schema core vocabulary (draft 2020-12: type, properties, required, enum,
// items, default, description) and the Model Context Protocol's tool shape (name, description, inputSchema); no code
// from another project.
#include "okay/Core/Json.hpp"

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace okay {
class Scene;
class GameObject;
class PropertyValue;
namespace vs { class VsValue; }
}

namespace okay::automation {

/// What a parameter holds. Vec3 is {"x":..,"y":..,"z":..} or [x, y, z]; Object is a JSON object; Any is anything.
enum class ParamType : std::uint8_t { String, Number, Integer, Boolean, Vec3, Object, Array, Any };
const char* ParamTypeName(ParamType t);

struct Param {
    std::string name;
    ParamType type = ParamType::String;
    std::string description;
    bool required = false;
    std::vector<std::string> options;   ///< String: the only values allowed (written as "enum")
    json::Value defaultValue;           ///< what a left-out optional parameter means (shown in the schema; Null = none)
};

/// How a command may run.
namespace flag {
enum : unsigned {
    Mutates = 1u << 0,       ///< changes the scene, the project or the editor
    Undoable = 1u << 1,      ///< its change is one step of the host's undo history (implies Mutates)
    NeedsEditor = 1u << 2,   ///< only a host with the editor's state (EditorHost) can run it
    NeedsPlay = 1u << 3,     ///< only while the game is playing
    AllowInPlay = 1u << 4,   ///< a Mutates command that may run while playing (it acts on the running game)
    Slow = 1u << 5,          ///< may take seconds or minutes (a build, a long play run)
};
}

/// The error codes a command can answer with (Error::code).
namespace code {
inline constexpr const char* UnknownCommand = "unknown_command";
inline constexpr const char* InvalidParams = "invalid_params";
inline constexpr const char* NotFound = "not_found";          ///< an object, component, file or name does not exist
inline constexpr const char* Ambiguous = "ambiguous";         ///< a name matched several objects
inline constexpr const char* Refused = "refused";             ///< not now (Play is running, read-only, nothing to undo)
inline constexpr const char* Unavailable = "unavailable";     ///< this host cannot do it (no editor, no project)
inline constexpr const char* Failed = "failed";               ///< the engine tried and reported a problem
inline constexpr const char* NeedsUi = "needs_ui";            ///< it opens a dialog a person must answer (details name a parameter that does it without one, when there is)
inline constexpr const char* Internal = "internal";           ///< a C++ exception escaped the handler
}

struct Error {
    std::string code;
    std::string message;   ///< one plain sentence
    json::Value details;   ///< more, when there is more (Null = none)
};

/// A picture a command made, for MCP image content (base64 of the file bytes).
struct Attachment {
    std::string mimeType;   ///< "image/png"
    std::string base64;
};

struct Result {
    bool ok = true;
    json::Value data;                    ///< the answer (an object for every command)
    Error error;                         ///< set when !ok
    std::vector<Attachment> attachments;

    static Result Ok(json::Value data = json::Value::Obj());
    static Result Fail(std::string code, std::string message, json::Value details = json::Value());
    /// {"ok":true,"data":...} or {"ok":false,"error":{"code","message","details"}}: the one JSON form every host prints.
    json::Value ToJson() const;
};

/// Where commands act. EditorHost (editor/AutomationEditor.hpp) is the real one; tests make small ones.
class Host {
public:
    virtual ~Host() = default;
    /// The scene commands read and change (never null for a usable host).
    virtual Scene* scene() = 0;
    /// The open project's folder ("" when none).
    virtual std::string projectDir() const { return {}; }
    /// "editor" for the running editor, "headless" for okay mcp / okay automation, anything for a test host.
    virtual std::string kind() const { return "test"; }
    virtual bool hasEditor() const { return false; }
    virtual bool isPlaying() const { return false; }
    /// Around an Undoable command: everything the handler changes becomes ONE undo step called `name`. EndEdit gets
    /// whether the handler succeeded; a failed command records nothing it can avoid recording.
    virtual void BeginEdit(const std::string& name) { (void)name; }
    virtual void EndEdit(bool ok) { (void)ok; }
    /// The editor's Scene view camera, for viewport.screenshot's view "editor": where it is, what it looks at, and for a
    /// 2D view its orthographic half height. False when the host has no such view.
    virtual bool EditorView(float eye[3], float target[3], bool& orthographic, float& size) const {
        (void)eye; (void)target; (void)orthographic; (void)size;
        return false;
    }
    /// Whether script.run may run code here. The running editor answers its "Allow scripts from Automation" option
    /// (View > Automation Link); a host an agent started itself (okay mcp --headless) allows it.
    virtual bool ScriptsAllowed() const { return true; }
    /// script.run ran `lines` lines of `language` here: the editor writes a notice line in its Console.
    virtual void NoteScriptRun(const std::string& language, int lines) { (void)language; (void)lines; }
};

/// What a handler gets: the host and the checked parameters, with readers that fill in the schema's defaults.
class Call {
public:
    Call(Host& host, const json::Value& params) : m_host(host), m_params(params) {}
    Host& host() const { return m_host; }
    Scene& scene() const;   ///< host().scene(); Registry::Run refuses a host without one before a handler runs
    const json::Value& params() const { return m_params; }
    bool Has(const char* name) const;   ///< given and not null
    const json::Value* Get(const char* name) const;
    std::string Str(const char* name, const std::string& def = {}) const;
    double Num(const char* name, double def = 0.0) const;
    long long Int(const char* name, long long def = 0) const;
    bool Bool(const char* name, bool def = false) const;
    /// A Vec3 parameter as three floats (def when absent). JSON of either form ({x,y,z} or [x,y,z]).
    void Vec(const char* name, float out[3], float dx = 0, float dy = 0, float dz = 0) const;

private:
    Host& m_host;
    const json::Value& m_params;
};

using Handler = std::function<Result(Call&)>;

struct Command {
    std::string name;      ///< "namespace.verb_object": lower case, digits, '_' and one or more '.'
    std::string summary;   ///< one plain sentence: what it does and which engine system does it
    std::vector<Param> params;
    std::string result;    ///< the shape of Result::data, in words ("{id, name, path}")
    std::vector<std::string> errors;   ///< error codes it can answer with besides the general ones
    unsigned flags = 0;
    std::string undoName;  ///< the undo step's name ("" = the command's summary is not used: the scene change names it)
    Handler handler;
    std::string aliasOf;   ///< set on an alias: the command it runs (Registry::AddAlias)
};

class Registry {
public:
    /// Add a command. False (nothing added) for a name already taken (also as an MCP tool name, '.' read as '_'), a
    /// malformed name, a missing handler, or two parameters with one name.
    bool Add(Command c);
    /// `alias` runs `target` (same parameters, flags and handler); its summary says so. For two names of one action
    /// (an op of a menu item that a richer hand-written command already does): one handler, never two copies. False when
    /// `target` does not exist or `alias` is taken.
    bool AddAlias(const std::string& alias, const std::string& target);
    const Command* Find(const std::string& name) const;
    /// Every command, sorted by name; `prefix` ("scene.") keeps those that start with it.
    std::vector<const Command*> List(const std::string& prefix = {}) const;
    std::size_t Size() const { return m_commands.size(); }

    /// Run `name` with `params` (an object, or null for none) on `host`. Never throws.
    Result Run(Host& host, const std::string& name, const json::Value& params) const;

    /// The command as data: name, summary, flags, parameters, inputSchema (JSON Schema), result, errors.
    static json::Value Describe(const Command& c);
    /// The command's parameters as a JSON Schema object ({"type":"object","properties":...,"required":[...]}).
    static json::Value InputSchema(const Command& c);
    /// Check `params` against the command's parameters; "" when they fit, else what is wrong (unknown name, missing
    /// required one, wrong type, a value not among the options).
    static std::string Validate(const Command& c, const json::Value& params);

private:
    std::vector<Command> m_commands;
};

/// A system hands over the function that adds its commands. Called once, the first time Commands() is used after it;
/// handing the same function over twice keeps one.
void AddProvider(const char* system, void (*fn)(Registry&));
/// The process's command table: the engine's providers (Automation.cpp) and every AddProvider so far.
Registry& Commands();

// ---- Helpers the command files share (EngineCommands.cpp, editor/Automation*.cpp, tools/okay-cli/AutomationCli.cpp) ---
/// A parameter; one with fixed choices (a String); one with a default.
Param Arg(const char* name, ParamType type, const char* description, bool required = false);
Param ArgChoice(const char* name, std::vector<std::string> options, const char* description, const char* def);
Param ArgDefault(const char* name, ParamType type, const char* description, json::Value def);
Command MakeCommand(const char* name, const char* summary, std::vector<Param> params, const char* result, unsigned flags,
                    Handler handler, std::vector<std::string> errors = {});
/// The failure an Error describes (from FindObject and the like).
Result Fail(const Error& e);

/// The object a parameter names: its GUID, else a hierarchy path ("Level/Door"), else a unique name. Null with `error`
/// (not_found / ambiguous) when there is none or several.
GameObject* FindObject(Scene& scene, const std::string& ref, Error& error);
/// {id (GUID), name, path, active, components: [type names]}; with `deep` also transform and children ids.
json::Value ObjectSummary(const GameObject& go, bool deep = false);
json::Value Vec3Json(float x, float y, float z);

/// Script values <-> JSON (EngineCommands.cpp): numbers, true/false, text, {x,y,z} (Vec3), {r,g,b,a} (Color),
/// {"object": id|path|name} (a GameObject), arrays and other objects (maps).
json::Value ScriptValueToJson(const vs::VsValue& v);
bool JsonToScriptValue(Scene& scene, const json::Value& j, vs::VsValue& out, std::string& error);
/// A property value as JSON (vectors as {x,y,z(,w)}, colours as {r,g,b,a}, an enum as its option name), and a JSON value
/// in the property text form PropertyValue::FromString reads (false for a value with no such form).
json::Value PropertyToJson(const PropertyValue& v);
bool JsonToPropertyText(const json::Value& j, std::string& out);

/// The engine log's last lines, kept for log.read: InstallLogRing chains onto Log::sink (the sink that was there still
/// gets every line). Lines are numbered from 1 so a reader can ask only for what came after. Safe from any thread.
struct LogLine { std::uint64_t seq = 0; int level = 0; std::string text; };
void InstallLogRing(std::size_t keep = 2000);
bool LogRingInstalled();
std::vector<LogLine> LogSince(std::uint64_t after, std::size_t max, int minLevel);

} // namespace okay::automation
