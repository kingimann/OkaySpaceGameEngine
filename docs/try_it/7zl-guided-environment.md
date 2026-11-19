---
id: 7zl
row: | 7zl | Guided environment: trees (9 species), rocks and cliffs, roads (path-drawn), bridges, hedges, plants, street lamps | Modeling > **Guided** > Environment; File > Examples > Guided modeling > `guided_environment`; `examples\modeling\guided_environment.okayscene` | New in this change; engine tested on Linux, panel never opened in a real window (unverified) |
---

## 7zl. Guided environment: trees, rocks, roads, bridges, hedges, plants, lamps

**What it is.** Seven environment templates on the Modeling window's **Guided** page, same rules as 7zg to 7zk: every one is a set of named parts that can be given another design, with a **Seed** setting for the random-looking shapes (the same seed always gives the same tree), the **Detail** level (Low to Ultra), Randomise within style, the model check and the save-as-template round trip.

**Status.** Tested on Linux (`ctest -R "guided_environment|guided_detail|guided_quality"`): every start design of every template is sound at all four Detail levels; the road and hedge follow a path you draw; every setting at its limits; fuzz; swapping designs; your edits surviving. Picture: a picture folder (pictures are no longer stored in the repository) (every start design, headless render by our own small rasteriser, not an editor screenshot). Rocks, plants and the street lamp parts are not always one connected piece by design (pebbles, flower beds).

**Try it.**

1. **File > Examples > Guided modeling > guided_environment** (55 models in rows: trees, rocks, roads, bridges, hedges, plants, lamps; 16 m apart). Click one in the Hierarchy and open Modeling > Guided.
2. **Trees**: change **Crown** (round, clusters, pine, spruce, palm fronds, willow, bare), **Trunk**, **Height**, **Crown size**, **Lean** and **Seed**.
3. **Roads and hedges**: with a road selected press **Draw in the Scene view** and click on the ground; the road, a bridge's approach or a hedge follows the points. Change **Surface** (asphalt, concrete slabs, dirt, gravel, cobblestones, paving, railway) and markings.
4. **Rocks, plants, lamps**: change **Seed** for another rock; plants: **Count** and **Spread**; lamps: **Pole**, **Arm**, **Lamp**.
5. **Randomise within style** (Variation and check) with a seed, **Detail** slider, **Save as template** work as for every guided model.

**Not done / unverified.** No wind, no leaf cards or billboards (trees are solid low-poly meshes), no terrain placement; the page was never clicked in a real window.
