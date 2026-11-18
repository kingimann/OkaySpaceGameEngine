---
id: 7zj
row: | 7zj | Guided vehicles (car, truck, bus, motorcycle, trailer, 6+ designs each) and a Detail level (Low to Ultra) on every guided model | Modeling > **Guided** > Vehicles; File > Examples > Guided modeling > `guided_vehicles`; `examples\modeling\guided_vehicles.okayscene` | New in this change; engine rules tested on Linux, editor page compiles but was never opened in a real window (unverified) |
---
## 7zj. Guided vehicles, and the Detail level

**What it is.** Vehicles on the Modeling window's **Guided** page, built on the same rules as 7zg and 7zi, plus one new setting on
every guided model: **Detail** (Low, Medium, High, Ultra) with the triangle count of each level next to it.

**Status.** Tested on Linux (`ctest -R "guided_vehicles|guided_detail"`: about 2300 and 980 checks: every design sound, parts never
overlap, every setting at its limits, fuzz, swapping wheels, headlights and bumpers, your edits surviving, the triangle counts and
level changes). The page was never opened in a real window; nothing ran on Windows. Pictures
(a picture folder (pictures are no longer stored in the repository), `vehicles_*.png`, `detail_*.png`) are headless renders of the generated meshes by our own
small rasteriser (the detail pictures show triangle edges), not editor screenshots. Motorcycle pictures: a picture folder (pictures are no longer stored in the repository) and `motorcycles_three_quarter.png` (Medium detail). The motorcycles are still blocky at Medium: tubes, boxes and simple profiles, not shaped like real tanks and engines.

**Try it.**

1. **File > Examples > Guided modeling > guided_vehicles** (33 vehicles in rows: cars, trucks, buses, motorcycles, trailers; 16 m
   apart). Click one in the Hierarchy, open Modeling > Guided.
2. Change **Wheel radius**, **Wheelbase**, **Length**, **Cabin size**; under **Parts and materials** change **Wheels** to Eight spoke mesh,
   **Headlights** to Twin lamps, **Bumpers** to Sport lip, **Spoiler** to Wing, **Roof fitting** to Light bar, **Exhaust** to Dual pipes.
3. **Each part > Wheels**: give one wheel its own design (Select jumps to it in Edit Mesh).
4. **Detail**: drag the **Detail** slider (0% blocky low poly to 100% finest) or press Low, Medium, High or Ultra. The model rebuilds when you let
   go, and "N triangles now" shows the cost. Above 250,000 triangles a window asks before it builds. High and Ultra are smooth shaded.
   Your own additions stay; a part you edited by hand is asked about first.
4c. **Fine detail**: set Detail to High or Ultra on a car. Under **Parts and materials** the new **Fine fittings** (door handles, seams, window rubber,
   wipers, grille bars, lamp lenses, mirror glass, plate, exhaust tips, underbody, brake discs, tyre ridges) appear and can be switched off. A building has
   **Tiles and gutters** (rows of roof tiles, gutters, a down pipe); a human with a top has **Fine fittings** (buttons, placket, pockets). Ultra is about 100k
   triangles for a car, 30k to 115k for a building and 115k to 125k for a human (table in docs/guided_detail.md).
4a. **Car styles side by side**: File > Examples > Guided modeling > `guided_car_styles` (sedan, hatchback, coupe, SUV, pickup, sports, van at the
   same size; only the shape differs). Wheels sit in round arches on axles; the mirrors and bumpers are joined to the body.
4b. Open **Variation and check**: it shows whether the model is one connected object with no bad faces. Under **Each part**, **Remove** takes a part off;
   it appears under **Removed parts** with **Add back**.
5. Modeling > Guided > Vehicles > Car/Truck/Bus/Motorcycle/Trailer: pick a design card, **Create**.
6. **Motorcycles**: create a Sport, then change **Rake** (Frame group: 18 to 40 degrees, the fork leans back more and the bike gets longer in the front),
   **Fork length**, **Wheelbase** and the wheel radii. Under **Parts and materials** change **Frame** (tube, twin spar, step-through), **Fork** (telescopic,
   upside-down, springer), **Swingarm**, **Rear shock**, **Engine** (single, V-twin, inline four, scooter unit), **Handlebar** (flat, pull-back, ape hanger, clip-ons,
   touring), **Fairing**, **Luggage**, **Exhaust**. A scooter engine unit swings with the wheel, so it has no swingarm. **Randomise within style** keeps that rule.

**Not done / unverified.** Doors that open, interiors, chassis details, rigged wheels or vehicle physics are not built (the
Vehicle components are separate: put them on a guided vehicle yourself). A box-only design has the same triangle count at Low and Medium.
The editor's Detail slider, Variation and check, and Remove/Add back controls compile but were never seen in a window (unverified).
