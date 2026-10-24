#include "okay/Modeling/MeshAsset.hpp"
#include "okay/Modeling/PolyMesh.hpp"

#include <cstring>
#include <fstream>

namespace okay::modeling {

namespace {
bool Exists(const std::string& p) { std::ifstream f(p, std::ios::binary); return f.good(); }

bool LoadOne(const std::string& path, Mesh& out, std::string* texture, std::string* error) {
    if (IsOkayMeshPath(path)) {
        PolyMesh pm;
        if (!PolyMesh::Load(path, pm, error)) return false;
        out = pm.ToMesh();
        if (out.vertices.empty()) { if (error) *error = path + " has no faces"; return false; }
        return true;
    }
    bool ok = false;
    Mesh m = Mesh::LoadOBJ(path, &ok, texture);
    if (!ok || m.vertices.empty()) { if (error) *error = "couldn't read " + path; return false; }
    out = std::move(m);
    return true;
}
} // namespace

bool LoadMeshAsset(const std::string& path, Mesh& out, std::string* texture, std::string* error, const std::string& fallbackDir) {
    if (path.empty()) { if (error) *error = "no mesh file"; return false; }
    if (Exists(path)) return LoadOne(path, out, texture, error);
    if (!fallbackDir.empty()) {
        const std::size_t slash = path.find_last_of("/\\");
        const std::string name = slash == std::string::npos ? path : path.substr(slash + 1);
        std::string dir = fallbackDir;
        if (dir.back() != '/' && dir.back() != '\\') dir += '/';
        if (Exists(dir + name)) return LoadOne(dir + name, out, texture, error);
        if (Exists(dir + path)) return LoadOne(dir + path, out, texture, error);
    }
    if (error) *error = "can't find " + path;
    return false;
}

bool SaveMeshAsset(const std::string& path, const Mesh& mesh, std::string* error) {
    if (mesh.vertices.empty() || mesh.triangles.empty()) { if (error) *error = "the mesh is empty"; return false; }
    return PolyMesh::FromMesh(mesh).Save(path, error);
}

std::uint64_t GeometryHash(const Mesh& mesh) {
    // FNV-1a over the raw numbers. Exact: any edit at all changes it.
    std::uint64_t h = 1469598103934665603ull;
    auto mix = [&](const void* data, std::size_t n) {
        const unsigned char* p = static_cast<const unsigned char*>(data);
        for (std::size_t i = 0; i < n; ++i) { h ^= p[i]; h *= 1099511628211ull; }
    };
    const std::size_t counts[4] = {mesh.vertices.size(), mesh.triangles.size(), mesh.uvs.size(), mesh.triColors.size()};
    mix(counts, sizeof(counts));
    if (!mesh.vertices.empty()) mix(mesh.vertices.data(), mesh.vertices.size() * sizeof(Vec3));
    if (!mesh.triangles.empty()) mix(mesh.triangles.data(), mesh.triangles.size() * sizeof(int));
    if (!mesh.uvs.empty()) mix(mesh.uvs.data(), mesh.uvs.size() * sizeof(Vec2));
    if (!mesh.triColors.empty()) mix(mesh.triColors.data(), mesh.triColors.size() * sizeof(Color));
    return h == 0 ? 1 : h;   // 0 means "not known" to callers
}

} // namespace okay::modeling
