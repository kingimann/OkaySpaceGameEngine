#include "okay/Core/FileWatcher.hpp"
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <map>

#if defined(__linux__)
#include <cerrno>
#include <sys/inotify.h>
#include <unistd.h>
#elif defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

namespace okay {
namespace fs = std::filesystem;

namespace {

class PollingWatcher final : public FileWatcher {
public:
    bool Start(const std::string&, std::string*) override { return true; }
    void Stop() override {}
    bool Active() const override { return false; }
    const char* Name() const override { return "polling"; }
    WatchState Take(std::vector<std::string>*) override { return WatchState::Unknown; }
};

constexpr std::size_t kMaxPaths = 256;   // paths kept per Take(); the flag is what matters

#if defined(__linux__)

class InotifyWatcher final : public FileWatcher {
public:
    ~InotifyWatcher() override { Stop(); }

    bool Start(const std::string& root, std::string* error) override {
        Stop();
        std::error_code ec;
        fs::path r = fs::absolute(fs::u8path(root), ec);
        if (ec || !fs::is_directory(r, ec)) { if (error) *error = "not a folder: " + root; return false; }
        m_fd = ::inotify_init1(IN_NONBLOCK | IN_CLOEXEC);
        if (m_fd < 0) { if (error) *error = std::string("inotify_init1: ") + std::strerror(errno); return false; }
        if (!AddTree(r, error)) { Stop(); return false; }
        m_rootWd = RootWatch();
        return true;
    }

    void Stop() override {
        if (m_fd >= 0) ::close(m_fd);   // closing the descriptor drops every watch
        m_fd = -1;
        m_dirs.clear();
        m_root.clear();
        m_overflow = false;
        m_broken = false;
        m_rootWd = -1;
    }

    bool Active() const override { return m_fd >= 0; }
    const char* Name() const override { return "inotify"; }

    WatchState Take(std::vector<std::string>* paths) override {
        if (m_fd < 0) return WatchState::Unknown;
        if (m_broken) {
            // The watched folder was deleted or moved away. We cannot tell what changes: answer Unknown (the
            // caller scans on its own timer) until the folder is back, then watch it afresh and say Changed once.
            std::error_code ec;
            if (!fs::is_directory(fs::u8path(m_root), ec) || !Rearm()) return WatchState::Unknown;
            return WatchState::Changed;
        }
        bool changed = m_overflow;
        alignas(inotify_event) char buf[16384];
        for (;;) {
            const ssize_t n = ::read(m_fd, buf, sizeof buf);
            if (n <= 0) break;   // EAGAIN: drained
            for (char* p = buf; p < buf + n;) {
                const auto* ev = reinterpret_cast<const inotify_event*>(p);
                p += sizeof(inotify_event) + ev->len;
                if (ev->mask & IN_Q_OVERFLOW) { m_overflow = true; changed = true; continue; }
                auto it = m_dirs.find(ev->wd);
                if (ev->wd == m_rootWd && (ev->mask & (IN_DELETE_SELF | IN_MOVE_SELF | IN_IGNORED))) {
                    m_broken = true;   // the root itself is gone
                    changed = true;
                    continue;
                }
                if (ev->mask & IN_IGNORED) { if (it != m_dirs.end()) m_dirs.erase(it); changed = true; continue; }
                if (it == m_dirs.end()) continue;
                changed = true;
                const std::string full = ev->len ? it->second + "/" + ev->name : it->second;
                if (paths && paths->size() < kMaxPaths) paths->push_back(full);
                // A new folder (made or moved in): watch it too. Files put into it before the watch
                // existed are not reported, but the caller rescans on Changed and finds them.
                if ((ev->mask & IN_ISDIR) && (ev->mask & (IN_CREATE | IN_MOVED_TO))) {
                    AddTree(fs::u8path(full), nullptr);
                }
            }
        }
        if (m_overflow) {
            // Events were lost: re-walk so new folders are watched, and report a change.
            m_overflow = false;
            if (!m_root.empty()) AddTree(fs::u8path(m_root), nullptr);
            changed = true;
        }
        return changed ? WatchState::Changed : WatchState::Quiet;
    }

private:
    int RootWatch() const {
        for (const auto& kv : m_dirs) if (kv.second == m_root) return kv.first;
        return -1;
    }

    // Start over on the same root with a fresh descriptor (old watches die with the old one).
    bool Rearm() {
        const std::string root = m_root;
        if (m_fd >= 0) ::close(m_fd);
        m_dirs.clear();
        m_overflow = false;
        m_fd = ::inotify_init1(IN_NONBLOCK | IN_CLOEXEC);
        if (m_fd < 0) return false;
        if (!AddTree(fs::u8path(root), nullptr)) return false;   // m_root stays set, so it is kept
        m_rootWd = RootWatch();
        m_broken = false;
        return true;
    }

    // Watch `dir` and every folder below it. A failure on the first folder is reported (no
    // room for watches: the caller falls back to polling); a failure deeper is skipped.
    bool AddTree(const fs::path& dir, std::string* error) {
        if (m_root.empty()) m_root = dir.generic_u8string();   // the first call is the root
        std::vector<fs::path> stack{dir};
        bool first = true;
        while (!stack.empty()) {
            fs::path d = std::move(stack.back());
            stack.pop_back();
            const std::string ds = d.generic_u8string();
            const int wd = ::inotify_add_watch(m_fd, d.c_str(),
                IN_CREATE | IN_DELETE | IN_MODIFY | IN_CLOSE_WRITE | IN_MOVED_FROM | IN_MOVED_TO |
                IN_DELETE_SELF | IN_MOVE_SELF | IN_ATTRIB | IN_ONLYDIR);
            if (wd < 0) {
                if (first) {
                    if (error) *error = std::string("inotify_add_watch: ") + std::strerror(errno);
                    return false;
                }
                // Too many watches deeper down would silently miss changes: say so by failing the first
                // call only; later folders just go unwatched and the safety rescan covers them.
            } else {
                m_dirs[wd] = ds;
            }
            first = false;
            std::error_code ec;
            for (fs::directory_iterator it(d, fs::directory_options::skip_permission_denied, ec), end; !ec && it != end; it.increment(ec)) {
                std::error_code e2;
                if (it->is_directory(e2) && !it->is_symlink(e2)) stack.push_back(it->path());
            }
        }
        return true;
    }

    int m_fd = -1;
    bool m_overflow = false;
    bool m_broken = false;     // root deleted/moved: answer Unknown until it returns
    int m_rootWd = -1;
    std::string m_root;
    std::map<int, std::string> m_dirs;   // watch descriptor -> folder
};

std::unique_ptr<FileWatcher> MakeNative() { return std::unique_ptr<FileWatcher>(new InotifyWatcher); }

#elif defined(_WIN32)

class WinWatcher final : public FileWatcher {
public:
    ~WinWatcher() override { Stop(); }

    bool Start(const std::string& root, std::string* error) override {
        Stop();
        std::error_code ec;
        fs::path r = fs::absolute(fs::u8path(root), ec);
        if (ec || !fs::is_directory(r, ec)) { if (error) *error = "not a folder: " + root; return false; }
        m_root = r.generic_u8string();
        m_dir = ::CreateFileW(r.c_str(), FILE_LIST_DIRECTORY,
                              FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
                              FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OVERLAPPED, nullptr);
        if (m_dir == INVALID_HANDLE_VALUE) { if (error) *error = "CreateFileW failed"; return false; }
        m_event = ::CreateEventW(nullptr, TRUE, FALSE, nullptr);
        if (!m_event) { Stop(); if (error) *error = "CreateEventW failed"; return false; }
        m_buf.assign(64 * 1024, 0);
        if (!Issue()) { Stop(); if (error) *error = "ReadDirectoryChangesW failed"; return false; }
        m_broken = false;
        return true;
    }

    void Stop() override {
        if (m_dir != INVALID_HANDLE_VALUE) {
            if (m_pending) { ::CancelIoEx(m_dir, &m_ov); DWORD n = 0; ::GetOverlappedResult(m_dir, &m_ov, &n, TRUE); }
            ::CloseHandle(m_dir);
        }
        if (m_event) ::CloseHandle(m_event);
        m_dir = INVALID_HANDLE_VALUE;
        m_event = nullptr;
        m_pending = false;
        m_broken = false;
    }

    bool Active() const override { return m_dir != INVALID_HANDLE_VALUE; }
    const char* Name() const override { return "ReadDirectoryChangesW"; }

    WatchState Take(std::vector<std::string>* paths) override {
        if (m_dir == INVALID_HANDLE_VALUE && !m_broken) return WatchState::Unknown;
        if (m_broken) {
            // A re-arm failed (folder deleted, share gone, ...): we cannot tell what changes, so answer Unknown
            // (the caller scans on its own timer) and retry from scratch each time until it works again.
            const std::string root = m_root;
            std::string err;
            if (!Start(root, &err)) { m_broken = true; return WatchState::Unknown; }   // Start() cleared m_broken
            return WatchState::Changed;
        }
        bool changed = false;
        while (m_pending) {
            DWORD n = 0;
            if (!::GetOverlappedResult(m_dir, &m_ov, &n, FALSE)) {
                if (::GetLastError() == ERROR_IO_INCOMPLETE) break;   // nothing yet
                m_pending = false;
                changed = true;                                       // the call failed: rescan to be safe
                if (!Issue()) m_broken = true;
                break;
            }
            m_pending = false;
            changed = true;
            if (n == 0) {   // buffer overflowed: events were lost, the caller rescans
                if (!Issue()) m_broken = true;
                break;
            }
            const char* p = m_buf.data();
            for (;;) {
                const auto* fi = reinterpret_cast<const FILE_NOTIFY_INFORMATION*>(p);
                if (paths && paths->size() < kMaxPaths) {
                    fs::path rel(std::wstring(fi->FileName, fi->FileNameLength / sizeof(wchar_t)));
                    paths->push_back(m_root + "/" + rel.generic_u8string());
                }
                if (!fi->NextEntryOffset) break;
                p += fi->NextEntryOffset;
            }
            if (!Issue()) { m_broken = true; break; }
        }
        return changed ? WatchState::Changed : WatchState::Quiet;
    }

private:
    bool Issue() {
        std::memset(&m_ov, 0, sizeof m_ov);
        m_ov.hEvent = m_event;
        ::ResetEvent(m_event);
        const DWORD filter = FILE_NOTIFY_CHANGE_FILE_NAME | FILE_NOTIFY_CHANGE_DIR_NAME | FILE_NOTIFY_CHANGE_SIZE |
                             FILE_NOTIFY_CHANGE_LAST_WRITE | FILE_NOTIFY_CHANGE_CREATION;
        DWORD ignored = 0;
        m_pending = ::ReadDirectoryChangesW(m_dir, m_buf.data(), static_cast<DWORD>(m_buf.size()), TRUE, filter,
                                            &ignored, &m_ov, nullptr) != 0;
        return m_pending;
    }

    HANDLE m_dir = INVALID_HANDLE_VALUE;
    HANDLE m_event = nullptr;
    OVERLAPPED m_ov{};
    bool m_pending = false;
    bool m_broken = false;     // re-arm failed: answer Unknown and retry
    std::string m_root;
    std::vector<char> m_buf;   // DWORD aligned: vector storage comes from operator new
};

std::unique_ptr<FileWatcher> MakeNative() { return std::unique_ptr<FileWatcher>(new WinWatcher); }

#else

std::unique_ptr<FileWatcher> MakeNative() { return nullptr; }

#endif

} // namespace

std::unique_ptr<FileWatcher> CreateNativeFileWatcher() { return MakeNative(); }
std::unique_ptr<FileWatcher> CreatePollingFileWatcher() { return std::unique_ptr<FileWatcher>(new PollingWatcher); }

std::unique_ptr<FileWatcher> MakeFileWatcher(const std::string& root, std::string* fallbackReason) {
    if (auto w = CreateNativeFileWatcher()) {
        std::string err;
        if (w->Start(root, &err)) return w;
        if (fallbackReason) *fallbackReason = err;
    } else if (fallbackReason) {
        *fallbackReason = "no native watcher on this platform";
    }
    auto p = CreatePollingFileWatcher();
    p->Start(root, nullptr);
    return p;
}

} // namespace okay
