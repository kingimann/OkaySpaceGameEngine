#pragma once
#include <cstdint>
#include <string>

namespace okay {

/// Records what the player pressed, frame by frame, and plays it back, for bug reports ("here is
/// exactly what I did") and for tests that need the same input twice.
///
/// It works on the finished input state, once per frame inside InputActions::Tick: recording
/// copies the keys, mouse, wheel and every controller as they stand after the runtime fed them;
/// playback puts a recorded frame back over whatever the runtime fed, with the press and release
/// edges rebuilt from the frame before it. Nothing else in the game has to change.
///
/// The text form is plain lines and versioned ("okayinputrec 1"). A recording also carries the
/// random seed and a note (the scene it was made in) so a tester can replay it the same way.
/// Written from the spec in this header; no outside code.
class InputRecorder {
public:
    static void StartRecording();
    static void StopRecording();
    static bool Recording();
    /// The recording so far (or the one loaded) as text.
    static std::string Text();
    /// Frames in the recording / the frame playback is at.
    static std::size_t FrameCount();
    static std::size_t PlaybackFrame();

    /// Parse a recording and start playing it from the first frame. False when it is not one
    /// (or a newer version than this build reads).
    static bool StartPlayback(const std::string& text);
    static void StopPlayback();
    static bool Playing();
    /// True on the frame playback ran out (it stops itself on the next frame).
    static bool Finished();

    /// The note kept with a recording: the random seed the game used and a free text (scene path).
    static void SetSeed(std::uint64_t seed);
    static std::uint64_t Seed();
    static void SetNote(const std::string& note);
    static std::string Note();

    /// Called by InputActions::Tick at the start of a frame (playback) and after sampling (record).
    static void BeginFrame(float dt);
    static void EndFrame(float dt);
    static void Clear();
};

} // namespace okay
