# Guided generator quality audit

Why the first guided models looked alike, what was measured, what was changed, and what is still weak.
Measured with `tests/test_guided_quality.cpp` (`guided_quality`, which also runs as `guided_roundtrip`). Written from our own
measurements; no outside code or models were used.

## How a "shape difference" is measured

The side view of each car body style is filled in (176 x 60 grid, same length, height, width, wheelbase and clearance for every
style, no spoiler or roof fitting). The overlap of two outlines is their intersection over union (IoU). 1.000 means the same
outline, lower means more different.

| | Before the rework | After the rework |
|---|---|---|
| Mean pairwise IoU (all style pairs) | 0.835 | 0.800 |
| Worst pair | sedan / taxi 1.000 (same body) | 0.920 (a style pair, under the 0.93 limit) |
| Coupe / sports | 0.950 | below 0.93 |
| SUV / van | 0.936 | below 0.93 |

The taxi and the patrol car are a sedan body with other fittings on purpose, so they are not counted as separate styles.
The test fails if any pair of the seven body styles is above 0.93 or the mean is above 0.84.

## Findings (cars)

1. One profile formula for every body: only a few numbers moved a shared trapezoid. Fixed: each style has its own table of
   hood, windscreen, roof, rear window and trunk positions (`CarStyle` in `GuidedVehicles.cpp`), smoothed with corner cutting
   so the roofline is a curve at Medium and above.
2. The body was a box with the cabin on top. Fixed: the body is a side-view outline pushed across the width.
3. Wheels sat beside the body and floated. Fixed: round arches are cut into the body, each wheel is centred on its axle and
   stands on the ground, and an axle part joins the pair. Tests: `guided_quality` "arches" checks.
4. Small parts floated (mirror arms stopped short of the cabin, a chrome bumper started beyond the nose). Found by the new
   validator (parts connected by touching bounding boxes) and fixed. Every car start design is now one connected object.
5. Extreme slider values folded the body (tall wheels on a short car, cabin below the belt line). Fixed by limiting the
   wheel size to the length, keeping the arches apart and inside the body, and keeping a minimum cabin height.

## Validator and variation

`Validate()` checks each part for bad numbers, bad indices, degenerate faces, open or non-manifold edges and inward faces,
and counts connected groups. `Randomise()` moves the numbers inside a style's range and may change designs, then applies the
template's compatibility rules (for example no wing on a pickup, SUV or van). Result: 1000 seeds for each of the 15 templates
(15,000 models) with 0 validation failures; the same seed always gives the same model; 25 or more distinct models in the
first 30 seeds.

## Speed (standalone build, -O1, one thread, this machine; first start design of each template)

Generation takes under 1 ms at Low and Medium for most templates. At Ultra (Catmull-Clark subdivision) the worst cases
are the human at 29 ms (115k triangles), the car at 10 ms (42k) and the building at 8 ms (35k). The test only guards a
loose limit (5 s) so it is not flaky.

## Still weak (honest list)

- Motorcycles still use the old rough generator (next in the order).
- Characters: organic body exists from Medium up; styles, proportion sliders and clothing parts are not finished.
- The silhouette limit is a floor, not proof that every style looks good. Pictures: `docs/images/guided_modeling/cars_side_views.png`
  and `cars_three_quarter.png` (headless software renders).
- Parts that touch are tested by bounding boxes, not by exact surfaces.
- Mesh and draw-call counts, caching by parameter hash and instancing are not done yet.
