---
id: 7zm
row: | 7zm | Guided props: furniture, containers, tools, decorations (27 designs) | Modeling > **Guided** > Props; File > Examples > Guided modeling > `guided_props`; `examples\modeling\guided_props.okayscene` | New in this change; engine tested on Linux, panel never opened in a real window (unverified) |
---

## 7zm. Guided props: furniture, containers, tools, decorations

**What it is.** Four prop templates on the **Guided** page (category Props), same rules and the same Detail, Randomise, model check and template controls.

**Status.** Tested on Linux (`ctest -R "guided_quality|guided_detail|guided"`): every start design of the four templates is one connected, sound object at all four Detail levels, every start is different, every slot has designs that change the model, 1000 random seeds per template without a failure. Picture: `docs/images/guided_modeling/props_overview.png` (headless render).

**Try it.**

1. **File > Examples > Guided modeling > guided_props** (27 props in rows, 3 m apart).
2. **Furniture** (chair, table, round table, armchair, sofa, bed, bookshelf, desk, wardrobe): change **Kind**, **Width / Depth / Height**, **Legs** (square, round, turned, splayed, end panels), **Top**, **Back**, **Arms**, **Cushion**, **Storage**; **Shelves** for the bookshelf.
3. **Containers**: change **Body** (crate, barrel, chest, bucket, jar, sack), **Lid**, **Bands**, **Handles**.
4. **Tools**: change **Head** (hammer, axe, pickaxe, shovel, wrench, sword blade, hoe), **Handle**, **Hand guard**, **End**; **Length** and **Head size**.
5. **Decorations**: change **Base**, **Shaft**, **Top** (sign, arrow, flag, orb, bust, lantern, bowl).
6. **Each part** lets you swap or remove one leg, one cushion, one door; **Add back** returns it. Select a part, edit it in Edit Mesh, and rebuild: your edit is kept.

**Not done / unverified.** Props are simple low-poly shapes (no fabric folds, no carved detail beyond Ultra smoothing); no interaction or physics setup; the page was never clicked in a real window.
