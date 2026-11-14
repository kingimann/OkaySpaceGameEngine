# Volumes and zones

A **Volume** is a region of the level (a box, a sphere or a cylinder) that does one thing to what is inside it.
One component, one shape, six kinds. The editor draws every volume as a translucent shape in the Scene view,
in a colour per kind, and you draw and resize them there.

Header: `engine/include/okay/Components/Volume.hpp`. Sources: `engine/src/Components/Volume.cpp`,
`editor/VolumeTools.cpp`. Test: `volumes`.

## The shape

`VolumeGeometry` is the region itself: a `shape`, a `size` and an `offset`, both in the object's local units (the
Transform's scale multiplies them, and the object's rotation turns them).

| Shape | Region |
|---|---|
| Box | the box `size` |
| Sphere | the ellipsoid that fits inside that box |
| Cylinder | the elliptic cylinder that fits inside it, standing on the local Y axis |

`Contains(transform, point)` tells whether a world point is inside; `DepthAt` is 1 at the centre, 0 on the surface
and negative outside (the Atmosphere kind uses it to fade in). A region with a size of zero or less on any axis
holds nothing and is reported by the scene validator (`volume.zero_size`).

A volume needs **no collider**. It tests the shape itself each frame, so it works on any object, and an object with
a Volume does not have to be solid or visible.

## The six kinds

A *body* is an object with a Rigidbody 3D, or any object whose tag matches `activatorTag` (a body with a Rigidbody 3D
also matches by name text). `activatorTag` defaults to `Player`. When both a parent and its child match, only the
parent is the body.

| Kind | What it does at Play |
|---|---|
| Checkpoint | A body inside makes **the middle of the volume's floor** its respawn point. Put the bottom of the box on the ground. |
| Kill | A body that enters takes `killDamage` health (if it has a Health Stat or Survival Stats), then goes back to its last checkpoint, or to where it was first seen (zeroing its velocity). With action Destroy it is removed instead. |
| Force | Every physics step, pushes each dynamic Rigidbody 3D inside it: a steady direction (`forceDirection`, turning with the object when `forceLocal`) or Radial (away from the centre; negative `forceStrength` pulls in). `forceStrength` is an acceleration in m/s squared, so heavy and light bodies move alike. Leave `activatorTag` blank to push every body. |
| Atmosphere | While the **camera** is inside, the scene's fog and vignette fade to this volume's (`fogColor`, `fogStart`, `fogEnd`, `vignette`), reaching full strength `blend` of the way in from the edge, and fade back to the scene's own settings when it leaves. Several overlapping volumes blend in order. |
| Ambience | Plays the Audio Source on the same object while the camera is inside, fading in and out over `fadeTime`. Give the Audio Source a clip and tick Loop. |
| Reverb | While the listener is inside, 3D sounds gain this zone's echo: Room, Hall, Cave or Outdoor (`reverbPreset`), scaled by Strength (`reverbAmount`, 0 is dry). Edge blend (`blend`) is how far in from the surface it takes to reach full strength. 2D sounds stay dry. Overlapping zones blend. See `docs/audio.md`. |

Checkpoint and Kill can also send an `eventName` to scripts as an On Logic Signal (`on_logic_signal(name)`, the visual
script On Logic Signal node), so a checkpoint can show a message or a kill can play a sound.

While Atmosphere is holding the fog and vignette, anything else that writes them (weather, a script) is overridden for
that time; they are restored when the camera leaves. Atmosphere does not touch the sky or the ambient light.

### What these reuse and do not replace

Water already has depth, buoyancy and swimming, `Damage On Touch` and the Survival Zone already hurt what stands in a
trigger collider, `Jump Pad` and `Teleporter` already launch and move bodies, and `Checkpoint Trigger` (Save
Framework) already saves the game. Volumes add what was missing: a shape that needs no collider, a respawn point per
body, a push, and fog and sound zones. A Logic Source's pressure plate and trigger zone now show in the Scene view
with the same tooling (translucent shape, resize handles, visibility switch).

## In the editor

* **Draw one:** GameObject > Volume > Draw Checkpoint / Kill / Force / Atmosphere / Ambience / Reverb, then drag on the
  ground in the Scene view. The drag is the footprint; the height comes from the Height field in that menu. A click
  without a drag makes a 4 m one. Esc stops drawing. Pick the shape (Box, Sphere, Cylinder) in the same menu.
* **Create one:** GameObject > Volume > New ... Volume puts one where the Scene view looks. Add Component > Gameplay
  has a row per kind.
* **Resize:** select a volume (or a Logic Source plate or trigger zone): six coloured dots appear on its faces. Drag
  one to move that face; the opposite face stays put. The size shows while you drag.
* **Hide:** Scene Overlays > Volumes: Show volumes, Labels, and a switch per kind plus one for logic zones. The
  selected volume is always drawn so its handles stay usable.
* **Inspector:** Kind, Shape, Size, Offset and the settings of the chosen kind. During Play it shows how many bodies
  are inside (or the camera's weight for Atmosphere and Ambience).

The overlay and the switches are editor-only: nothing of them is in a build.

## Saving

A volume is saved in the scene file as a `volume` record (one per component, so two on one object both survive) and
in prefabs the same way. It is also in the property registry (`Volume.kind`, `Volume.size` ...), so scripts, undo and
prefab overrides reach each field.

## Scene validator

| Code | Severity | Meaning |
|---|---|---|
| `volume.zero_size` | warning | a Volume, or a Logic Source plate or trigger zone, has a size of zero or less on an axis |
| `volume.force_zero` | warning | a Force volume with strength 0 |
| `volume.no_audio` | warning | an Ambience volume with no Audio Source, or one with no clip |

## Not done

These were considered and left out because the engine has nothing underneath them to drive: a **post-process** zone beyond fog and vignette (no other
post settings are per scene), a **navigation area** volume (area costs live on a Nav Mesh Modifier, not on a volume), **convex region**
volumes, a **swim** zone (Water covers it) and a plain **damage** zone (Damage On Touch and the Survival Zone cover it).
