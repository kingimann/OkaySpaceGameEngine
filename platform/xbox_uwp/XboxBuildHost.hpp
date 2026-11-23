#pragma once
// The editor's side of the one-click Xbox build: the Internal steps (sdk, export, collect, pack) done with std::filesystem and our own
// packer, and process spawning handed to a function (XboxProcess.hpp on Windows). Pure C++17, runs on Linux in the tests.
// Written from the C++ standard and this folder's XboxAppx.hpp. The Windows process glue it is wired to is UNVERIFIED.
#include "XboxAppx.hpp"
#include "XboxBuildPlan.hpp"
#include "okay/Core/FileCopy.hpp"   // CopyFileOver: copy over an existing file (MinGW copy_file overwrite_existing fails on Windows)
#include <filesystem>
#include <fstream>

namespace okay::xbox {

struct BuildHostConfig {
    bool sdkAvailable = false;
    bool exportOk = false;
    std::string exportMessage;
    std::vector<std::string> exportWarnings;
    std::string exportDir, appxPath, configuration = "Release";
    std::function<std::unique_ptr<ProcessHandle>(const Command&, std::string&)> spawn;
};

class XboxBuildHost : public BuildHost {
public:
    explicit XboxBuildHost(BuildHostConfig c) : c_(std::move(c)) {}
    bool RunInternal(const Step& s, std::vector<std::string>& log, std::string& err) override {
        namespace fs = std::filesystem;
        const fs::path pkg = fs::u8path(c_.exportDir) / "package";
        std::error_code ec;
        if (s.id == "sdk") { if (!c_.sdkAvailable) { err = "A required Windows tool is missing: see the list on the Xbox page."; return false; } log.push_back("Visual Studio, UWP tools and Windows SDK found."); return true; }
        if (s.id == "export") {
            if (!c_.exportOk) { err = c_.exportMessage.empty() ? "export failed" : c_.exportMessage; return false; }
            log.push_back(c_.exportMessage);
            for (const std::string& w : c_.exportWarnings) log.push_back("Warning: " + w);
            return true;
        }
        if (s.id == "collect") {
            const fs::path root = fs::u8path(c_.exportDir) / "build" / "app";
            fs::path exe; std::vector<fs::path> cso;
            for (fs::recursive_directory_iterator it(root, fs::directory_options::skip_permission_denied, ec), end; !ec && it != end; it.increment(ec)) {
                if (!it->is_regular_file(ec)) continue;
                const std::string n = it->path().filename().string();
                if (n == "OkaySpaceXbox.exe" && (exe.empty() || it->path().u8string().find(c_.configuration) != std::string::npos)) exe = it->path();
                else if (n == "tri_vs.cso" || n == "tri_ps.cso") cso.push_back(it->path());
            }
            if (exe.empty()) { err = "The build finished but OkaySpaceXbox.exe was not found under " + root.u8string(); return false; }
            okay::CopyFileOver(exe, pkg / "OkaySpaceXbox.exe", ec);
            if (ec) { err = "Could not copy the program into the package: " + ec.message(); return false; }
            log.push_back("Copied " + exe.filename().string());
            for (const fs::path& p : cso) { okay::CopyFileOver(p, pkg / p.filename(), ec); log.push_back("Copied " + p.filename().string()); }
            return true;
        }
        if (s.id == "pack") {
            // the helper files the shader step leaves are not part of the app
            fs::remove(pkg / "Shaders" / "okay3d.hlsl", ec); fs::remove(pkg / "Shaders" / "entries.txt", ec);
            if (!fs::exists(pkg / "OkaySpaceXbox.exe", ec)) { err = "package/OkaySpaceXbox.exe is missing (the program was not built)."; return false; }
            appx::PackResult r = appx::PackFolder(pkg.u8string());
            if (!r.ok) { err = "okay-appx: " + r.error; return false; }
            appx::VerifyResult v = appx::Verify(r.bytes);
            if (!v.ok) { for (auto& p : v.problems) log.push_back("PROBLEM: " + p); err = "Our own check of the package failed: " + (v.problems.empty() ? std::string("?") : v.problems[0]); return false; }
            std::ofstream o(fs::u8path(c_.appxPath), std::ios::binary | std::ios::trunc);
            o.write(r.bytes.data(), (std::streamsize)r.bytes.size());
            if (!o) { err = "Could not write " + c_.appxPath; return false; }
            log.push_back("okay-appx: wrote UNSIGNED package (" + std::to_string(r.files) + " files, " + std::to_string((unsigned long long)r.blocks) + " blocks) and our checker accepted it.");
            return true;
        }
        err = "unknown step " + s.id; return false;
    }
    std::unique_ptr<ProcessHandle> Spawn(const Command& c, std::string& err) override {
        if (!c_.spawn) { err = "Starting programs is only possible on Windows."; return nullptr; }
        return c_.spawn(c, err);
    }
private:
    BuildHostConfig c_;
};

} // namespace okay::xbox
