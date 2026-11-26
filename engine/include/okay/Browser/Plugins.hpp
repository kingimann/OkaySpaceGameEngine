#pragma once
// Browser (page engine): browser plugins. A plugin is a JSON manifest (RFC 8259, read with okay/Core/Json.hpp) that says what it
// adds to the browser: toolbar buttons, page actions (buttons that show on the sites they name), content rules (addresses to
// block, style sheets to add to the sites they name) and sections of the new-tab page. A plugin runs no code: it has no file
// and no network access of its own. It only gets what it asks for in "permissions" AND the browser (the person) grants; the
// rest of its manifest is ignored. Style sheets it adds cannot load anything (@import and url() are taken out), its buttons
// can only open http / https addresses, and its rules can only block, never send a request elsewhere.
//
// The browser program (shared/BrowserApp) keeps one BrowserPlugins, asks it before every fetch (Blocked), for the style sheets
// of a page (StylesFor), for its toolbar and page-action buttons and for the new-tab page; the buttons are also commands of the
// shared plugin registry (editor/PluginRegistry.hpp, host "browser": shared/BrowserPluginCommands.hpp), so the same command
// palette, shortcuts and owner removal work for them. Standard library and okay/Core/Json.hpp only.
#include <cstddef>
#include <string>
#include <vector>

namespace okay::browser {

/// What a plugin may do (bits). A manifest asks for some; the browser grants some; only both count.
enum PluginPermission : unsigned {
    kPermToolbar = 1u << 0,      ///< "toolbar": buttons on the toolbar
    kPermPageActions = 1u << 1,  ///< "pageActions": buttons for the page shown
    kPermBlock = 1u << 2,        ///< "block": stop requests to addresses
    kPermStyles = 1u << 3,       ///< "styles": add style sheets to pages
    kPermNewTab = 1u << 4,       ///< "newTab": sections on the new-tab page
};
/// "toolbar" ... to its bit (0 for a name we do not know).
unsigned PermissionFromName(const std::string& name);
std::string PermissionNames(unsigned perms);   ///< "toolbar, block" (for the person deciding)

/// A button: on the toolbar, or a page action. Pressing it opens `open` (an http / https address), or turns the style sheet
/// `toggleStyle` on and off for the pages its plugin may style.
struct PluginButton {
    std::string plugin, id, title, tooltip;
    std::string open;           ///< "" or an http(s) address
    std::string toggleStyle;    ///< "" or CSS (with nothing it could load)
    std::vector<std::string> sites;   ///< page actions: the sites it shows on ("example.com", "*.example.com"; empty: every site)
    bool on = false;            ///< toggleStyle: on now
};
struct PluginLink { std::string title, url; };
struct NewTabSection { std::string plugin, title; std::vector<PluginLink> links; };
/// A style sheet a plugin adds to the sites it names.
struct PluginStyle { std::string plugin; std::vector<std::string> sites; std::string css; };

struct BrowserPlugin {
    std::string id, name, version, description, source;   ///< source: where it was loaded from (a file path)
    unsigned requested = 0, granted = 0;
    bool enabled = true;
    std::vector<PluginButton> toolbar, pageActions;
    std::vector<std::string> block;   ///< address patterns ("*" matches anything; "||host" a host and its subdomains)
    std::vector<PluginStyle> styles;
    std::vector<NewTabSection> newTab;
    std::vector<std::string> problems;   ///< what in the manifest was refused, and why
    unsigned Allowed() const { return enabled ? (requested & granted) : 0u; }
};

/// What pressing a button did.
struct PluginEffect {
    enum Kind { None, OpenUrl, StyleOn, StyleOff } kind = None;
    std::string url;
};

struct PluginLimits {
    std::size_t maxManifestBytes = 256u << 10;
    std::size_t maxPlugins = 64;
    std::size_t maxItems = 64;        ///< buttons, rules, styles, sections, links (each, per plugin)
    std::size_t maxCssBytes = 64u << 10;
    std::size_t maxTextBytes = 200;   ///< titles and ids
};

class BrowserPlugins {
public:
    explicit BrowserPlugins(const PluginLimits& limits = PluginLimits()) : m_limits(limits) {}
    /// Read a manifest and add the plugin (one with the same id is replaced; it keeps the permissions granted before). False and
    /// `why` for a manifest that is not JSON, has no id or name, or is too big; parts that are wrong are skipped and listed in
    /// the plugin's problems.
    bool Load(const std::string& manifest, const std::string& source, std::string& why);
    bool Remove(const std::string& id);
    /// The browser's answer: the permissions this plugin may use (only those it asked for count).
    bool Grant(const std::string& id, unsigned perms);
    bool SetEnabled(const std::string& id, bool on);
    const BrowserPlugin* Find(const std::string& id) const;
    const std::vector<BrowserPlugin>& All() const { return m_plugins; }

    /// The toolbar buttons of the plugins that are on and may have them.
    std::vector<PluginButton> ToolbarButtons() const;
    /// The page actions for the page at `url`.
    std::vector<PluginButton> PageActionsFor(const std::string& url) const;
    /// Press a button (toolbar or page action) of a plugin, for the page at `url`.
    PluginEffect Press(const std::string& plugin, const std::string& button, const std::string& url);
    /// Whether a request for `url` is stopped, and which plugin stopped it.
    bool Blocked(const std::string& url, std::string* by = nullptr) const;
    /// The style sheets plugins add to the page at `url` (their content rules and the page actions turned on), in plugin order.
    std::vector<std::string> StylesFor(const std::string& url) const;
    std::vector<NewTabSection> NewTabSections() const;
    /// The new-tab page's plugin sections as HTML (every text escaped; links only http / https).
    std::string NewTabHtml() const;

private:
    PluginLimits m_limits;
    std::vector<BrowserPlugin> m_plugins;
};

/// Does `url` match an address pattern (a glob where '*' matches any run of characters; "||example.com^..." style is
/// "||host": that host and its subdomains)? ASCII case is ignored.
bool UrlMatchesPattern(const std::string& url, const std::string& pattern);
/// Does the host of `url` match a site ("example.com" or "*.example.com": the host and its subdomains)?
bool SiteMatches(const std::string& url, const std::string& site);
/// CSS with everything that could load something taken out: @import rules and url(...) values (replaced by "none").
std::string InertCss(const std::string& css);

} // namespace okay::browser
