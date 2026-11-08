#pragma once
// ---------------------------------------------------------------------------
// Pose Capture — in Play, save the pose that is on the model right now as a
// one-frame clip, or record the next few seconds into a clip. The clip is
// added to the Model Animator, so the Animator can play it.
// ---------------------------------------------------------------------------
#include "okay/Scene/Component.hpp"
#include "okay/Scene/GameObject.hpp"
#include "okay/Scene/Scene.hpp"
#include "okay/Scene/Transform.hpp"
#include "okay/Components/ModelAnimator.hpp"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <string>
#include <vector>

namespace okay {

class PoseCapture : public Component {
public:
    std::string clipName = "Captured";
    float recordSeconds = 1.0f;

    bool IsRecording() const { return m_recording; }
    float RecordedTime() const { return m_time; }
    int LastClip() const { return m_last; }
    /// The clip name stored by the last capture (Captured_1, not the default label).
    const std::string& LastName() const { return m_lastName; }
    /// The clip file written by the last capture (empty until one is saved).
    const std::string& LastFile() const { return m_file; }

    /// Where clip files go. The editor sets this to the project's Assets folder
    /// when Play starts, so a capture is still there after Stop.
    static void SetAssetBaseDir(const std::string& dir) { AssetDir() = dir; }
    static const std::string& AssetBaseDir() { return AssetDir(); }

    /// Counts from one ImportFolder pass, so the editor can say why nothing was added.
    struct ImportStats {
        int added = 0;
        int skippedRig = 0;   ///< file is a clip, but this rig is missing a posed node
        int already = 0;
        int unreadable = 0;
    };

    /// Load .okayclip files under `dir` (including subfolders) onto `ma` when
    /// this rig has every node the clip poses. A clip for another skeleton in
    /// the same scene is left alone, and a clip already on the animator is left
    /// as it is. Returns how many were added. `stats` (optional) breaks down the rest.
    static int ImportFolder(ModelAnimator& ma, const std::string& dir, ImportStats* stats = nullptr) {
        if (stats) *stats = {};
        if (dir.empty()) return 0;
        namespace fs = std::filesystem;
        std::error_code ec;
        if (!fs::is_directory(dir, ec)) return 0;
        int added = 0, seen = 0;
        fs::recursive_directory_iterator it(dir, fs::directory_options::skip_permission_denied, ec);
        for (; !ec && it != fs::recursive_directory_iterator() && seen < 8000; it.increment(ec)) {
            ++seen;
            if (!it->is_regular_file(ec)) continue;
            std::string ext = it->path().extension().string();
            for (char& c : ext) c = (char)std::tolower((unsigned char)c);
            if (ext != ".okayclip") continue;
            const int r = ImportFile(ma, it->path().string(), false);
            if (r > 0) ++added;
            if (!stats) continue;
            if (r > 0) ++stats->added;
            else if (r == 0) ++stats->skippedRig;
            else if (r == -2) ++stats->already;
            else ++stats->unreadable;
        }
        return added;
    }

    /// Load one clip file onto `ma`. 1 = added or replaced, 0 = this rig is
    /// missing a node the clip poses, -1 = the file is not a clip, -2 = that
    /// name is already on the animator and `replace` is false.
    /// `replace` overwrites a clip of the same name (a drop or double-click).
    static int ImportFile(ModelAnimator& ma, const std::string& path, bool replace) {
        ModelAnimator::Clip clip;
        if (!LoadClip(path, clip) || clip.name.empty()) return -1;
        if (!RigHasClipNodes(ma, clip)) return 0;
        for (int i = 0; i < (int)ma.clips.size(); ++i) {
            if (ma.clips[i].name != clip.name) continue;
            if (!replace) return -2;
            ma.clips[i] = std::move(clip);
            ma.InvalidateBindings();
            return 1;
        }
        ma.clips.push_back(std::move(clip));
        ma.InvalidateBindings();
        return 1;
    }

    /// True when a Model Animator is on this object or its model.
    bool HasModel() const { return FindModel() != nullptr; }

    /// Read a clip file written by CaptureFrame. False if it is missing or not a clip.
    static bool LoadClip(const std::string& path, ModelAnimator::Clip& clip) {
        std::ifstream in(path);
        if (!in) return false;
        std::string tag;
        int ver = 0;
        in >> tag >> ver;
        if (!in || tag != "okayclip") return false;
        std::string key;
        in >> key;
        clip.name = ReadToken(in);
        int nodes = 0;
        in >> key >> nodes;
        if (!in || nodes < 0 || nodes > 100000) return false;
        clip.nodes.clear();
        for (int n = 0; n < nodes; ++n) {
            in >> key;
            ModelAnimator::NodeClip node;
            node.node = ReadToken(in);
            if (!in || node.node.empty()) return false;
            node.clip.smoothKeys = false;
            node.clip.loop = false;
            int keys = 0;
            in >> key >> keys;
            if (!in || keys < 0 || keys > 1000000) return false;
            for (int k = 0; k < keys; ++k) {
                float t = 0.0f;
                Vec3 p{0, 0, 0};
                Quat q = Quat::Identity;
                Vec3 s{1, 1, 1};
                in >> t >> p.x >> p.y >> p.z >> q.x >> q.y >> q.z >> q.w >> s.x >> s.y >> s.z;
                if (!in) return false;
                node.clip.AddKey("position.x", t, p.x);
                node.clip.AddKey("position.y", t, p.y);
                node.clip.AddKey("position.z", t, p.z);
                node.clip.AddKey("rotation.qx", t, q.x);
                node.clip.AddKey("rotation.qy", t, q.y);
                node.clip.AddKey("rotation.qz", t, q.z);
                node.clip.AddKey("rotation.qw", t, q.w);
                node.clip.AddKey("scale.x", t, s.x);
                node.clip.AddKey("scale.y", t, s.y);
                node.clip.AddKey("scale.z", t, s.z);
            }
            clip.nodes.push_back(std::move(node));
        }
        return !clip.nodes.empty();
    }

    /// One frame of the current pose, stored on the Model Animator. -1 if there
    /// is no model. Replaces a clip that already has this name.
    int CaptureFrame() {
        ModelAnimator* ma = FindModel();
        if (!ma) return -1;
        ModelAnimator::Clip clip;
        clip.name = AllocateName(ma);
        Sample(clip, 0.0f);
        Sample(clip, 1.0f / 60.0f);
        m_last = Store(ma, clip);
        if (m_last >= 0) ma->PlayIndex(m_last, true, 0.0f);
        return m_last;
    }

    /// Record `seconds` of motion (0 uses recordSeconds). A later CaptureFrame
    /// or a finished timer stores the clip.
    void StartRecord(float seconds) {
        if (seconds <= 0.0f) seconds = recordSeconds;
        if (seconds <= 0.0f) seconds = 1.0f;
        m_build = ModelAnimator::Clip{};
        m_build.name = AllocateName(FindModel());
        m_time = 0.0f;
        m_left = seconds;
        m_recording = true;
        Sample(m_build, 0.0f);
    }

    /// Stop early and store what was recorded. -1 if nothing was sampled.
    int StopRecord() {
        m_recording = false;
        ModelAnimator* ma = FindModel();
        if (!ma || m_build.nodes.empty()) return -1;
        if (m_time < 1.0f / 60.0f) Sample(m_build, 1.0f / 60.0f);
        m_last = Store(ma, m_build);
        m_build = {};
        if (m_last >= 0) ma->PlayIndex(m_last, true, 0.0f);
        return m_last;
    }

    void Start() override {
        ModelAnimator* ma = FindModel();
        if (ma && !AssetDir().empty()) ImportFolder(*ma, AssetDir());
    }

    void Update(float dt) override {
        if (!m_recording) return;
        if (dt < 0.0f) dt = 0.0f;
        m_time += dt;
        m_left -= dt;
        Sample(m_build, m_time);
        if (m_left <= 0.0f) StopRecord();
    }

private:
    bool m_recording = false;
    float m_time = 0.0f;
    float m_left = 0.0f;
    int m_last = -1;
    std::string m_lastName;
    std::string m_file;
    ModelAnimator::Clip m_build;

    static std::string& AssetDir() { static std::string d; return d; }

    /// True when every node the clip poses is on this animator, not some other rig.
    static bool RigHasClipNodes(const ModelAnimator& ma, const ModelAnimator::Clip& clip) {
        if (clip.nodes.empty() || !ma.gameObject || !ma.gameObject->transform) return false;
        auto onRig = [&](const std::string& name) {
            if (name.empty()) return false;
            std::vector<Transform*> stack{ma.gameObject->transform};
            while (!stack.empty()) {
                Transform* t = stack.back();
                stack.pop_back();
                if (t->gameObject && t->gameObject->name == name) return true;
                for (Transform* c : t->Children()) if (c) stack.push_back(c);
            }
            return false;
        };
        for (const auto& n : clip.nodes)
            if (!onRig(n.node)) return false;
        return true;
    }

    static std::string ReadToken(std::istream& in) {
        in >> std::ws;
        if (in.peek() == '"') {
            in.get();
            std::string s;
            char c = 0;
            while (in.get(c)) {
                if (c == '\\') {
                    if (!in.get(c)) break;
                    s.push_back(c);
                    continue;
                }
                if (c == '"') break;
                s.push_back(c);
            }
            return s;
        }
        std::string s;
        in >> s;
        return s;
    }
    static std::string SafeName(const std::string& n) {
        std::string o = n.empty() ? "Captured" : n;
        for (char& c : o)
            if (c == '/' || c == '\\' || c == ':' || c == ' ') c = '_';
        return o;
    }
    bool AutoName() const { return clipName.empty() || clipName == "Captured"; }
    std::string FilePathFor(const std::string& name) const {
        if (AssetDir().empty()) return {};
        return AssetDir() + "/" + SafeName(name) + ".okayclip";
    }
    bool NameTaken(const ModelAnimator* ma, const std::string& name) const {
        if (ma) for (const auto& c : ma->clips) if (c.name == name) return true;
        if (AssetDir().empty()) return false;
        std::error_code ec;
        return std::filesystem::exists(FilePathFor(name), ec);
    }
    /// Default "Captured" becomes Captured, Captured_1, ... A name the user typed is kept.
    std::string AllocateName(const ModelAnimator* ma) const {
        if (!AutoName()) return clipName;
        if (!NameTaken(ma, "Captured")) return "Captured";
        for (int i = 1; i < 10000; ++i) {
            const std::string n = "Captured_" + std::to_string(i);
            if (!NameTaken(ma, n)) return n;
        }
        return "Captured";
    }
    static std::string Quote(const std::string& s) {
        std::string o = "\"";
        for (char c : s) { if (c == '"') o.push_back('\\'); o.push_back(c); }
        o.push_back('"');
        return o;
    }
    std::string WriteClip(const ModelAnimator::Clip& clip) {
        const std::string path = FilePathFor(clip.name);
        if (path.empty()) return {};
        std::error_code ec;
        std::filesystem::create_directories(AssetDir(), ec);
        std::ofstream out(path);
        if (!out) return {};
        out << std::setprecision(9);
        out << "okayclip 1\n";
        out << "name " << Quote(clip.name) << "\n";
        out << "nodes " << clip.nodes.size() << "\n";
        for (const auto& n : clip.nodes) {
            out << "node " << Quote(n.node) << "\n";
            std::vector<float> times;
            for (const auto& kv : n.clip.Tracks())
                for (const auto& k : kv.second.Keys()) times.push_back(k.time);
            std::sort(times.begin(), times.end());
            times.erase(std::unique(times.begin(), times.end(),
                                    [](float a, float b) { return std::fabs(a - b) < 1e-4f; }), times.end());
            out << "keys " << times.size() << "\n";
            for (float t : times) {
                Vec3 p{0, 0, 0};
                Quat q = Quat::Identity;
                Vec3 s{1, 1, 1};
                n.clip.SampleTRS(t, p, q, s);
                out << t << ' ' << p.x << ' ' << p.y << ' ' << p.z << ' '
                    << q.x << ' ' << q.y << ' ' << q.z << ' ' << q.w << ' '
                    << s.x << ' ' << s.y << ' ' << s.z << '\n';
            }
        }
        return out ? path : std::string{};
    }

    ModelAnimator* FindModel() const {
        if (!gameObject) return nullptr;
        if (auto* m = gameObject->GetComponent<ModelAnimator>()) return m;
        for (Transform* t = gameObject->transform; t; t = t->Parent())
            if (t->gameObject) if (auto* m = t->gameObject->GetComponent<ModelAnimator>()) return m;
        if (!gameObject->scene()) return nullptr;
        for (const auto& up : gameObject->scene()->Objects())
            if (up && up->IsSelfOrDescendantOf(gameObject))
                if (auto* m = up->GetComponent<ModelAnimator>()) return m;
        return nullptr;
    }

    void Sample(ModelAnimator::Clip& clip, float time) const {
        ModelAnimator* ma = FindModel();
        if (!ma || !ma->gameObject || !ma->gameObject->scene()) return;
        // The animator host is the model's placement, not a bone. Keying it
        // pinned the character and made the clip require that object's name,
        // so a matching skeleton with another name never received it.
        int bones = 0;
        for (const auto& up : ma->gameObject->scene()->Objects()) {
            if (!up || !up->transform || up.get() == ma->gameObject) continue;
            if (!up->IsSelfOrDescendantOf(ma->gameObject)) continue;
            ++bones;
            SampleNode(clip, up.get(), time);
        }
        if (bones == 0) SampleNode(clip, ma->gameObject, time);
    }

    static void SampleNode(ModelAnimator::Clip& clip, GameObject* up, float time) {
        if (!up || !up->transform) return;
        {
            ModelAnimator::NodeClip* node = nullptr;
            for (auto& n : clip.nodes) if (n.node == up->name) { node = &n; break; }
            if (!node) {
                clip.nodes.push_back({});
                node = &clip.nodes.back();
                node->node = up->name;
                node->clip.smoothKeys = false;
                node->clip.loop = false;
            }
            const Vec3 p = up->transform->localPosition;
            const Quat q = up->transform->localRotation;
            const Vec3 s = up->transform->localScale;
            node->clip.AddKey("position.x", time, p.x);
            node->clip.AddKey("position.y", time, p.y);
            node->clip.AddKey("position.z", time, p.z);
            node->clip.AddKey("rotation.qx", time, q.x);
            node->clip.AddKey("rotation.qy", time, q.y);
            node->clip.AddKey("rotation.qz", time, q.z);
            node->clip.AddKey("rotation.qw", time, q.w);
            node->clip.AddKey("scale.x", time, s.x);
            node->clip.AddKey("scale.y", time, s.y);
            node->clip.AddKey("scale.z", time, s.z);
        }
    }

    int Store(ModelAnimator* ma, const ModelAnimator::Clip& clip) {
        m_lastName = clip.name;
        m_file = WriteClip(clip);
        for (int i = 0; i < (int)ma->clips.size(); ++i) {
            if (ma->clips[i].name == clip.name) { ma->clips[i] = clip; return i; }
        }
        ma->clips.push_back(clip);
        return (int)ma->clips.size() - 1;
    }
};

} // namespace okay
