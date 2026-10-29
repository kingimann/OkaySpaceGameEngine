#pragma once
// Plays a Sequence (a .okayseq cutscene) in a scene: drives the main camera from
// the camera track and hands it back untouched at the end, moves the tracked
// objects, plays clips and sounds, and fires each event once as the playhead
// crosses it. Scripts start one with play_sequence(name) / Sequence.Play(name).
#include "okay/Components/LoadingScreenLayout.hpp"   // LoadingDrawCmd: the overlay is drawn like a loading screen
#include "okay/Components/Sequence.hpp"
#include "okay/Scene/Component.hpp"
#include <functional>
#include <string>
#include <vector>

namespace okay {

class SequencePlayer : public Behaviour {
public:
    std::string sequencePath;      ///< the .okayseq file, project-relative ("Assets/Intro.okayseq")
    bool  playOnStart = false;
    bool  loop = false;            ///< wrap at the loop markers (the sequence's own loop flag also counts)
    float timeScale = 1.0f;        ///< playback rate (the game's time scale applies on top)
    char  skipKey = 27;            ///< jumps to the end (27 = Escape; 0 = none)
    bool  lockPlayer = true;       ///< the player's controllers ignore input while it plays
    std::function<void()> onStarted, onFinished;
    /// A named event on the event track (scripts get on_sequence_event too).
    std::function<void(const std::string&)> onEvent;

    /// After the controllers (-10), the view toggle (95) and IK (110): the camera
    /// shot is written after everything else placed the camera this frame.
    int ExecutionOrder() const override { return 120; }

    // ---- Playback ----
    /// Start from the beginning (loading `sequencePath` when it isn't loaded yet).
    /// False when there is no sequence to play.
    bool Play();
    /// Load `path` and play it.
    bool Play(const std::string& path);
    void Pause() { if (m_playing) m_paused = true; }
    void Resume() { m_paused = false; }
    /// Stop now: the camera goes back, finished events fire; later events do not.
    void Stop();
    /// Jump to the end: the remaining actions still apply (objects switched,
    /// teleported, named events delivered) but subtitles and waits are skipped.
    void Skip();
    /// Jump to `t` and show that moment; events between here and there do not fire.
    void Seek(float t);
    bool  IsPlaying() const { return m_playing; }
    bool  IsPaused() const { return m_paused; }
    /// Held on a wait-for-input event until the confirm key.
    bool  IsWaiting() const { return m_playing && m_waiting; }
    float Time() const { return m_time; }
    float Length() const { return m_seq.Duration(); }
    /// True while playing with `lockPlayer`: the runtimes treat it like a modal UI.
    bool  LocksInput() const { return m_playing && lockPlayer; }
    /// The fade and subtitle to draw now.
    SeqOverlay Overlay() const;

    // ---- The sequence ----
    Sequence& sequence() { return m_seq; }
    const Sequence& sequence() const { return m_seq; }
    bool HasSequence() const { return m_loaded; }
    /// Use `seq` directly (tests, the editor) instead of a file.
    void SetSequence(const Sequence& seq) { m_seq = seq; m_seq.Sort(); m_loaded = true; m_loadedPath = sequencePath; }
    /// Read `sequencePath`. False (with `error`) when the file can't be read or parsed.
    bool Load(std::string* error = nullptr);
    /// `sequencePath` made absolute under the project folder when it is relative
    /// (the same folder a Character's clips file resolves against).
    std::string ResolvedPath() const;

    // ---- Stepping (Update does this; tests and the editor may call it directly) ----
    /// Advance by `dt` seconds (scaled by `timeScale`), firing the events crossed.
    void Step(float dt);
    /// Write the transforms and the camera at `t` without firing anything.
    void Evaluate(float t);

    void Start() override;
    void Update(float dt) override;
    void LateUpdate(float dt) override;
    void OnDisable() override { Stop(); }
    void OnDestroy() override { Stop(); }

    // ---- Scene-wide helpers (scripts) ----
    /// The first player that is playing, or null.
    static SequencePlayer* Playing(const Scene& scene);
    /// Play a sequence by name: the player whose file is `name` (its stem or
    /// path), else the first player (or one added to `from`) loading
    /// "Assets/<name>.okayseq" under the project folder. False when nothing played.
    static bool PlayByName(Scene& scene, GameObject* from, const std::string& name);
    static void StopAll(Scene& scene);
    /// True when any player is playing with its input lock on.
    static bool AnyLocksInput(const Scene& scene);
    /// The name of the event being delivered to scripts ("" outside one).
    static const std::string& LastEvent();
    /// The overlay as draw commands for a W x H screen: the fade, then the
    /// subtitle card in the launcher look (slate panel, hairline border, accent
    /// speaker). The player and the editor's Game view both draw this list.
    static std::vector<LoadingDrawCmd> BuildOverlay(const SeqOverlay& overlay, float W, float H);

private:
    void FireBetween(float a, float b, bool includeA);
    void Fire(const SeqEvent& e);
    void Deliver(const char* scriptEvent, const char* vsEntry, const std::string& name);
    void Finish();
    bool ConfirmPressed() const;

    Sequence m_seq;
    bool m_loaded = false;
    std::string m_loadedPath;
    SequenceBinding m_binding;
    bool  m_playing = false, m_paused = false, m_waiting = false;
    float m_time = 0.0f;
    float m_last = -1.0f;          ///< the time events were last fired up to
    float m_savedTimeScale = 1.0f; ///< the game's time scale at Play, restored at the end when an action changed it
    bool  m_changedTimeScale = false;
    bool  m_audioWarned = false, m_clipWarned = false;
    bool  m_skipping = false;      ///< Skip() is firing the rest: waits are ignored
    bool  m_startDeferred = false; ///< playOnStart arrived while scripts were off
};

} // namespace okay
