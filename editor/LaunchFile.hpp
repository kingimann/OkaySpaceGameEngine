#pragma once
// Starting another program from the editor, on purpose and without a shell: the Project panel's "Open with Default App",
// "Open in External Editor" and "Show in Explorer", and the Script editor's "Open in External IDE".
//
//   OpenWithDefaultApp  the program the system uses for the file's type. Refused for a type that runs code (a program, an
//                       installer, a shortcut, a script: okaypb::ctx::LaunchRefusal), whatever the caller checked before.
//   OpenInTextEditor    a text editor, never the system's "open" for the file: Visual Studio Code when it is installed,
//                       else Notepad (Windows), `open -t` (macOS) or the first GUI text editor found (Linux). A script is
//                       edited, never run, because the editor is named explicitly instead of asked from the file type.
//   Reveal              the file manager: a folder is opened, a file is shown (selected on Windows and macOS).
//   OpenLocalUrl        the default web browser on a page this computer serves (http://127.0.0.1:<port>/ only).
//
// Windows uses ShellExecuteW with the wide path and a fixed program, so there is no command line to build and nothing in a
// file name ($, %, a quote, a space) is read by cmd.exe. macOS and Linux start the program with fork and execv, no shell.
// Each returns what went wrong in plain words instead of failing silently.
#include <string>

namespace oked {
namespace launch {

struct Result {
    bool ok = false;
    std::string error;   ///< plain English, set when !ok
};

Result OpenWithDefaultApp(const std::string& path);
Result OpenInTextEditor(const std::string& path);
Result Reveal(const std::string& path, bool selectFile);
/// The default web browser on a page of this computer: only "http://127.0.0.1:<port>/..." with no spaces, quotes or
/// control characters (Build Settings > Web > Run in browser). Anything else is refused.
Result OpenLocalUrl(const std::string& url);

} // namespace launch
} // namespace oked
