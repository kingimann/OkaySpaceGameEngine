// OkaySpace Launcher
//
// A small self-updating front end: it checks GitHub for a newer version of the
// engine, pulls it, rebuilds, and launches the game. Run it instead of the game
// binary and players always stay up to date.
//
//   okayspace-launcher [options] [-- <args passed to the game>]
//
// Options:
//   --no-update     Skip the GitHub update check
//   --check-only    Report whether an update is available, then exit
//   --no-build      Don't rebuild after updating
//   --no-run        Update/build only; don't launch the game
//   --game <name>   Which built binary to run (default: sandbox)
//   --branch <b>    Branch to track (default: the current branch)
#include <algorithm>
#include <array>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace {

std::string Trim(std::string s) {
    auto notSpace = [](unsigned char c) { return !std::isspace(c); };
    s.erase(s.begin(), std::find_if(s.begin(), s.end(), notSpace));
    s.erase(std::find_if(s.rbegin(), s.rend(), notSpace).base(), s.end());
    return s;
}

// Run a command, returning its exit code.
int Run(const std::string& cmd) { return std::system(cmd.c_str()); }

// Run a command and capture stdout.
std::string Capture(const std::string& cmd) {
    std::string full = cmd + " 2>/dev/null";
#if defined(_WIN32)
    FILE* pipe = _popen(cmd.c_str(), "r");
#else
    FILE* pipe = popen(full.c_str(), "r");
#endif
    if (!pipe) return {};
    std::string out;
    std::array<char, 256> buf{};
    while (fgets(buf.data(), (int)buf.size(), pipe)) out += buf.data();
#if defined(_WIN32)
    _pclose(pipe);
#else
    pclose(pipe);
#endif
    return Trim(out);
}

// Walk up from `start` to find the repository root (the dir containing .git).
fs::path FindRepoRoot(fs::path start) {
    for (fs::path p = start; !p.empty(); p = p.parent_path()) {
        if (fs::exists(p / ".git")) return p;
        if (p == p.root_path()) break;
    }
    return {};
}

struct Options {
    bool update = true, checkOnly = false, build = true, run = true;
    std::string game = "sandbox";
    std::string branch;
    std::vector<std::string> gameArgs;
};

} // namespace

int main(int argc, char** argv) {
    Options opt;
    bool passthrough = false;
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        if (passthrough) { opt.gameArgs.push_back(a); continue; }
        if (a == "--")               passthrough = true;
        else if (a == "--no-update") opt.update = false;
        else if (a == "--check-only")opt.checkOnly = true;
        else if (a == "--no-build")  opt.build = false;
        else if (a == "--no-run")    opt.run = false;
        else if (a == "--game" && i + 1 < argc) opt.game = argv[++i];
        else if (a == "--branch" && i + 1 < argc) opt.branch = argv[++i];
        else if (a == "--help" || a == "-h") {
            std::cout << "Usage: okayspace-launcher [--no-update] [--check-only] "
                         "[--no-build] [--no-run] [--game <name>] [--branch <b>] "
                         "[-- <game args>]\n";
            return 0;
        } else {
            std::cerr << "Unknown option: " << a << "\n";
            return 2;
        }
    }

    fs::path exeDir = fs::absolute(fs::path(argv[0])).parent_path();
    fs::path root = FindRepoRoot(exeDir);
    if (root.empty()) root = FindRepoRoot(fs::current_path());
    if (root.empty()) {
        std::cerr << "[launcher] Could not locate the OkaySpace git repository.\n";
        return 1;
    }
    std::cout << "[launcher] Repository: " << root << "\n";

    const std::string git = "git -C \"" + root.string() + "\"";
    std::string branch = opt.branch.empty()
        ? Capture(git + " rev-parse --abbrev-ref HEAD") : opt.branch;
    if (branch.empty() || branch == "HEAD") branch = "main";

    bool updateApplied = false;
    if (opt.update || opt.checkOnly) {
        std::cout << "[launcher] Checking GitHub for updates on '" << branch << "'...\n";
        Run(git + " fetch --quiet origin " + branch);
        std::string local  = Capture(git + " rev-parse HEAD");
        std::string remote = Capture(git + " rev-parse origin/" + branch);

        if (remote.empty()) {
            std::cout << "[launcher] Could not reach origin; continuing offline.\n";
        } else if (local == remote) {
            std::cout << "[launcher] Already up to date (" << local.substr(0, 8) << ").\n";
        } else {
            std::cout << "[launcher] Update available: " << local.substr(0, 8)
                      << " -> " << remote.substr(0, 8) << "\n";
            if (opt.checkOnly) return 10; // signal "update available"
            if (opt.update) {
                std::cout << "[launcher] Pulling latest...\n";
                if (Run(git + " pull --ff-only origin " + branch) == 0) {
                    updateApplied = true;
                    std::cout << "[launcher] Updated to " << remote.substr(0, 8) << ".\n";
                } else {
                    std::cerr << "[launcher] Pull failed (local changes?). Continuing "
                                 "with the current version.\n";
                }
            }
        }
    }
    if (opt.checkOnly) return 0;

    fs::path buildDir = root / "build";
    if (opt.build && (updateApplied || !fs::exists(buildDir / "bin"))) {
        std::cout << "[launcher] Building engine...\n";
        if (!fs::exists(buildDir))
            Run("cmake -S \"" + root.string() + "\" -B \"" + buildDir.string() +
                "\" -DCMAKE_BUILD_TYPE=Release");
        if (Run("cmake --build \"" + buildDir.string() + "\" -j") != 0) {
            std::cerr << "[launcher] Build failed.\n";
            return 1;
        }
    }

    if (!opt.run) return 0;

#if defined(_WIN32)
    fs::path gameExe = buildDir / "bin" / (opt.game + ".exe");
#else
    fs::path gameExe = buildDir / "bin" / opt.game;
#endif
    if (!fs::exists(gameExe)) {
        std::cerr << "[launcher] Game binary not found: " << gameExe << "\n";
        return 1;
    }

    std::string cmd = "\"" + gameExe.string() + "\"";
    for (auto& a : opt.gameArgs) cmd += " \"" + a + "\"";
    std::cout << "[launcher] Launching " << opt.game << "...\n";
    return Run(cmd);
}
