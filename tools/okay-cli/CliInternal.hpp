#pragma once
// okay CLI: helpers shared by Cli.cpp (the general commands) and Vc.cpp (okay vc and git's drivers). Not a public API.
#include <string>
#include <vector>

#include "Cli.hpp"
#include "okay/Core/CommandLine.hpp"

namespace okaycli::detail {

/// The whole file ("" when it can't be read; `ok` says which).
std::string ReadFile(const std::string& path, bool* ok = nullptr);
std::string JoinWords(const std::vector<std::string>& v, std::size_t from = 0);
std::string Pad(std::string s, std::size_t n);
/// `path` relative to `root` with forward slashes (`path` itself when that is not possible).
std::string RelTo(const std::string& path, const std::string& root);
/// `p` made absolute and real, with forward slashes and no trailing slash: symbolic links resolved and, on Windows,
/// short 8.3 folder names (C:/Users/RUNNER~1) expanded to the long ones git prints. Parts that do not exist yet are kept.
std::string LongPath(const std::string& p);
/// Where `path` is inside the folder `root` ("Assets/Level.okayscene", forward slashes), or "" when it is not inside it
/// (or is `root` itself). Short and long names of the same folder match; on Windows case and slashes do not matter.
std::string RelInside(const std::string& path, const std::string& root);
/// Parse a command's arguments (adds --help/-h); a bad command line is said in plain words with the command's help.
bool ParseArgs(okay::CommandLine& cl, const std::vector<std::string>& args, const std::string& command, const Io& io);
/// A line of text-merge clash marks (<<<<<<< or >>>>>>>) is in `text`.
bool HasClashMarks(const std::string& text);
/// The okay program git's drivers should run: one that survives the launcher removing an old version. `okay` on PATH,
/// else the launcher-level Tools/okay when this one is a kept copy under Versions/<v>/, else this program.
std::string StableOkayPath();
/// True while RunInProcess runs a command on this thread (inside the editor, not the okay program). CommandJob.cpp.
bool RunningInsideHost();
/// `s` as a JSON string, quotes included.
std::string JsonText(const std::string& s);
/// `okay project|scene|script|vs|asset <sub> ...` (args start after the command). Content.cpp.
int ProjectMain(const std::vector<std::string>& args, const std::string& project, const Io& io);
int SceneMain(const std::vector<std::string>& args, const std::string& project, const Io& io);
int ScriptMain(const std::vector<std::string>& args, const std::string& project, const Io& io);
int VsMain(const std::vector<std::string>& args, const std::string& project, const Io& io);
int AssetMain(const std::vector<std::string>& args, const std::string& project, const Io& io);
/// `okay vc <sub> ...` (args start after "vc").
int VcMain(const std::vector<std::string>& args, const std::string& project, const Io& io);

} // namespace okaycli::detail
