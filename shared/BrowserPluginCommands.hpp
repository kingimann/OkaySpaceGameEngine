#pragma once
// Browser plugins (engine/include/okay/Browser/Plugins.hpp) as commands of the shared plugin registry (editor/PluginRegistry.hpp),
// the way its header says the browser uses it: the browser's own PluginRegistry, one command per toolbar button and page action,
// owned by "browser-plugin:<id>" so RemoveOwner takes a plugin's commands away when it is turned off or reloaded. The command
// palette, shortcuts and menus then list them like the Script Editor's plugin commands. Header only; the browser program links
// editor/PluginRegistry.cpp.
#include "../editor/PluginRegistry.hpp"
#include "okay/Browser/Plugins.hpp"

#include <functional>
#include <string>

namespace browserplugins {

inline std::string OwnerOf(const std::string& pluginId) { return "browser-plugin:" + pluginId; }

/// Put the plugins' buttons into `reg` (the old ones of every plugin removed first). Running a command presses the button for the
/// page shown (`currentUrl()`) and hands the effect to `apply` (open the address, lay the page out again with the style on / off).
/// Returns how many commands were added.
inline int SyncCommands(okay::browser::BrowserPlugins& plugins, pluginreg::PluginRegistry& reg, std::function<std::string()> currentUrl,
                        std::function<void(const okay::browser::PluginEffect&)> apply) {
    namespace br = okay::browser;
    for (const br::BrowserPlugin& p : plugins.All()) reg.RemoveOwner(OwnerOf(p.id));
    int added = 0;
    auto add = [&](const br::PluginButton& b, const char* category) {
        pluginreg::Command c;
        c.id = "browser." + b.plugin + "." + b.id;
        c.title = b.title;
        c.category = category;
        c.owner = OwnerOf(b.plugin);
        const std::string plugin = b.plugin, button = b.id;
        c.run = [&plugins, currentUrl, apply, plugin, button]() {
            const br::PluginEffect e = plugins.Press(plugin, button, currentUrl ? currentUrl() : std::string());
            if (apply && e.kind != br::PluginEffect::None) apply(e);
        };
        if (reg.AddCommand(std::move(c))) ++added;
    };
    for (const br::PluginButton& b : plugins.ToolbarButtons()) add(b, "Browser toolbar");
    for (const br::BrowserPlugin& p : plugins.All()) {
        reg.SetOwnerName(OwnerOf(p.id), p.name);
        if (!(p.Allowed() & br::kPermPageActions)) continue;
        for (const br::PluginButton& b : p.pageActions) add(b, "Page action");
    }
    return added;
}

} // namespace browserplugins
