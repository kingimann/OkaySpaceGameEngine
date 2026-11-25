#include "okayms/implib.hpp"

// Module-definition files and import libraries: see include/okayms/implib.hpp for the documents this follows.

#include <cctype>

namespace okayms {
namespace {

std::string upper_copy(std::string text) {
    for (char& c : text) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    return text;
}

// Splits one line into words; '=' and '@' are words of their own (name=internal, @7 and "@ 7" are all read).
std::vector<std::string> def_words(const std::string& line) {
    std::vector<std::string> words;
    std::string cur;
    bool quoted = false;
    for (char c : line) {
        if (c == '"') {
            quoted = !quoted;
            continue;
        }
        if (!quoted && (c == ' ' || c == '\t' || c == '\r' || c == ',')) {
            if (!cur.empty()) words.push_back(cur);
            cur.clear();
            continue;
        }
        if (!quoted && (c == '=' || c == '@')) {
            if (!cur.empty()) words.push_back(cur);
            cur.clear();
            words.push_back(std::string(1, c));
            continue;
        }
        cur.push_back(c);
    }
    if (!cur.empty()) words.push_back(cur);
    return words;
}

bool all_digits(const std::string& s) {
    if (s.empty()) return false;
    for (char c : s)
        if (c < '0' || c > '9') return false;
    return true;
}

}  // namespace

Result<DefFile> parse_def(const std::string& text) {
    DefFile def;
    bool in_exports = false;
    size_t line_no = 0;
    size_t pos = 0;
    while (pos <= text.size()) {
        size_t end = text.find('\n', pos);
        if (end == std::string::npos) end = text.size();
        std::string line = text.substr(pos, end - pos);
        pos = end + 1;
        ++line_no;
        const size_t semi = line.find(';');
        if (semi != std::string::npos) line.resize(semi);
        const std::vector<std::string> words = def_words(line);
        if (words.empty()) continue;
        const std::string head = upper_copy(words[0]);
        if (head == "LIBRARY") {
            in_exports = false;
            if (words.size() > 1 && upper_copy(words[1]) != "BASE") {
                def.library = words[1];
                if (def.library.find('.') == std::string::npos) def.library += ".dll";
            }
            continue;
        }
        if (head == "EXPORTS") {
            in_exports = true;
            continue;
        }
        if (head == "NAME" || head == "DESCRIPTION" || head == "VERSION" || head == "HEAPSIZE" || head == "STACKSIZE" ||
            head == "SECTIONS" || head == "IMPORTS") {
            in_exports = false;
            continue;
        }
        if (!in_exports) {
            if (head == "STUB" || head == "CODE" || head == "DATA") continue;
            return Result<DefFile>::fail("def line " + std::to_string(line_no) + ": unknown statement " + words[0]);
        }
        DefExport e;
        e.name = words[0];
        size_t i = 1;
        if (i + 1 < words.size() && words[i] == "=") i += 2;  // name=internal: the internal name is the DLL's business
        while (i < words.size()) {
            const std::string w = upper_copy(words[i]);
            if (w == "@") {
                if (i + 1 >= words.size() || !all_digits(words[i + 1]) || std::stoul(words[i + 1]) == 0 ||
                    std::stoul(words[i + 1]) > 65535)
                    return Result<DefFile>::fail("def line " + std::to_string(line_no) + ": expected an ordinal after @");
                e.ordinal = static_cast<uint16_t>(std::stoul(words[i + 1]));
                i += 2;
                continue;
            }
            if (w == "NONAME") e.noname = true;
            else if (w == "DATA" || w == "CONSTANT") e.data = true;
            else if (w == "PRIVATE") e.is_private = true;
            else return Result<DefFile>::fail("def line " + std::to_string(line_no) + ": unknown export attribute " + words[i]);
            ++i;
        }
        if (e.noname && e.ordinal == 0)
            return Result<DefFile>::fail("def line " + std::to_string(line_no) + ": NONAME needs an @ordinal");
        def.exports.push_back(e);
    }
    return Result<DefFile>::success(std::move(def));
}

Result<Archive> make_import_library(const DefFile& def) {
    if (def.library.empty()) return Result<Archive>::fail("the def file names no LIBRARY");
    Archive archive;
    archive.symbol_index = true;
    for (const DefExport& e : def.exports) {
        if (e.is_private) continue;
        ArchiveMember member;
        member.name = def.library;
        member.is_import = true;
        member.import.machine = kMachineAmd64;
        member.import.symbol = e.name;
        member.import.dll = def.library;
        member.import.import_type = e.data ? kImportData : kImportCode;
        if (e.noname) {
            member.import.name_type = kImportByOrdinal;
            member.import.hint_or_ordinal = e.ordinal;
        } else {
            member.import.name_type = kImportByName;
            member.import.hint_or_ordinal = e.ordinal;  // a hint: where the loader looks first in the export table
        }
        archive.members.push_back(std::move(member));
    }
    return Result<Archive>::success(std::move(archive));
}

}  // namespace okayms
