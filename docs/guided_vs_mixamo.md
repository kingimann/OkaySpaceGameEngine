# Guided human compared with Mixamo-type characters

Mixamo (by Adobe) is a web service that gives you ready-made rigged humans and a large library of motion clips. This page is an
honest comparison of what that kind of character gives you with what the Guided human gives you. Everything here is written from
general knowledge and from our own code. **No Mixamo model, animation or data is used or shipped.** The only thing taken from
that world is a naming convention: the bones of a Guided human are named the way Mixamo names them (`mixamorig:Hips`,
`mixamorig:LeftUpLeg` ...), because the engine already understands those names (`Animation/HumanoidBones.hpp`, the humanoid
retarget and the model importer). A clip that targets those node names plays on a Guided human.

| | A Mixamo-type character | The Guided human |
|---|---|---|
| Mesh | Scanned or sculpted high-detail realistic mesh, tens of thousands of triangles, real texture maps | Made from rings and curves by code: about 3k / 8k / 21k / 44k triangles at Low / Medium / High / Ultra; flat face colours, no texture maps |
| Body shape | A handful of fixed characters | Any body from sliders: height, shoulders, chest depth, head size, legs, arms, limb thickness, neck, Build, Muscle, body type; 13 start designs; a random seed |
| Face | Detailed, often with blend shapes | Simple: brow, eyes with whites and lids, nose, lips, ears, a few designs of each; no blend shapes, no facial rig |
| Hands and feet | Full hands with finger bones | Four fingers and a thumb as shapes; no finger bones, so fingers cannot be animated separately |
| Hair and clothes | Real hair cards or meshes, clothing variants, textures | 13 solid hair styles, hats, glasses, fitted clothes (shirt, jacket, hoodie, trousers, skirt, shoes); solid shells, no strands, no cloth |
| Rig | Automatic rig placed from marker points you click, 65 or so bones with fingers | 22 bones placed from the sliders (hips, 3 spine, neck, head, collar bones, arms, legs, toes), skin weights from distance to the bones; no twist bones, no corrective shapes |
| Animations | A large library of motion-captured clips, any of which you can apply | None bundled. The engine's built-in humanoid clips (idle, walk, run, jump and more) play through the humanoid retarget; a Mixamo-named clip from your own download plays through the Model Animator |
| Cost and licence | Free to use, but an Adobe account is needed and the files are Adobe's | Free, built in, no account, our own code, nothing to download or license |
| Editing | The mesh is a finished file | An ordinary editable mesh from a recipe: swap parts, keep your own hand edits, regenerate, change Detail, save as a template |
| Repeatability | You get the file | The same recipe gives the same mesh on every machine |

## Gaps (what Mixamo-type characters do better today)

1. **Realism.** The Guided human reads as a person but not a photographic one. The surface is smooth, yet it has no texture, no
   skin detail and no wrinkles.
2. **Hands.** No finger bones. Grabbing and pointing poses need finger animation we do not have.
3. **Faces.** No blend shapes or facial rig, so no speech or expressions.
4. **Deformation.** Weights come from distance to the bones, not from painting. Elbows, knees and shoulders bend smoothly in the
   test but have no twist bones or corrective shapes, so extreme poses will pinch.
5. **Animation library.** We ship no motion-capture clips. Use the built-in humanoid clips, or bring your own clip.
6. **Hair and clothes** are solid shells.

## What is verified, and what is not

* Verified by tests (`tests/test_guided_animation.cpp`, `tests/test_guided_human.cpp`): the skeleton has 22 bones in the right
  parent order with unique names in both namings; the 15 humanoid bones map in the engine's mapper; every vertex is skinned with
  weights that sum to 1 and every bone moves some vertices; the built-in idle and walk move the legs and the skin follows; a clip
  written from the Mixamo node names plays through the Model Animator, turns the named bones, moves the skin and no vertex flies
  away: in the run on this branch the right arm turned up to 75 degrees, the skin moved up to 1.0 m, edges longer than 1 cm stretched at most 2.3 times and no edge grew by more than 8 cm (the test fails above 4 times and 12 cm). Skin weights come from the distance to each bone in units of the body's thickness there, and a vertex may only follow the bones its part is allowed to follow (a hand never follows a leg, a thigh never an arm), which is what removed the spikes at the hip and the armpit that the first version had. Proportions (head about 1/7.5 of the height, hand about 0.19
  m, foot about 0.30 m, left and right mirrored), sliders (build, muscle, height, shoulder width, body type) and the triangle
  budgets are checked on the generated parts.
* **Not verified:** real Mixamo files (none was used), how the deformation looks (the test checks numbers, not looks), the
  editor's "Animated copy" button in a real window, Windows.

How to try it: [try_it.md](try_it.md) (7zo).
