#pragma once
// One list of what is wrong in the project (feature map 15.11 / 23.4).
// A diagnostic is a severity, a stable code, a plain-English message, a
// how-to-fix hint and a source (object, component, asset, script line or
// visual-script node). Reporting the same code for the same source again
// updates the row instead of adding another. Clear by source or by the
// validator that owns the row ("project", "graph.editor", ...).
//
// New and changed rows are also written through Log, so the Console shows
// them. The message carries a [Object: path] or [Asset: file:line] tag and
// the call runs inside a ConsoleSourceScope, so a ConsoleModel attached to
// the log (and the editor Console) can select the object.
//
//     Diagnostics& d = EditorDiagnostics();
//     d.ReplaceOwner("project", CollectProjectDiagnostics(scene, opt).items);
//     d.ClearSource(src);          // that object / file / graph is gone
//
// Node requirement checks (the visual-script browser's metadata) report with
// AddNodeRequirement. CollectProjectDiagnostics reads that table.
#include "okay/Debug/ConsoleModel.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace okay {

enum class DiagSeverity { Info = 0, Warning = 1, Error = 2 };

enum class DiagFixKind {
    None = 0,
    AddComponent,      // arg = component type name ("BoxCollider3D"), or "*" for every missing requirement
    ClearReference,    // blank the field named by the source
    Reimport,          // point the field at arg (a file that is actually on disk)
    AddCamera,         // create a Main Camera
    MakeMainCamera     // flag the source object's camera as the main camera
};

/// A safe one-click fix. None means the row is only a report.
struct DiagFix {
    DiagFixKind kind = DiagFixKind::None;
    std::string arg;
};

/// Where the problem lives. Empty fields are "not this kind of source".
struct DiagSource {
    std::string objectGuid;
    std::string objectPath;
    std::string component;
    std::string field;
    std::string assetPath;
    std::string scriptPath;
    int scriptLine = 0;
    int scriptColumn = 0;
    std::string graphPath;
    std::string nodeId;

    bool Empty() const;
    /// Stable identity of this source, for dedupe. Two reports with the same
    /// code and the same key are one row.
    std::string Key() const;
    ConsoleSource ToConsole() const;
};

struct Diagnostic {
    std::uint64_t id = 0;
    DiagSeverity severity = DiagSeverity::Error;
    std::string code;
    std::string message;
    std::string hint;
    DiagSource source;
    std::string owner;     // which validator owns the row, for ClearOwner / ReplaceOwner
    DiagFix fix;
};

/// What a visual-script node needs on its object. The node browser registers
/// these; graph validation turns a missing one into a diagnostic.
struct NodeRequirement {
    std::string component;   // type name, "Rigidbody3D"
    std::string hint;
};

class Scene;

class Diagnostics {
public:
    Diagnostics();
    ~Diagnostics();
    Diagnostics(const Diagnostics&) = delete;
    Diagnostics& operator=(const Diagnostics&) = delete;

    void SetLogging(bool on);
    bool Logging() const;

    /// Insert or update. The same owner + code + source key is one row.
    /// A new row, or one whose message or severity changed, is logged.
    void Report(Diagnostic d);
    /// Drop every row this validator owns, then report `items` (ids of rows
    /// that are still the same problem are kept). Only new or changed rows log.
    void ReplaceOwner(const std::string& owner, std::vector<Diagnostic> items);
    void ClearOwner(const std::string& owner);
    /// Drop rows about this object, asset, script or graph. A set node id
    /// limits the clear to that node; a set guid clears the whole object.
    void ClearSource(const DiagSource& source);
    void ClearAll();

    std::vector<Diagnostic> Items() const;
    int Count(DiagSeverity severity) const;
    std::size_t Size() const;

    /// Rows about this object (guid), worst severity first.
    std::vector<Diagnostic> ForObject(const std::string& guid) const;

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

/// The editor's list. Tests use their own Diagnostics so they do not share it.
Diagnostics& EditorDiagnostics();

/// Register one node requirement. Later calls for the same node type and
/// component replace the hint. Pass an empty component to remove that node.
void AddNodeRequirement(const std::string& nodeType, const std::string& componentType, const std::string& hint);
void ClearNodeRequirements();
/// Everything registered for `nodeType` (empty when the browser has no metadata for it).
std::vector<NodeRequirement> NodeRequirementsFor(const std::string& nodeType);

/// Apply a row's fix to `scene`. False (and `error` set) when there is no safe fix
/// or the object is gone. Does not log and does not edit the diagnostic list.
bool ApplyDiagnosticFix(Scene& scene, const Diagnostic& d, std::string* error = nullptr);

} // namespace okay
