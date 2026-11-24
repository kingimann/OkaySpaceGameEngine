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
/// Parse a command's arguments (adds --help/-h); a bad command line is said in plain words with the command's help.
bool ParseArgs(okay::CommandLine& cl, const std::vector<std::string>& args, const std::string& command, const Io& io);
/// A line of text-merge clash marks (<<<<<<< or >>>>>>>) is in `text`.
bool HasClashMarks(const std::string& text);
/// The okay program git's drivers should run: one that survives the launcher removing an old version. `okay` on PATH,
/// else the launcher-level Tools/okay when this one is a kept copy under Versions/<v>/, else this program.
std::string StableOkayPath();
/// `okay vc <sub> ...` (args start after "vc").
int VcMain(const std::vector<std::string>& args, const std::string& project, const Io& io);

} // namespace okaycli::detail
