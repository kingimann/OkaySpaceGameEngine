// Localization: the .okayloc table and CSV formats, the fallback chain and its
// one-time reports, {0} / {name} / plural formatting, pseudo-localization, the
// player's language setting, localized widgets refreshing on a change of
// language, the scene record, the script builtins, the C#-style statics, the
// visual-script nodes and the font fallback for other scripts.
#include "test_framework.hpp"
#include <Okay.hpp>
#include "okay/Core/Localization.hpp"
#include "okay/Core/Prefs.hpp"
#include "okay/Core/Utf8.hpp"
#include "okay/Scene/SceneLocalization.hpp"
#include "okay/Scripting/NextScriptVM.hpp"
#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <sstream>
#include <string>
#include <vector>

using namespace okay;

static std::vector<std::string> g_warnings;

// A three-language table: English (default), French, Japanese.
static StringTable MakeTable() {
    StringTable t;
    t.AddLanguage("en", "English");
    t.AddLanguage("fr", "Français");
    t.AddLanguage("ja", "日本語");
    t.defaultLanguage = "en";
    t.Set("menu.title", "en", "Main Menu");
    t.Set("menu.title", "fr", "Menu principal");
    t.Set("menu.title", "ja", "メインメニュー");
    t.Set("menu.start", "en", "Start");
    t.Set("menu.start", "fr", "Commencer");
    t.Set("hud.coins", "en", "{0, plural, =0 {no coins} one {# coin} other {# coins}}");
    t.Set("hud.coins", "fr", "{0, plural, =0 {aucune pièce} one {# pièce} other {# pièces}}");
    t.Add("hud.coins").note = "{0} is how many coins the player carries";
    t.Set("greet", "en", "Hello, {player}!");
    t.Set("greet", "fr", "Bonjour, {player} !");
    t.Set("sign", "en", "Shop");
    t.Set("sign", "fr", "Boutique");
    t.Set("btn.ok", "en", "OK");
    t.Set("btn.ok", "fr", "D'accord");
    return t;
}

static float G(ScriptComponent* sc, const char* n) { return sc->VM()->GetGlobal(n).AsFloat(); }
static std::string GS(ScriptComponent* sc, const char* n) { return sc->VM()->GetGlobal(n).AsString(); }

int main() {
    RUN_SUITE("localization");
    Log::sink = [](Log::Level l, const std::string& m) { if (l == Log::Level::Warning) g_warnings.push_back(m); };

    // ---- The .okayloc format: write, parse, the same table back ----
    {
        StringTable t = MakeTable();
        t.Set("story.intro", "en", "Line one\nLine two with \"quotes\", a \\ and a\ttab");
        t.Add("todo.empty");   // a key nobody wrote text for yet stays
        t.FindLanguage("ja")->font = "Assets/Fonts/NotoSansJP.ttf";
        const std::string text = t.Write();
        CHECK(text.rfind("okayloc 1\n", 0) == 0);
        CHECK(text.find("language en \"English\" default") != std::string::npos);
        CHECK(text.find("language ja \"日本語\" font \"Assets/Fonts/NotoSansJP.ttf\"") != std::string::npos);
        CHECK(text.find("key hud.coins \"{0} is how many coins the player carries\"") != std::string::npos);
        CHECK(text.find("text fr menu.start \"Commencer\"") != std::string::npos);
        CHECK(text.find("\\n") != std::string::npos);   // the newline is escaped: one record per line
        StringTable r;
        std::string err;
        CHECK(r.Parse(text, &err));
        CHECK(r.Write() == text);
        CHECK(r.languages.size() == 3 && r.DefaultLanguage() == "en");
        CHECK(r.FindLanguage("fr") && r.FindLanguage("fr")->name == "Français");
        CHECK(r.FindLanguage("ja") && r.FindLanguage("ja")->font == "Assets/Fonts/NotoSansJP.ttf");
        CHECK(r.Text("story.intro", "en") && *r.Text("story.intro", "en") == "Line one\nLine two with \"quotes\", a \\ and a\ttab");
        CHECK(r.Has("todo.empty") && !r.Text("todo.empty", "en"));
        CHECK(r.Find("hud.coins") && r.Find("hud.coins")->note == "{0} is how many coins the player carries");
        CHECK(r.Size() == t.Size());

        // Hand-written: comments, blank lines, CRLF, a value over two lines, unknown records skipped.
        const std::string hand =
            "\xEF\xBB\xBF# My game's strings\r\n"
            "okayloc 1\r\n"
            "\r\n"
            "language de \"Deutsch\"\r\n"
            "language en \"English\" default\r\n"
            "   # an indented comment\r\n"
            "glossary sword \"a record a newer build might write\"\r\n"
            "key quest.title \"The title in the journal\"\r\n"
            "text en quest.title \"The Lost Sword\"\r\n"
            "text de quest.title \"Das verlorene\r\nSchwert\"\r\n"
            "text de \"key with spaces\" \"Wert # kein Kommentar\"\r\n";
        StringTable h;
        CHECK(h.Parse(hand, &err));
        CHECK(h.DefaultLanguage() == "en" && h.languages.size() == 2);
        CHECK(h.Text("quest.title", "de") && *h.Text("quest.title", "de") == "Das verlorene\nSchwert");
        CHECK(h.Text("key with spaces", "de") && *h.Text("key with spaces", "de") == "Wert # kein Kommentar");
        CHECK(h.Size() == 2 && !h.Has("sword"));
        StringTable h2;
        CHECK(h2.Parse(h.Write(), &err) && h2.Write() == h.Write());   // keys with spaces come back quoted
        CHECK(!h2.Parse("not a table\n", &err) && !err.empty());

        // File round trip.
        const std::string path = (std::filesystem::temp_directory_path() / "okay_test_strings.okayloc").string();
        CHECK(t.Save(path));
        StringTable f;
        CHECK(f.Load(path, &err) && f.Write() == text);
        std::filesystem::remove(path);
    }

    // ---- CSV for translators: export, import, quoting, BOM, separators ----
    {
        StringTable t = MakeTable();
        t.Set("tricky", "en", "Commas, \"quotes\" and\na newline");
        t.Set("tricky", "fr", "Virgules, « guillemets » et\nun retour");
        const std::string csv = t.ExportCsv();
        CHECK(csv.rfind("\xEF\xBB\xBF" "key,note,en,fr,ja\r\n", 0) == 0);
        CHECK(csv.find("\"Commas, \"\"quotes\"\" and\na newline\"") != std::string::npos);
        StringTable back;
        back.AddLanguage("en");
        int rows = 0;
        std::string err;
        CHECK(back.ImportCsv(csv, &err, &rows));
        CHECK(rows == (int)t.Size());
        CHECK(back.languages.size() == 3);
        for (const auto& kv : t.Entries())
            for (const auto& l : t.languages) {
                const std::string* a = t.Text(kv.first, l.code);
                const std::string* b = back.Text(kv.first, l.code);
                CHECK((a == nullptr) == (b == nullptr));
                if (a && b) CHECK(*a == *b);
            }
        CHECK(back.Find("hud.coins") && back.Find("hud.coins")->note == t.Find("hud.coins")->note);

        // A translator's file without a BOM, semicolons (a European spreadsheet), CRLF,
        // an empty cell (keeps the old text) and a new language column.
        const std::string fromTranslator =
            "Key;Comment;fr;de\r\n"
            "menu.start;;Jouer;Starten\r\n"
            "menu.title;;;Hauptmenü\r\n"
            "\"new.key\";\"made by the translator\";\"Nouveau; ok\";\r\n";
        CHECK(back.ImportCsv(fromTranslator, &err, &rows));
        CHECK(rows == 3);
        CHECK(*back.Text("menu.start", "fr") == "Jouer" && *back.Text("menu.start", "de") == "Starten");
        CHECK(*back.Text("menu.title", "fr") == "Menu principal");   // an empty cell doesn't erase
        CHECK(*back.Text("new.key", "fr") == "Nouveau; ok" && back.Find("new.key")->note == "made by the translator");
        CHECK(back.FindLanguage("de") != nullptr);
        CHECK(!back.ImportCsv("en,fr\nhello,bonjour\n", &err) && err.find("key") != std::string::npos);
    }

    // ---- The fallback chain: current language, then the default, then the key; reported once ----
    {
        Localization::Reset();
        Localization::SetTable(MakeTable());
        CHECK(Localization::Language() == "en");
        CHECK(Localization::Get("menu.start") == "Start");
        CHECK(Localization::SetLanguage("fr"));
        CHECK(Localization::Get("menu.start") == "Commencer");
        CHECK(Localization::SetLanguage("ja"));
        g_warnings.clear();
        CHECK(Localization::Get("menu.title") == "メインメニュー");
        CHECK(Localization::Get("menu.start") == "Start");          // no Japanese: the default language
        CHECK(Localization::Get("menu.start") == "Start");
        CHECK(Localization::Get("no.such.key") == "no.such.key");   // nowhere: the key itself
        CHECK(Localization::Get("no.such.key") == "no.such.key");
        CHECK(g_warnings.size() == 2);                               // each miss reported once
        const auto& rep = Localization::Reported();
        CHECK(std::find(rep.begin(), rep.end(), "ja: menu.start") != rep.end());
        CHECK(std::find(rep.begin(), rep.end(), "?: no.such.key") != rep.end());
        std::string out;
        CHECK(!Localization::TryGet("no.such.key", out));
        CHECK(Localization::TryGet("menu.start", out) && out == "Start");
        CHECK(!Localization::SetLanguage("xx") && Localization::Language() == "ja");   // unknown: nothing changes
        CHECK(Localization::Languages() == (std::vector<std::string>{"en", "fr", "ja"}));
        CHECK(Localization::LanguageName("fr") == "Français" && Localization::LanguageName("zz") == "zz");
    }

    // ---- Arguments and plurals ----
    {
        Localization::Reset();
        Localization::SetTable(MakeTable());
        CHECK(Localize("hud.coins", 0) == "no coins");
        CHECK(Localize("hud.coins", 1) == "1 coin");
        CHECK(Localize("hud.coins", 7) == "7 coins");
        CHECK(Localize("hud.coins", 2.5) == "2.5 coins");
        CHECK(Localize("greet", LocArg::Named("player", "Ana")) == "Hello, Ana!");
        Localization::SetLanguage("fr");
        CHECK(Localize("hud.coins", 0) == "aucune pièce");
        CHECK(Localize("hud.coins", 1) == "1 pièce");
        CHECK(Localize("hud.coins", 1.5) == "1.5 pièce");   // French: 0 <= n < 2 is "one"
        CHECK(Localize("hud.coins", 2) == "2 pièces");
        CHECK(Localize("greet", LocArg::Named("player", "Ana")) == "Bonjour, Ana !");
        // Positional and named, a missing argument kept, literal braces.
        CHECK(Localization::Format("{0} vs {1}", {LocArg("A"), LocArg(3)}) == "A vs 3");
        CHECK(Localization::Format("{who} has {0} {missing}", {LocArg::Named("who", "Bo"), LocArg(5)}) == "Bo has Bo {missing}");
        CHECK(Localization::Format("{{literal}} {0}", {LocArg("x")}) == "{literal} x");
        CHECK(Localization::Format("{n, plural, one {# item for {who}} other {# items for {who}}}",
                                   {LocArg::Named("n", 3), LocArg::Named("who", "Cy")}, "en") == "3 items for Cy");
        CHECK(Localization::Format("{0, plural, one {one} other {#}}", {LocArg("1")}, "en") == "one");   // a numeric string
        // The hook for languages with more forms (Russian: one / few / many).
        Localization::SetPluralRule("ru", [](double n) {
            const long i = (long)n, m10 = i % 10, m100 = i % 100;
            if (m10 == 1 && m100 != 11) return Localization::PluralForm::One;
            if (m10 >= 2 && m10 <= 4 && (m100 < 12 || m100 > 14)) return Localization::PluralForm::Few;
            return Localization::PluralForm::Many;
        });
        const std::string ru = "{0, plural, one {# монета} few {# монеты} many {# монет} other {# монеты}}";
        CHECK(Localization::Format(ru, {LocArg(1)}, "ru") == "1 монета");
        CHECK(Localization::Format(ru, {LocArg(3)}, "ru") == "3 монеты");
        CHECK(Localization::Format(ru, {LocArg(5)}, "ru") == "5 монет");
        CHECK(Localization::Format(ru, {LocArg(21)}, "ru-RU") == "21 монета");   // the base language's rule
        CHECK(Localization::Plural("ja", 1) == Localization::PluralForm::Other);
        CHECK(Localization::Plural("en", 1) == Localization::PluralForm::One);
    }

    // ---- Pseudo-localization ----
    {
        Localization::Reset();
        Localization::SetTable(MakeTable());
        const std::string p = Localization::Pseudo("Press {0} to start");
        CHECK(p.front() == '[' && p.back() == ']');
        CHECK(p.find("{0}") != std::string::npos);                         // placeholders untouched
        CHECK(p.find("Press") == std::string::npos && p.find("š") != std::string::npos);
        CHECK(utf8::Length(p) >= (std::size_t)(utf8::Length(std::string("Press {0} to start")) * 1.3));
        CHECK(Localization::SetLanguage(Localization::kPseudo));
        CHECK(Localization::Get("menu.start") == Localization::Pseudo("Start"));
        CHECK(Localize("hud.coins", 2) == Localization::Pseudo("2 coins"));
        const std::vector<std::string> langs = Localization::Languages();
        CHECK(std::find(langs.begin(), langs.end(), "qps-ploc") == langs.end());
    }

    // ---- The player's language: OS locale on the first run, then the saved choice ----
    {
        Localization::Reset();
        Localization::SetTable(MakeTable());
        CHECK(Localization::PickLanguage({"fr_CA.UTF-8", "en_US"}) == "fr");   // the base language
        CHECK(Localization::PickLanguage({"de-DE", "ja-JP"}) == "ja");          // the first that matches
        CHECK(Localization::PickLanguage({"C", "es"}) == "en");                 // none: the default
        StringTable pt;
        pt.AddLanguage("en"); pt.AddLanguage("pt-BR");
        Localization::SetTable(pt);
        CHECK(Localization::PickLanguage({"pt"}) == "pt-BR");                   // a regional table language
        Localization::SetTable(MakeTable());
        Prefs::Delete(Localization::kPrefsKey);
        Localization::LoadSettings({"fr-FR"});
        CHECK(Localization::Language() == "fr");                  // first run: from the OS
        CHECK(!Prefs::Has(Localization::kPrefsKey));              // ... and not stored as a choice
        Localization::SetLanguage("ja");
        Localization::SaveSettings();
        CHECK(Prefs::GetString(Localization::kPrefsKey) == "ja");
        const std::string prefsPath = (std::filesystem::temp_directory_path() / "okay_test_loc.okayprefs").string();
        CHECK(Prefs::Save(prefsPath));
        Prefs::Clear();
        Localization::SetLanguage("en");
        CHECK(Prefs::Load(prefsPath));
        Localization::LoadSettings({"fr-FR"});
        CHECK(Localization::Language() == "ja");                  // the saved choice wins over the OS
        std::filesystem::remove(prefsPath);
        Prefs::Delete(Localization::kPrefsKey);
    }

    // ---- Listeners and the version ----
    {
        Localization::Reset();
        Localization::SetTable(MakeTable());
        std::vector<std::string> heard;
        const int id = Localization::AddListener([&](const std::string& l) { heard.push_back(l); });
        const std::uint32_t v0 = Localization::Version();
        Localization::SetLanguage("fr");
        Localization::SetLanguage("fr");   // no change: no event
        Localization::SetLanguage("en");
        CHECK(heard == (std::vector<std::string>{"fr", "en"}));
        CHECK(Localization::Version() == v0 + 2);
        Localization::Table().Set("menu.start", "en", "Play");
        Localization::TableChanged();
        CHECK(Localization::Version() == v0 + 3 && heard.size() == 2);   // the table changed, not the language
        Localization::RemoveListener(id);
        Localization::SetLanguage("fr");
        CHECK(heard.size() == 2);
        LocalizedText lt;
        std::string field = "literal";
        CHECK(!lt.Apply(field) && field == "literal");   // no key: the literal stays
        lt.key = "menu.start";
        CHECK(lt.Apply(field) && field == "Commencer");
        CHECK(!lt.Apply(field));                         // nothing changed since
        Localization::SetLanguage("en");
        CHECK(lt.Apply(field) && field == "Play");
    }

    // ---- A change of language reaches UI text, a 3D text, a button, a hold prompt and scripts ----
    {
        Localization::Reset();
        Localization::SetTable(MakeTable());
        StringTable& tt = Localization::Table();
        for (const auto& k : HoldPrompt::LocKeys()) tt.Set(k.first, "en", k.second);
        tt.Set("hold.pickup", "fr", "{0} pour ramasser");
        Localization::TableChanged();
        Scene s("L");
        GameObject* ui = s.CreateGameObject("Title");
        auto* title = ui->AddComponent<TextRenderer>();
        title->screenSpace = true; title->text = "Main Menu (literal)"; title->loc.key = "menu.title";
        GameObject* sign = s.CreateGameObject("Sign3D");
        auto* sign3d = sign->AddComponent<TextRenderer>();   // world-space 3D text
        sign3d->text = "Shop"; sign3d->loc.key = "sign";
        GameObject* b = s.CreateGameObject("Start");
        auto* btn = b->AddComponent<UIButton>();
        btn->loc.key = "menu.start";
        GameObject* lit = s.CreateGameObject("Literal");
        auto* plain = lit->AddComponent<TextRenderer>();
        plain->text = "Score: 10";                           // literal text keeps working
        GameObject* label = s.CreateGameObject("Hold Prompt");
        auto* prompt = label->AddComponent<TextRenderer>();
        GameObject* player = s.CreateGameObject("Player");
        auto* hp = player->AddComponent<HoldPrompt>();
        hp->localized = true;
        GameObject* crate = s.CreateGameObject("Crate");
        crate->transform->localPosition = {0.5f, 0.0f, 0.0f};
        auto* pk = crate->AddComponent<ItemPickup>();
        pk->item = "crate"; pk->holdInHands = true; pk->interactKey = 'e';
        auto* sc = player->AddComponent<ScriptComponent>("okayscript");
        std::string err;
        CHECK(sc->LoadSource("var heard = \"\";\nvar count = 0;\n"
                             "function on_language_changed(code) { heard = code; count = count + 1; }\n", &err));
        auto* typed = player->AddComponent<ScriptComponent>(NextScriptVM::kLanguage);
        CHECK(typed->LoadSource("class T : Behaviour {\n    public string lang = \"\";\n"
                                "    public void OnLanguageChanged() { lang = Localization.Language; }\n}\n", &err));
        auto* vc = player->AddComponent<VisualScriptComponent>();
        CHECK(vc->LoadFromText("node 1 OnLanguageChanged\nentry OnLanguageChanged 1\n"
                               "node 2 SetVar lang\nnode 3 EventName\nexec 1 0 2\ndata 2 0 3 0\n"
                               "node 4 SetVar text\nnode 5 Localize menu.title\nexec 2 0 4\ndata 4 0 5 0\n", &err));
        s.Start();
        s.Update(1.0f / 60.0f);
        CHECK(title->text == "Main Menu" && sign3d->text == "Shop" && btn->label == "Start");
        CHECK(plain->text == "Score: 10");
        CHECK(prompt->text == "E to pick up");
        CHECK(GS(sc, "heard").empty());                        // no change yet
        Localization::SetLanguage("fr");
        s.Update(1.0f / 60.0f);
        CHECK(title->text == "Menu principal" && sign3d->text == "Boutique" && btn->label == "Commencer");
        CHECK(plain->text == "Score: 10");
        CHECK(prompt->text == "E pour ramasser");
        CHECK(GS(sc, "heard") == "fr" && G(sc, "count") == 1.0f);
        CHECK(typed->VM()->GetGlobal("lang").AsString() == "fr");
        CHECK(vc->GetVariable("lang").AsString() == "fr" && vc->GetVariable("text").AsString() == "Menu principal");
        s.Update(1.0f / 60.0f);
        CHECK(G(sc, "count") == 1.0f);                         // once per change
        // A text made after the change and one whose key changed catch up on the next frame.
        GameObject* late = s.CreateGameObject("Late");
        auto* lateText = late->AddComponent<TextRenderer>();
        lateText->loc.key = "btn.ok";
        title->loc.key = "menu.start";
        s.Update(1.0f / 60.0f);
        CHECK(lateText->text == "D'accord");
        LocalizeScene(s, true);   // the editor's walk picks up an edited key
        CHECK(title->text == "Commencer");
        // The keys in use, for Find in Scene and unused keys.
        const auto uses = CollectLocKeys(s);
        auto used = [&](const std::string& k) {
            return std::any_of(uses.begin(), uses.end(), [&](const LocKeyUse& u) { return u.key == k; });
        };
        CHECK(used("menu.start") && used("sign") && used("btn.ok") && used("hold.pickup") && !used("menu.title"));
        // A non-ASCII text measures by characters: the 8x8 font has no "é", a TTF stands in.
        TextRenderer accent;
        accent.text = "Café";
        accent.typeSpeed = 0.0f; accent.visibleChars = 3;
        CHECK(accent.DisplayText() == "Caf");
        accent.visibleChars = 4;
        CHECK(accent.DisplayText() == "Café");
    }

    // ---- Pause menu and loading screen ----
    {
        Localization::Reset();
        StringTable t = MakeTable();
        for (const auto& k : PauseMenu::LocKeys()) t.Set(k.key, "en", k.english);
        t.Set("pause.resume", "fr", "Reprendre");
        t.Set("pause.title", "fr", "Pause");
        t.Set("loading.title", "en", "Loading...");
        t.Set("loading.title", "fr", "Chargement...");
        t.Set("loading.tip1", "en", "Hold Shift to run");
        t.Set("loading.tip1", "fr", "Maintenez Maj pour courir");
        Localization::SetTable(t);
        Scene s("P");
        GameObject* host = s.CreateGameObject("Menu");
        auto* pm = host->AddComponent<PauseMenu>();
        pm->localized = true;
        pm->EnsureBuilt();
        GameObject* resume = s.Find("Pause_Resume");
        GameObject* ptitle = s.Find("Pause_Title");
        CHECK(resume && ptitle);
        if (resume && ptitle) {
            CHECK(resume->GetComponent<UIButton>()->loc.key == "pause.resume");
            CHECK(ptitle->GetComponent<TextRenderer>()->loc.key == "pause.title");
            Localization::SetLanguage("fr");
            LocalizeScene(s);
            CHECK(resume->GetComponent<UIButton>()->label == "Reprendre");
            CHECK(ptitle->GetComponent<TextRenderer>()->text == "Pause");
            pm->localized = false;   // back to literal words
            pm->ApplyLocalization();
            CHECK(resume->GetComponent<UIButton>()->loc.key.empty() && resume->GetComponent<UIButton>()->label == "Resume");
            CHECK(ptitle->GetComponent<TextRenderer>()->text == "Paused");
        }
        LoadingScreen ls;
        ls.localized = true;
        ls.title = "loading.title";
        ls.tips = {"loading.tip1"};
        ls.Start();
        const auto cmds = BuildLoadingScreen(ls, 800.0f, 600.0f);
        auto shows = [&](const std::string& txt) {
            return std::any_of(cmds.begin(), cmds.end(), [&](const LoadingDrawCmd& d) { return d.kind == LoadingDrawCmd::Kind::Text && d.text == txt; });
        };
        CHECK(shows("Chargement...") && shows("Maintenez Maj pour courir"));
        ls.localized = false;
        const auto lit = BuildLoadingScreen(ls, 800.0f, 600.0f);
        CHECK(std::any_of(lit.begin(), lit.end(), [](const LoadingDrawCmd& d) { return d.text == "loading.title"; }));
    }

    // ---- A running dialogue line, its speaker and choices follow the language; quests too ----
    {
        Localization::Reset();
        StringTable t = MakeTable();
        DialogueGraph g;
        g.name = "Old Smith";
        int lineId = 0, choiceId = 0;
        { DialogueNode& n = g.Add(DialogueNode::Kind::Line, 0, 0); n.speaker = "Smith"; n.text = "Welcome, traveller!"; lineId = n.id; }
        {
            DialogueNode& n = g.Add(DialogueNode::Kind::Choice, 0, 0);
            DialogueChoice buy; buy.text = "Buy";
            DialogueChoice leave; leave.text = "Leave";
            n.choices = {buy, leave};
            choiceId = n.id;
        }
        g.Find(lineId)->next = choiceId;
        g.start = lineId;
        const auto strings = DialogueStrings(g, g.name);
        CHECK(strings.size() == 4);   // the line, its speaker, two choices
        CHECK(DialogueLineKey("Old Smith", lineId) == "dlg.old_smith." + std::to_string(lineId));
        for (const auto& kv : strings) t.Set(kv.first, "en", kv.second);
        t.Set(DialogueLineKey(g.name, lineId), "fr", "Ça va, voyageur ?");
        t.Set(DialogueSpeakerKey("Smith"), "fr", "Forgeron");
        t.Set(DialogueChoiceKey(g.name, choiceId, 1), "fr", "Acheter");
        t.Set(DialogueChoiceKey(g.name, choiceId, 2), "fr", "Partir");
        QuestDef q;
        q.id = "find_sword"; q.title = "The Lost Sword";
        QuestObjective ob; ob.kind = QuestObjective::Kind::Custom; ob.description = "Find the sword";
        q.objectives = {ob};
        for (const auto& kv : QuestStrings(q)) t.Set(kv.first, "en", kv.second);
        t.Set(QuestTitleKey("find_sword"), "fr", "L'épée perdue");
        t.Set(QuestObjectiveKey("find_sword", 1), "fr", "Trouver l'épée");
        t.Set("quest.started", "fr", "Quête commencée");
        Localization::SetTable(t);

        Scene s("D");
        GameObject* npc = s.CreateGameObject("Smith");
        auto* r = npc->AddComponent<DialogueRunner>();
        r->SetGraph(g);
        r->typewriterSpeed = 10.0f;
        r->drawUI = false;
        s.Start();
        CHECK(r->StartDialogue());
        DialogueRunner::Line l = r->CurrentLine();
        CHECK(l.text == "Welcome, traveller!" && l.speaker == "Smith" && l.shownText.empty());
        Localization::SetLanguage("fr");
        l = r->CurrentLine();   // the line on screen switches with the language
        CHECK(l.text == "Ça va, voyageur ?" && l.speaker == "Forgeron");
        r->Update(0.2f);        // two characters typed: "Ç" is one character
        CHECK(r->CurrentLine().shownText == "Ça");
        r->Advance();           // finish typing
        r->Advance();           // on to the choice
        l = r->CurrentLine();
        CHECK(l.choice && l.choices == (std::vector<std::string>{"Acheter", "Partir"}));
        CHECK(l.text == "Ça va, voyageur ?");   // the question stays above the choices
        Localization::SetLanguage("en");
        CHECK(r->CurrentLine().choices == (std::vector<std::string>{"Buy", "Leave"}));
        // Text without a key in the table shows as typed.
        DialogueGraph plain = g;
        plain.name = "Untranslated";
        r->SetGraph(plain);
        CHECK(r->StartDialogue() && r->CurrentLine().text == "Welcome, traveller!");
        r->Stop();

        QuestLog* log = QuestLog::Get(&s, true);
        log->quests = {q};
        log->drawUI = false;
        Localization::SetLanguage("fr");
        CHECK(log->StartQuest("find_sword"));
        CHECK(!log->notifications.empty() && log->notifications.back() == "Quête commencée: L'épée perdue");
        CHECK(QuestObjectiveText(q, 0) == "Trouver l'épée" && QuestTitle(q) == "L'épée perdue");
        Localization::SetLanguage("ja");   // no Japanese: the default language's text
        CHECK(QuestTitle(q) == "The Lost Sword");
        const auto uses = CollectLocKeys(s);
        auto used = [&](const std::string& k) {
            return std::any_of(uses.begin(), uses.end(), [&](const LocKeyUse& u) { return u.key == k; });
        };
        CHECK(used(DialogueSpeakerKey("Smith")) && used(QuestTitleKey("find_sword")) && used("dialogue.talk") && used("quest.started"));
    }

    // ---- The scene record; an older scene without it loads unchanged ----
    {
        Scene s("R");
        GameObject* a = s.CreateGameObject("Label");
        auto* tr = a->AddComponent<TextRenderer>();
        tr->text = "Main Menu"; tr->loc.key = "menu.title";
        GameObject* b = s.CreateGameObject("Button");
        auto* btn = b->AddComponent<UIButton>();
        btn->label = "Start"; btn->loc.key = "menu \"start\"";
        b->AddComponent<UITooltip>()->loc.key = "tip.start";
        GameObject* c = s.CreateGameObject("Prompt");
        c->AddComponent<HoldPrompt>()->localized = true;
        GameObject* d = s.CreateGameObject("Literal");
        d->AddComponent<TextRenderer>()->text = "Plain";
        const std::string text = SceneSerializer::Serialize(s);
        CHECK(text.find("  loc text=\"menu.title\"") != std::string::npos);
        CHECK(text.find("button=\"menu \\\"start\\\"\" tooltip=\"tip.start\"") != std::string::npos);
        CHECK(text.find("  loc hold=1") != std::string::npos);
        std::size_t locLines = 0;
        for (std::size_t p = text.find("  loc "); p != std::string::npos; p = text.find("  loc ", p + 1)) ++locLines;
        CHECK(locLines == 3);   // literal text writes nothing
        Scene r("R2");
        std::string err;
        CHECK(SceneSerializer::Deserialize(r, text, &err));
        auto* rt = r.Find("Label") ? r.Find("Label")->GetComponent<TextRenderer>() : nullptr;
        auto* rb = r.Find("Button") ? r.Find("Button")->GetComponent<UIButton>() : nullptr;
        CHECK(rt && rt->loc.key == "menu.title" && rt->text == "Main Menu");
        CHECK(rb && rb->loc.key == "menu \"start\"" && r.Find("Button")->GetComponent<UITooltip>()->loc.key == "tip.start");
        CHECK(r.Find("Prompt") && r.Find("Prompt")->GetComponent<HoldPrompt>()->localized);
        CHECK(SceneSerializer::Serialize(r) == text);
        std::string older;
        {
            std::istringstream in(text); std::string line;
            while (std::getline(in, line)) if (line.rfind("  loc ", 0) != 0) older += line + "\n";
        }
        Scene o("O");
        CHECK(SceneSerializer::Deserialize(o, older, &err));
        auto* ot = o.Find("Label") ? o.Find("Label")->GetComponent<TextRenderer>() : nullptr;
        CHECK(ot && ot->loc.key.empty() && ot->text == "Main Menu");
        CHECK(o.Find("Prompt") && !o.Find("Prompt")->GetComponent<HoldPrompt>()->localized);
        // A duplicated (prefab) object keeps its key.
        Scene dup("D");
        CHECK(SceneSerializer::MergeFromText(dup, SceneSerializer::SerializeObject(*a), Vec3{}, nullptr, &err));
        CHECK(dup.Find("Label") && dup.Find("Label")->GetComponent<TextRenderer>()->loc.key == "menu.title");
    }

    // ---- OkayScript builtins ----
    {
        Localization::Reset();
        Localization::SetTable(MakeTable());
        Prefs::Delete(Localization::kPrefsKey);
        Scene s("S");
        GameObject* g = s.CreateGameObject("G");
        auto* sc = g->AddComponent<ScriptComponent>("okayscript");
        std::string err;
        CHECK(sc->LoadSource(
            "var a = \"\"; var b = \"\"; var c = \"\"; var lang = \"\"; var n = 0; var first = \"\"; var name = \"\"; var ok = -1; var bad = -1;\n"
            "function go() {\n"
            "  a = loc(\"menu.start\");\n"
            "  b = loc(\"hud.coins\", 3);\n"
            "  c = loc(\"greet\", map_set(map(), \"player\", \"Ana\"));\n"
            "  ok = set_language(\"fr\");\n"
            "  bad = set_language(\"xx\");\n"
            "  lang = get_language();\n"
            "  var all = languages(); n = count(all); first = all[0];\n"
            "  name = language_name(\"ja\");\n"
            "}\n", &err));
        s.Start();
        sc->VM()->CallEvent("go");
        CHECK(GS(sc, "a") == "Start" && GS(sc, "b") == "3 coins" && GS(sc, "c") == "Hello, Ana!");
        CHECK(G(sc, "ok") == 1.0f && G(sc, "bad") == 0.0f && GS(sc, "lang") == "fr");
        CHECK(G(sc, "n") == 3.0f && GS(sc, "first") == "en" && GS(sc, "name") == "日本語");
        CHECK(Localization::Language() == "fr" && Prefs::GetString(Localization::kPrefsKey) == "fr");   // remembered
        Prefs::Delete(Localization::kPrefsKey);
    }

    // ---- C#-style statics ----
    {
        Localization::Reset();
        Localization::SetTable(MakeTable());
        Scene s("C");
        GameObject* g = s.CreateGameObject("G");
        auto* sc = g->AddComponent<ScriptComponent>(NextScriptVM::kLanguage);
        std::string err;
        const bool loaded = sc->LoadSource(
            "class T : Behaviour {\n"
            "    public string a = \"\";\n    public string b = \"\";\n    public string c = \"\";\n"
            "    public string lang = \"\";\n    public int count = 0;\n    public string joined = \"\";\n"
            "    public bool ok = false;\n    public bool bad = true;\n    public string name = \"\";\n"
            "    public void Go() {\n"
            "        a = Localization.Get(\"menu.title\");\n"
            "        b = Localization.Get(\"hud.coins\", 1);\n"
            "        ok = Localization.SetLanguage(\"fr\");\n"
            "        bad = Localization.SetLanguage(\"xx\");\n"
            "        c = Localization.Get(\"hud.coins\", 4);\n"
            "        lang = Localization.Language;\n"
            "        count = Localization.Languages.Length;\n"
            "        foreach (string code in Localization.Languages) joined = joined + code + \";\";\n"
            "        name = Localization.LanguageName(\"fr\");\n"
            "    }\n"
            "}\n", &err);
        CHECK(loaded);
        if (!loaded) std::fprintf(stderr, "  typed script: %s\n", err.c_str());
        s.Start();
        if (loaded) {
            sc->VM()->CallEvent("Go");
            auto get = [&](const char* n) { return sc->VM()->GetGlobal(n); };
            CHECK(get("a").AsString() == "Main Menu" && get("b").AsString() == "1 coin");
            CHECK(get("ok").AsBool() && !get("bad").AsBool());
            CHECK(get("c").AsString() == "4 pièces" && get("lang").AsString() == "fr");
            CHECK(get("count").AsFloat() == 3.0f && get("joined").AsString() == "en;fr;ja;");
            CHECK(get("name").AsString() == "Français");
        }
        Prefs::Delete(Localization::kPrefsKey);
    }

    // ---- Visual-script nodes: Localize, Set Language, Get Language ----
    {
        Localization::Reset();
        Localization::SetTable(MakeTable());
        Scene s("V");
        GameObject* g = s.CreateGameObject("G");
        auto* vc = g->AddComponent<VisualScriptComponent>();
        std::string err;
        CHECK(vc->LoadFromText("node 1 OnStart\nentry OnStart 1\n"
                               "node 2 SetVar coins\nnode 3 Localize hud.coins\nnode 4 Const 2\nexec 1 0 2\ndata 2 0 3 0\ndata 3 0 4 0\n"
                               "node 5 SetLanguage fr\nexec 2 0 5\n"
                               "node 6 SetVar lang\nnode 7 GetLanguage\nexec 5 0 6\ndata 6 0 7 0\n", &err));
        s.Start();
        s.Update(1.0f / 60.0f);
        CHECK(vc->GetVariable("coins").AsString() == "2 coins");
        CHECK(vc->GetVariable("lang").AsString() == "fr" && Localization::Language() == "fr");
        Prefs::Delete(Localization::kPrefsKey);
    }

    // ---- Fonts: a font with the glyphs stands in, and new characters are baked on demand ----
    {
        Localization::Reset();
        TtfFont* ui = BuiltinUIFont();
        CHECK(ui != nullptr);
        if (ui) {
            CHECK(FontForText(nullptr, "Hello") == nullptr);           // ASCII: the 8x8 font is fine
            CHECK(FontForText(nullptr, "Français") == ui);            // the 8x8 font has no ç: the UI font has
            CHECK(FontForText(ui, "Français") == ui);
            const std::uint32_t gen = ui->Generation();
            const int h0 = ui->Atlas().Height();
            const TtfFont::Glyph* cedilla = ui->GlyphFor(0xE7);
            CHECK(cedilla != nullptr && cedilla->x1 > cedilla->x0 && cedilla->xadvance > 0.0f);
            CHECK(ui->Generation() != gen);                            // hosts upload the atlas again
            CHECK(ui->GlyphFor(0xE7) == cedilla);                      // baked once
            CHECK(ui->Atlas().Height() >= h0);
            CHECK(ui->Measure("ç", 16.0f) > 0.0f && ui->Measure("aç", 16.0f) > ui->Measure("a", 16.0f));
            CHECK(ui->Has('E') && ui->Has(' ') && ui->Has('\n'));
            CHECK(ui->Has(0x0416) && ui->HasAll("Жизнь"));             // Cyrillic is in the UI font
            CHECK(!ui->Has(0x65E5));                                   // CJK is not
            // A CJK text goes to a font that has it: the language's own font when it names one.
            std::string cjk;
            for (const std::string& path : SystemFallbackFonts())
                if (TtfFont* f = GetFont(path); f && f->HasAll("日本語")) { cjk = path; break; }
            if (!cjk.empty()) {
                TtfFont* got = FontForText(ui, "日本語");
                CHECK(got && got != ui && got->HasAll("日本語"));
                StringTable t = MakeTable();
                t.FindLanguage("ja")->font = cjk;
                Localization::SetTable(t);
                Localization::SetLanguage("ja");
                CHECK(Localization::LanguageFont() == cjk);
                CHECK(FontForText(ui, "メインメニュー") == GetFont(cjk));
                // Mixed with Latin letters and digits, only a font that has both will do.
                Localization::Reset();
                TtfFont* mixed = FontForText(ui, "E で拾う (10)");
                CHECK(mixed && mixed->Has('E') && mixed->Has('1') && mixed->HasAll("E で拾う (10)"));
            } else {
                std::printf("[test_localization] no CJK system font here; skipping that part\n");
            }
        }
    }

    Localization::Reset();
    TEST_MAIN_RESULT();
}
