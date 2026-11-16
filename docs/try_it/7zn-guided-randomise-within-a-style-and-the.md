---
id: 7zn
row: | 7zn | Guided: Randomise within a style (seed), validation check, save-edit-reopen round trip | Modeling > **Guided** > Variation and check; File > Examples > Guided modeling > `guided_car_styles` | New in this change; tested on Linux, buttons never clicked in a window (unverified) |
---
## 7zn. Guided: randomise within a style, and the save-edit-reopen round trip

**What it is.** On the **Guided** page, **Variation and check** has a **Seed** field, **How different** and **Randomise within style** (and **Next seed**).
The same seed and the same starting model always give the same result. The check line shows the validator's verdict.

**Status.** Tested on Linux (`ctest -R "guided_quality|guided_roundtrip"`): 1000 seeds on each of 15 templates (15,000 models) with 0 validation
failures; the round trip (generate, customise, edit by hand, save, reopen, edit again, regenerate) passes. The buttons themselves compile but were never
clicked in a real window (unverified).

**Try it.**

1. Open **File > Examples > Guided modeling > guided_car_styles**, click a car, Modeling > Guided.
2. Open **Variation and check**, type a seed (say 7), press **Randomise within style**. The car changes size and parts but stays that style. Press it
   again with the same seed on the same car: the same result. **Next seed** tries another. **Undo guided step** goes back.
3. Round trip: in Edit Mesh pull a headlight forward and add a box on the roof. Change **Wheels** to Eight spoke mesh. Both edits stay and nothing
   asks. Change **Headlights**: it asks first about the headlight you pulled. Save the scene, close, reopen, click the car: the edits are listed
   ("edited by you", "faces of your own"). Open **My parts and templates**, name it and press **Save as template**: it shows with the starting designs.

**Not done / unverified.** Randomise has compatibility rules only for cars so far.
