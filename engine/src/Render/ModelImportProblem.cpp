// DescribeModelImportProblem: the checks run in order of cost, and the first that finds something answers. Written from
// the formats' own rules only (glTF 2.0 spec: a .glb starts with the magic "glTF" and a 12-byte header; a .gltf is JSON).
#include "okay/Render/ModelImportProblem.hpp"
#include "okay/Render/ModelImport.hpp"
#include "okay/Render/ModelData.hpp"
#include "okay/Render/FbxReader.hpp"

#include <cctype>
#include <cstdio>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <fstream>
#include <system_error>

namespace okay {

namespace {
namespace fs = std::filesystem;

std::string Lower(std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}
bool EndsWith(const std::string& s, const std::string& end) {
    return s.size() >= end.size() && s.compare(s.size() - end.size(), end.size(), end) == 0;
}
// The extension as the import table sees it: the longest listed one the name ends with (".mesh.xml" over ".xml").
std::string ListedExtension(const std::string& lowerName, const std::string& list) {
    std::string best, cur;
    const std::string l = list + " ";
    for (char c : l) {
        if (c != ' ') { cur += c; continue; }
        if (!cur.empty() && EndsWith(lowerName, cur) && cur.size() > best.size()) best = cur;
        cur.clear();
    }
    return best;
}
std::string Megabytes(std::uintmax_t b) { return std::to_string((b + 512 * 1024) / (1024 * 1024)) + " MB"; }
} // namespace

std::string DescribeModelImportProblem(const std::string& path) {
    try {
        if (path.empty()) return "no file was given";
        const fs::path p = fs::u8path(path);
        std::error_code ec;
        const fs::file_status st = fs::status(p, ec);
        if (ec || !fs::exists(st)) return "the file is not there (it was moved, renamed or deleted)";
        if (fs::is_directory(st)) return "it is a folder, not a model file";
        if (!fs::is_regular_file(st)) return "it is not an ordinary file";
        const std::uintmax_t size = fs::file_size(p, ec);
        if (ec) return "its size can't be read (" + ec.message() + ")";
        if (size == 0) return "the file is empty (0 bytes): the program that saved it may have stopped half way";

        const std::string name = Lower(p.filename().u8string());
        const std::string list = ImportableExtensions();
        const std::string ext = ListedExtension(name, list);
        if (ext.empty()) {
            const std::string dot = p.extension().u8string();
            return dot.empty() ? std::string("the file name has no extension, so the format is not known; this build reads ") + list
                               : "this build does not read " + Lower(dot) + " files; it reads " + list;
        }
        std::ifstream in(p, std::ios::binary);
        if (!in) return "the file can't be opened (another program may be using it, or there is no permission to read it)";
        char head[16] = {0};
        in.read(head, sizeof(head));
        const std::size_t got = static_cast<std::size_t>(in.gcount());

        if (ext == ".glb") {
            if (got < 12) return "it is too short to be a .glb file (" + std::to_string(size) + " bytes; the header alone is 12)";
            if (std::string(head, 4) != "glTF") {
                if (got >= 1 && head[0] == '{') return "it is a text .gltf file saved with the .glb extension: rename it to .gltf";
                return "it is not a .glb file: it does not start with \"glTF\" (another kind of file renamed to .glb?)";
            }
            const std::uint32_t version = static_cast<unsigned char>(head[4]) | (static_cast<unsigned char>(head[5]) << 8) |
                                          (static_cast<unsigned char>(head[6]) << 16) | (static_cast<std::uint32_t>(static_cast<unsigned char>(head[7])) << 24);
            const std::uint32_t length = static_cast<unsigned char>(head[8]) | (static_cast<unsigned char>(head[9]) << 8) |
                                         (static_cast<unsigned char>(head[10]) << 16) | (static_cast<std::uint32_t>(static_cast<unsigned char>(head[11])) << 24);
            if (version != 2) return "it is a glTF " + std::to_string(version) + " binary file; only glTF 2.0 is read (export it again as glTF 2.0)";
            if (length > size) return "the file is cut off: its header says " + std::to_string(length) + " bytes, but it has " + std::to_string(size);
            return "the glTF reader could not make sense of it (damaged, or it needs an extension this reader does not have; the Console line above may say which)";
        }
        if (ext == ".gltf") {
            std::size_t i = 0;
            if (got >= 3 && static_cast<unsigned char>(head[0]) == 0xEF && static_cast<unsigned char>(head[1]) == 0xBB && static_cast<unsigned char>(head[2]) == 0xBF) i = 3;
            while (i < got && std::isspace(static_cast<unsigned char>(head[i]))) ++i;
            if (got >= 4 && std::string(head, 4) == "glTF") return "it is a binary .glb file saved with the .gltf extension: rename it to .glb";
            if (i < got && head[i] != '{') return "it is not a glTF file: a .gltf is text that starts with '{'";
            return "the glTF reader could not make sense of it (damaged JSON, a missing .bin or texture file next to it, or glTF 1.0; the Console line above may say which)";
        }
        if (ext == ".fbx") {   // read by its own built-in path, not the seam
            const bool binary = got >= 16 && std::string(head, 16) == "Kaydara FBX Bina";
            bool text = false;
            if (!binary) {   // an ASCII FBX starts with comment lines ("; FBX 7.4.0 project file") or the header record
                std::string first(head, got);
                text = first.find(';') != std::string::npos || first.find("FBXHeader") != std::string::npos;
            }
            if (!binary && !text) return "it is not an FBX file: it does not start like a binary or a text FBX (another kind of file renamed to .fbx?)";
            // The version the file says it is: bytes 23 to 26 of a binary file, the "; FBX 6.1.0" comment of a text one.
            std::uint32_t version = 0;
            {
                std::ifstream again(p, std::ios::binary);
                char h[64] = {0};
                again.read(h, sizeof(h));
                const std::size_t n = static_cast<std::size_t>(again.gcount());
                if (binary && n >= 27)
                    version = static_cast<unsigned char>(h[23]) | (static_cast<unsigned char>(h[24]) << 8) | (static_cast<unsigned char>(h[25]) << 16) |
                              (static_cast<std::uint32_t>(static_cast<unsigned char>(h[26])) << 24);
                else if (!binary && n > 8 && std::string(h, 6) == "; FBX ") {
                    int a = -1, b = -1;
                    std::string line(h + 6, n - 6);
                    if (std::sscanf(line.c_str(), "%d.%d", &a, &b) == 2 && a >= 0 && a < 100 && b >= 0 && b < 10) version = static_cast<std::uint32_t>(a * 1000 + b * 100);
                }
            }
            if (version && (version < 6000 || version > 9999)) return fbx::UnsupportedVersionMessage(version);
            // Otherwise the reader itself says what is wrong.
            fbx::Scene sc; std::string why;
            if (!fbx::LoadFile(path, sc, &why))
                return "the FBX reader could not read it: " + (why.empty() ? std::string("damaged or cut off, or larger than the reader allows") : why);
            if (sc.nodes.empty()) return "the file was read but holds no objects (no models, meshes or bones)";
            if (sc.meshes.empty()) return "the file was read but holds no meshes (only empty nodes, bones, cameras, lights or animation)";
            std::string err;
            if (!fbx::BuildParts(sc, &err) || !fbx::BuildClips(sc, &err)) return "the FBX reader could not read it: " + err;
            return "the file was read but no object could be made from it";
        }
        if (size > (std::uintmax_t(2) << 30)) return "it is very large (" + Megabytes(size) + "); the readers stop at files this size";

        // Everything else is read through the import seam, which gives the reader's own reason.
        ImportedModel m;
        std::string err;
        if (!ImportModelData(path, m, ImportBackend::Default, false, &err))
            return err.empty() ? "the " + ext + " reader could not read it (damaged, or written by an exporter this reader does not know)" : err;
        if (m.parts.empty()) return "the file was read but holds no meshes (only empty nodes, cameras, lights or animation)";
        return "the file was read but no object could be made from it";
    } catch (const std::exception& e) {
        return std::string("it could not be checked: ") + e.what();
    } catch (...) {
        return "it could not be checked";
    }
}

} // namespace okay
