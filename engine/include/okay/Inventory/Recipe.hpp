#pragma once
#include "okay/Components/Inventory.hpp"
#include "okay/Math/Vec3.hpp"
#include <deque>
#include <string>
#include <vector>

namespace okay {

class Scene;

struct Ingredient { std::string item; int count = 1; };

/// One crafting recipe, shared by Crafting (a menu on the player) and CraftingStation
/// (a bench): inputs → output (+ byproducts), optionally taking `craftTime` seconds,
/// needing an open bench tagged `station`, and only known once the game variable
/// `unlockFlag` is set.
struct Recipe {
    std::vector<Ingredient> inputs;
    std::string output;
    int outputCount = 1;
    std::vector<Ingredient> byproducts;   ///< extra outputs
    float craftTime = 0.0f;               ///< seconds (0 = instant)
    std::string station;                  ///< bench tag it needs ("" = anywhere)
    std::string unlockFlag;               ///< game variable that must be non-zero ("" = always known)

    Recipe() = default;
    /// Bench-style: {{inputs...}, "output", count}.
    Recipe(std::vector<Ingredient> in, std::string out, int n = 1) : inputs(std::move(in)), output(std::move(out)), outputCount(n) {}
    /// Menu-style: {"output", count, {inputs...}}.
    Recipe(std::string out, int n, std::vector<Ingredient> in) : inputs(std::move(in)), output(std::move(out)), outputCount(n) {}

    /// The output plus byproducts, as stacks.
    std::vector<Inventory::Stack> Outputs() const;
    /// Known yet? (no unlockFlag, or that game variable is non-zero)
    bool Unlocked() const;
};

/// A set of recipes in its own file (`.okayrecipes`) that any Crafting or CraftingStation
/// can point at, so a game defines its recipes once.
///
///     okayrecipes 1
///     recipe "plank" 4          # output  count
///       in "wood" 2
///       out "sawdust" 1         # byproduct
///       time 1.5
///       station "workbench"
///       unlock "knows_planks"
///     end
struct RecipeBook {
    std::vector<Recipe> recipes;
    bool LoadText(const std::string& text, std::string* err = nullptr);
    std::string Serialize() const;
    bool Load(const std::string& path, std::string* err = nullptr);
    /// Loaded once per path and reused.
    static const RecipeBook& Cached(const std::string& path);
};

/// Crafts with craft time: inputs are taken when a job is queued (so they can't be
/// spent twice), the output arrives when it finishes; anything that no longer fits is
/// dropped next to the crafter instead of lost. Cancel() refunds the unfinished jobs.
class CraftQueue {
public:
    struct Job { Recipe recipe; float left = 0.0f; };

    /// Craft `times` of `r` from `inv`: instantly when it has no craft time, else queued.
    /// Returns how many were crafted or queued (stops early when inputs run out).
    int Craft(Inventory& inv, const Recipe& r, int times = 1);
    /// Advance the front job; finished ones put their outputs in `inv` (overflow drops at `dropPos`).
    void Update(float dt, Inventory& inv, Scene* scene, const Vec3& dropPos);
    /// Refund every unfinished job's inputs to `inv`.
    void Cancel(Inventory& inv, Scene* scene, const Vec3& dropPos);
    const std::deque<Job>& Jobs() const { return m_jobs; }
    /// 0..1 progress of the job being made (0 when idle).
    float Progress() const;
    /// How many times `inv` can afford `r` right now.
    static int Affordable(const Inventory& inv, const Recipe& r);

private:
    static void Give(Inventory& inv, const std::vector<Inventory::Stack>& stacks, Scene* scene, const Vec3& dropPos);
    std::deque<Job> m_jobs;
};

} // namespace okay
