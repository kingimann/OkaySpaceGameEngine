#pragma once
// Keeps a test's file output out of the source tree.
//
// Some code under test writes files named by what it is given, relative to the current folder or to the
// save folder (Prefs saves, creations, save slots, script file builtins). A test that runs those with
// a stray name ("x") and is started from the repository would leave the file in the checkout.
// A Sandbox makes a private folder under the system temp folder, points the save folder (Save::BaseDir)
// at it and makes it the current folder, so every relative write lands there; the destructor goes back
// and deletes it. It also refuses to run under the source tree, and Verify() compares the source tree's
// top-level files and its `saves` / `creations` folders with how they were when the Sandbox was made, so a test
// that still writes there fails by name instead of silently dirtying the checkout.
//
//   okaytest::Sandbox sandbox("vs_script_coverage", OKAY_SOURCE_DIR);   // first thing in main()
//   ...
//   CHECK(sandbox.Verify());                                            // last thing before TEST_MAIN_RESULT()
#include "okay/Core/SaveData.hpp"

#include <chrono>
#include <cstdlib>
#include <cstdio>
#include <filesystem>
#include <map>
#include <string>
#include <system_error>

namespace okaytest {

class Sandbox {
public:
    /// `sourceDir`: the source tree to guard ("" = nothing to guard).
    explicit Sandbox(const std::string& name, const std::string& sourceDir = std::string()) : m_source(sourceDir) {
        namespace fs = std::filesystem;
        std::error_code ec;
        m_oldCwd = fs::current_path(ec);
        m_oldBase = okay::Save::BaseDir();
        const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
        m_dir = fs::temp_directory_path(ec) / ("okay_sandbox_" + name + "_" + std::to_string((long long)stamp));
        fs::create_directories(m_dir, ec);
        m_dir = fs::weakly_canonical(m_dir, ec);
        if (!m_source.empty()) {
            const fs::path src = fs::weakly_canonical(fs::path(m_source), ec);
            const std::string d = m_dir.generic_string(), s = src.generic_string();
            if (!s.empty() && d.compare(0, s.size(), s) == 0) {
                std::fprintf(stderr, "Sandbox: the temp folder %s is inside the source tree %s\n", d.c_str(), s.c_str());
                std::abort();
            }
            m_before = Snapshot();
        }
        fs::current_path(m_dir, ec);
        okay::Save::SetBaseDir(m_dir.generic_string());
    }
    ~Sandbox() {
        namespace fs = std::filesystem;
        std::error_code ec;
        okay::Save::SetBaseDir(m_oldBase);
        fs::current_path(m_oldCwd, ec);
        fs::remove_all(m_dir, ec);
    }
    Sandbox(const Sandbox&) = delete;
    Sandbox& operator=(const Sandbox&) = delete;

    const std::filesystem::path& Dir() const { return m_dir; }

    /// True when nothing in the guarded source tree was added, removed or rewritten since construction.
    /// Prints what changed.
    bool Verify() const {
        if (m_source.empty()) return true;
        const auto now = Snapshot();
        bool same = true;
        for (const auto& kv : now) {
            auto it = m_before.find(kv.first);
            if (it == m_before.end()) { std::fprintf(stderr, "  wrote into the source tree: new file %s\n", kv.first.c_str()); same = false; }
            else if (it->second != kv.second) { std::fprintf(stderr, "  wrote into the source tree: changed %s\n", kv.first.c_str()); same = false; }
        }
        for (const auto& kv : m_before)
            if (!now.count(kv.first)) { std::fprintf(stderr, "  wrote into the source tree: removed %s\n", kv.first.c_str()); same = false; }
        return same;
    }

private:
    struct Stamp {
        std::uintmax_t size = 0; long long time = 0;
        bool operator!=(const Stamp& o) const { return size != o.size || time != o.time; }
    };
    // The files a stray relative write lands in: the top of the tree, and the save / creation folders.
    std::map<std::string, Stamp> Snapshot() const {
        namespace fs = std::filesystem;
        std::map<std::string, Stamp> out;
        std::error_code ec;
        auto add = [&](const fs::directory_entry& e) {
            std::error_code e2;
            if (!e.is_regular_file(e2)) return;
            Stamp s; s.size = e.file_size(e2);
            s.time = (long long)e.last_write_time(e2).time_since_epoch().count();
            out[e.path().generic_string()] = s;
        };
        const fs::path root(m_source);
        for (fs::directory_iterator it(root, ec), end; !ec && it != end; it.increment(ec)) add(*it);
        for (const char* sub : {"saves", "creations"}) {
            ec.clear();
            for (fs::recursive_directory_iterator it(root / sub, ec), end; !ec && it != end; it.increment(ec)) add(*it);
        }
        return out;
    }

    std::string m_source;
    std::filesystem::path m_dir, m_oldCwd;
    std::string m_oldBase;
    std::map<std::string, Stamp> m_before;
};

} // namespace okaytest
