// Issue #888 findings 70 and 73.
//  70: the script editor's Find matched without regard to case, Replace and Replace All matched case exactly, and Replace
//      did not act on the highlighted match. Now one matcher (okay/Core/TextFind.hpp) drives all of them, with a Match case
//      switch (off by default, as Find behaved before).
//  73: an Info flood pushed every Error out of the editor Console (one ring for all levels). Now the least important,
//      oldest line goes first, so errors and warnings stay while the total stays bounded.
#include "test_framework.hpp"
#include "okay/Core/TextFind.hpp"
#include "okay/Debug/ConsoleModel.hpp"

#include <string>
#include <vector>

using namespace okay;
using L = Log::Level;
namespace tf = okay::textfind;

int main() {
    RUN_SUITE("find_replace_console");

    // ---- Finding 70: Find, counts, Replace and Replace All agree --------------------------------------------------
    {
        const std::string src = "Coin coin COIN";
        // Match case off (the default): three matches, Replace All replaces those three.
        std::vector<std::size_t> at = tf::FindAll(src, "coin", false);
        CHECK(at.size() == 3);
        std::string t = src;
        std::size_t n = tf::ReplaceAll(t, "coin", "gem", false);
        CHECK(n == 3);
        CHECK(t == "gem gem gem");
        CHECK(tf::FindAll(t, "coin", false).empty());

        // Match case on: one match, Replace All replaces that one.
        at = tf::FindAll(src, "coin", true);
        CHECK(at.size() == 1 && !at.empty() && at[0] == 5);
        t = src;
        n = tf::ReplaceAll(t, "coin", "gem", true);
        CHECK(n == 1);
        CHECK(t == "Coin gem COIN");
    }
    {
        // Replace changes exactly the highlighted match: the one the "k of M" counter names for the cursor.
        const std::string src = "Coin coin COIN coin";            // the line from the GUI repro in the addendum
        std::vector<std::size_t> at = tf::FindAll(src, "coin", false);
        CHECK(at.size() == 4);
        for (int k = 0; k < 4 && at.size() == 4; ++k) {
            const std::size_t cursor = at[(std::size_t)k];      // what Next/Previous leave the cursor on
            CHECK(tf::CurrentIndex(at, cursor) == k);
            std::string t = src;
            const long long p = tf::ReplaceCurrent(t, cursor, "coin", "gem", false);
            CHECK(p == (long long)at[(std::size_t)k]);
            std::string expect = src;
            expect.replace(at[(std::size_t)k], 4, "gem");
            CHECK(t == expect);
        }
        // Highlighted match 1 of 4 is "Coin" (capital C): Replace must change it, not skip to the next exact "coin".
        std::string t = src;
        CHECK(tf::ReplaceCurrent(t, 0, "coin", "gem", false) == 0);
        CHECK(t == "gem coin COIN coin");
        // With Match case on, the cursor on "Coin" is not a match: the current one is the next exact "coin".
        t = src;
        std::vector<std::size_t> cs = tf::FindAll(src, "coin", true);
        CHECK(cs.size() == 2);
        CHECK(tf::CurrentIndex(cs, 0) == 0);
        CHECK(tf::ReplaceCurrent(t, 0, "coin", "gem", true) == 5);
        CHECK(t == "Coin gem COIN coin");
        // Cursor past every match: the counter shows the last one, and Replace changes that one.
        t = src;
        CHECK(tf::CurrentIndex(at, src.size()) == 3);
        CHECK(tf::ReplaceCurrent(t, src.size(), "coin", "gem", false) == 15);
        CHECK(t == "Coin coin COIN gem");
        // No match, empty needle: nothing changes.
        t = src;
        CHECK(tf::ReplaceCurrent(t, 0, "zzz", "gem", false) == -1 && t == src);
        CHECK(tf::ReplaceAll(t, "", "gem", false) == 0 && t == src);
        CHECK(tf::FindAll(src, "", false).empty());
    }
    {
        // The replacement containing the needle must not be matched again (no endless loop, no double replace).
        std::string t = "a A a";
        CHECK(tf::ReplaceAll(t, "a", "aa", false) == 3);
        CHECK(t == "aa aa aa");
        // Matches never overlap and are found left to right.
        std::vector<std::size_t> at = tf::FindAll("aaaa", "aa", false);
        CHECK(at.size() == 2 && at.size() == 2 && at[0] == 0 && at[1] == 2);
        // Only ASCII letters fold; other bytes (UTF-8) compare exactly, and a match never starts inside a character.
        CHECK(tf::FindAll("\xC3\xA9t\xC3\xA9", "\xC3\xA9", false).size() == 2);
        CHECK(tf::FindAll("\xC3\xA9", "\xA9", false).empty());
        CHECK(tf::MatchAt("PLAYER_SPEED", 0, "player", false));
        CHECK(!tf::MatchAt("PLAYER_SPEED", 0, "player", true));
        CHECK(!tf::MatchAt("PLAY", 0, "player", false));
    }

    // ---- Finding 73: an Info flood cannot push errors out ---------------------------------------------------------
    {
        const bool oldConsole = Log::console;
        Log::console = false;
        ConsoleModel m(2000);
        m.Add(L::Error, "NullReference in Enemy.update");
        for (int i = 0; i < 100000; ++i) m.Add(L::Info, "player pos 1,2,3");
        ConsoleCounts c = m.Counts();
        CHECK(c.error == 1);
        CHECK(c.info == 1999);
        CHECK(m.Size() == 2000);                        // bounded: never more than the capacity
        CHECK(m.TotalReceived() == 100001);
        CHECK(m.Find("NullReference") >= 0);
        m.SetCollapse(true);
        std::vector<ConsoleRow> rows = m.Rows();
        CHECK(rows.size() == 2);
        if (rows.size() == 2) {
            CHECK(rows[0].entry.level == L::Error);         // oldest first: the error is still at the top
            CHECK(rows[1].repeat == 1999);
        }
        ConsoleFilter ef;
        ef.levelMask = ConsoleModel::LevelBit(L::Error);
        CHECK(m.Rows(ef, true).size() == 1);

        // Warnings outrank Info too, and errors outrank warnings when only those are left.
        ConsoleModel w(4);
        w.Add(L::Error, "e1");
        w.Add(L::Warning, "w1");
        for (int i = 0; i < 50; ++i) w.Add(L::Info, "i" + std::to_string(i));
        c = w.Counts();
        CHECK(c.error == 1 && c.warning == 1 && c.info == 2 && w.Size() == 4);
        for (int i = 0; i < 50; ++i) w.Add(L::Warning, "w" + std::to_string(i + 2));
        c = w.Counts();
        CHECK(c.error == 1 && c.warning == 3 && c.info == 0 && w.Size() == 4);
        std::vector<ConsoleEntry> es = w.Entries();
        CHECK(es.size() == 4 && es[0].message == "e1" && es[3].message == "w51");
        // Errors fill the whole budget when nothing else is left; the newest errors are kept.
        for (int i = 0; i < 10; ++i) w.Add(L::Error, "e" + std::to_string(i + 2));
        c = w.Counts();
        CHECK(c.error == 4 && w.Size() == 4);
        es = w.Entries();
        CHECK(es.size() == 4 && es[3].message == "e11");
        // Shrinking keeps errors over info as well.
        ConsoleModel s(10);
        s.Add(L::Error, "keep me");
        for (int i = 0; i < 9; ++i) s.Add(L::Info, "x");
        s.SetCapacity(3);
        c = s.Counts();
        CHECK(c.error == 1 && c.info == 2 && s.Size() == 3);
        Log::console = oldConsole;
    }

    TEST_MAIN_RESULT();
}
