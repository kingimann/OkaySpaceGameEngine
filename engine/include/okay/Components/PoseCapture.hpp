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
#include <cmath>
#include <filesystem>
#include <fstream>
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
    /// The clip file written by the last capture (empty until one is saved).
    const std::string& LastFile() const { return m_file; }

    /// Where clip files go. The editor sets this to the project's Assets folder
    /// when Play starts, so a capture is still there after Stop.
    static void SetAssetBaseDir(const std::string& dir) { AssetDir() = dir; }
    static const std::string& AssetBaseDir() { return AssetDir(); }

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
        clip.nodes.clear();
        for (int n = 0; n < nodes && in; ++n) {
            in >> key;
            ModelAnimator::NodeClip node;
            node.node = ReadToken(in);
            node.clip.smoothKeys = false;
            node.clip.loop = false;
            int keys = 0;
            in >> key >> keys;
            for (int k = 0; k < keys && in; ++k) {
                float t = 0.0f;
                Vec3 p{0, 0, 0};
                Quat q = Quat::Identity;
                Vec3 s{1, 1, 1};
                in >> t >> p.x >> p.y >> p.z >> q.x >> q.y >> q.z >> q.w >> s.x >> s.y >> s.z;
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
        clip.name = clipName.empty() ? "Captured" : clipName;
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
        m_build.name = clipName.empty() ? "Captured" : clipName;
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
        if (AssetDir().empty()) return;
        ModelAnimator* ma = FindModel();
        if (!ma) return;
        const std::string path = FilePath();
        for (const auto& c : ma->clips) if (c.name == (clipName.empty() ? "Captured" : clipName)) return;
        ModelAnimator::Clip clip;
        if (!LoadClip(path, clip)) return;
        m_file = path;
        for (int i = 0; i < (int)ma->clips.size(); ++i)
            if (ma->clips[i].name == clip.name) { ma->clips[i] = std::move(clip); m_last = i; return; }
        ma->clips.push_back(std::move(clip));
        m_last = (int)ma->clips.size() - 1;
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
    std::string m_file;
    ModelAnimator::Clip m_build;

    static std::string& AssetDir() { static std::string d; return d; }

    static std::string ReadToken(std::istream& in) {
        in >> std::ws;
        if (in.peek() == '"') {
            in.get();
            std::string s;
            std::getline(in, s, '"');
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
    std::string FilePath() const {
        if (AssetDir().empty()) return {};
        return AssetDir() + "/" + SafeName(clipName) + ".okayclip";
    }
    static std::string Quote(const std::string& s) {
        std::string o = "\"";
        for (char c : s) { if (c == '"') o.push_back('\\'); o.push_back(c); }
        o.push_back('"');
        return o;
    }
    std::string WriteClip(const ModelAnimator::Clip& clip) {
        const std::string path = FilePath();
        if (path.empty()) return {};
        std::error_code ec;
        std::filesystem::create_directories(AssetDir(), ec);
        std::ofstream out(path);
        if (!out) return {};
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
        for (const auto& up : ma->gameObject->scene()->Objects()) {
            if (!up || !up->transform) continue;
            if (!up->IsSelfOrDescendantOf(ma->gameObject)) continue;
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
        m_file = WriteClip(clip);
        for (int i = 0; i < (int)ma->clips.size(); ++i) {
            if (ma->clips[i].name == clip.name) { ma->clips[i] = clip; return i; }
        }
        ma->clips.push_back(clip);
        return (int)ma->clips.size() - 1;
    }
};

} // namespace okay
