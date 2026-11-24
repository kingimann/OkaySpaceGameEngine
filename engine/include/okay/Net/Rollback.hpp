#pragma once
// ---------------------------------------------------------------------------
// net::Rollback — optional rollback netcode for deterministic games (off until
// a game calls Start). Every player runs the same simulation at a fixed tick
// from the same inputs; only inputs travel. A player's own input is used
// `inputDelay` frames after it is sampled, which hides that much of the round
// trip. When another player's input for a frame is not here yet, it is
// predicted (that player's last known input); when the real one arrives and
// differs, the game is put back to the saved state of that frame and the
// frames since are run again with the real inputs. Nothing runs more than
// `maxPrediction` frames ahead of the slowest player's inputs (it waits).
//
//   auto& rb = nm.Rollback();
//   rb.Start(settings,
//            [&] { return SaveMyState(); },                 // the whole game state as bytes
//            [&](const std::string& s) { LoadMyState(s); },
//            [&] { return ReadMyInput(); },                 // this player's input, a few bytes
//            [&](std::uint32_t frame, const std::vector<net::Rollback::FrameInput>& in) { Step(in); });
//
// Every player in the session when Start is called takes part (call it on
// every peer, after Players() lists everyone, with the same settings: the
// players, tick rate, input delay and input size are compared and a mismatch
// stops it with LastError; the order they start in does not matter; a game started again is the next run on every peer, and
// packets of another run are ignored). Inputs go through the Network Manager ("__rb", unreliable,
// every packet repeats all inputs the others have not confirmed yet, so a
// lost one costs nothing but a resend). A checksum of a confirmed frame's
// state goes along, so a game that is not deterministic is found
// (Desyncs()). A player who leaves stops it (LastError says so); players who
// join later do not take part. Written for this engine from the general idea
// of input-delay plus rollback netcode (no outside code).
// ---------------------------------------------------------------------------
#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <string>
#include <unordered_map>
#include <vector>

namespace okay {
class NetworkManager;

namespace net {

class Rollback {
public:
    struct Settings {
        float tickRate = 60.0f;          ///< simulation frames per second
        int   inputDelay = 2;            ///< frames between sampling an input and using it (0..30)
        int   maxPrediction = 8;         ///< frames past the slowest player's inputs it may run (1..120)
        std::size_t maxInputBytes = 64;  ///< an input longer than this is cut (at most 255)
    };
    /// One player's input for a frame. `predicted` = the real one is not here yet.
    struct FrameInput { std::uint32_t player = 0; std::string input; bool predicted = false; };

    using SaveFn = std::function<std::string()>;
    using LoadFn = std::function<void(const std::string&)>;
    using InputFn = std::function<std::string()>;
    using StepFn = std::function<void(std::uint32_t frame, const std::vector<FrameInput>& inputs)>;

    explicit Rollback(NetworkManager& nm) : m_nm(nm) {}

    /// Start (or restart) with the players in the session now. False, with
    /// LastError(), when not in a session or a callback is missing.
    bool Start(const Settings& settings, SaveFn save, LoadFn load, InputFn input, StepFn step);
    void Stop(const std::string& why = "");
    bool Running() const { return m_running; }

    /// The next frame to run (= frames run so far).
    std::uint32_t Frame() const { return m_frame; }
    /// Every frame below this one has every player's real input: it is final.
    std::uint32_t ConfirmedFrame() const;
    /// True while frames are being run again after a late input (skip sounds and effects).
    bool Resimulating() const { return m_resim; }
    const std::vector<std::uint32_t>& Players() const { return m_players; }
    const Settings& GetSettings() const { return m_settings; }

    /// The saved state from just before `frame` ran, if it is still kept (the
    /// last 128 frames). For tests and tools: two players compare a confirmed frame.
    bool StateAt(std::uint32_t frame, std::string& out) const;

    std::uint32_t Rollbacks() const { return m_rollbacks; }         ///< times it went back
    std::uint32_t FramesResimulated() const { return m_resimFrames; }
    std::uint32_t Stalls() const { return m_stalls; }               ///< ticks it waited for a slow player
    std::uint32_t Desyncs() const { return m_desyncs; }             ///< confirmed frames whose state differed from another player's
    std::uint32_t ChecksumsCompared() const { return m_compared; }
    const std::string& LastError() const { return m_lastError; }
    /// Inputs held for every player (entries and bytes): bounded by how far ahead a player may be.
    std::size_t HeldInputs() const;
    std::size_t HeldInputBytes() const;

    /// Called by the Network Manager: each Update, and for every "__rb" message.
    void Tick(float dt);
    void OnMessage(std::uint32_t from, const std::string& data);

    static constexpr std::size_t kKeepStates = 128;
    static constexpr std::size_t kMaxFramesPerPacket = 64;
    /// Frames past what a player can honestly be ahead (inputDelay + maxPrediction past our frame)
    /// that its inputs may still be for; later ones are not kept (they come again when due).
    static constexpr std::uint32_t kAheadMargin = 16;

private:
    struct Peer {
        std::map<std::uint32_t, std::string> inputs;   // frame -> input
        std::uint32_t have = 0;     // inputs for every frame below this are here
        std::uint32_t acked = 0;    // this player has every input of ours below this
    };
    NetworkManager& m_nm;
    Settings m_settings;
    SaveFn m_save; LoadFn m_load; InputFn m_input; StepFn m_step;
    bool m_running = false, m_resim = false;
    std::uint32_t m_me = 0;
    std::uint8_t m_run = 0;                            // the n-th Start: packets of another run are not ours
    std::vector<std::uint32_t> m_players;              // sorted, me included
    std::unordered_map<std::uint32_t, Peer> m_peers;   // every player, me included
    std::map<std::uint32_t, std::string> m_states;     // frame -> state before it ran
    std::map<std::uint32_t, std::vector<std::string>> m_used;   // frame -> inputs it ran with (players' order)
    std::map<std::uint32_t, std::uint64_t> m_myHashes, m_theirHashes;   // confirmed frame -> state checksum (StringId::HashOf)
    std::uint32_t m_frame = 0;
    std::uint32_t m_rollbackTo = 0xFFFFFFFFu;
    std::uint32_t m_hashedUpTo = 0;
    float m_accum = 0.0f;
    std::uint32_t m_playersHash = 0;                   // the players who started, as one number (compared with the others')
    std::uint32_t m_rollbacks = 0, m_resimFrames = 0, m_stalls = 0, m_desyncs = 0, m_compared = 0;
    std::string m_lastError;

    std::string InputFor(std::uint32_t player, std::uint32_t frame, bool& predicted) const;
    void RunFrame(std::uint32_t frame);
    void SendInputs();
    void Prune();
    void HashConfirmed();
    void Compare(std::uint32_t frame);
};

} // namespace net
} // namespace okay
