#pragma once

// Import libraries made from a list of a DLL's exports, so a program links calls into a Windows DLL with okay-ms's own
// .lib instead of MinGW's or the Windows SDK's. The list is a module-definition (.def) file, read as Microsoft's
// "Module-Definition (.Def) Files" documentation describes LIBRARY and EXPORTS; the library is a COFF archive of short
// import members (Microsoft "PE Format", "Import Library Format"), with the two linker members that index its symbols
// (write_archive). Written from those documents; no code from lib.exe, MinGW's dlltool, LLVM or any other project.

#include "okayms/archive.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace okayms {

struct DefExport {
    std::string name;       // the name the DLL exports and programs call
    uint16_t ordinal = 0;   // @n, or 0 when none was given
    bool noname = false;    // NONAME: exported by ordinal only
    bool data = false;      // DATA: a variable (only __imp_name is defined, no call thunk)
    bool is_private = false;  // PRIVATE: left out of the import library
};

struct DefFile {
    std::string library;  // LIBRARY name, with ".dll" added when it has no extension
    std::vector<DefExport> exports;
};

// Reads a .def file. Accepted: LIBRARY [name] [BASE=n], EXPORTS entries `name[=internal] [@ordinal [NONAME]] [DATA]
// [PRIVATE]`, comments after ';', and NAME/DESCRIPTION/VERSION/HEAPSIZE/STACKSIZE/SECTIONS lines (ignored).
Result<DefFile> parse_def(const std::string& text);

// One short import member per export (by name, or by ordinal for NONAME), for x64.
Result<Archive> make_import_library(const DefFile& def);

}  // namespace okayms
