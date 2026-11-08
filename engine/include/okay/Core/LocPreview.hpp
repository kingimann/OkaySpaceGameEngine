#pragma once
// Previewing a language and pseudo-localization without changing anything that is saved
// (docs/localization_tools.md).
//
// LocPreview looks texts up in a StringTable in the language you pick, optionally pseudo-
// localized, and never touches the table or the Localization service: no language switch, no
// listener fires, no "missing key" is logged, nothing is saved. A tool shows the result;
// closing the preview leaves the project as it was.
//
//   Pseudo-localization:  Start  ->  [!!! Šţåŕţ~~ !!!]
//   accents show text drawn from the wrong font, the extra length shows text that doesn't
//   fit, the markers show text that never went through the table (it stays plain).
//   Mirrored: the same text in reverse order, to try a layout for a right-to-left language.
#include "okay/Core/Localization.hpp"

#include <string>
#include <vector>

namespace okay {

struct LocPseudoOptions {
    bool accents = true;     ///< letters become accented look-alikes
    bool markers = true;     ///< wrapped in [!!! ... !!!]
    float expand = 0.3f;     ///< extra length as a share of the text (0.3 = 30% longer), as "~"
    bool mirror = false;     ///< characters in reverse order; placeholders stay whole
};

/// The pseudo-localized form of a message. {0} / {name} placeholders and plural blocks keep
/// their syntax (words inside a plural form are accented), so the result still formats.
std::string LocPseudoText(const std::string& message, const LocPseudoOptions& options = {});
/// `text` with its characters in reverse order per line (combining marks stay with their letter,
/// brackets swap sides, `{...}` placeholders stay whole): how a right-to-left text looks when
/// drawn by a left-to-right renderer that doesn't shape or reorder.
std::string LocMirrorText(const std::string& text);
/// True for languages written right to left: Arabic, Hebrew, Persian, Urdu, Yiddish...
bool LocIsRtl(const std::string& lang);

class LocPreview {
public:
    /// The table is read, never changed; it must outlive the preview.
    explicit LocPreview(const StringTable& table) : m_table(&table) {}

    /// The language to show ("" = the table's default). Localization::kPseudo is accepted:
    /// the default language, pseudo-localized.
    void SetLanguage(const std::string& code);
    /// The language shown: the one set, or the table's default.
    std::string Language() const;

    /// Pseudo-localize everything shown.
    void SetPseudo(bool on, const LocPseudoOptions& options = LocPseudoOptions{});
    bool Pseudo() const { return m_pseudo; }
    const LocPseudoOptions& PseudoOptions() const { return m_options; }

    /// Draw right-to-left languages mirrored, as the engine's left-to-right text would need
    /// them to look right (default on). Off to see the stored order.
    void SetMirrorRtl(bool on) { m_mirrorRtl = on; }
    /// True when the shown language is written right to left.
    bool Rtl() const { return LocIsRtl(Language()); }

    /// The text of a key in the shown language, else the default language's, else the key.
    std::string Get(const std::string& key) const;
    /// False (and `out` untouched) when neither language has text for the key.
    bool TryGet(const std::string& key, std::string& out) const;
    /// Get(key) with its {0} / {name} / plural placeholders filled in.
    std::string LocalizeArgs(const std::string& key, const std::vector<LocArg>& args) const;
    template <class... A> std::string Localize(const std::string& key, A&&... args) const {
        return LocalizeArgs(key, std::vector<LocArg>{LocArg(std::forward<A>(args))...});
    }
    /// A message as the preview shows it (pseudo-localized and mirrored as set), placeholders unfilled.
    std::string Render(const std::string& message) const;

    /// A table with one language, the shown one, whose texts are the preview's (accents and
    /// length as set; not mirrored). `Localization::SetTable` takes it to see the preview in a
    /// running game: a copy, so the project's table is untouched.
    StringTable Bake() const;

private:
    bool Mirrored() const { return m_options.mirror || (m_mirrorRtl && Rtl()); }
    const StringTable* m_table;
    std::string m_language;
    bool m_pseudo = false;
    LocPseudoOptions m_options;
    bool m_mirrorRtl = true;
};

} // namespace okay
