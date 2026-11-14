// Item, loot-table and recipe-book files: load/save round trip in the formats the
// game already uses, plain-English checks for missing items, and the loot roll's
// weight, chance and quantity distribution.
#include "test_framework.hpp"
#include <okay/Core/Random.hpp>
#include <okay/Inventory/ItemDatabase.hpp>
#include <okay/Inventory/LootTable.hpp>
#include <okay/Inventory/Recipe.hpp>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

using namespace okay;
namespace fs = std::filesystem;

static bool Has(const std::vector<std::string>& lines, const std::string& a, const std::string& b = "") {
    for (const auto& s : lines)
        if (s.find(a) != std::string::npos && (b.empty() || s.find(b) != std::string::npos)) return true;
    return false;
}

static void WriteText(const fs::path& path, const std::string& text) {
    std::ofstream o(path, std::ios::binary | std::ios::trunc);
    o << text;
}

int main() {
    RUN_SUITE("item_loot_recipes");
    const fs::path dir = fs::temp_directory_path() / "okay_test_item_loot_recipes";
    fs::remove_all(dir);
    fs::create_directories(dir);
    ItemDatabase& db = ItemDatabase::Get();

    const char* kItems =
        "okayitems 1\n"
        "item \"wood\"\n"
        "  name \"Wood\"\n"
        "  description \"A \\\"log\\\".\"\n"
        "  stack 64\n"
        "  weight 0.5\n"
        "  category \"Material\"\n"
        "  tags \"fuel\" \"building\"\n"
        "end\n"
        "item \"plank\"\n"
        "  name \"Plank\"\n"
        "  stack 32\n"
        "end\n"
        "item \"apple\"\n"
        "  name \"Apple\"\n"
        "  use \"Eat\" 15\n"
        "end\n";

    // --- Item file round trip, and id warnings ---
    {
        db.Clear();
        std::string err;
        CHECK(db.LoadText(kItems, &err));
        CHECK(db.IsDefined("wood") && !db.IsDefined("missing") && db.Issues().empty());
        const fs::path path = dir / "items.okayitems";
        CHECK(db.Save(path.string()));
        const std::string text = db.Serialize();
        db.Clear();
        CHECK(!db.IsDefined("wood"));
        CHECK(db.Load(path.string(), &err));
        CHECK(db.Serialize() == text);
        CHECK(db.Path() == path.string());
        CHECK(db.Find("wood") && db.Find("wood")->description == "A \"log\".");

        CHECK(db.LoadText("okayitems 1\nitem \"wood\"\n  name \"A\"\nend\nitem \"wood\"\n  name \"B\"\nend\n"
                          "item \"Plank\"\n  name \"Plank\"\nend\nitem \"plank\"\n  name \"plank\"\nend\n", &err));
        const auto issues = db.Issues();
        CHECK(Has(issues, "wood", "own id"));
        CHECK(Has(issues, "case", "plank") || Has(issues, "case", "Plank"));
    }

    // --- Loot file round trip and missing-item warnings ---
    {
        db.Clear();
        std::string err;
        CHECK(db.LoadText(kItems, &err));
        LootTable t;
        CHECK(t.LoadText("okayloot 1\nrolls 1 3\nentry \"Wood\" 5 1 4 1\nentry \"apple\" 2 1 2 0.5\nentry \"gem\" 1 1 1 1\n", &err));
        CHECK(t.entries.size() == 3 && t.entries[0].item == "wood");   // display name becomes the id
        CHECK(db.Find("gem") && db.Find("gem")->loose);
        const auto issues = t.Issues(db);
        CHECK(Has(issues, "gem", "isn't in the Item Database"));
        CHECK(!Has(issues, "wood", "isn't in the Item Database"));
        CHECK(!Has(issues, "apple", "isn't in the Item Database"));

        const std::string text = t.Serialize();
        const fs::path path = dir / "chest.okayloot";
        CHECK(t.Save(path.string()));
        LootTable back;
        CHECK(back.Load(path.string(), &err));
        CHECK(back.Serialize() == text);
        CHECK(back.rollsMin == 1 && back.rollsMax == 3 && back.entries[1].chance == 0.5f);

        LootTable empty;
        CHECK(Has(empty.Issues(db), "no entries", "drops nothing"));
        empty.rollsMin = 0; empty.rollsMax = 0;
        CHECK(Has(empty.Issues(db), "rolls 0", "drops nothing"));
        LootTable bad;
        bad.entries.push_back({"apple", 0.0f, 1, 1, 1.0f});
        bad.entries.push_back({"apple", 1.0f, 1, 1, 0.0f});
        bad.entries.push_back({"", 1.0f, 1, 1, 1.0f});
        const auto more = bad.Issues(db);
        CHECK(Has(more, "no weight", "never gets picked"));
        CHECK(Has(more, "0% chance", "never drops"));
        CHECK(Has(more, "no item chosen"));
    }

    // --- One pick's share follows the weights and the chance ---
    {
        LootTable t;
        t.entries.push_back({"a", 1.0f, 1, 1, 1.0f});
        t.entries.push_back({"b", 3.0f, 2, 4, 0.5f});
        t.entries.push_back({"c", 0.0f, 1, 1, 1.0f});
        const auto sh = t.Shares();
        CHECK(sh.size() == 3);
        CHECK_NEAR(sh[0].weightShare, 0.25f, 0.0001f);
        CHECK_NEAR(sh[1].weightShare, 0.75f, 0.0001f);
        CHECK_NEAR(sh[1].dropShare, 0.375f, 0.0001f);
        CHECK_NEAR(sh[2].weightShare, 0.0f, 0.0001f);
        float sum = 0.0f;
        for (const auto& s : sh) sum += s.weightShare;
        CHECK_NEAR(sum, 1.0f, 0.0001f);
    }

    // --- Roll distribution: weights, chance, quantity range, roll count ---
    {
        LootTable weights;
        weights.rollsMin = 1; weights.rollsMax = 1;
        weights.entries.push_back({"a", 1.0f, 1, 1, 1.0f});
        weights.entries.push_back({"b", 2.0f, 1, 1, 1.0f});
        weights.entries.push_back({"c", 3.0f, 1, 1, 1.0f});
        weights.entries.push_back({"d", 4.0f, 1, 1, 1.0f});
        const int N = 10000;
        int count[4] = {0, 0, 0, 0};
        Random rng(1234567);
        for (int i = 0; i < N; ++i) {
            auto got = weights.Roll(rng);
            CHECK(got.size() == 1 && got[0].count == 1);
            if (got.size() == 1) {
                if (got[0].item == "a") ++count[0];
                else if (got[0].item == "b") ++count[1];
                else if (got[0].item == "c") ++count[2];
                else if (got[0].item == "d") ++count[3];
            }
        }
        const float expect[4] = {0.10f, 0.20f, 0.30f, 0.40f};
        for (int i = 0; i < 4; ++i) {
            const float p = (float)count[i] / (float)N;
            if (std::fabs(p - expect[i]) > 0.025f)
                std::cerr << "  weight share " << char('a' + i) << " got " << p << " expected " << expect[i] << "\n";
            CHECK(std::fabs(p - expect[i]) <= 0.025f);
        }

        LootTable qty;
        qty.rollsMin = 1; qty.rollsMax = 1;
        qty.entries.push_back({"wood", 1.0f, 2, 4, 1.0f});
        Random rq(99);
        long long sum = 0;
        const int Q = 3000;
        for (int i = 0; i < Q; ++i) {
            auto got = qty.Roll(rq);
            CHECK(got.size() == 1);
            if (got.size() == 1) {
                CHECK(got[0].count >= 2 && got[0].count <= 4);
                sum += got[0].count;
            }
        }
        const double avg = (double)sum / (double)Q;
        if (std::fabs(avg - 3.0) > 0.15) std::cerr << "  quantity average " << avg << "\n";
        CHECK(std::fabs(avg - 3.0) <= 0.15);

        LootTable coin;
        coin.rollsMin = 1; coin.rollsMax = 1;
        coin.entries.push_back({"apple", 1.0f, 1, 1, 0.5f});
        Random rc(7);
        int drops = 0;
        const int C = 4000;
        for (int i = 0; i < C; ++i) if (!coin.Roll(rc).empty()) ++drops;
        const float rate = (float)drops / (float)C;
        if (std::fabs(rate - 0.5f) > 0.04f) std::cerr << "  chance rate " << rate << "\n";
        CHECK(std::fabs(rate - 0.5f) <= 0.04f);

        LootTable never;
        never.rollsMin = 5; never.rollsMax = 5;
        never.entries.push_back({"apple", 1.0f, 1, 1, 0.0f});
        Random rn(1);
        for (int i = 0; i < 50; ++i) CHECK(never.Roll(rn).empty());

        LootTable multi;
        multi.rollsMin = 3; multi.rollsMax = 3;
        multi.entries.push_back({"plank", 1.0f, 1, 1, 1.0f});
        Random rm(3);
        for (int i = 0; i < 20; ++i) {
            auto got = multi.Roll(rm);
            CHECK(got.size() == 1 && got[0].count == 3);
        }
    }

    // --- Cached loot is forgotten when the file changes ---
    {
        const fs::path path = dir / "cache.okayloot";
        WriteText(path, "okayloot 1\nrolls 1 1\nentry \"wood\" 1 1 1 1\n");
        CHECK(LootTable::Cached(path.string()).entries.size() == 1);
        CHECK(LootTable::Cached(path.string()).entries[0].item == "wood");
        WriteText(path, "okayloot 1\nrolls 1 1\nentry \"plank\" 1 2 2 1\n");
        CHECK(LootTable::Cached(path.string()).entries[0].item == "wood");
        LootTable::Forget(path.string());
        CHECK(LootTable::Cached(path.string()).entries[0].item == "plank");
        CHECK(LootTable::Cached(path.string()).entries[0].min == 2);
    }

    // --- Recipe book round trip and missing-item warnings ---
    {
        db.Clear();
        std::string err;
        CHECK(db.LoadText(kItems, &err));
        RecipeBook book;
        CHECK(book.LoadText("okayrecipes 1\n"
                            "recipe \"Plank\" 4\n  in \"Wood\" 2\n  out \"sawdust\" 1\n  time 1.5\n  station \"workbench\"\nend\n"
                            "recipe \"sword\" 1\n  in \"plank\" 3\n  in \"gem\" 1\n  time 4\n  station \"forge\"\n  unlock \"knows_smithing\"\nend\n",
                            &err));
        CHECK(book.recipes.size() == 2);
        CHECK(book.recipes[0].output == "plank" && book.recipes[0].inputs[0].item == "wood");
        CHECK(book.recipes[0].craftTime == 1.5f && book.recipes[0].station == "workbench");
        CHECK(book.recipes[1].unlockFlag == "knows_smithing" && book.recipes[1].inputs.size() == 2);
        const auto issues = book.Issues(db);
        CHECK(Has(issues, "sawdust", "isn't in the Item Database"));
        CHECK(Has(issues, "gem", "isn't in the Item Database"));
        CHECK(!Has(issues, "needs \"wood\"", "isn't"));
        CHECK(!Has(issues, "needs \"plank\"", "isn't"));

        const std::string text = book.Serialize();
        const fs::path path = dir / "book.okayrecipes";
        CHECK(book.Save(path.string()));
        RecipeBook back;
        CHECK(back.Load(path.string(), &err));
        CHECK(back.Serialize() == text);

        RecipeBook none;
        CHECK(Has(none.Issues(db), "no recipes"));
        Recipe bare;
        bare.output = "apple";
        bare.craftTime = -2.0f;
        bare.outputCount = 0;
        none.recipes.push_back(bare);
        const auto bad = none.Issues(db);
        CHECK(Has(bad, "no ingredients", "cannot be crafted"));
        CHECK(Has(bad, "negative crafting time", "instant"));
        CHECK(Has(bad, "fewer than 1"));

        RecipeBook twice;
        Recipe a; a.output = "apple"; a.outputCount = 1; a.inputs.push_back({"wood", 1});
        twice.recipes.push_back(a);
        twice.recipes.push_back(a);
        CHECK(Has(twice.Issues(db), "2 recipes", "apple"));

        WriteText(dir / "cache.okayrecipes", "okayrecipes 1\nrecipe \"apple\" 1\n  in \"wood\" 1\nend\n");
        const fs::path cpath = dir / "cache.okayrecipes";
        CHECK(RecipeBook::Cached(cpath.string()).recipes.size() == 1);
        WriteText(cpath, "okayrecipes 1\nrecipe \"plank\" 2\n  in \"wood\" 1\nend\n");
        CHECK(RecipeBook::Cached(cpath.string()).recipes[0].output == "apple");
        RecipeBook::Forget(cpath.string());
        CHECK(RecipeBook::Cached(cpath.string()).recipes[0].output == "plank");
        CHECK(RecipeBook::Cached(cpath.string()).recipes[0].outputCount == 2);
    }

    db.Clear();
    fs::remove_all(dir);
    TEST_MAIN_RESULT();
}
