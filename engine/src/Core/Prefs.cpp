#include "okay/Core/Prefs.hpp"
#include "okay/Core/Log.hpp"
#include "okay/Core/SaveData.hpp"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <filesystem>
#include <vector>

// The private threading shim (never <mutex> here: see CoreThreads.hpp). Last of the includes,
// because on Windows it pulls in <windows.h>.
#include "CoreThreads.hpp"

namespace okay {

namespace {

// One lock for the keys, the listeners and the last error. Listeners are called outside it
// (they may read or set prefs themselves).
corethreads::Mutex& Lock() {
    static corethreads::Mutex m;
    return m;
}
std::vector<std::pair<int, Prefs::Listener>>& Listeners() {
    static std::vector<std::pair<int, Prefs::Listener>> l;
    return l;
}
int g_nextListener = 1;
std::string& ErrorText() { static std::string e; return e; }
bool g_usedBackup = false;

void Notify(const std::string& key) {
    std::vector<std::pair<int, Prefs::Listener>> copy;   // a listener may remove itself
    {
        corethreads::LockGuard g(Lock());
        copy = Listeners();
    }
    for (const auto& l : copy) if (l.second) l.second(key);
}
std::string FloatText(float value) { return savefmt::FormatFloat(value); }
// Read a float in the classic ("C") locale, so a program that changed the global
// locale can't misparse "0.75". False when the text is not a number.
bool ParseFloat(const std::string& text, float& out) { return savefmt::TryParseFloat(text, out); }
std::string Lower(std::string s) {
    for (char& c : s) c = (char)std::tolower((unsigned char)c);
    return s;
}
std::vector<std::string> SplitCommas(const std::string& s) {
    std::vector<std::string> out;
    std::size_t from = 0;
    while (true) {
        const std::size_t c = s.find(',', from);
        out.push_back(s.substr(from, c == std::string::npos ? std::string::npos : c - from));
        if (c == std::string::npos) break;
        from = c + 1;
    }
    return out;
}
} // namespace

std::unordered_map<std::string, std::string>& Prefs::Store() {
    static std::unordered_map<std::string, std::string> s;
    return s;
}

namespace {
// Store `value` under `key`, telling listeners when that changed something.
void Put(std::unordered_map<std::string, std::string>& store, const std::string& key, std::string value) {
    {
        corethreads::LockGuard g(Lock());
        auto it = store.find(key);
        if (it != store.end() && it->second == value) return;
        store[key] = std::move(value);
    }
    Notify(key);
}
// The value of `key`, or false when there is none.
bool Read(const std::unordered_map<std::string, std::string>& store, const std::string& key, std::string& out) {
    corethreads::LockGuard g(Lock());
    auto it = store.find(key);
    if (it == store.end()) return false;
    out = it->second;
    return true;
}
} // namespace

int Prefs::AddListener(Listener fn) {
    corethreads::LockGuard g(Lock());
    const int id = g_nextListener++;
    Listeners().push_back({id, std::move(fn)});
    return id;
}
void Prefs::RemoveListener(int id) {
    corethreads::LockGuard g(Lock());
    auto& ls = Listeners();
    ls.erase(std::remove_if(ls.begin(), ls.end(), [id](const auto& l) { return l.first == id; }), ls.end());
}

void Prefs::SetInt(const std::string& key, int value) { Put(Store(), key, std::to_string(value)); }
void Prefs::SetFloat(const std::string& key, float value) { Put(Store(), key, FloatText(value)); }
void Prefs::SetString(const std::string& key, const std::string& value) { Put(Store(), key, value); }
void Prefs::SetBool(const std::string& key, bool value) { Put(Store(), key, value ? "1" : "0"); }
void Prefs::SetVec3(const std::string& key, const Vec3& v) {
    Put(Store(), key, FloatText(v.x) + "," + FloatText(v.y) + "," + FloatText(v.z));
}
void Prefs::SetColor(const std::string& key, const Color& c) {
    Put(Store(), key, FloatText(c.r) + "," + FloatText(c.g) + "," + FloatText(c.b) + "," + FloatText(c.a));
}

int Prefs::GetInt(const std::string& key, int def) {
    std::string v;
    if (!Read(Store(), key, v)) return def;
    try {
        std::size_t used = 0;
        const int n = std::stoi(v, &used);
        if (used < v.size() && (v[used] == '.' || v[used] == 'e' || v[used] == 'E')) {
            // A float's text ("1e+09" as SetFloat writes it, "3.7"): read it whole. One beyond
            // the int range gives the default, as an integer that big does.
            float f = 0.0f;
            if (ParseFloat(v, f)) return (f > -2147483648.0f && f < 2147483648.0f) ? (int)f : def;
        }
        return n;
    } catch (...) { return def; }
}
float Prefs::GetFloat(const std::string& key, float def) {
    std::string v;
    if (!Read(Store(), key, v)) return def;
    float f = 0.0f;
    return ParseFloat(v, f) ? f : def;
}
std::string Prefs::GetString(const std::string& key, const std::string& def) {
    std::string v;
    return Read(Store(), key, v) ? v : def;
}
bool Prefs::GetBool(const std::string& key, bool def) {
    std::string raw;
    if (!Read(Store(), key, raw)) return def;
    const std::string v = Lower(raw);
    if (v == "true" || v == "yes" || v == "on") return true;
    if (v == "false" || v == "no" || v == "off") return false;
    float f = 0.0f;
    return ParseFloat(v, f) ? f != 0.0f : def;
}
Vec3 Prefs::GetVec3(const std::string& key, const Vec3& def) {
    std::string v;
    if (!Read(Store(), key, v)) return def;
    const auto parts = SplitCommas(v);
    float c[3];
    if (parts.size() != 3) return def;
    for (int i = 0; i < 3; ++i) if (!ParseFloat(parts[i], c[i])) return def;
    return Vec3(c[0], c[1], c[2]);
}
Color Prefs::GetColor(const std::string& key, const Color& def) {
    std::string v;
    if (!Read(Store(), key, v)) return def;
    const auto parts = SplitCommas(v);
    float c[4] = {0, 0, 0, 1.0f};
    if (parts.size() != 3 && parts.size() != 4) return def;
    for (std::size_t i = 0; i < parts.size(); ++i) if (!ParseFloat(parts[i], c[i])) return def;
    return Color(c[0], c[1], c[2], c[3]);
}

bool Prefs::Has(const std::string& key) {
    corethreads::LockGuard g(Lock());
    return Store().count(key) != 0;
}
void Prefs::Delete(const std::string& key) {
    bool removed = false;
    {
        corethreads::LockGuard g(Lock());
        removed = Store().erase(key) != 0;
    }
    if (removed) Notify(key);
}
void Prefs::Clear() {
    std::vector<std::string> keys;
    {
        corethreads::LockGuard g(Lock());
        keys.reserve(Store().size());
        for (const auto& kv : Store()) keys.push_back(kv.first);
        Store().clear();
    }
    for (const auto& k : keys) Notify(k);
}

namespace {
const char* const kEndLine = "#end";
// Encode a string onto a single line: backslash-escape backslashes, newlines,
// carriage returns and tabs (a raw tab in a key would split it at the wrong
// place). Files written before tab/CR escaping still decode the same way.
std::string Encode(const std::string& s) {
    std::string out;
    for (char c : s) {
        if (c == '\\') out += "\\\\";
        else if (c == '\n') out += "\\n";
        else if (c == '\t') out += "\\t";
        else if (c == '\r') out += "\\r";
        else out += c;
    }
    return out;
}
std::string Decode(const std::string& s) {
    std::string out;
    for (std::size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '\\' && i + 1 < s.size()) {
            char n = s[++i];
            out += (n == 'n') ? '\n' : (n == 't') ? '\t' : (n == 'r') ? '\r' : n;
        } else {
            out += s[i];
        }
    }
    return out;
}

constexpr std::size_t kMaxPrefsBytes = 64u * 1024u * 1024u;
// The newest prefs format this build writes; a higher number is refused (never parsed).
constexpr int kPrefsFormat = 3;
// The last two lines of a version 3 file: `#end`, then `#sum <16 hex>` over every byte before the `#sum` line.
const char kTrailerTail[] = "#end\n#sum ";
const char kHandEditHint[] = "; to edit the file by hand, set its first line to okayprefs 2 and delete the #sum line";

enum class ReadState { Ok, Missing, Damaged, Newer };

// Parse a prefs file's text into `fresh`. `why` says what is wrong with a damaged one (or that a newer
// build wrote it).
//
// What each writer wrote (Prefs.cpp history): #24 and #279 `okayprefs 1` and `key<TAB>value` lines; #552 and
// #611 `okayprefs 2`, the same lines and a last `#end` line; from this change `okayprefs 3`, the same lines,
// `#end` and `#sum <16 hex>`. Load has always also taken a file with no header (`key<TAB>value` lines only).
// So:
//  * the `#end` / `#sum` lines are checked whenever they are there, whatever the first line says (the sum
//    covers the first line too, so a byte changed there fails it like a byte anywhere else);
//  * a first line that starts `okayprefs` must go on with one space and a number; anything else is damage,
//    never "version 1" (which would skip the end-line check);
//  * a number above kPrefsFormat is a newer format: refused, not parsed (its sum is checked first when it
//    has one, so a damaged digit is still damage);
//  * version 3 must have the trailer and version 2 the `#end` line (a file cut short has lost them);
//  * no header is the oldest layout: its first line must be a key line, and it never had an `#end` line.
ReadState ParsePrefsText(const std::string& raw, std::unordered_map<std::string, std::string>& fresh, std::string* why) {
    fresh.clear();
    // A raw carriage return only comes from a tool that changed the line ends (the writer escapes the
    // ones in values), so a file saved with CR LF reads like one with LF; a UTF-8 byte order mark an
    // editor put in front is dropped too. Both before the checksum.
    std::string text = raw;
    if (text.compare(0, 3, "\xEF\xBB\xBF") == 0) text.erase(0, 3);
    if (text.find('\r') != std::string::npos) text.erase(std::remove(text.begin(), text.end(), '\r'), text.end());
    const std::size_t nl = text.find('\n');
    int version = 0;   // 0: no header
    std::string shown;
    switch (savefmt::ParseHeaderVersion(text.substr(0, nl), "okayprefs", version, &shown)) {
        case savefmt::HeaderVersion::Other: version = 0; break;
        case savefmt::HeaderVersion::Ok: break;
        case savefmt::HeaderVersion::Damaged:
            if (why) *why = shown.empty() ? "has no format version on its first line (damaged)"
                                          : "has an unknown format version '" + shown + "' (damaged)";
            return ReadState::Damaged;
    }
    const bool header = version > 0;
    std::size_t bodyEnd = text.size();
    const std::string tail = kTrailerTail;
    const std::size_t at = text.rfind(tail);
    if (at != std::string::npos) {
        std::string hex = text.substr(at + tail.size());
        while (!hex.empty() && hex.back() == '\n') hex.pop_back();
        if (hex.size() != 16) { if (why) *why = "is cut short (bad checksum line)"; return ReadState::Damaged; }
        const std::size_t sumLen = at + std::strlen("#end\n");
        if (savefmt::Hex16(savefmt::Fnv1a(text.data(), sumLen)) != hex) {
            if (why) *why = std::string("doesn't match its checksum (damaged or edited)") + kHandEditHint;
            return ReadState::Damaged;
        }
        bodyEnd = at;
    } else if (version == kPrefsFormat) {
        if (why) *why = "is cut short (no end line)";
        return ReadState::Damaged;
    }
    if (version > kPrefsFormat) {
        if (why) *why = "was saved by a newer version of the game (prefs format " + shown + ", this build reads up to " +
                        std::to_string(kPrefsFormat) + ")";
        return ReadState::Newer;
    }
    std::size_t pos = header ? (nl == std::string::npos ? bodyEnd : nl + 1) : 0;
    bool sawEnd = at != std::string::npos, firstKeyLine = true;
    while (pos < bodyEnd) {
        std::size_t e = text.find('\n', pos);
        if (e == std::string::npos || e > bodyEnd) e = bodyEnd;
        const std::string line = text.substr(pos, e - pos);
        pos = e + 1;
        if (line.empty()) continue;
        if (line == kEndLine) { sawEnd = true; continue; }
        const auto tab = line.find('\t');
        if (!header && firstKeyLine && tab == std::string::npos) {
            // The oldest layout is nothing but key lines. A first line that isn't one (a damaged
            // `okayprefs` line, zeros after a power cut, any other file) means this is not that layout.
            if (why) *why = "isn't a prefs file (no okayprefs header, and its first line is not a key)";
            return ReadState::Damaged;
        }
        firstKeyLine = false;
        if (tab == std::string::npos) continue;
        fresh[Decode(line.substr(0, tab))] = Decode(line.substr(tab + 1));
    }
    if (!header && sawEnd) {
        if (why) *why = "isn't a prefs file (an end line but no okayprefs header: its first line is damaged)";
        return ReadState::Damaged;
    }
    if (version == 2 && !sawEnd) { if (why) *why = "is cut short (no end line)"; return ReadState::Damaged; }
    return ReadState::Ok;
}

ReadState ReadPrefsFile(const std::string& path, std::unordered_map<std::string, std::string>& fresh, std::string* why) {
    std::error_code ec;
    if (!std::filesystem::exists(path, ec)) { fresh.clear(); return ReadState::Missing; }
    std::string text, readWhy;
    if (!savefmt::ReadFileCapped(path, kMaxPrefsBytes, text, &readWhy)) {
        fresh.clear();
        if (why) *why = "can't be read (" + readWhy + ")";
        return ReadState::Damaged;
    }
    return ParsePrefsText(text, fresh, why);
}
} // namespace

bool Prefs::Save(const std::string& path) {
    // Written aside and swapped in (a crash never leaves a half file). The end line lets
    // Load tell a cut-off file from a complete one; older readers skip it (no tab).
    std::vector<std::pair<std::string, std::string>> rows;
    {
        corethreads::LockGuard g(Lock());
        rows.assign(Store().begin(), Store().end());
    }
    std::sort(rows.begin(), rows.end());
    std::string out = "okayprefs " + std::to_string(kPrefsFormat) + "\n";
    for (const auto& [k, v] : rows)
        out += Encode(k) + '\t' + Encode(v) + '\n';
    out += kEndLine;
    out += '\n';
    out += "#sum " + savefmt::Hex16(savefmt::Fnv1a(out.data(), out.size())) + "\n";
    // The file being replaced: a good one becomes the backup, a damaged one is set aside, and one from
    // a newer build is first kept as <path>.newer.bak (going back to that build finds it).
    int call = 0;   // KeepBackup looks at the file on disk first, then (when that one is bad) at the .bak
    savefmt::KeepBackup(path, [&](const std::string& bytes) {
        std::unordered_map<std::string, std::string> parsed;
        const ReadState st = ParsePrefsText(bytes, parsed, nullptr);
        if (st == ReadState::Newer && call == 0) {
            const std::string newer = path + ".newer.bak";
            std::string kept;
            if (!(savefmt::ReadFileCapped(newer, kMaxPrefsBytes, kept, nullptr) && kept == bytes)) savefmt::AtomicWrite(newer, bytes);
        }
        ++call;
        return st == ReadState::Ok;
    }, kMaxPrefsBytes);
    savefmt::WriteOptions opt;
    opt.sync = true;
    std::string err;
    const bool ok = savefmt::AtomicWriteChecked(path, out, &err, opt);
    corethreads::LockGuard g(Lock());
    ErrorText() = ok ? std::string() : err;
    return ok;
}

bool Prefs::Load(const std::string& path) {
    // Build the new store aside; the live one is replaced only by a file that read whole.
    std::unordered_map<std::string, std::string> fresh;
    std::string why;
    ReadState st = ReadPrefsFile(path, fresh, &why);
    bool usedBackup = false;
    std::string error;
    if (st == ReadState::Newer) {
        // Never parsed, and its .bak isn't read instead: that is most likely the same newer build's
        // file, and an older backup would quietly undo what was set in the newer build. The live keys
        // stay; Save keeps the file as <path>.newer.bak before replacing it.
        error = path + " " + why;
        Log::Warning("Prefs file ", error, "; it was not loaded and the current settings were kept");
        corethreads::LockGuard g(Lock());
        ErrorText() = error;
        g_usedBackup = false;
        return false;
    }
    if (st == ReadState::Damaged) {
        std::unordered_map<std::string, std::string> back;
        if (ReadPrefsFile(path + ".bak", back, nullptr) == ReadState::Ok) {
            fresh = std::move(back);
            st = ReadState::Ok;
            usedBackup = true;
            error = path + " was damaged (" + why + "); loaded the backup of the previous save instead";
            Log::Warning("Prefs file ", path, " was damaged and its backup was loaded instead: it ", why);
        } else {
            error = path + " " + why;
            Log::Warning("Prefs file ", error, "; no good backup, so the current settings were kept");
        }
    }
    if (st == ReadState::Damaged) {   // keep what we have
        corethreads::LockGuard g(Lock());
        ErrorText() = error;
        g_usedBackup = false;
        return false;
    }
    bool changed = true;
    {
        corethreads::LockGuard g(Lock());
        ErrorText() = error;
        g_usedBackup = usedBackup;
        if (st == ReadState::Missing) {   // no file = an empty store
            changed = !Store().empty();
            Store().clear();
        } else {
            Store() = std::move(fresh);
        }
    }
    if (st == ReadState::Missing) {
        if (changed) Notify("");
        return false;
    }
    Notify("");
    return true;
}

std::string Prefs::LastError() {
    corethreads::LockGuard g(Lock());
    return ErrorText();
}
bool Prefs::LastLoadUsedBackup() {
    corethreads::LockGuard g(Lock());
    return g_usedBackup;
}

} // namespace okay
