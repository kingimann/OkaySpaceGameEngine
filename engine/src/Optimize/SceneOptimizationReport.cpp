// Scene optimization report. See SceneOptimizationReport.hpp.
#include "okay/Optimize/SceneOptimizationReport.hpp"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <map>
#include <set>

namespace okay::optimize {
namespace {

std::string Fmt(const char* f, double a) { char b[96]; std::snprintf(b, sizeof(b), f, a); return b; }
std::string Num(double v) { return Fmt("%.1f", v); }
std::string Int(long long v) { return std::to_string(v); }

std::string NameOf(const OptScene& s, int i) {
    return s.objects[i].name.empty() ? "#" + std::to_string(i) : s.objects[i].name;
}

void List(Finding& f, const OptScene& s, int maxListed) {
    for (int i : f.objects) {
        if (static_cast<int>(f.listed.size()) >= maxListed) break;
        f.listed.push_back(NameOf(s, i));
    }
}

float LargestExtent(const Bounds& b) { return std::max(b.extents.x, std::max(b.extents.y, b.extents.z)) * 2.0f; }

int NextPow2(int v) { int p = 1; while (p < v && p < (1 << 20)) p <<= 1; return p; }

std::string JsonEscape(const std::string& s) {
    std::string o;
    for (unsigned char c : s) {
        switch (c) {
            case '"': o += "\\\""; break;
            case '\\': o += "\\\\"; break;
            case '\n': o += "\\n"; break;
            case '\r': o += "\\r"; break;
            case '\t': o += "\\t"; break;
            default:
                if (c < 0x20) { char b[8]; std::snprintf(b, sizeof(b), "\\u%04x", c); o += b; }
                else o += static_cast<char>(c);
        }
    }
    return o;
}

}  // namespace

const char* SeverityName(Severity s) {
    switch (s) { case Severity::Info: return "info"; case Severity::Warning: return "warning"; case Severity::Critical: return "critical"; }
    return "info";
}

const char* FindingKindName(FindingKind k) {
    switch (k) {
        case FindingKind::TinyDrawCalls: return "tiny_draw_calls";
        case FindingKind::HugeTextureSmallObject: return "huge_texture_small_object";
        case FindingKind::HighPolyNoLod: return "high_poly_no_lod";
        case FindingKind::DuplicateMaterials: return "duplicate_materials";
        case FindingKind::OverdrawRisk: return "overdraw_risk";
        case FindingKind::TooManyDrawCalls: return "too_many_draw_calls";
        case FindingKind::TextureMemory: return "texture_memory";
    }
    return "unknown";
}

int OptimizationReport::Count(Severity s) const {
    int n = 0;
    for (const Finding& f : findings) if (f.severity == s) ++n;
    return n;
}

const Finding* OptimizationReport::Get(FindingKind k) const {
    for (const Finding& f : findings) if (f.kind == k) return &f;
    return nullptr;
}

OptimizationReport AnalyzeScene(const OptScene& scene, const OptSettings& set) {
    OptimizationReport rep;
    const int n = static_cast<int>(scene.objects.size());
    SceneStats& st = rep.stats;
    st.objects = n;
    st.materials = static_cast<int>(scene.materials.size());
    st.estimatedDrawCalls = n;
    std::set<std::string> seenTex;
    double texBytes = 0.0;
    for (const OptObject& o : scene.objects) {
        st.triangles += o.triangles;
        if (o.isStatic) ++st.staticObjects;
        if (o.transparent) ++st.transparentObjects;
        for (const OptTexture& t : o.textures) {
            if (t.name.empty() || seenTex.insert(t.name).second) texBytes += t.Bytes();
        }
    }
    st.uniqueTextures = static_cast<int>(seenTex.size());
    st.textureMB = static_cast<float>(texBytes / (1024.0 * 1024.0));

    // --- tiny draw calls ------------------------------------------------------
    {
        Finding f;
        f.kind = FindingKind::TinyDrawCalls;
        int tinyStatic = 0;
        std::set<int> tinyMaterials;
        for (int i = 0; i < n; ++i) {
            const OptObject& o = scene.objects[i];
            if (o.triangles > 0 && o.triangles <= set.tinyTriangleLimit && !o.transparent) {
                f.objects.push_back(i);
                if (o.isStatic) ++tinyStatic;
                tinyMaterials.insert(o.materialId);
            }
        }
        const int count = static_cast<int>(f.objects.size());
        if (count >= set.tinyWarnCount / 2 && count > 0) {
            f.severity = count >= set.tinyCriticalCount ? Severity::Critical
                       : (count >= set.tinyWarnCount ? Severity::Warning : Severity::Info);
            f.title = Int(count) + " tiny draw calls";
            f.detail = Int(count) + " objects have " + Int(set.tinyTriangleLimit) + " triangles or fewer, so each draw call "
                       "costs far more than the geometry it draws. " + Int(tinyStatic) + " of them are static and use " +
                       Int(static_cast<long long>(tinyMaterials.size())) + " material(s).";
            f.suggestion = "Merge static ones with MergeStaticMeshes / PlanBatches, and instance repeated meshes.";
            f.impact = static_cast<float>(count);
            List(f, scene, set.maxListed);
            rep.findings.push_back(std::move(f));
        }
    }

    // --- huge textures on small objects ------------------------------------
    {
        Finding f;
        f.kind = FindingKind::HugeTextureSmallObject;
        double wasted = 0.0;
        for (int i = 0; i < n; ++i) {
            const OptObject& o = scene.objects[i];
            if (o.textures.empty()) continue;
            const float useful = o.screenPixels > 0.0f ? o.screenPixels : LargestExtent(o.bounds) * set.recommendedTexelsPerUnit;
            if (useful <= 0.0f) continue;
            bool flagged = false;
            for (const OptTexture& t : o.textures) {
                const int dim = std::max(t.width, t.height);
                if (dim < set.hugeTextureSize) continue;
                const int wanted = std::max(NextPow2(static_cast<int>(std::ceil(useful))), 64);
                if (static_cast<float>(dim) < set.hugeTextureRatio * static_cast<float>(wanted)) continue;
                const float scale = static_cast<float>(wanted) / static_cast<float>(dim);
                wasted += static_cast<double>(t.Bytes()) * (1.0 - static_cast<double>(scale) * static_cast<double>(scale));
                flagged = true;
            }
            if (flagged) f.objects.push_back(i);
        }
        if (!f.objects.empty()) {
            const double mb = wasted / (1024.0 * 1024.0);
            f.severity = mb >= set.wasteCriticalMB ? Severity::Critical : (mb >= set.wasteWarnMB ? Severity::Warning : Severity::Info);
            f.title = "Huge textures on small objects";
            f.detail = Int(static_cast<long long>(f.objects.size())) + " object(s) use textures far larger than they can show on screen, "
                       "wasting about " + Num(mb) + " MB.";
            f.suggestion = "Lower the import size or cap the mip level (MipBudget) for these textures.";
            f.impact = static_cast<float>(mb);
            List(f, scene, set.maxListed);
            rep.findings.push_back(std::move(f));
        }
    }

    // --- high poly without LOD ---------------------------------------------
    {
        Finding f;
        f.kind = FindingKind::HighPolyNoLod;
        long long tris = 0;
        int worst = 0;
        for (int i = 0; i < n; ++i) {
            const OptObject& o = scene.objects[i];
            if (o.triangles >= set.highPolyTriangles && !o.hasLod) {
                f.objects.push_back(i);
                tris += o.triangles;
                worst = std::max(worst, o.triangles);
            }
        }
        if (!f.objects.empty()) {
            f.severity = worst >= set.highPolyCritical ? Severity::Critical : Severity::Warning;
            f.title = "High-poly objects without LOD";
            f.detail = Int(static_cast<long long>(f.objects.size())) + " object(s) have " + Int(set.highPolyTriangles) +
                       " triangles or more and no LOD chain (" + Int(tris) + " triangles together, the densest has " + Int(worst) + ").";
            f.suggestion = "Run GenerateLodChain on them and use a LodGroup so distant copies draw far fewer triangles.";
            f.impact = static_cast<float>(tris) / 1000.0f;
            List(f, scene, set.maxListed);
            rep.findings.push_back(std::move(f));
        }
    }

    // --- duplicate materials -----------------------------------------------
    if (!scene.materials.empty()) {
        std::map<std::pair<std::string, std::string>, std::vector<int>> groups;     // (shader, signature) -> material indices
        for (int i = 0; i < static_cast<int>(scene.materials.size()); ++i)
            groups[std::make_pair(scene.materials[i].shader, scene.materials[i].signature)].push_back(i);
        std::map<int, int> usage;                                                    // material id -> object count
        for (const OptObject& o : scene.objects) ++usage[o.materialId];
        struct G { std::vector<int> mats; int objects; };
        std::vector<G> dup;
        for (auto& kv : groups) {
            if (kv.second.size() < 2) continue;
            G g;
            g.mats = kv.second;
            g.objects = 0;
            for (int m : g.mats) g.objects += usage[scene.materials[m].id];
            dup.push_back(g);
        }
        std::sort(dup.begin(), dup.end(), [](const G& a, const G& b) {
            if (a.objects != b.objects) return a.objects > b.objects;
            return a.mats.front() < b.mats.front();
        });
        int shown = 0;
        for (const G& g : dup) {
            if (shown++ >= 5) break;
            Finding f;
            f.kind = FindingKind::DuplicateMaterials;
            f.severity = (g.mats.size() >= 4 || g.objects >= 20) ? Severity::Warning : Severity::Info;
            std::string names;
            for (std::size_t k = 0; k < g.mats.size() && k < 4; ++k) {
                if (k) names += ", ";
                names += scene.materials[g.mats[k]].name.empty() ? "#" + Int(scene.materials[g.mats[k]].id) : scene.materials[g.mats[k]].name;
            }
            if (g.mats.size() > 4) names += ", ...";
            f.title = Int(static_cast<long long>(g.mats.size())) + " identical materials";
            f.detail = "Materials " + names + " share the same shader and settings but are separate, so objects using them "
                       "cannot be batched together (" + Int(g.objects) + " object(s) affected).";
            f.suggestion = "Point the objects at one shared material.";
            f.impact = static_cast<float>(g.objects);
            for (int i = 0; i < n; ++i)
                for (int m : g.mats) if (scene.objects[i].materialId == scene.materials[m].id) { f.objects.push_back(i); break; }
            List(f, scene, set.maxListed);
            rep.findings.push_back(std::move(f));
        }
    }

    // --- overdraw risk -----------------------------------------------------
    {
        struct T { float lo[3], hi[3]; int index; };
        std::vector<T> ts;
        for (int i = 0; i < n; ++i) {
            const OptObject& o = scene.objects[i];
            if (!o.transparent) continue;
            const Vec3 mn = o.bounds.Min(), mx = o.bounds.Max();
            ts.push_back({{mn.x, mn.y, mn.z}, {mx.x, mx.y, mx.z}, i});
        }
        std::sort(ts.begin(), ts.end(), [](const T& a, const T& b) {
            if (a.lo[0] != b.lo[0]) return a.lo[0] < b.lo[0];
            return a.index < b.index;
        });
        std::vector<int> layers(ts.size(), 1);
        long long ops = 0;
        for (std::size_t i = 0; i < ts.size() && ops < 4000000; ++i)
            for (std::size_t j = i + 1; j < ts.size() && ts[j].lo[0] <= ts[i].hi[0]; ++j, ++ops) {
                if (ts[j].lo[1] <= ts[i].hi[1] && ts[j].hi[1] >= ts[i].lo[1] &&
                    ts[j].lo[2] <= ts[i].hi[2] && ts[j].hi[2] >= ts[i].lo[2]) { ++layers[i]; ++layers[j]; }
            }
        int worst = 0;
        std::vector<std::pair<int, int>> flagged;       // (layers, object index)
        for (std::size_t i = 0; i < ts.size(); ++i) {
            worst = std::max(worst, layers[i]);
            if (layers[i] >= set.overdrawLayers) flagged.emplace_back(layers[i], ts[i].index);
        }
        if (!flagged.empty()) {
            std::sort(flagged.begin(), flagged.end(), [](const auto& a, const auto& b) {
                if (a.first != b.first) return a.first > b.first;
                return a.second < b.second;
            });
            Finding f;
            f.kind = FindingKind::OverdrawRisk;
            f.severity = worst >= set.overdrawCritical ? Severity::Critical : Severity::Warning;
            for (const auto& p : flagged) f.objects.push_back(p.second);
            f.title = "Overdraw risk from overlapping transparent objects";
            f.detail = Int(static_cast<long long>(flagged.size())) + " transparent object(s) overlap " + Int(set.overdrawLayers) +
                       " or more others; up to " + Int(worst) + " layers deep. Every layer is shaded again for each pixel it covers.";
            f.suggestion = "Use alpha cutout instead of blending, merge or remove overlapping quads, or shrink the quads to the visible shape.";
            f.impact = static_cast<float>(worst);
            List(f, scene, set.maxListed);
            rep.findings.push_back(std::move(f));
        }
    }

    // --- draw call budget ----------------------------------------------------
    if (n > set.drawCallBudget) {
        Finding f;
        f.kind = FindingKind::TooManyDrawCalls;
        f.severity = n > set.drawCallBudget * 2 ? Severity::Critical : Severity::Warning;
        f.title = Int(n) + " objects exceed the draw call budget";
        f.detail = "The scene has " + Int(n) + " renderers against a budget of " + Int(set.drawCallBudget) +
                   " draw calls; without batching each is one draw call.";
        f.suggestion = "Batch static objects (PlanBatches), instance repeated meshes, and cull what cannot be seen.";
        f.impact = static_cast<float>(n - set.drawCallBudget);
        rep.findings.push_back(std::move(f));
    }

    // --- texture memory ----------------------------------------------------
    if (st.textureMB > set.textureBudgetMB) {
        Finding f;
        f.kind = FindingKind::TextureMemory;
        f.severity = st.textureMB > set.textureBudgetMB * 2.0f ? Severity::Critical : Severity::Warning;
        f.title = "Texture memory over budget";
        f.detail = "The scene's textures take about " + Num(st.textureMB) + " MB against a budget of " + Num(set.textureBudgetMB) + " MB.";
        f.suggestion = "Plan a mip budget (PlanMips), compress textures and lower the size of textures on small objects.";
        f.impact = st.textureMB - set.textureBudgetMB;
        rep.findings.push_back(std::move(f));
    }

    std::stable_sort(rep.findings.begin(), rep.findings.end(), [](const Finding& a, const Finding& b) {
        if (a.severity != b.severity) return static_cast<int>(a.severity) > static_cast<int>(b.severity);
        if (a.impact != b.impact) return a.impact > b.impact;
        return static_cast<int>(a.kind) < static_cast<int>(b.kind);
    });
    return rep;
}

std::string OptimizationReport::ToText() const {
    std::string s = "Scene optimization report\n";
    s += "  objects " + Int(stats.objects) + " (" + Int(stats.staticObjects) + " static, " + Int(stats.transparentObjects) +
         " transparent), " + Int(stats.triangles) + " triangles, " + Int(stats.materials) + " materials, " +
         Int(stats.uniqueTextures) + " textures (" + Num(stats.textureMB) + " MB), ~" + Int(stats.estimatedDrawCalls) + " draw calls\n";
    s += "  findings: " + Int(Count(Severity::Critical)) + " critical, " + Int(Count(Severity::Warning)) + " warning, " +
         Int(Count(Severity::Info)) + " info\n";
    if (findings.empty()) s += "  nothing to fix.\n";
    for (const Finding& f : findings) {
        std::string tag = SeverityName(f.severity);
        std::transform(tag.begin(), tag.end(), tag.begin(), [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
        s += "\n[" + tag + "] " + f.title + "\n";
        s += "  " + f.detail + "\n";
        if (!f.listed.empty()) {
            s += "  e.g. ";
            for (std::size_t i = 0; i < f.listed.size(); ++i) { if (i) s += ", "; s += f.listed[i]; }
            if (f.objects.size() > f.listed.size()) s += ", ... (" + Int(static_cast<long long>(f.objects.size())) + " total)";
            s += "\n";
        }
        s += "  fix: " + f.suggestion + "\n";
    }
    return s;
}

std::string OptimizationReport::ToJson() const {
    std::string j = "{\"stats\":{";
    j += "\"objects\":" + Int(stats.objects) + ",\"staticObjects\":" + Int(stats.staticObjects) +
         ",\"transparentObjects\":" + Int(stats.transparentObjects) + ",\"triangles\":" + Int(stats.triangles) +
         ",\"materials\":" + Int(stats.materials) + ",\"uniqueTextures\":" + Int(stats.uniqueTextures) +
         ",\"textureMB\":" + Fmt("%.2f", stats.textureMB) + ",\"estimatedDrawCalls\":" + Int(stats.estimatedDrawCalls) + "}";
    j += ",\"counts\":{\"critical\":" + Int(Count(Severity::Critical)) + ",\"warning\":" + Int(Count(Severity::Warning)) +
         ",\"info\":" + Int(Count(Severity::Info)) + "}";
    j += ",\"findings\":[";
    for (std::size_t i = 0; i < findings.size(); ++i) {
        const Finding& f = findings[i];
        if (i) j += ",";
        j += "{\"kind\":\"" + std::string(FindingKindName(f.kind)) + "\",\"severity\":\"" + SeverityName(f.severity) + "\"";
        j += ",\"title\":\"" + JsonEscape(f.title) + "\",\"detail\":\"" + JsonEscape(f.detail) + "\",\"suggestion\":\"" +
             JsonEscape(f.suggestion) + "\"";
        j += ",\"impact\":" + Fmt("%.2f", f.impact) + ",\"objectCount\":" + Int(static_cast<long long>(f.objects.size())) + ",\"objects\":[";
        for (std::size_t k = 0; k < f.listed.size(); ++k) { if (k) j += ","; j += "\"" + JsonEscape(f.listed[k]) + "\""; }
        j += "]}";
    }
    j += "]}";
    return j;
}

} // namespace okay::optimize
