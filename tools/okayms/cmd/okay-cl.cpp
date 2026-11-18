// okay-cl: /c /Fo /Fe /I /D, and /link for the linker flags.
// It writes COFF with the compiler in this tree. It does not call okaycc.

#include "okayms/compile.hpp"
#include "okayms/link.hpp"

#include <cctype>
#include <cstring>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

namespace {

std::string lower_copy(std::string text) {
    for (char& c : text) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
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

std::string read_text(const std::string& path, std::string* error) {
    const std::vector<uint8_t> bytes = read_file(path, error);
    if (!error->empty()) return {};
    return std::string(bytes.begin(), bytes.end());
}

bool write_file(const std::string& path, const std::vector<uint8_t>& bytes, std::string* error) {
    std::ofstream out(path.c_str(), std::ios::binary);
    if (!out) {
        *error = "cannot write " + path;
        return false;
    }
    out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    return static_cast<bool>(out);
}

void fail(const std::string& message) { std::cerr << "okay-cl: " << message << "\n"; }

std::string replace_ext(std::string path, const char* ext) {
    const size_t slash = path.find_last_of('/');
    const size_t dot = path.find_last_of('.');
    if (dot != std::string::npos && (slash == std::string::npos || dot > slash)) path.resize(dot);
    return path + ext;
}

}  // namespace

int main(int argc, char** argv) {
    bool compile_only = false;
    bool make_dll = false;
    std::string object_out;
    std::string exe_out;
    okayms::CompileInput common;
    okayms::LinkOptions link;
    std::vector<std::string> sources;
    std::vector<std::string> objects_in;
    std::vector<std::string> libs_in;
    std::vector<std::string> libpaths;

    auto take_value = [&](int* index, const std::string& arg, const std::string& body, const std::string& key) -> std::string {
        if (body.size() > key.size() && body[key.size()] == ':') return body.substr(key.size() + 1);
        if (body.size() > key.size()) return body.substr(key.size());
        if (*index + 1 < argc && argv[*index + 1][0] != '/') {
            ++(*index);
            return argv[*index];
        }
        fail("flag needs a value: " + arg);
        return {};
    };
    auto set_subsystem = [&](const std::string& value) {
        const std::string sub = lower_copy(value);
        if (sub == "console") link.subsystem = okayms::kSubsystemConsole;
        else if (sub == "windows") link.subsystem = okayms::kSubsystemWindowsGui;
        else {
            fail("subsystem must be CONSOLE or WINDOWS");
            return false;
        }
        return true;
    };

    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg.empty()) continue;
        if (lower_copy(arg) == "/link" || lower_copy(arg) == "-link") {
            for (int j = i + 1; j < argc; ++j) {
                const std::string extra = argv[j];
                if (extra.empty() || extra[0] != '/' || file_exists(extra)) {
                    objects_in.push_back(extra);
                    continue;
                }
                std::string body = extra;
                while (!body.empty() && (body[0] == '/' || body[0] == '-')) body.erase(body.begin());
                const std::string low = lower_copy(body);
                std::string key;
                if (low.compare(0, 9, "subsystem") == 0) key = "subsystem";
                else if (low.compare(0, 7, "libpath") == 0) key = "libpath";
                else if (low.compare(0, 5, "entry") == 0) key = "entry";
                else if (low.compare(0, 3, "out") == 0) key = "out";
                else {
                    fail("unknown flag: " + extra);
                    return 1;
                }
                const std::string value = body.size() > key.size() && body[key.size()] == ':' ? body.substr(key.size() + 1)
                                                                                               : body.substr(key.size());
                if (value.empty()) {
                    fail("flag needs a value: " + extra);
                    return 1;
                }
                if (key == "out") exe_out = value;
                else if (key == "entry") link.entry = value;
                else if (key == "libpath") libpaths.push_back(value);
                else if (!set_subsystem(value)) return 1;
            }
            break;
        }
        if ((arg[0] == '/' || arg[0] == '-') && !file_exists(arg)) {
            std::string body = arg;
            while (!body.empty() && (body[0] == '/' || body[0] == '-')) body.erase(body.begin());
            const std::string low = lower_copy(body);
            if (low == "c") {
                compile_only = true;
                continue;
            }
            if (low == "o1") {
                common.optimize = true;
                common.opt_level = 1;
                continue;
            }
            if (low == "o2") {
                common.optimize = true;
                common.opt_level = 2;
                continue;
            }
            if (low == "ld") {
                make_dll = true;
                link.dll = true;
                if (link.image_base == 0x140000000ULL) link.image_base = 0x180000000ULL;
                continue;
            }
            std::string key;
            if (low.compare(0, 9, "subsystem") == 0) key = "subsystem";
            else if (low.compare(0, 7, "libpath") == 0) key = "libpath";
            else if (low.compare(0, 5, "entry") == 0) key = "entry";
            else if (low.compare(0, 2, "fo") == 0) key = "fo";
            else if (low.compare(0, 2, "fe") == 0) key = "fe";
            else if (low.compare(0, 1, "i") == 0) key = "i";
            else if (low.compare(0, 1, "d") == 0) key = "d";
            else {
                fail("unknown flag: " + arg);
                return 1;
            }
            const std::string value = take_value(&i, arg, body, key);
            if (value.empty()) return 1;
            if (key == "fo") object_out = value;
            else if (key == "fe") exe_out = value;
            else if (key == "i") common.include_paths.push_back(value);
            else if (key == "d") common.defines.push_back(value);
            else if (key == "entry") link.entry = value;
            else if (key == "libpath") libpaths.push_back(value);
            else if (!set_subsystem(value)) return 1;
            continue;
        }
        const std::string low = lower_copy(arg);
        if (low.size() >= 2 && low.compare(low.size() - 2, 2, ".c") == 0) sources.push_back(arg);
        else if (low.size() >= 4 && low.compare(low.size() - 4, 4, ".lib") == 0) libs_in.push_back(arg);
        else objects_in.push_back(arg);
    }

    if (sources.empty() && objects_in.empty() && libs_in.empty()) {
        fail("no input files");
        return 1;
    }
    if (compile_only && sources.size() > 1 && !object_out.empty()) {
        fail("/Fo names one object; compile one source at a time");
        return 1;
    }

    std::vector<okayms::ObjectFile> objects;
    std::vector<std::string> exports;
    for (const std::string& source : sources) {
        std::string error;
        okayms::CompileInput input = common;
        input.name = source;
        input.source = read_text(source, &error);
        if (!error.empty()) {
            fail(error);
            return 1;
        }
        const auto compiled = okayms::compile_c(input);
        if (!compiled.ok()) {
            fail(source + ": " + compiled.error);
            return 1;
        }
        if (compile_only) {
            const std::string path = !object_out.empty() ? object_out : replace_ext(source, ".obj");
            const auto bytes = okayms::write_coff(compiled.value.object);
            if (!bytes.ok()) {
                fail(bytes.error);
                return 1;
            }
            if (!write_file(path, bytes.value, &error)) {
                fail(error);
                return 1;
            }
            object_out.clear();
        } else {
            objects.push_back(compiled.value.object);
            exports.insert(exports.end(), compiled.value.exports.begin(), compiled.value.exports.end());
        }
    }
    if (compile_only) return 0;

    auto resolve = [&](std::string name, std::string* error) {
        if (file_exists(name)) return name;
        for (const std::string& dir : libpaths) {
            const std::string candidate = dir + "/" + name;
            if (file_exists(candidate)) return candidate;
        }
        *error = "cannot find " + name;
        return std::string();
    };

    std::vector<okayms::Archive> libraries;
    std::string error;
    for (const std::string& name : objects_in) {
        const std::string path = resolve(name, &error);
        if (!error.empty()) {
            fail(error);
            return 1;
        }
        const std::vector<uint8_t> bytes = read_file(path, &error);
        if (!error.empty()) {
            fail(error);
            return 1;
        }
        if (bytes.size() >= 8 && std::memcmp(bytes.data(), "!<arch>\n", 8) == 0) {
            const auto archive = okayms::read_archive(bytes.data(), bytes.size());
            if (!archive.ok()) {
                fail(archive.error);
                return 1;
            }
            libraries.push_back(archive.value);
        } else {
            const auto object = okayms::read_coff(bytes.data(), bytes.size());
            if (!object.ok()) {
                fail(object.error);
                return 1;
            }
            objects.push_back(object.value);
        }
    }
    for (const std::string& name : libs_in) {
        const std::string path = resolve(name, &error);
        if (!error.empty()) {
            fail(error);
            return 1;
        }
        const std::vector<uint8_t> bytes = read_file(path, &error);
        if (!error.empty()) {
            fail(error);
            return 1;
        }
        const auto archive = okayms::read_archive(bytes.data(), bytes.size());
        if (!archive.ok()) {
            fail(archive.error);
            return 1;
        }
        libraries.push_back(archive.value);
    }
    if (objects.empty()) {
        fail("no object to link");
        return 1;
    }
    if (exe_out.empty()) exe_out = sources.empty() ? (make_dll ? "a.dll" : "a.exe") : replace_ext(sources[0], make_dll ? ".dll" : ".exe");
    if (make_dll) {
        link.dll = true;
        link.exports = exports;
        const size_t slash = exe_out.find_last_of('/');
        link.image_name = slash == std::string::npos ? exe_out : exe_out.substr(slash + 1);
    }
    const auto image = okayms::link_with_libraries(std::move(objects), libraries, link);
    if (!image.ok()) {
        fail(image.error);
        return 1;
    }
    if (!write_file(exe_out, image.value, &error)) {
        fail(error);
        return 1;
    }
    return 0;
}
