#include "okay/Debug/Diagnostics.hpp"
#include "okay/Core/Log.hpp"

#include <algorithm>
#include <mutex>
#include <unordered_map>
#include <utility>

namespace okay {

bool DiagSource::Empty() const {
    return objectGuid.empty() && objectPath.empty() && component.empty() && field.empty() &&
           assetPath.empty() && scriptPath.empty() && graphPath.empty() && nodeId.empty() &&
           scriptLine == 0;
}

std::string DiagSource::Key() const {
    std::string k;
    k.reserve(objectGuid.size() + objectPath.size() + component.size() + field.size() +
              assetPath.size() + scriptPath.size() + graphPath.size() + nodeId.size() + 16);
    k += objectGuid; k += '|';
    k += objectPath; k += '|';
    k += component; k += '|';
    k += field; k += '|';
    k += assetPath; k += '|';
    k += scriptPath; k += '|';
    k += std::to_string(scriptLine); k += '|';
    k += graphPath; k += '|';
    k += nodeId;
    return k;
}

ConsoleSource DiagSource::ToConsole() const {
    ConsoleSource s;
    if (!graphPath.empty() || !nodeId.empty()) s.kind = ConsoleSource::Kind::Graph;
    else if (!scriptPath.empty()) s.kind = ConsoleSource::Kind::Script;
    else if (!assetPath.empty()) s.kind = ConsoleSource::Kind::Asset;
    else if (!objectGuid.empty() || !objectPath.empty()) s.kind = ConsoleSource::Kind::Object;
    if (!scriptPath.empty()) s.path = scriptPath;
    else if (!assetPath.empty()) s.path = assetPath;
    else if (!graphPath.empty()) s.path = graphPath;
    else s.path = objectPath;
    s.guid = objectGuid;
    s.component = component;
    s.nodeId = nodeId;
    s.line = scriptLine;
    s.column = scriptColumn;
    return s;
}

namespace {

std::string RowKey(const Diagnostic& d) {
    return d.owner + "\n" + d.code + "\n" + d.source.Key();
}

std::string LogLine(const Diagnostic& d) {
    std::string line;
    const ConsoleSource src = d.source.ToConsole();
    if (src.kind == ConsoleSource::Kind::Object && !src.path.empty())
        line += "[Object: " + src.path + "] ";
    else if ((src.kind == ConsoleSource::Kind::Script || src.kind == ConsoleSource::Kind::Asset ||
              src.kind == ConsoleSource::Kind::Graph) && !src.path.empty()) {
        line += "[Asset: " + src.path;
        if (src.line > 0) line += ":" + std::to_string(src.line);
        line += "] ";
    } else if (!d.source.objectPath.empty())
        line += "[Object: " + d.source.objectPath + "] ";
    line += d.message;
    if (!d.code.empty()) line += " [" + d.code + "]";
    return line;
}

bool SameProblem(const Diagnostic& a, const Diagnostic& b) {
    return a.severity == b.severity && a.message == b.message && a.hint == b.hint &&
           a.fix.kind == b.fix.kind && a.fix.arg == b.fix.arg;
}

void Emit(const Diagnostic& d) {
    if (d.severity == DiagSeverity::Info) return;   // the Problems list holds infos; the Console stays for warnings and errors
    ConsoleSourceScope scope(d.source.ToConsole());
    const std::string line = LogLine(d);
    if (d.severity == DiagSeverity::Error) Log::Error(line);
    else Log::Warning(line);
}

bool SourceMatches(const DiagSource& row, const DiagSource& want) {
    if (!want.objectGuid.empty() && row.objectGuid == want.objectGuid) {
        if (!want.nodeId.empty()) return row.nodeId == want.nodeId;
        return true;
    }
    if (!want.scriptPath.empty() && row.scriptPath == want.scriptPath) return true;
    if (!want.assetPath.empty() && row.assetPath == want.assetPath) return true;
    if (!want.graphPath.empty() && row.graphPath == want.graphPath) {
        if (!want.nodeId.empty()) return row.nodeId == want.nodeId;
        return true;
    }
    if (!want.objectPath.empty() && want.objectGuid.empty() && row.objectPath == want.objectPath) return true;
    return false;
}

struct ReqTable {
    std::mutex mu;
    // node type -> (component type -> hint)
    std::unordered_map<std::string, std::unordered_map<std::string, std::string>> byNode;
};

ReqTable& Requirements() {
    static ReqTable t;
    return t;
}

} // namespace

struct Diagnostics::Impl {
    std::mutex mu;
    bool logging = true;
    std::uint64_t nextId = 1;
    std::vector<Diagnostic> items;
};

Diagnostics::Diagnostics() : m_impl(std::make_unique<Impl>()) {}
Diagnostics::~Diagnostics() = default;

void Diagnostics::SetLogging(bool on) { std::lock_guard<std::mutex> g(m_impl->mu); m_impl->logging = on; }
bool Diagnostics::Logging() const { std::lock_guard<std::mutex> g(m_impl->mu); return m_impl->logging; }

void Diagnostics::Report(Diagnostic d) {
    bool doLog = false;
    Diagnostic logged;
    {
        std::lock_guard<std::mutex> g(m_impl->mu);
        const std::string key = RowKey(d);
        bool found = false;
        for (Diagnostic& have : m_impl->items) {
            if (RowKey(have) != key) continue;
            const bool changed = !SameProblem(have, d);
            const std::uint64_t id = have.id;
            have = std::move(d);
            have.id = id;
            if (changed && m_impl->logging) { doLog = true; logged = have; }
            found = true;
            break;
        }
        if (!found) {
            d.id = m_impl->nextId++;
            if (m_impl->logging) { doLog = true; logged = d; }
            m_impl->items.push_back(std::move(d));
        }
    }
    if (doLog) Emit(logged);
}

void Diagnostics::ReplaceOwner(const std::string& owner, std::vector<Diagnostic> items) {
    std::vector<Diagnostic> toLog;
    {
        std::lock_guard<std::mutex> g(m_impl->mu);
        std::unordered_map<std::string, Diagnostic> previous;
        std::vector<Diagnostic> kept;
        kept.reserve(m_impl->items.size());
        for (Diagnostic& have : m_impl->items) {
            if (have.owner != owner) { kept.push_back(std::move(have)); continue; }
            previous.emplace(RowKey(have), std::move(have));
        }
        for (Diagnostic& d : items) {
            d.owner = owner;
            const std::string key = RowKey(d);
            auto it = previous.find(key);
            if (it == previous.end()) {
                d.id = m_impl->nextId++;
                if (m_impl->logging) toLog.push_back(d);
            } else {
                d.id = it->second.id;
                if (m_impl->logging && !SameProblem(it->second, d)) toLog.push_back(d);
            }
            kept.push_back(std::move(d));
        }
        m_impl->items = std::move(kept);
    }
    for (const Diagnostic& d : toLog) Emit(d);
}

void Diagnostics::ClearOwner(const std::string& owner) {
    std::lock_guard<std::mutex> g(m_impl->mu);
    m_impl->items.erase(std::remove_if(m_impl->items.begin(), m_impl->items.end(),
                                        [&](const Diagnostic& d) { return d.owner == owner; }),
                        m_impl->items.end());
}

void Diagnostics::ClearSource(const DiagSource& source) {
    std::lock_guard<std::mutex> g(m_impl->mu);
    m_impl->items.erase(std::remove_if(m_impl->items.begin(), m_impl->items.end(),
                                        [&](const Diagnostic& d) { return SourceMatches(d.source, source); }),
                        m_impl->items.end());
}

void Diagnostics::ClearAll() {
    std::lock_guard<std::mutex> g(m_impl->mu);
    m_impl->items.clear();
}

std::vector<Diagnostic> Diagnostics::Items() const {
    std::lock_guard<std::mutex> g(m_impl->mu);
    return m_impl->items;
}

int Diagnostics::Count(DiagSeverity severity) const {
    std::lock_guard<std::mutex> g(m_impl->mu);
    int n = 0;
    for (const Diagnostic& d : m_impl->items) if (d.severity == severity) ++n;
    return n;
}

std::size_t Diagnostics::Size() const {
    std::lock_guard<std::mutex> g(m_impl->mu);
    return m_impl->items.size();
}

std::vector<Diagnostic> Diagnostics::ForObject(const std::string& guid) const {
    std::vector<Diagnostic> out;
    if (guid.empty()) return out;
    std::lock_guard<std::mutex> g(m_impl->mu);
    for (const Diagnostic& d : m_impl->items)
        if (d.source.objectGuid == guid) out.push_back(d);
    std::stable_sort(out.begin(), out.end(), [](const Diagnostic& a, const Diagnostic& b) {
        return (int)a.severity > (int)b.severity;
    });
    return out;
}

Diagnostics& EditorDiagnostics() {
    static Diagnostics d;
    return d;
}

void AddNodeRequirement(const std::string& nodeType, const std::string& componentType, const std::string& hint) {
    ReqTable& t = Requirements();
    std::lock_guard<std::mutex> g(t.mu);
    if (componentType.empty()) { t.byNode.erase(nodeType); return; }
    t.byNode[nodeType][componentType] = hint;
}

void ClearNodeRequirements() {
    ReqTable& t = Requirements();
    std::lock_guard<std::mutex> g(t.mu);
    t.byNode.clear();
}

std::vector<NodeRequirement> NodeRequirementsFor(const std::string& nodeType) {
    std::vector<NodeRequirement> out;
    ReqTable& t = Requirements();
    std::lock_guard<std::mutex> g(t.mu);
    auto it = t.byNode.find(nodeType);
    if (it == t.byNode.end()) return out;
    out.reserve(it->second.size());
    for (const auto& kv : it->second) out.push_back({kv.first, kv.second});
    std::sort(out.begin(), out.end(), [](const NodeRequirement& a, const NodeRequirement& b) {
        return a.component < b.component;
    });
    return out;
}

} // namespace okay
