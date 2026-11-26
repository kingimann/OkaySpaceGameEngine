// Browser plugins (engine/include/okay/Browser/Plugins.hpp, shared/BrowserPluginCommands.hpp): loading a manifest, what a
// plugin gets only when it asks AND the browser grants, a sample plugin with every kind of addition (toolbar button, page
// action, block rules, a style sheet, a new-tab section), that a plugin cannot reach files or the network (http(s) links only,
// style sheets that load nothing, rules that only block), hostile manifests, and the buttons as commands of the shared plugin
// registry (editor/PluginRegistry.hpp) with the owner removal it gives.
#include "test_framework.hpp"
#include "okay/Browser/Plugins.hpp"
#include "BrowserPluginCommands.hpp"

#include <string>

using namespace okay::browser;

// The sample plugin: what a person would write.
static const char* kSample = R"J({
  "id": "reading-tools", "name": "Reading Tools", "version": "1.0", "description": "Calmer pages.",
  "permissions": ["toolbar", "pageActions", "block", "styles", "newTab"],
  "toolbar": [ {"id": "docs", "title": "Docs", "open": "https://docs.example.test/"} ],
  "pageActions": [ {"id": "dark", "title": "Dark page", "sites": ["*.news.test"],
                    "toggleStyle": "body { background: #111 url(http://evil.test/pixel.png); color: #eee } @import 'http://evil.test/x.css';"} ],
  "block": ["||ads.test", "*/tracker.js*"],
  "styles": [ {"sites": ["news.test", "*.news.test"], "css": "aside.ad { display: none }"} ],
  "newTab": [ {"title": "Reading <list>", "links": [ {"title": "News & more", "url": "https://news.test/"},
                                                    {"title": "Bad", "url": "javascript:alert(1)"} ]} ]
})J";

int main() {
    RUN_SUITE("browser plugins");
    {
        BrowserPlugins plugins;
        std::string why;
        CHECK(plugins.Load(kSample, "/plugins/reading.json", why));
        const BrowserPlugin* p = plugins.Find("reading-tools");
        CHECK(p && p->name == "Reading Tools" && p->requested == (kPermToolbar | kPermPageActions | kPermBlock | kPermStyles | kPermNewTab));
        CHECK(p && p->toolbar.size() == 1 && p->pageActions.size() == 1 && p->block.size() == 2 && p->styles.size() == 1 && p->newTab.size() == 1);
        CHECK(p && p->newTab[0].links.size() == 1 && p->problems.size() == 1);   // the javascript: link is refused and listed
        // nothing works until the browser grants it
        CHECK(plugins.ToolbarButtons().empty());
        CHECK(!plugins.Blocked("https://ads.test/banner.js"));
        CHECK(plugins.StylesFor("https://news.test/").empty());
        CHECK(plugins.NewTabSections().empty());
        CHECK(plugins.Press("reading-tools", "docs", "https://x.test/").kind == PluginEffect::None);

        // grant only some: only those work
        plugins.Grant("reading-tools", kPermToolbar | kPermBlock);
        CHECK(plugins.ToolbarButtons().size() == 1 && plugins.PageActionsFor("https://www.news.test/a").empty());
        std::string by;
        CHECK(plugins.Blocked("https://ads.test/banner.js", &by) && by == "reading-tools");
        CHECK(plugins.Blocked("https://cdn.ads.test/x"));
        CHECK(!plugins.Blocked("https://badads.test/x"));   // a host is matched whole, not as a suffix of a word
        CHECK(plugins.Blocked("https://site.test/js/tracker.js?v=2"));
        CHECK(!plugins.Blocked("https://site.test/js/app.js"));
        const PluginEffect open = plugins.Press("reading-tools", "docs", "https://x.test/");
        CHECK(open.kind == PluginEffect::OpenUrl && open.url == "https://docs.example.test/");
        CHECK(plugins.StylesFor("https://news.test/").empty());   // styles not granted

        // grant everything
        plugins.Grant("reading-tools", ~0u);
        CHECK(plugins.StylesFor("https://news.test/").size() == 1 && plugins.StylesFor("https://news.test/")[0] == "aside.ad { display: none }");
        CHECK(plugins.StylesFor("https://other.test/").empty());
        CHECK(plugins.PageActionsFor("https://www.news.test/a").size() == 1);
        CHECK(plugins.PageActionsFor("https://other.test/").empty());
        CHECK(plugins.PageActionsFor("about:newtab").empty());
        CHECK(plugins.Press("reading-tools", "dark", "https://www.news.test/a").kind == PluginEffect::StyleOn);
        const std::vector<std::string> on = plugins.StylesFor("https://www.news.test/a");
        CHECK(on.size() == 2);
        // the toggled sheet can load nothing: the url() and the @import are gone
        CHECK(on.size() == 2 && on[1].find("evil.test") == std::string::npos && on[1].find("@import") == std::string::npos &&
              on[1].find("background: #111 none") != std::string::npos);
        CHECK(plugins.Press("reading-tools", "dark", "https://www.news.test/a").kind == PluginEffect::StyleOff);
        CHECK(plugins.StylesFor("https://www.news.test/a").size() == 1);
        // the new-tab page: escaped text, only the http(s) link
        const std::string html = plugins.NewTabHtml();
        CHECK(html.find("Reading &lt;list&gt;") != std::string::npos && html.find("News &amp; more") != std::string::npos);
        CHECK(html.find("javascript") == std::string::npos && html.find("https://news.test/") != std::string::npos);

        // turned off: nothing; reloaded: it keeps what was granted
        plugins.SetEnabled("reading-tools", false);
        CHECK(plugins.ToolbarButtons().empty() && !plugins.Blocked("https://ads.test/"));
        plugins.SetEnabled("reading-tools", true);
        CHECK(plugins.Load(kSample, "/plugins/reading.json", why));
        CHECK(plugins.All().size() == 1 && plugins.Find("reading-tools")->granted == ~0u && plugins.Blocked("https://ads.test/"));
        CHECK(plugins.Remove("reading-tools") && plugins.All().empty());
    }
    {
        // a part the plugin did not ask permission for is dropped and listed, even when the browser grants everything
        BrowserPlugins plugins;
        std::string why;
        CHECK(plugins.Load(R"({"id":"sneaky","name":"Sneaky","permissions":["toolbar"],"block":["*"],
                              "toolbar":[{"id":"f","title":"Files","open":"file:///etc/passwd"},{"id":"s","title":"Style","toggleStyle":"p{}"}]})", "", why));
        plugins.Grant("sneaky", ~0u);
        CHECK(!plugins.Blocked("https://any.test/"));
        CHECK(plugins.ToolbarButtons().empty());   // file: refused, a style without 'styles' refused
        CHECK(plugins.Find("sneaky")->problems.size() == 3);
    }
    {
        // broken and hostile manifests
        BrowserPlugins plugins;
        std::string why;
        CHECK(!plugins.Load("not json", "", why) && !why.empty());
        CHECK(!plugins.Load("[1,2]", "", why));
        CHECK(!plugins.Load(R"({"name":"no id"})", "", why));
        CHECK(!plugins.Load(R"({"id":"Bad Id!","name":"x"})", "", why));
        CHECK(!plugins.Load(std::string(300000, ' ') + "{}", "", why));
        std::string deep = R"({"id":"d","name":"d","x":)";
        for (int i = 0; i < 5000; ++i) deep += "[";
        CHECK(!plugins.Load(deep, "", why));
        std::string many = R"({"id":"m","name":"m","permissions":["toolbar","block"],"block":[)";
        for (int i = 0; i < 1000; ++i) many += std::string(i ? "," : "") + "\"*a*a*a*a*a*a*a*a*b\"";
        many += R"(],"toolbar":[)";
        for (int i = 0; i < 200; ++i) many += std::string(i ? "," : "") + "{\"id\":\"b" + std::to_string(i) + "\",\"title\":\"" + std::string(500, 'x') + "\",\"open\":\"https://a.test/\"}";
        many += "]}";
        CHECK(plugins.Load(many, "", why));
        plugins.Grant("m", ~0u);
        const BrowserPlugin* m = plugins.Find("m");
        CHECK(m && m->block.size() == PluginLimits().maxItems && m->toolbar.size() == PluginLimits().maxItems);
        CHECK(m && m->toolbar[0].title.size() == PluginLimits().maxTextBytes);
        CHECK(!plugins.Blocked(std::string(4000, 'a')));   // many stars against a long address: no backtracking blow-up
        for (int i = 0; i < 100; ++i) plugins.Load("{\"id\":\"p" + std::to_string(i) + "\",\"name\":\"p\"}", "", why);
        CHECK(plugins.All().size() == PluginLimits().maxPlugins);
    }
    {
        // patterns and sites
        CHECK(UrlMatchesPattern("https://A.test/x", "https://a.test/*"));
        CHECK(!UrlMatchesPattern("https://a.test/x", "https://b.test/*"));
        CHECK(SiteMatches("https://www.news.test:8080/a", "*.news.test") && SiteMatches("https://news.test/", "*.news.test"));
        CHECK(!SiteMatches("https://fakenews.test/", "*.news.test") && !SiteMatches("about:blank", "*"));
        CHECK(InertCss("a{background:URL( 'x.png' )}") == "a{background:none}");
        CHECK(InertCss("a{content:'url(x)'}") == "a{content:'url(x)'}");   // inside a string it loads nothing
        CHECK(InertCss("@IMPORT url(x.css) screen; p{}") == " p{}");
        CHECK(InertCss("a{b:image-set('x.png' 1x)}") == "a{b:none}");
    }
    {
        // the buttons as commands of the shared plugin registry (host "browser")
        BrowserPlugins plugins;
        std::string why;
        CHECK(plugins.Load(kSample, "", why));
        plugins.Grant("reading-tools", ~0u);
        pluginreg::PluginRegistry reg;
        PluginEffect last;
        int added = browserplugins::SyncCommands(plugins, reg, [] { return std::string("https://www.news.test/a"); },
                                                 [&](const PluginEffect& e) { last = e; });
        CHECK(added == 2);
        const pluginreg::Command* docs = reg.FindCommand("browser.reading-tools.docs");
        CHECK(docs && docs->category == "Browser toolbar" && docs->owner == "browser-plugin:reading-tools");
        CHECK(reg.Run("browser.reading-tools.docs") && last.kind == PluginEffect::OpenUrl && last.url == "https://docs.example.test/");
        CHECK(reg.Run("browser.reading-tools.dark") && last.kind == PluginEffect::StyleOn);
        CHECK(reg.OwnerName("browser-plugin:reading-tools") == "Reading Tools");
        // syncing again does not duplicate; turning the plugin off and syncing removes its commands
        CHECK(browserplugins::SyncCommands(plugins, reg, nullptr, nullptr) == 2 && reg.Commands().size() == 2);
        plugins.SetEnabled("reading-tools", false);
        CHECK(browserplugins::SyncCommands(plugins, reg, nullptr, nullptr) == 0 && reg.Commands().empty());
    }
    TEST_MAIN_RESULT();
}
