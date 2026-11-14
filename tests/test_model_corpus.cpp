// Real-file validation of our own FBX, COLLADA, 3DS, BVH and Blender readers.
//
// The files are NOT in this repository: they come from a directory named by the environment variable
// OKAY_MODEL_CORPUS (for example the test/models tree of an Assimp source checkout; see docs/model_corpus.md).
// Without that variable this test prints "skipped" and passes. With it, every row of
// tests/data/model_corpus_manifest.tsv is read again and must give the recorded result: a file that read must
// still read with the same counts, a file recorded as failing must still fail (so a fix shows up as a manifest change).
//
//   test_model_corpus                       manifest check (what ctest runs)
//   test_model_corpus --one <reader> <file> print one TSV result line (used by the comparison scripts)
//   test_model_corpus --list <reader> <dir> print the result line of every file below <dir> for that reader
// readers: fbx-own, fbx-ufbx, dae, 3ds, bvh, obj, stl, ply, gltf (gltf covers .gltf and .glb), off, smd, md5, md5anim, ms3d, ase, blend
#include "test_framework.hpp"
#include <Okay.hpp>
#include "okay/Render/FbxReader.hpp"
#include "okay/Render/ModelData.hpp"
#include "okay/Render/ModelImport.hpp"
#include "okay/Render/Gltf.hpp"
#include "okay/Components/MeshRenderer.hpp"
#include "okay/Components/ModelAnimator.hpp"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <set>
#include <sstream>
#include <string>
#include <vector>

using namespace okay;
namespace fs = std::filesystem;

struct CorpusStats {
    bool ok = false;
    std::string err;
    int objects = 0, meshes = 0, skinned = 0, bones = 0, verts = 0, tris = 0, clips = 0, materials = -1, notes = 0;
    int textures = -1, nodes = -1;      // distinct texture files over all materials; node count (-1 = not measured)
    bool uv = false, nrm = false;       // some mesh has per-vertex UVs / normals
    bool measured = false;              // textures / nodes / uv / nrm were measured (obj, stl, ply, gltf)
    std::size_t keys = 0;
    double dur = 0, area = 0;
    Vec3 lo{0, 0, 0}, hi{0, 0, 0};
    double ms = 0;
};

static const char* ExtOf(const std::string& reader) {
    if (reader == "off") return ".off";
    if (reader == "smd") return ".smd";
    if (reader == "md5") return ".md5mesh";
    if (reader == "md5anim") return ".md5anim";
    if (reader == "ms3d") return ".ms3d";
    if (reader == "ase") return ".ase";
    return reader == "dae" ? ".dae" : reader == "3ds" ? ".3ds" : reader == "bvh" ? ".bvh" : reader == "obj" ? ".obj" :
           reader == "stl" ? ".stl" : reader == "ply" ? ".ply" : reader == "gltf" ? ".gltf" : reader == "blend" ? ".blend" : ".fbx";
}
static bool ExtMatches(const std::string& reader, std::string ext) {
    for (char& c : ext) c = (char)std::tolower((unsigned char)c);
    return ext == ExtOf(reader) || (reader == "gltf" && ext == ".glb");
}
static bool IsRawReader(const std::string& r) { return r == "dae" || r == "3ds" || r == "bvh" || r == "obj" || r == "stl" || r == "ply" || r == "off" || r == "smd" || r == "md5" || r == "md5anim" || r == "ms3d" || r == "ase" || r == "blend"; }

static CorpusStats MeasureFile(const std::string& reader, const std::string& path) {
    CorpusStats s;
    std::shared_ptr<ImportedModel> rawModel;   // dae / 3ds / bvh / blend: the reader's own result, before the importer rescales it
    const auto t0 = std::chrono::steady_clock::now();
    if (reader == "fbx-own") {
        fbx::Scene fs_; std::string e;
        if (!fbx::LoadFile(path, fs_, &e)) { s.err = e; return s; }   // the importer would fall back to ufbx here: report it
        fbx::SetOwnReaderEnabled(true);
    } else if (reader == "fbx-ufbx") {
        fbx::SetOwnReaderEnabled(false);
    } else if (IsRawReader(reader)) {
        SetImportBackend(ExtOf(reader), ImportBackend::Own);
        ImportedModel im; std::string e;
        if (!ImportModelData(path, im, ImportBackend::Own, false, &e)) { s.err = e; return s; }
        s.materials = (int)im.materials.size();
        s.notes = (int)im.notes.size();
        rawModel = std::make_shared<ImportedModel>(std::move(im));
    } else if (reader != "gltf") { s.err = "unknown reader " + reader; return s; }
    Scene scene; scene.physicsEnabled = false;
    bool ok = false;
    GameObject* root = ImportModelScene(scene, path, &ok);
    if (!ok || !root) { s.err = "scene import failed"; return s; }
    s.ok = true;
    ModelReport rep = AnalyzeModel(scene, root);
    s.objects = rep.objects; s.skinned = rep.skinnedMeshes; s.bones = rep.bones; s.clips = rep.clips; s.keys = rep.keys;
    bool any = false;
    std::set<std::string> sceneTex;
    for (const auto& up : scene.Objects()) {
        GameObject* go = up.get();
        if (!go || !go->IsSelfOrDescendantOf(root)) continue;
        auto* mr = go->GetComponent<MeshRenderer>();
        if (!mr || mr->mesh.vertices.empty()) continue;
        ++s.meshes; s.verts += (int)mr->mesh.vertices.size(); s.tris += mr->mesh.TriangleCount();
        if (mr->mesh.HasUVs()) s.uv = true;
        if (mr->mesh.HasNormals()) s.nrm = true;
        for (const std::string* t : {&mr->texture, &mr->normalMap, &mr->specularMap, &mr->aoMap, &mr->metallicRoughnessMap, &mr->emissiveMap})
            if (!t->empty()) sceneTex.insert(*t);
        const Mat4 w = go->transform->LocalToWorldMatrix();
        for (std::size_t t = 0; t + 2 < mr->mesh.triangles.size(); t += 3) {
            const Vec3 a = w.MultiplyPoint(mr->mesh.vertices[(std::size_t)mr->mesh.triangles[t]]);
            const Vec3 b = w.MultiplyPoint(mr->mesh.vertices[(std::size_t)mr->mesh.triangles[t + 1]]);
            const Vec3 c = w.MultiplyPoint(mr->mesh.vertices[(std::size_t)mr->mesh.triangles[t + 2]]);
            s.area += 0.5 * Vec3::Cross(b - a, c - a).Magnitude();
        }
        for (const Vec3& v : mr->mesh.vertices) {
            const Vec3 p = w.MultiplyPoint(v);
            if (!any) { s.lo = s.hi = p; any = true; }
            else { s.lo = Vec3{std::fmin(s.lo.x, p.x), std::fmin(s.lo.y, p.y), std::fmin(s.lo.z, p.z)}; s.hi = Vec3{std::fmax(s.hi.x, p.x), std::fmax(s.hi.y, p.y), std::fmax(s.hi.z, p.z)}; }
        }
    }
    if (reader == "gltf") {
        s.measured = true; s.nodes = rep.objects; s.textures = (int)sceneTex.size();
        const gltf_detail::GltfDoc doc = gltf_detail::LoadDoc(path);
        const gltf_detail::JVal* mats = doc.root.Find("materials");
        s.materials = mats ? (int)mats->arr.size() : 0;
    }
    if (reader == "gltf") {   // the importer scales a very large or tiny model to about two units (AutoNormalizeImportScale): undo it, the file's own size is what other readers report
        const double k = root->transform ? (double)root->transform->localScale.x : 1.0;
        if (k > 0 && std::fabs(k - 1.0) > 1e-9) {
            s.lo = Vec3{(float)(s.lo.x / k), (float)(s.lo.y / k), (float)(s.lo.z / k)}; s.hi = Vec3{(float)(s.hi.x / k), (float)(s.hi.y / k), (float)(s.hi.z / k)};
            s.area /= k * k;
        }
    }
    if (auto* ma = root->GetComponent<ModelAnimator>())
        for (const auto& c : ma->clips) for (const auto& n : c.nodes) s.dur = std::max(s.dur, (double)n.clip.Length());
    if (rawModel) {   // numbers from the reader's result in its own units: comparable with other readers' raw output
        const ImportedModel& im = *rawModel;
        s.meshes = (int)im.parts.size(); s.verts = 0; s.tris = 0; s.area = 0; s.clips = (int)im.clips.size(); s.dur = 0; s.keys = 0; any = false;
        std::vector<Mat4> world(im.nodes.size());
        for (std::size_t i = 0; i < im.nodes.size(); ++i) {
            const ImportedNode& n = im.nodes[i];
            const Mat4 l = Mat4::TRS(n.position, n.rotation, n.scale);
            world[i] = n.parent >= 0 ? world[(std::size_t)n.parent] * l : l;
        }
        std::set<std::string> bones;
        for (const ImportedPart& p : im.parts) {
            s.verts += (int)p.mesh.vertices.size(); s.tris += p.mesh.TriangleCount();
            for (const ImportedBone& b : p.bones) bones.insert(b.name);
            const Mat4 w = (p.node >= 0 && (std::size_t)p.node < world.size()) ? world[(std::size_t)p.node] : Mat4::Identity();
            for (std::size_t t = 0; t + 2 < p.mesh.triangles.size(); t += 3) {
                const Vec3 a = w.MultiplyPoint(p.mesh.vertices[(std::size_t)p.mesh.triangles[t]]);
                const Vec3 b = w.MultiplyPoint(p.mesh.vertices[(std::size_t)p.mesh.triangles[t + 1]]);
                const Vec3 c = w.MultiplyPoint(p.mesh.vertices[(std::size_t)p.mesh.triangles[t + 2]]);
                s.area += 0.5 * Vec3::Cross(b - a, c - a).Magnitude();
            }
            for (const Vec3& v : p.mesh.vertices) {
                const Vec3 q = w.MultiplyPoint(v);
                if (!any) { s.lo = s.hi = q; any = true; }
                else { s.lo = Vec3{std::fmin(s.lo.x, q.x), std::fmin(s.lo.y, q.y), std::fmin(s.lo.z, q.z)}; s.hi = Vec3{std::fmax(s.hi.x, q.x), std::fmax(s.hi.y, q.y), std::fmax(s.hi.z, q.z)}; }
            }
        }
        s.bones = (int)bones.size();
        if (reader == "obj" || reader == "stl" || reader == "ply" || reader == "off" || reader == "smd" || reader == "md5" || reader == "md5anim" || reader == "ms3d" || reader == "ase" || reader == "blend") {
            s.measured = true; s.nodes = (int)im.nodes.size();
            std::set<std::string> tex;
            for (const ImportedMaterial& m : im.materials)
                for (const std::string* t : {&m.texture, &m.normalMap, &m.specularMap, &m.aoMap}) if (!t->empty()) tex.insert(*t);
            s.textures = (int)tex.size();
            for (const ImportedPart& p : im.parts) { if (p.mesh.HasUVs()) s.uv = true; if (p.mesh.HasNormals()) s.nrm = true; }
        }
        for (const ImportedClip& c : im.clips) for (const ImportedChannel& ch : c.channels) {
            for (const auto& k : ch.position) s.dur = std::max(s.dur, (double)k.time);
            for (const auto& k : ch.rotation) s.dur = std::max(s.dur, (double)k.time);
            for (const auto& k : ch.scale) s.dur = std::max(s.dur, (double)k.time);
            s.keys += ch.position.size() + ch.rotation.size() + ch.scale.size();
        }
    }
    s.ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
    return s;
}

static std::string Line(const std::string& reader, const std::string& rel, const CorpusStats& s) {
    char b[600];
    if (!s.ok) {
        std::string e = s.err; for (char& c : e) if (c == '\t' || c == '\n') c = ' ';
        std::snprintf(b, sizeof b, "%s\t%s\tFAIL\t%s", reader.c_str(), rel.c_str(), e.c_str());
        return b;
    }
    std::snprintf(b, sizeof b, "%s\t%s\tok\tarea=%.4f\tmeshes=%d\ttris=%d\tbones=%d\tskinned=%d\tclips=%d\tdur=%.3f\tmat=%d\tbbox=%.3f,%.3f,%.3f,%.3f,%.3f,%.3f\tverts=%d\ttex=%d\tnodes=%d\tuv=%d\tnrm=%d\tms=%.0f",
                  reader.c_str(), rel.c_str(), s.area, s.meshes, s.tris, s.bones, s.skinned, s.clips, s.dur, s.materials,
                  s.lo.x, s.lo.y, s.lo.z, s.hi.x, s.hi.y, s.hi.z, s.verts, s.textures, s.nodes, s.uv ? 1 : 0, s.nrm ? 1 : 0, s.ms);
    return b;
}

static std::string Field(const std::string& line, const char* key) {
    const std::string k = std::string("\t") + key + "=";
    const std::size_t a = line.find(k);
    if (a == std::string::npos) return std::string();
    const std::size_t b = a + k.size();
    const std::size_t e = line.find('\t', b);
    return line.substr(b, e == std::string::npos ? std::string::npos : e - b);
}

int main(int argc, char** argv) {
    if (argc >= 4 && std::string(argv[1]) == "--one") {
        const std::string reader = argv[2], path = argv[3];
        std::cout << Line(reader, fs::path(path).filename().string(), MeasureFile(reader, path)) << std::endl;
        return 0;
    }
    if (argc >= 3 && std::string(argv[1]) == "--fbxdump") {   // what our FBX reader found, before the importer builds anything
        fbx::Scene sc; std::string e;
        if (!fbx::LoadFile(argv[2], sc, &e)) { std::cout << "FAIL " << e << "\n"; return 0; }
        std::cout << "creator '" << sc.creator << "' nodes " << sc.nodes.size() << " meshes " << sc.meshes.size() << " materials " << sc.materials.size()
                  << " textures " << sc.textures.size() << " clips " << sc.clips.size() << " unit " << sc.settings.unitScaleFactor << " up " << sc.settings.upAxis << "\n";
        for (const auto& c : sc.clips) {
            std::size_t keys = 0; for (const auto& n : c.nodes) keys += n.translation.size() + n.rotation.size() + n.scale.size();
            std::cout << "  clip '" << c.name << "' duration " << c.duration << " node tracks " << c.nodes.size() << " blend tracks " << c.blends.size() << " keys " << keys << "\n";
        }
        return 0;
    }
    if (argc >= 3 && std::string(argv[1]) == "--fbxdiff") {   // vertices one FBX reader has and the other lacks, per mesh object
        const std::string path = argv[2];
        fbx::SetOwnReaderEnabled(false);
        Scene su; GameObject* ru = ImportModelScene(su, path, nullptr);
        fbx::SetOwnReaderEnabled(true);
        Scene so; GameObject* ro = ImportModelScene(so, path, nullptr);
        auto key = [](const Mesh& m, std::size_t i) {
            char buf[200];
            std::snprintf(buf, sizeof buf, "%.4f,%.4f,%.4f|%.3f,%.3f,%.3f|%.3f,%.3f", m.vertices[i].x, m.vertices[i].y, m.vertices[i].z,
                          m.normals.size() > i ? m.normals[i].x : 0.f, m.normals.size() > i ? m.normals[i].y : 0.f, m.normals.size() > i ? m.normals[i].z : 0.f,
                          m.uvs.size() > i ? m.uvs[i].x : 0.f, m.uvs.size() > i ? m.uvs[i].y : 0.f);
            return std::string(buf);
        };
        for (const auto& up : su.Objects()) {
            if (!ru || !up->IsSelfOrDescendantOf(ru)) continue;
            auto* a = up->GetComponent<MeshRenderer>(); if (!a) continue;
            for (const auto& op : so.Objects()) {
                if (op->name != up->name || !ro || !op->IsSelfOrDescendantOf(ro)) continue;
                auto* b = op->GetComponent<MeshRenderer>(); if (!b) continue;
                std::printf("%s: ufbx %zu verts %d tris, own %zu verts %d tris\n", up->name.c_str(), a->mesh.vertices.size(), a->mesh.TriangleCount(), b->mesh.vertices.size(), b->mesh.TriangleCount());
                std::set<std::string> ka, kb;
                for (std::size_t i = 0; i < a->mesh.vertices.size(); ++i) ka.insert(key(a->mesh, i));
                for (std::size_t i = 0; i < b->mesh.vertices.size(); ++i) kb.insert(key(b->mesh, i));
                int shown = 0;
                for (const auto& k : ka) if (!kb.count(k) && shown++ < 6) std::printf("  only ufbx: %s\n", k.c_str());
                shown = 0;
                for (const auto& k : kb) if (!ka.count(k) && shown++ < 6) std::printf("  only own : %s\n", k.c_str());
            }
        }
        return 0;
    }
    if (argc >= 3 && std::string(argv[1]) == "--fbxclips") {   // clip contents of the two FBX readers side by side
        const std::string path = argv[2];
        Scene sc[2]; GameObject* rt[2];
        for (int i = 0; i < 2; ++i) { fbx::SetOwnReaderEnabled(i == 1); rt[i] = ImportModelScene(sc[i], path, nullptr); }
        ModelAnimator* ma[2] = {rt[0] ? rt[0]->GetComponent<ModelAnimator>() : nullptr, rt[1] ? rt[1]->GetComponent<ModelAnimator>() : nullptr};
        for (int i = 0; i < 2; ++i) { std::printf("%s: %zu clips\n", i ? "own" : "ufbx", ma[i] ? ma[i]->clips.size() : 0); if (ma[i]) for (auto& c : ma[i]->clips) { float len = 0; for (auto& n : c.nodes) len = std::max(len, n.clip.Length()); std::printf("   '%s' %zu node clips, length %.4f\n", c.name.c_str(), c.nodes.size(), len); } }
        if (ma[0] && ma[1]) for (std::size_t ci = 0; ci < std::min(ma[0]->clips.size(), ma[1]->clips.size()); ++ci)
            for (auto& nu : ma[0]->clips[ci].nodes) {
                const ModelAnimator::NodeClip* no = nullptr; for (auto& x : ma[1]->clips[ci].nodes) if (x.node == nu.node) no = &x;
                if (!no) { std::printf("  clip %zu node %s: missing in own\n", ci, nu.node.c_str()); continue; }
                for (const char* tr : {"position.x", "position.y", "position.z", "rotation.qx", "rotation.qy", "rotation.qz", "rotation.qw", "scale.x"}) {
                    std::printf("  clip %zu %-12s %-11s", ci, nu.node.c_str(), tr);
                    for (float t : {0.0f, 0.25f, 0.5f, 1.0f, 1.5f}) { bool f1 = false, f2 = false; const float a = nu.clip.Evaluate(tr, t, f1), b = no->clip.Evaluate(tr, t, f2); std::printf(" | t=%.2f %s%.3f %s%.3f", t, f1 ? "" : "-", a, f2 ? "" : "-", b); }
                    std::printf("\n");
                }
            }
        return 0;
    }
    if (argc >= 4 && std::string(argv[1]) == "--keys") {   // every animation channel: counts and first / last key (reader result, no importer)
        SetImportBackend(ExtOf(argv[2]), ImportBackend::Own);
        ImportedModel im; std::string e;
        if (!ImportModelData(argv[3], im, ImportBackend::Own, false, &e)) { std::cout << "FAIL " << e << "\n"; return 0; }
        for (const ImportedClip& c : im.clips) for (const ImportedChannel& ch : c.channels) {
            std::printf("%s\t%s\tpos %zu rot %zu scl %zu", c.name.c_str(), ch.node.c_str(), ch.position.size(), ch.rotation.size(), ch.scale.size());
            if (!ch.position.empty()) std::printf("\tpos0 %.4f,%.4f,%.4f posN t=%.4f %.4f,%.4f,%.4f", ch.position.front().value.x, ch.position.front().value.y, ch.position.front().value.z, ch.position.back().time, ch.position.back().value.x, ch.position.back().value.y, ch.position.back().value.z);
            if (!ch.rotation.empty()) std::printf("\trot0 %.4f,%.4f,%.4f,%.4f rotN t=%.4f %.4f,%.4f,%.4f,%.4f", ch.rotation.front().value.x, ch.rotation.front().value.y, ch.rotation.front().value.z, ch.rotation.front().value.w, ch.rotation.back().time, ch.rotation.back().value.x, ch.rotation.back().value.y, ch.rotation.back().value.z, ch.rotation.back().value.w);
            std::printf("\n");
        }
        return 0;
    }
    if (argc >= 4 && std::string(argv[1]) == "--list") {
        const std::string reader = argv[2], dir = argv[3];
        std::vector<std::string> files;
        for (auto& e : fs::recursive_directory_iterator(dir)) {
            if (!e.is_regular_file()) continue;
            if (ExtMatches(reader, e.path().extension().string())) files.push_back(e.path().string());
        }
        std::sort(files.begin(), files.end());
        for (const std::string& f : files) std::cout << Line(reader, fs::relative(f, dir).generic_string(), MeasureFile(reader, f)) << std::endl;
        return 0;
    }

    RUN_SUITE("model_corpus");
    const char* root = std::getenv("OKAY_MODEL_CORPUS");
    if (!root || !*root) { std::cout << "OKAY_MODEL_CORPUS not set: real-file corpus skipped\n"; TEST_MAIN_RESULT(); }
    std::ifstream f(std::string(OKAY_SOURCE_DIR) + "/tests/data/model_corpus_manifest.tsv");
    CHECK(f.good());
    std::string line;
    int rows = 0, read = 0, failing = 0;
    while (std::getline(f, line)) {
        if (line.empty() || line[0] == '#') continue;
        std::vector<std::string> col;
        { std::stringstream ss(line); std::string c; while (std::getline(ss, c, '\t')) col.push_back(c); }
        if (col.size() < 3) continue;
        ++rows;
        const std::string path = (fs::path(root) / col[1]).string();
        if (!fs::exists(path)) { std::cout << "  missing " << col[1] << " (is OKAY_MODEL_CORPUS the right tree?)\n"; CHECK(false); continue; }
        const CorpusStats s = MeasureFile(col[0], path);
        if (col[2] == "ok") {
            ++read;
            CHECK(s.ok);
            if (!s.ok) { std::cout << "  " << col[1] << ": expected to read, got: " << s.err << "\n"; continue; }
            const std::string got = Line(col[0], col[1], s);
            for (const char* k : {"meshes", "tris", "bones", "skinned", "clips", "mat", "tex", "nodes", "uv", "nrm"}) {
                if (Field(line, k).empty()) continue;   // older rows do not carry every field
                const bool same = Field(got, k) == Field(line, k);
                CHECK(same);
                if (!same) std::cout << "  " << col[1] << " (" << col[0] << "): " << k << " " << Field(line, k) << " -> " << Field(got, k) << "\n";
            }
        } else {
            ++failing;
            CHECK(!s.ok);
            if (s.ok) std::cout << "  " << col[1] << " (" << col[0] << "): recorded as failing but now reads; update the manifest\n";
        }
    }
    std::cout << rows << " manifest rows: " << read << " read, " << failing << " recorded as failing\n";
    TEST_MAIN_RESULT();
}
