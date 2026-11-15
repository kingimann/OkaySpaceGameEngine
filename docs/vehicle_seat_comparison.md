# Vehicle seats: `claude2/vehicle-seat` compared with `claude/vehicle-looks`

Two branches add "get in a car and drive it" with different designs. This page compares them so the work is not done twice.
Written from reading both branches; **nothing was built or run**.

| Branch | Head | Based on | Design |
|---|---|---|---|
| `claude2/vehicle-seat` | `0bc85b2` (1 commit) | `ac822f1`, **before** `VehicleSeat` reached `main` (#876) | The chair `Seat` becomes a vehicle seat when it sits under a `VehicleController`. The first `Seat` in the car's hierarchy is the driver's seat. `VehicleController::DriverSeat()` / `HasDriver()`. |
| `claude/vehicle-looks` | `1ba4f49` (3 commits) | `9c13768`, which already has `VehicleSeat` from #876 | A separate `VehicleSeat` component with a `driver` flag. The car counts its driver seats (`AddDriverSeat` / `RemoveDriverSeat`) and knows its `Driver()`. Adds sitting pose, ride camera, rolling wheels, a real car scene, review fixes. |

`main` already has `VehicleSeat` (#876): the component, the car's driver count, the scene file record `vehicleseat`
(`engine/src/Scene/SceneSerializer.cpp:2307`), the Inspector block and the Add Component entry (`editor/main.cpp:34699`, `:40346`),
and `tests/test_vehicle_seat.cpp` registered as `vehicle_seat` (`tests/CMakeLists.txt:970-973`).

A trial merge (`git merge-tree`, no files changed) of `claude2/vehicle-seat` into `main` conflicts in 7 files, including an
add/add conflict on `tests/test_vehicle_seat.cpp` and `tests/CMakeLists.txt` (both register a `test_vehicle_seat` target).
`claude/vehicle-looks` merges into `main` with no conflict.

File references below: **C2** = `origin/claude2/vehicle-seat`, **VL** = `origin/claude/vehicle-looks`.

## Feature by feature

### Which seat drives, and when the car takes the keys

| | C2 | VL |
|---|---|---|
| Where | `VehicleController.hpp:72` `DriverSeat()`, `:74-79` `HasDriver()`, `:88-91` input, `:124` brake | `VehicleController.hpp:72-77` `Driver()`, `HasDriverSeat()`, `AddDriverSeat()`; `:102-109` input; `:144` brake |
| Driver seat | The first `Seat` found by `GetComponentInChildren<Seat>()`. Cannot be chosen; not saved. | Any `VehicleSeat` with `driver = true` (saved field). Counted by `VehicleSeat::Sync` (`VehicleSeat.hpp:210-219`). |
| Car with no seat | Drives from the keys at once. | Drives from the keys at once (also with only passenger seats). |
| Empty driver seat | No input, handbrake on, brakes at `brakeForce`. | No input, brakes at `brakeForce`, chase camera paused (`:172`). |
| Who counts as driver | The seat's occupant, only if `IsLocallyControlled(occupant)`. | `m_driver`, set by `VehicleSeat::Enter`. No locally-controlled check on the driver itself. |
| One driver per car | Implied (one driver seat). | Enforced: a second driver seat refuses while one is taken (`VehicleSeat.hpp:100-103`). |
| Seat moved to another car, `driver` unticked while seated | Not handled (no count to go stale). | Handled: `m_reg` / `m_driverCar` (`VehicleSeat.hpp:210-219`, `:359-363`). |
| Tests | `test_vehicle_seat.cpp:80` (no seat), `:89` (parked, then driven, then parked) | `test_vehicle_seat.cpp:53`, `:82`, `:275`, `:330`, `:442` |

### Getting in and out

| | C2 | VL |
|---|---|---|
| Where | `HumanoidActions.cpp:2205-2260` (`Seat::Update`), `:2166` `Seat::Claim`, `:2189` `Seat::Release` | `VehicleSeat.hpp:92-125` `Enter`, `:128-142` `Exit`, `:158-188` `Update` |
| Key | E (hard-coded, `GetKeyDown('e')`). | `enterKey`, default E (saved). |
| Who can get in | The nearest locally-controlled object **with a `Character`** (the chair rule). An NPC with a `Character` could be picked. | Locally-controlled objects with a player controller (`IPlayerSim`). NPCs refused. Tested (`:197`). |
| Two seats in range | The **closest free seat** wins (chair rule, `HumanoidActions.cpp:2248-2255`). | The seat that updates first takes the player (`VehicleSeat.hpp:170-187`); the other then sees the player seated. |
| Same press in and out | `m_claimedThisFrame` (`HumanoidActions.cpp:2227-2235`). | `s_justExited` (`VehicleSeat.hpp:162-187`). Tested (`:245`). |
| Exit spot | The seat's `exitPoint`, no check. | `exitPoint`, then mirrored sides, then the roof, skipping blocked spots (`VehicleSeat.hpp:315-323`). Tested (`:137`). |
| Exit pose | As the chair `Seat` leaves it. | Upright, yaw kept (`VehicleSeat.hpp:136-141`). Tested (`:293`). |
| Tests | `:89` (get in, drive, get out, press does not get back in) | `:64`, `:82`, `:137`, `:245`, `:293` |

### The rider's body while seated

| | C2 | VL |
|---|---|---|
| Player controller | Stays on. First person, third person and third-person shooter controllers are edited so that moving does not stand up a vehicle rider (`FirstPersonController.hpp:407`, `ThirdPersonController.hpp:407`, `ThirdPersonShooterController.hpp:152`). | Switched off while seated (every `IPlayerSim` on the player, `VehicleSeat.hpp:108-109`), so no controller edits are needed. |
| Colliders | The player's **own** non-trigger colliders off; triggers stay on; children untouched (`HumanoidActions.cpp:2171-2177`). | Every `Collider3D` on the player **and its children** off, triggers and hitboxes included (`VehicleSeat.hpp:110-111`). Tested (`:293`). |
| Gravity | The chair `Seat`'s gravity handling. | Saved and set to 0, given back on exit (`VehicleSeat.hpp:112-117`, `:327-339`). |
| Sitting pose | The chair `Seat`'s sit (`RequestSit`). | Same chair sit clips via `HumanoidActions::BeginVehicleSit` (`HumanoidActions.cpp:1810`), plus a fix to the sit height for all sits (`:735`, `:750`). Tested (`:527`, `:541`). |
| Death | Seat freed, colliders back (`HumanoidActions.cpp:2205-2208`). | Put out of the seat, stays in its death pose (`VehicleSeat.hpp:133`, `:161`). Tested (`:261`, `:558`). |
| Driver, seat or car destroyed | Chair `Seat` death path only. | No stale pointer for any of the three (`VehicleSeat.hpp:197-201`). Tested (`:168`, `:184`). |
| Saving while seated | Not handled. | The save writes the player as before they got in (`VehicleSeat.hpp:147-152`, `SceneSerializer.cpp:3876-3878`). Tested (`:217`). |
| Chair and vehicle seat mixed | A chair `Seat` **is** the vehicle seat. | Refused both ways (`VehicleSeat.hpp:95`, `:221-229`). Tested (`:316`). |

### Only on one branch

| Feature | C2 | VL |
|---|---|---|
| Ride camera (chase in third person, eyes in first person) | no | `VehicleSeat.hpp:247-305`, `VehicleController.hpp:94`; tested (`:576`, `:598`) |
| Rolling and steering wheel objects | no | `VehicleController.hpp:82-91`, `:193-230`; tested (`:392`, `:619`) |
| Real car sample scene, loaded from disk in the test | no | `SceneTemplates.hpp:902` `VehicleSeats3D`, `examples/vehicle/vehicle_test.okayscene`; tested (`:656`) |
| Save and load of seat fields | no (nothing new to save) | tested (`:345`) |
| Third-person **shooter** player can use the seat | yes (`ThirdPersonShooterController.hpp:152`) | **no**: `ThirdPersonShooterController` is not an `IPlayerSim` (`ThirdPersonShooterController.hpp:38`), so `VehicleSeat` never offers it a seat (`VehicleSeat.hpp:175`) and would not switch it off |
| An ordinary chair keeps working (moving stands up, colliders stay on) | tested (`test_vehicle_seat.cpp:147`) | not tested in `vehicle_seat` (VL does not change the chair `Seat` code, only the shared sit height) |

## Worth porting onto `VehicleSeat` (small follow-ups, only after the coordinator's PR merges)

1. **Third-person shooter players.** Let a player with only a `ThirdPersonShooterController` board a `VehicleSeat`, and switch that
   controller off while seated. Today it is ignored (`VehicleSeat.hpp:175`). C2's controller edit is the evidence the case exists;
   the port is a change in `VehicleSeat` (or making the shooter controller an `IPlayerSim`), not C2's code. Add a test.
2. **Nearest seat wins.** When a driver seat and a passenger seat are both in range, board the closer one, as the chair `Seat`
   does (`HumanoidActions.cpp:2248-2255` in C2, the same code on `main`). Today the update order decides. Add a test with the player
   nearer the passenger seat.
3. **A chair `Seat` under a car.** On VL a chair `Seat` placed on a car still acts like a chair: W stands the rider up and also
   drives the car (no driver `VehicleSeat`), and the rider's colliders stay on. C2 made that case work. The small port is a
   warning (Inspector hint or a console line on Start: "this Seat is on a vehicle; use Vehicle Seat to drive"), not C2's
   behaviour.
4. **Chair regression test.** C2's "ordinary chair: moving stands up, colliders stay on" check (`test_vehicle_seat.cpp:147`),
   added to VL's test file, since VL changes the shared sit height.
5. **Later, with multiplayer:** C2's rule that only a locally-controlled occupant drives (`VehicleController.hpp:78` in C2).
   It changes nothing in single player (`IsLocallyControlled` is true for everything without a `NetworkSync`,
   `engine/include/okay/Net/NetOwnership.hpp:24-30`), so it belongs with the seat replication work, not now.

## Pure duplicates in `claude2/vehicle-seat`

Everything else in C2 does the same job as `VehicleSeat` (already on `main` from #876, or improved on VL), and should not be merged:

- `VehicleController::DriverSeat()` / `HasDriver()` and the parked-car input and brake change: duplicate of `Driver()` /
  `HasDriverSeat()` / `unmanned`.
- `Seat::OnVehicle()`, `Seat::IsDriverSeat()`, `HumanoidActions::InVehicleSeat()`: the "this chair is in a car" checks.
  `VehicleSeat` is its own component, so they are not needed.
- The edits to `FirstPersonController.hpp`, `ThirdPersonController.hpp` and `ThirdPersonShooterController.hpp` that stop moving from
  standing up a rider: `VehicleSeat` switches the controllers off instead (except the shooter, see port 1).
- E to get in and out, the same-press guard, switching off the rider's colliders, freeing the seat on death: all in `VehicleSeat`,
  with more cases covered.
- The "Getting in and driving (Seat)" section of `docs/vehicle_controller.md`, its `docs/unfinished_engine.md` row and feature-map
  lines: VL's docs describe the shipped design.
- `tests/test_vehicle_seat.cpp` and its `tests/CMakeLists.txt` lines: same file name and target as the existing test on `main`;
  its cases are covered by VL's tests except the chair check (port 4).

Nothing was deleted and neither branch was changed.
