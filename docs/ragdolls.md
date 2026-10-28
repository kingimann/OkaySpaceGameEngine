# Ragdolls and hit reactions

Add **Ragdoll** (Add Component → Ragdoll) to a humanoid: the blocky
Character or an imported rig that maps as a humanoid. Nothing else is needed.

- **Death**: when the body's HealthStat dies (or a script calls `die()`), the
  body goes limp and falls away from where the hit came from. It keeps the
  motion it had, so a running body tumbles forward. A revive stands it back up.
- **Hits**: a hit the body survives shoves the struck part, which springs back
  to the animation over **Recover Time**.

While limp, the body's animators, IK and movement controllers are switched off
and the root Rigidbody3D is stopped. They come back on when the ragdoll ends.

## How it works

Each of the 15 humanoid bones is a particle, plus a tip past the head, hands
and feet. Bone lengths are held by distance constraints, the torso is braced
into a box, and elbows, knees and the spine can bend but not fold flat. The
particles collide with the scene's 3D colliders and terrain by ray, ignoring
the body's own colliders. The simulation runs at 60 Hz after animation and IK,
and poses the bones from the particles.

## Settings

| Setting | What it does |
|---|---|
| Ragdoll On Death | Go limp when the body dies |
| Death Push | Speed away from the killing hit (m/s) |
| Hit Reactions | Flinch on hits the body survives |
| Hit Push | Speed given to the struck part (m/s) |
| Recover Time | Seconds to spring back to the animation |
| Gravity, Damping, Friction | How the body falls, slows and slides |
| Skin Radius | Gap kept between the particles and surfaces |
| Iterations | Constraint passes per step (stiffer joints with more) |
| Max Speed | Cap on particle speed |

## Scripts

```
ragdoll()                 // go limp
ragdoll(0)                // stand back up
ragdoll_push(0, 2, 5)     // shove the chest (m/s)
ragdoll_push(0, 2, 5, x, y, z)   // shove the part nearest a world point
is_ragdoll()              // 1 while limp
```

Damage with a source (`HealthStat::Damage(amount, from)`) aims the death fall
and the flinch. In C++: `Ragdoll::Activate()`, `Deactivate()`,
`Hit(point, velocity)`, `IsActive()`, `IsSettled()`.
