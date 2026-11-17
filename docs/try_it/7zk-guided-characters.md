---
id: 7zk
row: | 7zk | Guided characters (human, robot, creature) with swappable head, hair, clothes, hands, feet, hats, glasses, backpacks, capes and proportion sliders | Modeling > **Guided** > Characters; File > Examples > Guided modeling > `guided_characters`; `examples\modeling\guided_characters.okayscene` | New in this change; engine rules tested on Linux, editor page compiles but was never opened in a real window (unverified) |
---
## 7zk. Guided characters

**What it is.** Characters on the Modeling window's **Guided** page, same rules as 7zg to 7zj, including the **Detail** level (Low to Ultra).

**Status.** Tested on Linux (`ctest -R guided_characters`, about 2000 checks: every design of every part sound and non-overlapping, every
setting at its limits, fuzz, swapping hands, hair, hats, proportions, creature body plans, your edits and your own additions surviving). The page
was never opened in a real window; nothing ran on Windows. Pictures (`docs/images/guided_modeling/characters_*.png` sheets and
`character_<type>_<design>.png`, 26 files, plus `detail_human.png`) are headless renders by our own small rasteriser, not editor screenshots.
Humans from Medium up are organic (lofted body, separate face parts); robots and creatures are blocky; spiders and insects have crude legs. Style, hair and clothes pictures: `characters_styles.png` (realistic, stylised low poly, cartoon, blocky at Medium and High) and `characters_hairstyles.png` (13 hairstyles from behind and the side). A human can be given a skeleton and skin weights (see step 8). What is checked: `ctest -R guided_animation`.

**Try it.**

1. **File > Examples > Guided modeling > guided_characters** (26 characters: humans, robots and creatures in rows, 4 m apart). Click one in the
   Hierarchy, open Modeling > Guided.
2. **Proportions**: drag Height, Shoulder width, Head size, Leg length, Limb thickness; **Pose**: Arm angle, Elbow bend, Leg spread.
3. **Parts and materials**: change Hair, Hat, Glasses, Top, Bottoms, Hands, Feet, Backpack, Belt, Cape, Shoulder pads; change a colour.
4. **Each part > Hands**: give one hand its own design (Select jumps to it in Edit Mesh; move it with G, return: "[edited by you]"; change the
   Hands design for all: it asks first). Add your own geometry in Edit Mesh: it stays when you change settings.
5. **Creatures**: pick the Dragon, change **Wing span**, **Tail** and **Horns**; pick a Dog and change **Ears**; **Body plan** switches the layout.
6. **Detail**: Low, Medium, High, Ultra (triangle counts shown). Collision: Exact (mesh) / None.
7. To make your own: Modeling > Guided with nothing selected, Characters, pick a design card, Create. Select faces and **Save selection as part**
   to keep a hat or hand of your own as a design.
8. **Styles and looks** (a human): **Style** under Proportions (realistic, stylised low poly, cartoon, blocky: blocky stays boxes at every Detail
   level, stylised shows its facets, cartoon has a big head and short limbs). New **Hair** (buzz, bob, ponytail, afro, curly, side part, braid),
   **Head** (heart, long, wide), **Top** (tank, long sleeve, hoodie), **Bottoms** (jeans, cargo, leggings, long skirt) and a **Scarf**.
9. **Animation**: with a human selected open **Animation (skeleton)**. **Make animated copy** adds a skinned human with 15 bones next to it.
   To see it move: add a player to the scene (GameObject menu, Third Person Player), select the human and press **Use as the player's
   character**, then Play and walk with W. The engine's own idle and walk play on the bones (the same path an imported humanoid takes).

**What the animation test proves.** `guided_animation` builds the skeleton for every human design at Medium and High (all 15 bones map, every vertex has weights, no bone is dead), puts one on the engine's third-person player, and checks that idle then W moves the body more than a metre, swings the thighs more than 8 degrees apart, deforms the skin and keeps it finite and person-sized. It does not judge how nice the bending looks: weights are by distance to the bones, so shoulders, hips and the crotch bend roughly, and clothes and hair follow the nearest bone.

**Not done / unverified.** No facial expressions or finger joints; clothes are shells around the body, not cloth; the rig button and the page's Detail control were never clicked in a window; the rigged copy is a snapshot (it is not kept in step with later changes); only humans have a skeleton (robots and creatures do not).
