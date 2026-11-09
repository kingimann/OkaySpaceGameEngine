#pragma once
// Text-level tools for scene and prefab files (`okayscene N`), for version control:
// a tolerant parser, a canonical form that makes diffs stable, and the structure
// model the diff and merge tools (SceneDiff.hpp, SceneMerge.hpp) work on.
//
// Everything here works on the TEXT of a scene. It does not build a Scene, so it
// is cheap (a 5000 object scene parses in a few milliseconds) and safe on files
// this build only half understands: a record it does not know is carried along
// untouched, never dropped. See docs/scene_diff_merge.md.
//
// Header note: keep this header free of <mutex>, <thread> and <condition_variable>
// (MinGW builds include it from places that must not pull those in).
#include <cstddef>
#include <string>
#include <vector>

namespace okay {

class SceneText {
public:
    /// The `okayscene` version this build writes (SceneSerializer::kSceneFormatVersion).
    static int CurrentVersion();

    // ---- Document: the file, record by record -------------------------------

    /// One record: its first token (`key`) and its whole text with the indentation
    /// and trailing blanks removed. The text is one logical line: it may hold raw
    /// newlines, but only inside a quoted string (a script's source, a text label).
    struct Record {
        std::string key;
        std::string text;
        bool operator==(const Record& o) const { return text == o.text; }
    };
    /// One `gameobject <index> "name"` ... `end` block.
    struct DocObject {
        int index = -1;
        std::string name;
        std::string headerTail;           ///< anything after the name on the header line, kept as is
        std::vector<Record> records;      ///< every record between the header and `end`, in file order
    };
    struct Document {
        int version = 0;
        std::vector<Record> globals;      ///< file-level records (name, gravity, fog, ...), wherever they sat
        std::vector<DocObject> objects;
    };

    /// Split a file into records. False, with `error`, when the text cannot be a
    /// scene: no `okayscene N` header, an unterminated string, an object without
    /// `end`, a duplicate object index or a malformed object header. A version
    /// newer than this build still parses (its records are just unknown).
    static bool Parse(const std::string& text, Document& out, std::string* error = nullptr);
    /// Write a document the way the scene writer lays it out: LF line endings, no
    /// blank lines, objects as `gameobject i "name"`, records indented by two spaces.
    static std::string Write(const Document& doc);

    // ---- Canonical form -----------------------------------------------------

    struct CanonicalOptions {
        /// 2.0 -> 2, 0.50 -> 0.5 in the records the engine writes numbers into
        /// (the value is never changed, only trailing zeros are dropped).
        bool normalizeNumbers = true;
        /// Number the objects 0..n-1 in file order and rewrite `parent` to match.
        bool renumberObjects = true;
        /// Put an object's bookkeeping records (guid, active, tag, ..., parent,
        /// sibling) in the order the writer uses, and the file-level records too.
        bool sortHeaderRecords = true;
    };
    struct CanonicalReport {
        bool changed = false;     ///< the output differs from the input
        bool skipped = false;     ///< the input was returned untouched (see `note`)
        std::string note;
        int objects = 0;
    };
    /// Rewrite scene text so equal scenes have equal text: LF line endings, no
    /// trailing blanks or blank lines, single spaces between tokens, one trailing
    /// newline, tidy numbers, writer order for the bookkeeping records, contiguous
    /// object numbers. Idempotent, and the scene it loads as is the scene the input
    /// loads as. Text it cannot parse, or a version newer than this build, is
    /// returned unchanged (`report->skipped`), and a record it does not know is kept
    /// byte for byte (only its indentation and trailing blanks are normalised).
    /// Quoted strings are never touched, line breaks inside them included.
    static std::string Canonicalize(const std::string& text);
    static std::string Canonicalize(const std::string& text, const CanonicalOptions& opts, CanonicalReport* report = nullptr);

    // ---- Model: what diff and merge compare ---------------------------------

    struct ModelObject {
        std::string name;
        std::string guid;                 ///< valid and unique in the file, else empty
        std::string headerTail;
        int parent = -1;                  ///< position in Model::objects, -1 for a root
        int fileIndex = -1;               ///< the `gameobject` number it had
        std::string path;                 ///< "Level/Arena/Enemy", with #n added when siblings share a name
        std::vector<Record> records;      ///< every record except guid, parent and sibling
    };
    struct Model {
        int version = 0;
        std::vector<Record> globals;
        std::vector<ModelObject> objects;          ///< in file order
        /// Children of each object, in the order the loader gives them (file order,
        /// then the `sibling` positions). Same size as `objects`.
        std::vector<std::vector<int>> children;
        std::vector<int> roots;                    ///< objects without a parent, in file order
        std::vector<std::string> warnings;         ///< things the loader would warn about
    };
    /// Build the model. False, with `error`, when the text does not parse or the
    /// hierarchy has a cycle (the loader refuses such a file too). Dangling parents,
    /// malformed or repeated guids are tolerated the way the loader tolerates them
    /// (`warnings`).
    static bool BuildModel(const std::string& text, Model& out, std::string* error = nullptr);
    static bool BuildModel(const Document& doc, Model& out, std::string* error = nullptr);
    /// Reset `children` and `roots` to the order objects have in the file.
    static void SetFileChildOrder(Model& m);
    /// Write a model as canonical text. Object numbers are positions, `parent` comes
    /// from ModelObject::parent, and `sibling` records are written for exactly the
    /// parents whose `children` order differs from file order (what the writer does).
    static std::string Emit(const Model& m);

    // ---- Validation ---------------------------------------------------------

    struct Validation {
        bool ok = true;                   ///< nothing the loader would refuse or warn about
        std::vector<std::string> problems;
    };
    /// Check that text parses, every object has a valid and unique guid or none,
    /// every `parent` names an existing object and the hierarchy has no cycle.
    static Validation Validate(const std::string& text);

    // ---- Small helpers (shared with SceneDiff / SceneMerge) -----------------

    struct Token {
        std::string text;                 ///< as written, quotes included
        bool quoted = false;              ///< the whole token is one quoted string
    };
    /// Split a record into tokens on blanks outside quoted strings. A quote opens at
    /// the start of a token or right after `=` (the loader's rules).
    static std::vector<Token> Tokenize(const std::string& record);
    static std::string Quote(const std::string& s);
    /// The value of a token: a quoted token is unescaped, a bare one returned as is.
    static std::string Unquote(const std::string& token);
    /// The loader's rule for a persistent id: 1..64 characters of 0-9, a-f and '-'.
    static bool IsValidGuid(const std::string& g);
    /// Is `key` a record name this build's loader reads (inside an object)?
    static bool IsKnownRecord(const std::string& key);
    /// Is `key` a file-level record this build's loader reads?
    static bool IsKnownGlobalRecord(const std::string& key);
    /// Trailing zeros dropped from a decimal token ("2.50" -> "2.5", "3.0" -> "3");
    /// anything that is not a plain decimal comes back unchanged.
    static std::string NormalizeNumber(const std::string& token);
    /// The first token of a record text.
    static std::string KeyOf(const std::string& recordText);
};

} // namespace okay
