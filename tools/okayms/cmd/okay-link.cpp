// okay-link: a small MSVC-style linker driver.
// /OUT:  /ENTRY:  /SUBSYSTEM:  /LIBPATH:  and object or .lib paths.

#include "okayms/archive.hpp"
#include "okayms/link.hpp"

#include <cctype>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

namespace {

std::string lower_copy(std::string text) {
    for (char& c : text) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    return text;
}

bool file_exists(const std::string& path) {
    std::ifstream in(path.c_str(), std::ios::binary);
    return static_cast<bool>(in);
}

std::vector<uint8_t> read_file(const std::string& path, std::string* error) {
    std::ifstream in(path.c_str(), std::ios::binary);
    if (!in) {
        *error = "cannot open " + path;
        return {};
    }
    return std::vector<uint8_t>(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

bool write_file(const std::string& path, const std::vector<uint8_t>& bytes, std::string* error) {
    std::ofstream out(path.c_str(), std::ios::binary);
    if (!out) {
        *error = "cannot write " + path;
        return false;
    }
    out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    if (!out) {
        *error = "cannot write " + path;
        return false;
    }
    return true;
}

void fail(const std::string& message) {
    std::cerr << "okay-link: " << message << "\n";
}

}  // namespace

int main(int argc, char** argv) {
    okayms::LinkOptions options;
    std::vector<std::string> inputs;
    std::vector<std::string> libpaths;
    std::string out_path;
    std::string map_path;
    bool have_out = false;

    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg.empty()) continue;
        // A path that exists is an input, even when it starts with '/'.
        // Flags are the ones that are not files: /OUT: /ENTRY: /SUBSYSTEM: /LIBPATH:.
        if ((arg[0] != '/' && arg[0] != '-') || file_exists(arg)) {
            inputs.push_back(arg);
            continue;
        }
        std::string body = arg;
        while (!body.empty() && (body[0] == '/' || body[0] == '-')) body.erase(body.begin());
        if (lower_copy(body) == "dll") {
            options.dll = true;
            if (options.image_base == 0x140000000ULL) options.image_base = 0x180000000ULL;
            continue;
        }
        const size_t colon = body.find(':');
        if (colon == std::string::npos) {
            fail("flag needs a value: " + arg);
            return 1;
        }
        const std::string key = lower_copy(body.substr(0, colon));
        const std::string value = body.substr(colon + 1);
        if (value.empty()) {
            fail("flag needs a value: " + arg);
            return 1;
        }
        if (key == "out") {
            out_path = value;
            have_out = true;
        } else if (key == "entry") {
            options.entry = value;
        } else if (key == "subsystem") {
            const std::string sub = lower_copy(value);
            if (sub == "console") options.subsystem = okayms::kSubsystemConsole;
            else if (sub == "windows") options.subsystem = okayms::kSubsystemWindowsGui;
            else {
                fail("subsystem must be CONSOLE or WINDOWS");
                return 1;
            }
        } else if (key == "libpath") {
            libpaths.push_back(value);
        } else if (key == "map") {
            map_path = value;
        } else if (key == "export") {
            options.exports.push_back(value);
            options.dll = true;
            if (options.image_base == 0x140000000ULL) options.image_base = 0x180000000ULL;
        } else {
            fail("unknown flag: " + arg);
            return 1;
        }
    }
    if (!have_out) {
        fail("missing /OUT:");
        return 1;
    }
    if (inputs.empty()) {
        fail("no input files");
        return 1;
    }

    std::vector<okayms::ObjectFile> objects;
    std::vector<okayms::Archive> libraries;
    for (const std::string& name : inputs) {
        std::string path = name;
        if (!file_exists(path)) {
            bool found = false;
            for (const std::string& dir : libpaths) {
                const std::string candidate = dir + "/" + name;
                if (file_exists(candidate)) {
                    path = candidate;
                    found = true;
                    break;
                }
            }
            if (!found) {
                fail("cannot find " + name);
                return 1;
            }
        }
        std::string error;
        const std::vector<uint8_t> bytes = read_file(path, &error);
        if (!error.empty()) {
            fail(error);
            return 1;
        }
        if (bytes.size() >= 8 && std::memcmp(bytes.data(), "!<arch>\n", 8) == 0) {
            const auto archive = okayms::read_archive(bytes.data(), bytes.size());
            if (!archive.ok()) {
                fail(path + ": " + archive.error);
                return 1;
            }
            libraries.push_back(archive.value);
        } else {
            const auto object = okayms::read_coff(bytes.data(), bytes.size());
            if (!object.ok()) {
                fail(path + ": " + object.error);
                return 1;
            }
            objects.push_back(object.value);
        }
    }

    if (options.image_name.empty()) {
        const size_t slash = out_path.find_last_of('/');
        options.image_name = slash == std::string::npos ? out_path : out_path.substr(slash + 1);
    }
    std::string map_text;
    const auto image = okayms::link_with_libraries(std::move(objects), libraries, options,
                                                   map_path.empty() ? nullptr : &map_text);
    if (!image.ok()) {
        fail(image.error);
        return 1;
    }
    std::string error;
    if (!write_file(out_path, image.value, &error)) {
        fail(error);
        return 1;
    }
    if (!map_path.empty()) {
        std::ofstream map_out(map_path.c_str());
        if (!map_out) {
            fail("cannot write " + map_path);
            return 1;
        }
        map_out << map_text;
    }
    return 0;
}
