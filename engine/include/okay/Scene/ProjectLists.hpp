#pragma once
#include <string>

namespace okay {

/// Saves and loads the project's tag list and layer names in the project settings file
/// (project.okayproj: one `key=value` per line, the file the editor's Project Settings write).
/// It owns three kinds of line and leaves every other line alone:
///
///   tag=Boss                 a custom tag (the built-in tags are not written)
///   strict_tags=1            Tags::Strict()
///   layer_name_8=Enemy       a layer name that differs from the default; "layer_name_4=" is a
///                            default name that was cleared on purpose
///
/// Lines whose value is not valid (a bad tag name, a layer name another layer already has, an
/// index outside 0-31, a full tag list) are skipped, so a hand-edited or damaged file never
/// breaks the lists. Not thread safe: Tags and Layers are process-wide and main-thread.
///
/// For the editor: call ApplyLine(key, value) for every line it reads (it returns false for keys
/// that are not these, so the editor's own branches can follow), and append Write() to the lines
/// it saves. A tool that does not own the file calls Save(path), which keeps every other line.
struct ProjectLists {
    /// The lines described above, each ending in a newline. Stable: tags in list order, layers by index.
    static std::string Write();
    /// Apply one line. True when `key` is one of ours and the value was taken, false for any other
    /// key or a refused value.
    static bool ApplyLine(const std::string& key, const std::string& value);
    /// Apply every line of `text` (LF or CRLF); returns how many lines were taken.
    static int Apply(const std::string& text);
    /// Read the file and Apply it. False when it cannot be opened. Lists are not cleared first:
    /// call Reset() before to start from the defaults.
    static bool Load(const std::string& path);
    /// Rewrite the file with the live lists in place of the ones it has, keeping every other line
    /// (and their order); a missing file is created. The write goes through a temporary file.
    static bool Save(const std::string& path);
    /// No custom tags, tags not strict, layer names as a new project has them.
    static void Reset();
};

} // namespace okay
