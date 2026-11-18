#pragma once
// One-click Xbox build for the editor (pure logic, no Windows headers, no threads): the plan (steps and command lines), a step runner
// driven by polling, log line classification, secret redaction and plain-language error mapping. Tested on Linux with a fake host.
// The real process spawning is XboxProcess.hpp (UNVERIFIED on Windows); the steps themselves have never run on Windows.
// Written from Microsoft's documentation of the Windows command line (argument quoting), cmake's `--build` / `-G` / `-A` options,
// SignTool / New-SelfSignedCertificate / Add-AppxPackage, and the C++ standard. No code copied.
#include "XboxSdkCheck.hpp"
#include <algorithm>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace okay::xbox {

// ---- command lines ---------------------------------------------------------------------------------------------------------
/// Quote one argument for a Windows command line (the rules CommandLineToArgvW reads: backslashes before a quote are doubled).
inline std::string QuoteArg(const std::string& a) {
    if (!a.empty() && a.find_first_of(" \t\n\v\"") == std::string::npos) return a;
    std::string o = "\"";
    std::size_t bs = 0;
    for (char c : a) {
        if (c == '\\') { ++bs; continue; }
        if (c == '"') { o.append(bs * 2 + 1, '\\'); o += '"'; bs = 0; continue; }
        o.append(bs, '\\'); bs = 0; o += c;
    }
    o.append(bs * 2, '\\');   // trailing backslashes are doubled before the closing quote
    return o + "\"";
}

struct Command {
    std::string exe;
    std::vector<std::string> args;
    std::string workDir;
    bool elevated = false;   // starts a UAC prompt (the elevated work runs in a child; see the install step)
    std::string Line() const { std::string s = QuoteArg(exe); for (const std::string& a : args) s += " " + QuoteArg(a); return s; }
};

enum class StepKind { Internal, Process };
enum class Signer { PowerShellSignTool, OwnSigning };

struct Step {
    std::string id;        // sdk, export, configure, build, collect, shaders, pack, sign, install
    std::string title;     // shown in the editor
    StepKind kind = StepKind::Internal;
    Command cmd;           // Process steps
    std::string note;      // one plain line under the title
    bool optional = false; // a failure does not make the build fail (none today; the install step is only present when asked)
};

struct PlanInputs {
    SdkReport sdk;
    FsView fs;                    // to look for the Visual Studio cmake
    std::string sourceRoot;       // the OkaySpace source folder
    std::string exportDir;        // the folder the editor exports into (holds package/)
    std::string configuration = "Release";
    std::string subject;          // certificate subject = manifest Publisher
    Signer signer = Signer::PowerShellSignTool;
    bool signPackage = true;
    bool installOnThisPc = false;
    std::string appxPath;         // exportDir/<name>_<version>_x64.appx
};

inline std::string Join(const std::string& a, const std::string& b) { return a.empty() ? b : (a.back() == '/' || a.back() == '\\' ? a + b : a + "/" + b); }

inline std::string FindCmake(const SdkReport& sdk, const FsView& fs) {
    if (!sdk.visualStudioPath.empty()) {
        const std::string c = Join(sdk.visualStudioPath, "Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/cmake.exe");
        if (fs.list && fs.Exists(c)) return c;
    }
    return "cmake";   // Visual Studio's own cmake not found: the one on PATH (the step fails with a plain message if there is none)
}

inline const char* SignerLabel(Signer s) {
    return s == Signer::OwnSigning ? "our own signer (OKAY_OWN_SIGNING, not independently reviewed)" : "PowerShell New-SelfSignedCertificate + Microsoft SignTool";
}

inline std::vector<Step> PlanBuild(const PlanInputs& in) {
    std::vector<Step> v;
    auto internal = [&](const char* id, const char* title, const char* note) { Step s; s.id = id; s.title = title; s.note = note; v.push_back(s); };
    auto process = [&](const char* id, const char* title, const char* note, Command c) { Step s; s.id = id; s.title = title; s.note = note; s.kind = StepKind::Process; s.cmd = std::move(c); v.push_back(s); };
    const std::string appSrc = Join(in.sourceRoot, "platform/xbox_uwp/app");
    const std::string appBuild = Join(in.exportDir, "build/app");
    const std::string pkg = Join(in.exportDir, "package");
    const std::string ps = "powershell";
    auto psFile = [&](const std::string& script, std::vector<std::string> a) {
        Command c; c.exe = ps; c.args = {"-NoProfile", "-ExecutionPolicy", "Bypass", "-File", Join(in.sourceRoot, "scripts/" + script)};
        for (auto& x : a) c.args.push_back(x);
        return c;
    };
    internal("sdk", "Check the Windows tools", "Visual Studio 2022 with the UWP tools, a Windows SDK, SignTool.");
    internal("export", "Export the game data, manifest and tile images", "Writes package/Data, AppxManifest.xml and the images.");
    { Command c; c.exe = FindCmake(in.sdk, in.fs);
      c.args = {"-S", appSrc, "-B", appBuild, "-G", "Visual Studio 17 2022", "-A", "x64", "-DCMAKE_SYSTEM_NAME=WindowsStore", "-DCMAKE_SYSTEM_VERSION=10.0"};
      process("configure", "Set up the UWP app build", "cmake, with Visual Studio's UWP toolset.", c); }
    { Command c; c.exe = FindCmake(in.sdk, in.fs); c.args = {"--build", appBuild, "--config", in.configuration};
      process("build", "Compile the UWP app", "Microsoft's compiler builds OkaySpaceXbox.exe. This can take a few minutes.", c); }
    internal("collect", "Put the program into the package", "Copies OkaySpaceXbox.exe into package/.");
    process("shaders", "Compile the shaders (offline)", "fxc.exe turns the HLSL into .cso files (a UWP app cannot compile at run time).",
            psFile("xbox-compile-shaders.ps1", {"-SourceRoot", in.sourceRoot, "-OutDir", Join(pkg, "Shaders"), "-VsPath", in.sdk.visualStudioPath, "-WorkDir", Join(in.exportDir, "build/shaders")}));
    internal("pack", "Pack and check the .appx", "Our own packer, then our own checker. No MakeAppx needed.");
    if (in.signPackage) {
        if (in.signer == Signer::OwnSigning) internal("sign", "Sign the package (own signer)", "Our own signer, NOT independently reviewed.");
        else process("sign", "Sign the package (PowerShell + SignTool)", "Makes a test certificate and signs with Microsoft's SignTool.",
                     psFile("xbox-sign.ps1", {"-Appx", in.appxPath, "-Subject", in.subject, "-SignTool", in.sdk.signToolPath, "-OutDir", in.exportDir}));
    }
    if (in.installOnThisPc && in.signPackage) {
        Command c; c.exe = ps; c.elevated = true;
        const std::string script = Join(in.sourceRoot, "scripts/xbox-install-local.ps1");
        const std::string cer = Join(in.exportDir, "OkaySpaceXbox-test.cer");
        // One UAC prompt: the elevated child imports the certificate and runs Add-AppxPackage; it writes install-result.txt.
        const std::string res = Join(in.exportDir, "install-result.txt");
        auto q = [](const std::string& s) { std::string o = "'"; for (char ch : s) { if (ch == '\'') o += "''"; else o += ch; } return o + "'"; };
        c.args = {"-NoProfile", "-ExecutionPolicy", "Bypass", "-Command",
                  "Start-Process powershell -Verb RunAs -Wait -ArgumentList '-NoProfile','-ExecutionPolicy','Bypass','-File'," + q(q(script)) +
                  ",'-Cer'," + q(q(cer)) + ",'-Appx'," + q(q(in.appxPath)) + ",'-ResultFile'," + q(q(res)) +
                  "; if (Test-Path " + q(res) + ") { Get-Content " + q(res) + " } else { 'INSTALL: FAILED (no result: the administrator prompt was probably declined)' }"};
        process("install", "Install on this PC (asks for administrator permission once)", "Windows shows one prompt; accept it.", c);
    }
    return v;
}

// ---- log lines -------------------------------------------------------------------------------------------------------------
enum class LineClass { Info, Warning, Error };

inline std::string LowerAscii(std::string s) { for (char& c : s) if (c >= 'A' && c <= 'Z') c = char(c - 'A' + 'a'); return s; }
inline bool Has(const std::string& l, const char* needle) { return l.find(needle) != std::string::npos; }

/// Classify one line of compiler / cmake / PowerShell / fxc output.
inline LineClass ClassifyLine(const std::string& line) {
    const std::string l = LowerAscii(line);
    if (Has(l, "0 error") || Has(l, "0 warning") || Has(l, "no errors")) return LineClass::Info;   // "0 Error(s)" summaries
    if (Has(l, ": error ") || Has(l, "fatal error") || Has(l, "cmake error") || (Has(l, "msb") && Has(l, ": error")) ||
        l.rfind("error:", 0) == 0 || l.rfind("error ", 0) == 0 || Has(l, "fxc failed") || Has(l, "exception") || Has(l, "failed:") ||
        Has(l, "cannot find") || Has(l, "is not recognized") || Has(l, "okay-appx: self-check") || Has(l, "problem:") ||
        Has(l, "signtool error") || Has(l, "install failed") || Has(l, "install: failed") || l.rfind("throw", 0) == 0)
        return LineClass::Error;
    if (Has(l, ": warning ") || Has(l, "cmake warning") || l.rfind("warning:", 0) == 0) return LineClass::Warning;
    return LineClass::Info;
}

/// The first `n` error lines (trimmed, redacted).
inline std::string Redact(const std::string& line);
inline std::vector<std::string> FirstErrors(const std::vector<std::string>& lines, std::size_t n) {
    std::vector<std::string> o;
    for (const std::string& l : lines) { if (o.size() >= n) break; if (ClassifyLine(l) == LineClass::Error) o.push_back(Redact(l)); }
    return o;
}

/// Hide anything that looks like key material or a password. The scripts never print them; this is the second fence.
inline std::string Redact(const std::string& line) {
    const std::string l = LowerAscii(line);
    static const char* keys[] = {"password", "passwd", "pwd=", "secret", "private key", "-----begin", "pfx", "-p ", "/p ", "/csp", "/kc ", "token", "apikey", "api_key"};
    for (const char* k : keys) {
        const std::size_t at = l.find(k);
        if (at == std::string::npos) continue;
        if (std::string(k) == "pfx" || std::string(k) == "private key" || std::string(k) == "-----begin") return line.substr(0, at) + "[hidden: key material]";
        std::size_t e = at + std::string(k).size();
        while (e < line.size() && (line[e] == ' ' || line[e] == ':' || line[e] == '=')) ++e;
        std::size_t f = e; while (f < line.size() && line[f] != ' ') ++f;
        return line.substr(0, e) + "***" + (f < line.size() ? Redact(line.substr(f)) : std::string());
    }
    return line;
}

/// Pull `KEY: value` facts out of a step's output (the scripts print "SIGNER: ...", "CERT: path", "THUMBPRINT: ...").
inline std::string FindFact(const std::vector<std::string>& lines, const std::string& key) {
    const std::string pre = key + ": ";
    for (const std::string& l : lines) if (l.rfind(pre, 0) == 0) { std::string v = l.substr(pre.size()); while (!v.empty() && (v.back() == '\r' || v.back() == ' ')) v.pop_back(); return v; }
    return "";
}

// ---- plain error messages ----------------------------------------------------------------------------------------------------
inline std::string MapFailure(const std::string& stepId, int exitCode, bool couldNotStart, const std::vector<std::string>& errors) {
    std::string m;
    if (couldNotStart) {
        if (stepId == "configure" || stepId == "build") m = "Could not start cmake. Open the Visual Studio Installer, choose Modify, and tick \"C++ CMake tools for Windows\" (part of the C++ desktop workload), or install cmake.";
        else m = "Could not start PowerShell. It comes with Windows 10 and 11; is it blocked on this PC?";
    } else if (stepId == "configure") m = "CMake could not set up the UWP app. Most often the \"Universal Windows Platform development\" workload or its C++ UWP tools are not installed (Visual Studio Installer > Modify).";
    else if (stepId == "build") m = "The UWP app did not compile. The first error lines are below; send them if the cause is not clear.";
    else if (stepId == "shaders") m = "The shaders did not compile. fxc.exe comes with the Windows SDK (Visual Studio Installer > Modify > a Windows 10/11 SDK).";
    else if (stepId == "sign") m = "Signing failed. The package is built and checked but unsigned, so Windows will not install it.";
    else if (stepId == "install") m = "Installing on this PC failed. If you closed the administrator prompt, run the build again with \"Install on this PC\" and accept it.";
    else if (stepId == "pack") m = "Packing or checking the .appx failed (our own packer).";
    else if (stepId == "export") m = "Exporting the game data failed.";
    else if (stepId == "sdk") m = "A required Windows tool is missing (see the list above).";
    else m = "A build step failed.";
    if (!couldNotStart && exitCode != 0) m += " (exit code " + std::to_string(exitCode) + ")";
    for (const std::string& e : errors) m += "\n  " + e;
    return m;
}

// ---- the runner ------------------------------------------------------------------------------------------------------------
enum class StepStatus { Waiting, Running, Done, Failed, Skipped, Cancelled };
inline const char* StatusName(StepStatus s) {
    switch (s) { case StepStatus::Waiting: return "waiting"; case StepStatus::Running: return "running"; case StepStatus::Done: return "done";
                 case StepStatus::Failed: return "FAILED"; case StepStatus::Skipped: return "skipped"; case StepStatus::Cancelled: return "cancelled"; }
    return "";
}

/// A running child process. Poll returns true once it has exited; `out` receives output that arrived since the last call.
struct ProcessHandle {
    virtual ~ProcessHandle() = default;
    virtual bool Poll(std::string& out, int& exitCode) = 0;
    virtual void Kill() = 0;
};
/// What the runner needs from the editor / Windows. Fakes in the tests; the real one is XboxProcess.hpp + the editor.
struct BuildHost {
    virtual ~BuildHost() = default;
    /// Run an Internal step now (short). Lines it wants shown go to `log`. False + `err` = failed.
    virtual bool RunInternal(const Step& s, std::vector<std::string>& log, std::string& err) = 0;
    /// Start a Process step. Null + `err` = it could not be started.
    virtual std::unique_ptr<ProcessHandle> Spawn(const Command& c, std::string& err) = 0;
};

struct LogLine { std::string text; LineClass cls = LineClass::Info; int step = -1; };

class BuildRunner {
public:
    BuildRunner(std::vector<Step> steps, BuildHost& host) : steps_(std::move(steps)), host_(host), status_(steps_.size(), StepStatus::Waiting) {}
    bool Running() const { return started_ && !finished_; }
    bool Finished() const { return finished_; }
    bool Succeeded() const { return finished_ && !failed_ && !cancelled_; }
    bool Cancelled() const { return cancelled_; }
    const std::vector<Step>& Steps() const { return steps_; }
    const std::vector<StepStatus>& Status() const { return status_; }
    const std::vector<LogLine>& Log() const { return log_; }
    const std::string& FailureMessage() const { return failure_; }
    /// Facts the steps printed (SIGNER, CERT ...), valid after the step ran.
    std::string Fact(const std::string& key) const { std::vector<std::string> t; for (const LogLine& l : log_) t.push_back(l.text); return FindFact(t, key); }

    void Start() { if (started_) return; started_ = true; Next(); }
    /// Call once per editor frame. Returns true while there is more to do.
    bool Tick() {
        if (!Running()) return false;
        if (proc_) {
            std::string out; int code = 0;
            const bool done = proc_->Poll(out, code);
            Feed(out);
            if (done) { FlushPartial(); proc_.reset(); EndProcessStep(code); }
        }
        return Running();
    }
    void Cancel() {
        if (!Running()) return;
        if (proc_) { proc_->Kill(); proc_.reset(); }
        for (std::size_t i = 0; i < steps_.size(); ++i) if (status_[i] == StepStatus::Running) status_[i] = StepStatus::Cancelled; else if (status_[i] == StepStatus::Waiting) status_[i] = StepStatus::Skipped;
        cancelled_ = true; finished_ = true; failure_ = "Cancelled."; Add("Cancelled by you.", LineClass::Warning);
    }

private:
    void Add(const std::string& t, LineClass c) {
        std::string x = Redact(t); if (x.size() > 400) x.resize(400);
        log_.push_back({x, c, cur_}); if (log_.size() > 3000) log_.erase(log_.begin(), log_.begin() + 500);
    }
    void AddLine(const std::string& raw) { std::string l = raw; while (!l.empty() && (l.back() == '\r' || l.back() == '\n')) l.pop_back(); if (l.empty()) return; const LineClass c = ClassifyLine(l); Add(l, c); stepLines_.push_back(l); }
    void Feed(const std::string& out) {
        partial_ += out;
        std::size_t p;
        while ((p = partial_.find('\n')) != std::string::npos) { AddLine(partial_.substr(0, p)); partial_.erase(0, p + 1); }
        if (partial_.size() > 4000) { AddLine(partial_); partial_.clear(); }
    }
    void FlushPartial() { if (!partial_.empty()) { AddLine(partial_); partial_.clear(); } }
    void Fail(const std::string& msg) {
        status_[cur_] = StepStatus::Failed; failed_ = true; finished_ = true; failure_ = msg;
        for (std::size_t i = cur_ + 1; i < steps_.size(); ++i) status_[i] = StepStatus::Skipped;
        Add(msg, LineClass::Error);
    }
    void EndProcessStep(int code) {
        const Step& s = steps_[cur_];
        bool bad = code != 0;
        if (!bad && s.id == "install") { const std::string r = FindFact(stepLines_, "INSTALL"); if (r != "OK") bad = true; }   // the elevated child's verdict
        if (bad) {
            if (s.id == "install" && code == 0) { std::vector<std::string> e = FirstErrors(stepLines_, 3); Fail(MapFailure(s.id, 0, false, e)); }
            else Fail(MapFailure(s.id, code, false, FirstErrors(stepLines_, 5)));
            return;
        }
        status_[cur_] = StepStatus::Done; Next();
    }
    void Next() {
        for (;;) {
            if (finished_) return;
            cur_ = cur_ < 0 ? 0 : cur_ + 1;
            if (cur_ >= (int)steps_.size()) { finished_ = true; Add("All steps finished.", LineClass::Info); return; }
            const Step& s = steps_[cur_];
            status_[cur_] = StepStatus::Running; stepLines_.clear(); partial_.clear();
            Add("== " + s.title + " ==", LineClass::Info);
            if (s.kind == StepKind::Internal) {
                std::vector<std::string> lines; std::string err;
                const bool ok = host_.RunInternal(s, lines, err);
                for (const std::string& l : lines) AddLine(l);
                if (!ok) { Fail(MapFailure(s.id, 1, false, err.empty() ? FirstErrors(stepLines_, 5) : std::vector<std::string>{Redact(err)})); return; }
                status_[cur_] = StepStatus::Done;
                continue;
            }
            std::string err;
            Add("> " + s.cmd.Line(), LineClass::Info);
            proc_ = host_.Spawn(s.cmd, err);
            if (!proc_) { Fail(MapFailure(s.id, -1, true, err.empty() ? std::vector<std::string>() : std::vector<std::string>{Redact(err)})); return; }
            return;
        }
    }
    std::vector<Step> steps_; BuildHost& host_; std::vector<StepStatus> status_;
    std::vector<LogLine> log_; std::vector<std::string> stepLines_; std::string partial_, failure_;
    std::unique_ptr<ProcessHandle> proc_;
    int cur_ = -1; bool started_ = false, finished_ = false, failed_ = false, cancelled_ = false;
};

} // namespace okay::xbox
