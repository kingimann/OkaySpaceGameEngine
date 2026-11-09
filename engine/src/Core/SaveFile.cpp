// The file half of the save systems: the one crash-safe file write every save writer
// shares (savefmt::AtomicWriteChecked), and SaveFile's load and save with an end marker, a
// checksum and a backup. See SaveData.hpp.
#include "okay/Core/SaveData.hpp"
#include "okay/Core/Log.hpp"

#include <array>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <functional>
#include <mutex>
#include <system_error>

#if defined(_WIN32)
#  include <io.h>
#else
#  include <fcntl.h>
#  include <unistd.h>
#endif

namespace okay {
namespace savefmt {

namespace fs = std::filesystem;

namespace {

// Writers to the same path share the `.tmp` name, so they take turns. The locks are
// striped by path: unrelated files don't wait for each other's disk flush.
std::mutex& StripeFor(const std::string& path) {
    static std::array<std::mutex, 16> stripes;
    return stripes[std::hash<std::string>{}(path) % stripes.size()];
}

std::string Reason(int err) {
    if (err == ENOSPC) return "the disk is full";
#ifdef EDQUOT
    if (err == EDQUOT) return "the disk quota is used up";
#endif
    if (err == EACCES || err == EPERM || err == EROFS) return "permission denied (read-only folder?)";
    if (err == ENOENT) return "no such folder";
    if (err == EISDIR) return "a folder is in the way";
    if (err == 0) return "unknown error";
    return std::generic_category().message(err);
}

std::FILE* OpenForWrite(const fs::path& p) {
#if defined(_WIN32)
    return _wfopen(p.c_str(), L"wb");
#else
    return std::fopen(p.c_str(), "wb");
#endif
}

bool SyncFile(std::FILE* f) {
#if defined(_WIN32)
    return _commit(_fileno(f)) == 0;
#else
    return ::fsync(::fileno(f)) == 0;
#endif
}

// After the rename: make the new directory entry durable too (POSIX; Windows has no call for it).
void SyncDirectory(const fs::path& dir) {
#if !defined(_WIN32)
    const fs::path d = dir.empty() ? fs::path(".") : dir;
    const int fd = ::open(d.c_str(), O_RDONLY);
    if (fd >= 0) { ::fsync(fd); ::close(fd); }
#else
    (void)dir;
#endif
}

} // namespace

bool AtomicWriteChecked(const std::string& path, const std::string& content, std::string* error, const WriteOptions& opt) {
    auto fail = [&](const std::string& msg) { if (error) *error = msg; return false; };
    if (error) error->clear();
    if (path.empty()) return fail("no file name");
    const fs::path target = opt.utf8Path ? fs::u8path(path) : fs::path(path);
    fs::path tmp = target;
    tmp += ".tmp";
    std::lock_guard<std::mutex> lock(StripeFor(path));
    std::error_code ec;
    const fs::path parent = target.parent_path();
    if (opt.createDirs && !parent.empty()) {
        fs::create_directories(parent, ec);
        if (ec && !fs::is_directory(parent, ec)) return fail("can't create the folder " + parent.string() + ": " + Reason(ec.value()));
    }
    std::FILE* f = OpenForWrite(tmp);
    if (!f) return fail("can't create " + tmp.string() + ": " + Reason(errno));
    errno = 0;
    bool ok = content.empty() || std::fwrite(content.data(), 1, content.size(), f) == content.size();
    int err = errno;
    if (ok && std::fflush(f) != 0) { ok = false; err = errno; }
    if (ok && opt.sync && !SyncFile(f)) { ok = false; err = errno; }
    if (std::fclose(f) != 0 && ok) { ok = false; err = errno; }
    if (!ok) {
        fs::remove(tmp, ec);
        return fail("can't write " + target.string() + ": " + Reason(err));
    }
    fs::rename(tmp, target, ec);
    if (ec) {
        // Some platforms won't rename over an existing file. Only a plain file is replaced, and
        // the old one is put aside first so a failure puts it back (never delete, then hope).
        std::error_code e1;
        if (fs::is_regular_file(target, e1)) {
            fs::path aside = target;
            aside += ".swap";
            fs::remove(aside, e1);
            fs::rename(target, aside, e1);
            if (!e1) {
                std::error_code e2;
                fs::rename(tmp, target, e2);
                if (e2) {
                    fs::rename(aside, target, e1);
                    ec = e2;
                } else {
                    fs::remove(aside, e1);
                    ec.clear();
                }
            }
        }
        if (ec) {
            std::error_code ignore;
            fs::remove(tmp, ignore);
            return fail("can't replace " + target.string() + ": " + Reason(ec.value()));
        }
    }
    if (opt.sync) SyncDirectory(parent);
    return true;
}

bool IsReservedDeviceName(const std::string& name) {
    std::string base = name.substr(0, name.find('.'));
    while (!base.empty() && base.back() == ' ') base.pop_back();
    for (char& c : base) c = (char)std::tolower((unsigned char)c);
    if (base == "con" || base == "prn" || base == "aux" || base == "nul") return true;
    if (base.size() == 4 && (base.compare(0, 3, "com") == 0 || base.compare(0, 3, "lpt") == 0) && base[3] >= '1' && base[3] <= '9') return true;
    return false;
}

void KeepBackup(const std::string& path, const std::function<bool(const std::string&)>& isGood, std::size_t maxBytes) {
    std::error_code ec;
    if (!fs::is_regular_file(path, ec)) return;
    std::string old, why;
    if (!ReadFileCapped(path, maxBytes, old, &why)) return;
    if (isGood(old)) { AtomicWrite(path + ".bak", old); return; }
    std::string bak;
    if (ReadFileCapped(path + ".bak", maxBytes, bak, nullptr) && isGood(bak)) return;
    fs::remove(path + ".corrupt", ec);
    fs::rename(path, path + ".corrupt", ec);
}

void KeepBackupTrusted(const std::string& path) {
    std::error_code ec;
    if (!fs::is_regular_file(path, ec)) return;
    const std::string tmp = path + ".bak.tmp";
    fs::copy_file(path, tmp, fs::copy_options::overwrite_existing, ec);
    if (ec) { fs::remove(tmp, ec); return; }
    fs::rename(tmp, path + ".bak", ec);
    if (ec) fs::remove(tmp, ec);
}

} // namespace savefmt

// =====================================================================================
// SaveFile
// =====================================================================================
namespace {

namespace fs = std::filesystem;

const char kTrailerTail[] = "#end\n#sum ";

enum class ReadState { Ok, Missing, Damaged };

// Split a file's text into the keys it holds. `why` says what is wrong with a damaged one.
ReadState ParseSaveText(const std::string& raw, std::unordered_map<std::string, std::string>& out, std::string* why) {
    out.clear();
    // A raw carriage return can only come from a tool that changed the line ends (the writer
    // escapes the ones in values), so it is dropped before the checksum and the parse.
    std::string text = raw;
    if (text.find('\r') != std::string::npos) text.erase(std::remove(text.begin(), text.end(), '\r'), text.end());
    const std::size_t nl = text.find('\n');
    const std::string first = text.substr(0, nl);
    const bool header = first.rfind("okaysave", 0) == 0;
    int version = 0;   // 0: no header (the oldest files)
    if (header) {
        version = std::atoi(first.c_str() + 8);
        if (version < 1) version = 1;
    }
    std::size_t bodyEnd = text.size();
    if (version == SaveFile::kFormatVersion) {
        const std::string tail = kTrailerTail;
        const std::size_t at = text.rfind(tail);
        if (at == std::string::npos) { if (why) *why = "is cut short (no end marker)"; return ReadState::Damaged; }
        std::string hex = text.substr(at + tail.size());
        while (!hex.empty() && hex.back() == '\n') hex.pop_back();
        if (hex.size() != 16) { if (why) *why = "is cut short (bad checksum line)"; return ReadState::Damaged; }
        const std::size_t sumLen = at + std::strlen("#end\n");
        if (savefmt::Hex16(savefmt::Fnv1a(text.data(), sumLen)) != hex) {
            if (why) *why = "doesn't match its checksum (damaged or edited)";
            return ReadState::Damaged;
        }
        bodyEnd = at;
    }
    const bool escaped = version >= 2;
    auto addLine = [&](const std::string& line) {
        if (line.empty()) return;
        const std::size_t eq = escaped ? savefmt::FindSeparator(line) : line.find('=');
        if (eq == std::string::npos) return;
        if (escaped) out[savefmt::Unescape(line.substr(0, eq))] = savefmt::Unescape(line.substr(eq + 1));
        else out[line.substr(0, eq)] = line.substr(eq + 1);
    };
    std::size_t pos = header ? (nl == std::string::npos ? text.size() : nl + 1) : 0;
    while (pos < bodyEnd) {
        std::size_t e = text.find('\n', pos);
        if (e == std::string::npos || e > bodyEnd) e = bodyEnd;
        addLine(text.substr(pos, e - pos));
        pos = e + 1;
    }
    // A file with content but no header and not one key is not a save (a zero-filled
    // file after a power cut looks like this); an empty file is an empty store.
    if (!header && out.empty() && text.find_first_not_of(" \t\n") != std::string::npos) {
        if (why) *why = "isn't a save file";
        return ReadState::Damaged;
    }
    return ReadState::Ok;
}

// Read and parse one file. Missing = there is no such file; anything else wrong is Damaged.
ReadState ReadSaveFile(const std::string& path, std::unordered_map<std::string, std::string>& out, std::string* why, std::string* rawOut = nullptr) {
    std::error_code ec;
    if (!fs::exists(path, ec)) { out.clear(); return ReadState::Missing; }
    std::string text, readWhy;
    if (!savefmt::ReadFileCapped(path, SaveFile::kMaxFileBytes, text, &readWhy)) {
        out.clear();
        if (why) *why = "can't be read (" + readWhy + ")";
        return ReadState::Damaged;
    }
    if (rawOut) *rawOut = text;
    return ParseSaveText(text, out, why);
}

} // namespace

bool SaveFile::Load(const std::string& path) {
    m_error.clear();
    m_fromBackup = false;
    std::unordered_map<std::string, std::string> fresh;
    std::string why;
    ReadState st = ReadSaveFile(path, fresh, &why);
    if (st == ReadState::Ok) { m_data = std::move(fresh); return true; }
    if (st == ReadState::Missing) { m_data.clear(); return false; }
    std::unordered_map<std::string, std::string> back;
    std::string backWhy;
    if (ReadSaveFile(path + ".bak", back, &backWhy) == ReadState::Ok) {
        m_data = std::move(back);
        m_fromBackup = true;
        m_error = path + " was damaged (" + why + "); loaded the backup of the previous save instead";
        Log::Warning("Save file ", path, " was damaged; loaded its backup instead");
        return true;
    }
    m_data.clear();
    m_error = path + " " + why;
    Log::Warning("Save file ", m_error);
    return false;
}

bool SaveFile::Save(const std::string& path, std::string* error, bool durable, bool checkOld) const {
    // Keys in order: the same keys always write the same bytes (cloud sync compares them).
    std::vector<const std::pair<const std::string, std::string>*> rows;
    rows.reserve(m_data.size());
    for (const auto& kv : m_data) rows.push_back(&kv);
    std::sort(rows.begin(), rows.end(), [](const auto* a, const auto* b) { return a->first < b->first; });
    std::string out = "okaysave " + std::to_string(kFormatVersion) + "\n";
    for (const auto* kv : rows) out += savefmt::Escape(kv->first, true) + '=' + savefmt::Escape(kv->second) + '\n';
    out += "#end\n";
    out += "#sum " + savefmt::Hex16(savefmt::Fnv1a(out.data(), out.size())) + "\n";

    // The file about to be replaced: a good one becomes the backup, a damaged one is set aside.
    if (checkOld) {
        savefmt::KeepBackup(path, [](const std::string& bytes) {
            std::unordered_map<std::string, std::string> parsed;
            return ParseSaveText(bytes, parsed, nullptr) == ReadState::Ok;
        });
    } else {
        savefmt::KeepBackupTrusted(path);
    }
    savefmt::WriteOptions opt;
    opt.sync = durable;
    std::string werr;
    if (!savefmt::AtomicWriteChecked(path, out, &werr, opt)) {
        m_error = werr;
        if (error) *error = werr;
        return false;
    }
    m_error.clear();
    return true;
}

} // namespace okay
