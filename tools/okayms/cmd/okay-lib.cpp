// okay-lib: a small librarian in the style of Microsoft's lib.exe, written from Microsoft's documentation of LIB's
// options, of module-definition (.def) files and of the archive and import-library formats in "PE Format".
//   okay-lib /DEF:kernel32.def /OUT:kernel32.lib [/NAME:kernel32.dll] [/MACHINE:X64]
//       an import library: one short import member per export of the .def file, with the symbol index.
//   okay-lib /OUT:util.lib a.obj b.obj [other.lib]
//       a static library of those objects (and the members of the libraries given), with the symbol index.
//   okay-lib /LIST x.lib
//       prints each member's name and the public symbols it defines.
// Own limits: x64 only; no /EXPORT on the command line (a .def file lists the exports); no long-format import members.

#include "okayms/archive.hpp"
#include "okayms/implib.hpp"

#include <cctype>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

namespace {

std::string lower_copy(std::string text) {
    for (char& c : text) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return text;
}

bool read_file(const std::string& path, std::vector<uint8_t>* out) {
    std::ifstream in(path.c_str(), std::ios::binary);
    if (!in) return false;
    out->assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    return true;
}

bool write_file(const std::string& path, const std::vector<uint8_t>& bytes) {
    std::ofstream out(path.c_str(), std::ios::binary);
    out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    return static_cast<bool>(out);
}

int fail(const std::string& message) {
    std::fprintf(stderr, "okay-lib: %s\n", message.c_str());
    return 1;
}

std::string base_name(const std::string& path) {
    const size_t slash = path.find_last_of("/\\");
    return slash == std::string::npos ? path : path.substr(slash + 1);
}

}  // namespace

int main(int argc, char** argv) {
    std::string out_path;
    std::string def_path;
    std::string dll_name;
    bool list = false;
    std::vector<std::string> inputs;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        // An option is /KEY or /KEY:value with a known key (so /tmp/a.obj, an absolute path, is an input).
        std::string key;
        std::string value;
        if (arg.size() > 1 && (arg[0] == '/' || arg[0] == '-')) {
            const std::string body = arg.substr(1);
            const size_t colon = body.find(':');
            key = lower_copy(colon == std::string::npos ? body : body.substr(0, colon));
            value = colon == std::string::npos ? std::string() : body.substr(colon + 1);
            if (key != "out" && key != "def" && key != "name" && key != "list" && key != "machine" && key != "nologo") {
                if (arg[0] == '-') return fail("unknown option " + arg);
                key.clear();
            }
        }
        if (!key.empty()) {
            if (key == "out") out_path = value;
            else if (key == "def") def_path = value;
            else if (key == "name") dll_name = value;
            else if (key == "list") list = true;
            else if (key == "machine") {
                if (lower_copy(value) != "x64" && lower_copy(value) != "amd64") return fail("only /MACHINE:X64 is supported");
            } else if (key == "nologo") {
            } else {
                return fail("unknown option " + arg);
            }
            if ((key == "out" || key == "def" || key == "name") && value.empty()) return fail(arg + " needs a value");
            continue;
        }
        inputs.push_back(arg);
    }

    if (list) {
        if (inputs.size() != 1) return fail("/LIST takes one library");
        std::vector<uint8_t> bytes;
        if (!read_file(inputs[0], &bytes)) return fail("cannot open " + inputs[0]);
        const auto archive = okayms::read_archive(bytes.data(), bytes.size());
        if (!archive.ok()) return fail(inputs[0] + ": " + archive.error);
        for (const okayms::ArchiveMember& member : archive.value.members) {
            std::printf("%s\n", member.name.c_str());
            for (const std::string& symbol : okayms::member_symbols(member)) std::printf("  %s\n", symbol.c_str());
        }
        return 0;
    }

    if (out_path.empty()) return fail("no /OUT:file");
    okayms::Archive archive;
    if (!def_path.empty()) {
        if (!inputs.empty()) return fail("/DEF makes an import library; give no objects with it");
        std::vector<uint8_t> text;
        if (!read_file(def_path, &text)) return fail("cannot open " + def_path);
        auto def = okayms::parse_def(std::string(text.begin(), text.end()));
        if (!def.ok()) return fail(def_path + ": " + def.error);
        if (!dll_name.empty()) def.value.library = dll_name;
        if (def.value.library.empty()) {
            // No LIBRARY line and no /NAME: the DLL is named after the .def file, as LIB does.
            std::string stem = base_name(def_path);
            const size_t dot = stem.rfind('.');
            if (dot != std::string::npos) stem.resize(dot);
            def.value.library = stem + ".dll";
        }
        const auto made = okayms::make_import_library(def.value);
        if (!made.ok()) return fail(made.error);
        archive = made.value;
    } else {
        if (inputs.empty()) return fail("no input files");
        archive.symbol_index = true;
        for (const std::string& path : inputs) {
            std::vector<uint8_t> bytes;
            if (!read_file(path, &bytes)) return fail("cannot open " + path);
            if (lower_copy(path).size() >= 4 && lower_copy(path).compare(path.size() - 4, 4, ".lib") == 0) {
                const auto lib = okayms::read_archive(bytes.data(), bytes.size());
                if (!lib.ok()) return fail(path + ": " + lib.error);
                for (const okayms::ArchiveMember& member : lib.value.members) archive.members.push_back(member);
                continue;
            }
            const auto object = okayms::read_coff(bytes.data(), bytes.size());
            if (!object.ok()) return fail(path + ": " + object.error);
            okayms::ArchiveMember member;
            member.name = base_name(path);
            member.object = object.value;
            archive.members.push_back(member);
        }
    }
    const auto bytes = okayms::write_archive(archive);
    if (!bytes.ok()) return fail(bytes.error);
    if (!write_file(out_path, bytes.value)) return fail("cannot write " + out_path);
    return 0;
}
