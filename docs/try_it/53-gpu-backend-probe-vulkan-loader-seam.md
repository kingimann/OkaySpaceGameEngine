---
id: 53
row: | 53 | GPU backend table and Vulkan probe (`--gpu-probe`, `OKAY_VULKAN_PROBE=1`, off by default) | `Tools\OkaySpacePlayer.exe --gpu-probe` in a command prompt | New in this change; compiled and unit tested on Linux only, not run on Windows or on a GPU |
---
## 53. GPU backend table and Vulkan probe

What it is: a self-test that prints what each of our graphics backends (software, OpenGL, Direct3D 11, Direct3D 12) can do, and, only
when you switch it on, asks Windows for its Vulkan graphics driver and lists the graphics cards it finds. It draws nothing. The
engine still renders with Direct3D 11 / OpenGL / software exactly as before; Vulkan rendering does not exist yet
([render_backends.md](../render_backends.md)).

Steps (Windows release):
1. Open a command prompt in the release folder, in `Tools\`.
2. Run `OkaySpacePlayer.exe --gpu-probe`. You should see a "GPU backends (static capability table)" list with one line per backend,
   then the line `Vulkan probe is off. Set OKAY_VULKAN_PROBE=1 and run --gpu-probe again to list Vulkan devices.`
   The same text is written to `gpu-probe.txt` in the folder you ran it from (use this if no text shows in the window).
3. Switch the Vulkan part on for this window only: `set OKAY_VULKAN_PROBE=1`, then run `OkaySpacePlayer.exe --gpu-probe` again.

What you should see: on a PC with a Vulkan driver (most recent NVIDIA, AMD and Intel drivers) a line `Vulkan probe: loader vulkan-1.dll,
loader version 1.x.y`, `instance: created and destroyed`, then for each card its name, type (discrete or integrated GPU), Vulkan
version, a few limits and its queue families (for example `graphics+compute+transfer`). On a PC with no Vulkan driver you should see
`Vulkan probe: no Vulkan loader found on this machine.` Either answer is correct; the probe never changes how the game renders.
To switch it off again, close the window or `set OKAY_VULKAN_PROBE=`.

Status: unit tested on Linux (`test_gpu_backend`: the chooser against the old if-chains, the failure rest, the Vulkan structure layout
and report text; the live probe ran on the build machine and printed "no Vulkan loader"). NOT run on Windows, not run against a real
Vulkan driver, and the Direct3D 11 / 12 and OpenGL call paths in the player and editor were not run on a GPU (UNVERIFIED).
