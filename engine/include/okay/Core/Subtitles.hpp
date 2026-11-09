#pragma once
// Subtitles and captions (docs/accessibility.md).
//
// A game says what is spoken ("Say") and what is heard ("Sound": [door slams]); the
// CaptionManager decides what is on screen now and hands a UI draw-ready lines: wrapped
// text, a speaker label, colours with the player's opacity settings and the fade applied.
// It draws nothing and reads no clock: the game calls Update(dt) each frame.
//
//  * Timing: a line lasts as long as its audio when the audio length is known, else as long
//    as it takes to read.
//  * Queue: up to `maxLines` lines show at once; more wait, highest priority first, and a
//    line that waits too long is dropped. A higher priority line takes the place of the lowest
//    one on screen when there is no room.
//  * Settings the player chooses: on / off, sound captions (SDH), speaker names and colours,
//    text size, background and its opacity, where on screen. Saved through Prefs.
//  * SubtitleTrack: lines with start times, read from or written to .srt files and played back
//    against a clock.
#include "okay/Render/Color.hpp"

#include <functional>
#include <map>
#include <string>
#include <vector>

namespace okay {

struct AudioClip;

enum class SubtitleKind {
    Speech,   ///< what a character says
    Sound,    ///< a sound described for deaf and hard-of-hearing players: [door slams]
    Music,    ///< music described the same way: [tense music]
};

/// The player's subtitle settings. Defaults: speech subtitles on, sound captions off.
struct SubtitleSettings {
    bool enabled = true;             ///< show what characters say
    bool soundCaptions = false;      ///< also describe sounds and music (SDH)
    bool speakerNames = true;        ///< "Anna: " before a line
    bool speakerColours = true;      ///< colour the speaker's name (each speaker gets their own colour)
    float textScale = 1.0f;          ///< 0.5 .. 3, a multiplier for the UI's subtitle size
    bool background = true;          ///< a box behind each line
    float backgroundOpacity = 0.6f;  ///< 0 .. 1
    float textOpacity = 1.0f;        ///< 0 .. 1
    int maxLines = 3;                ///< lines on screen at once, 1 .. 6
    int maxCharsPerLine = 42;        ///< where a line wraps, 16 .. 120 character cells
    bool top = false;                ///< at the top of the screen instead of the bottom

    /// Bring every value into range.
    void Clamp();
    /// Read from / write to Prefs under "a11y.subtitles.*" (Prefs::Save writes them to disk).
    /// Load leaves a value alone when Prefs has none for it.
    void Load();
    void Save() const;
    bool operator==(const SubtitleSettings& o) const;
    bool operator!=(const SubtitleSettings& o) const { return !(*this == o); }
};

/// What a game asks to show.
struct SubtitleLine {
    std::string speaker;        ///< "" = none (narration, sounds)
    std::string text;           ///< the words, as written
    std::string textKey;        ///< a string-table key: its text in the current language replaces `text` when the table has it
    SubtitleKind kind = SubtitleKind::Speech;
    float duration = 0.0f;      ///< seconds on screen (0 = from audioSeconds, else from the length of the text)
    float audioSeconds = 0.0f;  ///< how long the audio line plays: the line stays for as long
    int priority = 0;           ///< higher wins a place on screen; sound captions default lower than speech
    float maxDelay = 2.0f;      ///< seconds it may wait for room before it is dropped
    bool hasColour = false;     ///< use `colour` for the speaker's name instead of the speaker's own
    Color colour = Color::White;
    std::string tag;            ///< anything: CancelTag removes every line with it
};

/// A line with a time in a track.
struct SubtitleCue {
    float start = 0.0f;         ///< seconds from the track's start
    SubtitleLine line;
};

/// Lines in time order, to play back with the audio or a cutscene.
class SubtitleTrack {
public:
    std::string name;
    std::vector<SubtitleCue> cues;   ///< sorted by start (Add keeps them so)

    SubtitleCue& Add(float start, const std::string& speaker, const std::string& text, float duration = 0.0f);
    /// A sound caption: "[description]" (brackets added when missing).
    SubtitleCue& AddSound(float start, const std::string& description, float duration = 0.0f);
    /// When the last cue ends (cues without a duration count their estimated reading time).
    float Length() const;

    /// SubRip (.srt): numbered blocks, "00:00:01,000 --> 00:00:03,500", then the text. A first
    /// "Name: " becomes the speaker; a line wholly in [brackets] becomes a sound caption.
    /// False (with `err`) when no cue could be read.
    static bool ParseSrt(const std::string& text, SubtitleTrack& out, std::string* err = nullptr);
    std::string ToSrt() const;
};

/// A line ready to draw. Positions and fonts are the UI's business.
struct SubtitleDrawLine {
    int id = 0;
    SubtitleKind kind = SubtitleKind::Speech;
    std::string speaker;                 ///< "" when none
    std::string speakerLabel;            ///< "Anna: " when names are on and there is a speaker, else ""
    std::vector<std::string> rows;       ///< the text wrapped to the line width, top to bottom
    Color speakerColour = Color::White;  ///< for the label (the speaker's colour, or white with colours off)
    Color textColour = Color::White;     ///< white, with the text opacity and the fade applied
    Color backgroundColour = Color::Black;   ///< the box behind it, opacity and fade applied (alpha 0 = no box)
    float alpha = 1.0f;                  ///< the fade, 0 .. 1 (already in the colours)
    float scale = 1.0f;                  ///< the text size multiplier
    bool italic = false;                 ///< sound and music captions
    int priority = 0;
};

class CaptionManager {
public:
    SubtitleSettings settings;
    float fadeIn = 0.12f;        ///< seconds
    float fadeOut = 0.25f;
    float linger = 0.25f;        ///< seconds a line stays after its audio ends
    int maxQueue = 16;           ///< lines waiting for room; the lowest priority is dropped past this

    /// The manager games share.
    static CaptionManager& Main();

    // ---- Showing ----
    /// Show a line (or queue it). Returns an id, or 0 when nothing was shown: subtitles are off
    /// (or sound captions, for those), the text is empty, or the queue turned it away. The same
    /// sound caption shown again while it is up only restarts its time and returns the same id.
    int Say(const SubtitleLine& line);
    int Say(const std::string& speaker, const std::string& text, float audioSeconds = 0.0f, int priority = 0);
    int Say(const std::string& speaker, const std::string& text, const AudioClip& clip, int priority = 0);
    /// A sound caption "[door slams]"; shown only with settings.soundCaptions.
    int Sound(const std::string& description, float duration = 0.0f, int priority = -1);
    /// A music caption "[tense music]".
    int Music(const std::string& description, float duration = 0.0f, int priority = -1);

    // ---- Tracks ----
    /// Play a track's cues against the Update clock from `startAt`. Returns a handle for Stop.
    int Play(const SubtitleTrack& track, float startAt = 0.0f);
    void Stop(int handle);
    bool Playing(int handle) const;

    // ---- Control ----
    /// Remove a line (on screen or waiting). False when there is none with this id.
    bool Cancel(int id);
    /// Remove every line with this tag; how many went.
    int CancelTag(const std::string& tag);
    /// Remove everything, tracks included.
    void Clear();
    /// Advance time: tracks emit their cues, lines age and fade, expired lines go, waiting ones move up.
    void Update(float dt);

    // ---- What to draw ----
    /// The lines on screen, oldest first (top to bottom).
    std::vector<SubtitleDrawLine> Lines() const;
    int VisibleCount() const { return (int)m_visible.size(); }
    int QueuedCount() const { return (int)m_queue.size(); }
    bool Empty() const { return m_visible.empty() && m_queue.empty(); }
    bool IsShowing(int id) const;
    bool IsQueued(int id) const;

    // ---- Speakers ----
    /// Give a speaker a colour (otherwise each new speaker takes the next colour of SafePalette).
    void SetSpeakerColour(const std::string& speaker, const Color& colour);
    /// The colour a speaker's name has (assigned now when they have none).
    Color SpeakerColour(const std::string& speaker);
    void ClearSpeakerColours();

    // ---- Hooks ----
    /// A line was dropped without being shown, or taken off the screen for a higher priority one.
    std::function<void(int id)> onDropped;
    /// A line has been shown to its end.
    std::function<void(int id)> onFinished;

    // ---- Timing and wrapping, usable on their own ----
    /// How long a line stays: the audio's length plus `linger` when it is known, else a reading
    /// time (about 15 character cells a second, at least 1.5 and at most 8 seconds).
    static float DurationFor(const std::string& text, float audioSeconds = 0.0f, float linger = 0.25f);
    /// Break `text` into rows of at most `maxCells` character cells (wide characters count 2) at
    /// spaces, hard-breaking words that don't fit; existing line breaks stay. `firstRowIndent`
    /// cells are already used on the first row (a speaker label).
    static std::vector<std::string> Wrap(const std::string& text, int maxCells, int firstRowIndent = 0);
    /// Character cells `text` takes (wide characters 2, combining marks 0).
    static int Cells(const std::string& text);

private:
    struct Entry {
        int id = 0;
        SubtitleLine line;
        std::string text;       // resolved (key looked up)
        float duration = 0.0f;
        float elapsed = 0.0f;   // visible: time on screen; queued: time waiting
        int order = 0;          // arrival order
    };
    struct TrackRun {
        int handle = 0;
        SubtitleTrack track;
        float time = 0.0f;
        std::size_t next = 0;
    };
    Entry* FindVisible(int id);
    void Show(Entry e);
    void Fill();
    int Drop(Entry& e);

    std::vector<Entry> m_visible;   // in the order they started
    std::vector<Entry> m_queue;     // highest priority first, then arrival
    std::vector<TrackRun> m_playing;
    std::map<std::string, Color> m_speakerColours;
    int m_nextId = 1;
    int m_nextHandle = 1;
    int m_order = 0;
    std::size_t m_nextColour = 0;
};

} // namespace okay
