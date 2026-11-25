#pragma once
// okay CLI: the tables of commands and sub-commands, with their usage and one-line summaries. The help (Cli.cpp HelpText),
// the shell completion scripts (CompletionScript) and the launcher's Command line page (launcher/CommandLine.cpp, Tab
// completion) all read these, so a new command shows up everywhere once it is listed here. Standard library only, so the
// launcher compiles CommandTable.cpp without the engine.
#include <string>
#include <vector>

namespace okaycli {

struct CommandHelp { const char* name; const char* usage; const char* summary; const char* details; };
struct HelpGroup { const char* title; std::vector<const char*> commands; };

/// Every top-level command, in the order the help lists them.
const std::vector<CommandHelp>& Commands();
/// The sub-commands of a command (vc, scene, asset ...); null for a command without them.
const std::vector<CommandHelp>* SubCommands(const std::string& command);
/// The help's groups ("Projects", "Version control" ...).
const std::vector<HelpGroup>& Groups();
/// The command called `name`, or null.
const CommandHelp* FindCommand(const std::string& name);

/// The command names, and a command's sub-command names (none for a command without them), as the help lists them.
std::vector<std::string> CommandNames();
std::vector<std::string> SubCommandNames(const std::string& command);
/// The one-line summary of "scene" or "scene list" ("" when unknown).
const char* CommandSummary(const std::string& command);

} // namespace okaycli
