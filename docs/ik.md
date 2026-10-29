# Inverse kinematics (IK)

IK bends bones so a hand, foot, head or tail tip reaches a goal, on top of
whatever the animation did this frame. Every IK component works on the built-in
Character and on imported skeletons, and runs after the animation each frame.

## Which component to use

| Component | Use it for |
| --- | --- |
| **Foot IK** | Both feet on uneven ground: stairs, slopes, rocks. Lowers the pelvis and tilts the feet to the slope. |
| **Full Body IK** | A humanoid reaching with its hands and feet, and looking at things. Far goals pull the hips and bend the spine. |
| **Limb IK** | One arm or leg reaching a target: grabbing a handle, a hand on a wall. |
| **Chain IK** | Long chains: tails, tentacles, spider legs, cables. |
| **Look-At IK** | A head (and some spine) turning to look at a point. |
| **Aim IK** | Pointing one bone at a target: a gun, a torch, a turret. |

The quickest start on an imported humanoid is **Set up humanoid IK** in the Model
Animator inspector. It adds Foot IK and Full Body IK with the bones found by
name, sensible joint limits, and hand and look target objects to move around.

## How the solvers work

- **Bones turn, they don't stretch.** In a parented bone chain (an imported
  skeleton) IK rotates the bones, so every bone keeps its length and a skinned
  mesh bends with them. Joints that aren't parented (the built-in Character's
  parts) are moved to the solved positions instead.
- **Every frame starts from the animation.** What IK wrote last frame is taken
  back off before the next solve, so corrections never build up, even on a bone
  that no animation touches. Switching IK off (or a weight of 0) shows the
  animated pose that same frame.
- **Weight blends the goal, not the pose.** At half weight the limb reaches
  halfway from where the animation put it to the goal, and stays at that half
  frame after frame.
- **Bone lengths are read live,** so a height slider, a rescale or a swapped
  model just works.
- **Broken input is skipped.** A target that is NaN or infinite, or a solve that
  fails, leaves that limb on its animated pose for the frame.

## Solve order

IK components run in a fixed order after the animation (and the state machine,
crossfades and layers):

| Order | Component | Why |
| --- | --- | --- |
| 85 | Foot IK | Plants the feet and lowers the pelvis first. |
| 90 | Procedural Object Holding | Places the hands on the held object, from the final body height. |
| 100 | Full Body IK | Body pull, spine, limbs, head. |
| 105 | Limb IK, Chain IK | Single limbs and chains, on top of the body pose. |
| 110 | Look-At IK, Aim IK | Last, so they aim from the final pose. |

When Full Body IK's body pull moves the hips on a character that also has Foot
IK, the feet are put back where Foot IK planted them, so they don't slide.

## Where a knee or elbow bends (the pole)

Limb IK and Chain IK have a **Pole Mode**; Foot IK and Full Body IK use the same
logic automatically:

- **Auto (keep the bend)** — the limb keeps bending the way the animation bends
  it. As the limb straightens (so the animation's bend can't be read) it eases
  over to the *Pole Dir* in the character's space, so a knee never flips.
- **Character** — always toward *Pole Dir*, turning with the character.
- **World direction** — always toward *Pole Dir* in the world.
- A **Pole** object (or a Full Body IK effector's **Hint**) overrides the mode:
  the joint bends toward it.

Chain IK also keeps **World point (legacy)**: bend toward *Pole point*, as older
scenes and `ik_set_pole(x, y, z)` do.

## Joint limits

Limb IK (*Upper / Lower limit*), Chain IK (*Joint limits*, one per bone), Foot IK
(*Hip / Knee limit*) and each Full Body IK effector (*Shoulder / Elbow* or
*Hip / Knee limit*) take a joint limit:

- **None** — unlimited (the default on hand-added components).
- **Hinge (elbow / knee)** — bends one way, between *Bend min/max*, with a little
  *Twist*.
- **Ball (shoulder / hip)** — a *Swing* cone around the rest pose, plus *Twist*.

**Preset** fills in humanoid angles (Elbow, Knee, Shoulder, Hip, Spine, Neck).
Angles are measured from each bone's rest pose, and with **Auto axes** the axes
come from the rig itself, so the same numbers work on both sides of a mirrored
skeleton. A limit only stops IK pushing a bone out of range: if the animation
itself already poses a bone outside it (a deep crouch, a kick), IK leaves it.

## Switching targets smoothly

Limb IK, Chain IK and every Full Body IK effector have **Target Blend (s)**
(default 0.15). When the target object changes, or the goal jumps more than about
30 cm in one frame, the limb swings over in that time instead of snapping. It is
measured on the character, so the character itself moving or respawning never
leaves a hand trailing behind. Set it to 0 for the old instant snap; scenes saved
before this setting existed load with 0.

Full Body IK also has **Blend Speed** (how fast a goal fades in and out when you
switch it on or off), **Goal Smoothing** (follow a moving target without jitter)
and, for looking, **Smoothing**. All of these ease per second, so they look the
same at 30 and 144 fps.

## Look-At IK

- **Chain** root to tip (spine, neck, head; *Auto-Detect Chain* finds them).
  Each bone takes a **share** of its turn and the last bone always finishes the
  look. Left alone the shares ramp up the chain — three bones split a turn a
  third, two thirds of what's left, the rest — so the body leans a little, the
  neck more and the head does the rest. Type your own shares next to the bone
  names (the spine 0.2 for a stiff character, 0.5 for one that turns its torso).
- **Max Angle / bone** is how far one bone may turn from its animated pose.
  **Clamp** is how far the whole look may swing from where the animation points
  the first chain bone (the body's forward): at the default 120, a target behind
  the character is watched from the corner of the eye, and when it crosses
  behind the head comes back round the front. 0 = no limit (scenes saved before
  this setting load with 0).
- **Eyes**: eye bones (found by *Auto-Detect Chain*, or named) aim straight at
  the target with no easing — the eyes lead, the head follows — within **Eye
  Max Angle** of where the animation points them.
- **Smoothing** eases the head over to a new target (per second; 0 snaps).

## Limb IK

- **Stretch** lets the arm or leg lengthen by up to that fraction (0.15 = 15 %)
  when the target is beyond its reach — a ledge grab, a long reach — instead of
  stopping short. The animated bone lengths come back with the next pose, so
  nothing accumulates. **Softness** eases the last stretch to full reach instead
  of snapping straight.

## Foot IK

- **Raycast Ground** finds the ground under each foot. **Ground Layers** limits
  which collider layers count as ground (blank = all). The character's own
  colliders and trigger colliders never count, so a held weapon, a pickup
  trigger or the character's capsule can't lift a foot.
- **Foot Offset** is the lowest the ankle may get above the ground.
  **Smoothing** eases the correction in and out (stairs and ledges don't pop).
- **Adjust Pelvis** lowers the body so the lower foot can still reach the ground;
  **Plant Down** also presses feet down onto lower ground.
- **Align To Slope** tilts the feet to the ground. **Sole Up L / R** are each
  foot's up axis; left at Up, each foot reads it from its own rest pose, so both
  soles of a mirrored rig lie flat.
- **Foot lock** pins a planted foot while it is still, so feet don't skate.

## Full Body IK

Bones map automatically from common rig names (Mixamo, Blender-style `.L/.R`,
`thigh_l`, the built-in Character parts), or by hand.

- Four effectors (left/right hand and foot), each with a **Target** object or a
  **Point**, **Position Weight**, **Rotation Weight** (match the target's
  rotation: a grip, a planted foot), a **Hint** for the elbow/knee, **Body Pull**,
  joint limits and **Target Blend**.
- **Body Pull** / **Max Body Offset**: goals out of reach drag the hips toward
  them. **Spine Reach** / **Max Spine Angle**: what the hips can't cover, the
  chest leans for, spread over the spine bones.
- **Soft Reach** eases a limb into full stretch instead of snapping straight.
  **Elbow / Knee Min Bend** stop limbs locking straight.
- **Look At**: the chest turns its share before the hands are placed (so they
  stay on their goals), then the head turns the rest, clamped to *Max Look
  Angle*.

## Scripting

`ik_enable(on)`, `ik_set_weight(w)`, `ik_set_target(x, y, z)`,
`ik_set_pole(x, y, z)`, and for Full Body IK `ik_hand("left", x, y, z [, weight])`,
`ik_foot(...)`, `ik_look(x, y, z)`, `ik_release()` and `ik_reached("left_hand")`. In
visual scripts: *IK Look At* and *IK Hand Reach*. State machine states have an
**IK Weight**, so IK can fade out during a roll or a climb and back in after.

## Troubleshooting

- **A knee or elbow bends the wrong way:** set the Pole Mode to *Character* with a
  *Pole Dir* pointing where the joint should go (knees forward), or add a Pole
  object. On imported rigs, *Auto* follows the animation's own bend.
- **Feet float or sink:** check *Ground Layers* includes the floor's layer and
  that the floor has a collider; raise *Foot Offset* if the ankle clips.
- **A limb flips or twists:** add a joint limit (the Elbow / Knee presets are a
  good start).
- **A hand snaps to a new target:** raise *Target Blend*.
