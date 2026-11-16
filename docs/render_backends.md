# Render backends: the shared contract, the chooser and the Vulkan probe

This is the foundation for a Vulkan renderer (issue #946, item 1). It adds three things and no Vulkan rendering.
Try it: [try_it.md, section 53](try_it.md).

## What exists now

| Piece | File | What it is | Written from |
|---|---|---|---|
| Capability table, chooser, failure rest | `engine/include/okay/Render/GpuBackendChooser.hpp` | Pure logic, no GPU. For each of software, OpenGL, Direct3D 11 and Direct3D 12: max lights, MSAA samples, sun and local shadows, GPU skinning, post chain, direct present, compute. `ChooseGpuBackends()` turns "what started" + "what the user wants" + "failure history" into an ordered list that always ends in Software. | The existing selection chains in `player/main.cpp` and the editor's `Render3DTexture`, and the numbers in `GLRenderer.cpp`, `D3D11Renderer.cpp`, `D3D12Renderer.cpp` and `RendererFeatures.hpp` |
| Backend contract | `engine/include/okay/Render/GpuBackend.hpp` | `IGpuBackend` (id, name, capabilities, `Init`, `Ready`, `Shutdown`, `RenderToPixels(GpuFrameDesc)`) and `GpuRendererAdapter<R>`, a thin wrapper that lets the three existing renderers satisfy it without changing what they draw. Hooks keep the host's own context handling (OpenGL `MakeCurrent`). | The three renderer headers' `RenderToPixels` / `Init` / `Destroy` |
| Vulkan loader seam | `engine/include/okay/Render/VulkanProbe.hpp`, `engine/src/Render/VulkanProbe.cpp` | Our own minimal loader: opens `vulkan-1.dll` / `libvulkan.so.1`, resolves `vkGetInstanceProcAddr` itself, creates an instance, lists physical devices (name, type, API version, a few limits, queue families), destroys the instance. The few Vulkan types and constants it needs are declared in the `.cpp`. | The Vulkan specification: instance creation, physical device enumeration, the layout of `VkPhysicalDeviceProperties` and `VkPhysicalDeviceLimits` (checked by a test that builds a buffer to that layout and reads it back), queue family properties. No Vulkan header, SDK or library is used or linked. |
| Tests | `tests/test_gpu_backend.cpp` (`gpu_backend`) | The chooser equals the old player chain for all 128 combinations of switches; the editor order; the failure rest incl. tick wrap-around; the adapter with a fake renderer; the Vulkan struct layout and report text | |

## What changed in the hosts

`player/main.cpp` and `editor/main.cpp` (both of `Render3DTexture` and `Render3DPassPixels`) no longer repeat their own
"Direct3D 12, then 11, then OpenGL" if-chains. They ask `ChooseGpuBackends()` and loop over the answer. The picks are the same as
before: player = Direct3D 12 (only when opted in), Direct3D 11, OpenGL, software; editor = Direct3D 11, OpenGL, software (never
Direct3D 12). The failure counters became `GpuFailureTracker`: three failed frames switch the GPU off for the rest of the run in the
player and rest it for 5 seconds in the editor, as before. The editor's zero-copy Direct3D 11 path (`RenderToTexture`) is untouched.

The hosts still call the renderers directly (they do not go through `IGpuBackend` yet); the adapter exists, is tested with a fake
renderer, and is the seam the next slice moves the hosts onto.

## The Vulkan probe (OFF by default)

- `OkaySpacePlayer.exe --gpu-probe` prints the capability table and writes `gpu-probe.txt`.
- The Vulkan part runs only when `OKAY_VULKAN_PROBE=1` is set. Without it the command says how to switch it on.
- Nothing else in the engine calls the probe. It does not render, make a surface or make a logical device.

## What is NOT done

- No Vulkan rendering: no logical device, swapchain, pipelines, shaders (SPIR-V) or memory management. `RenderBackend` has no Vulkan
  entry yet; it gets one when a Vulkan renderer exists.
- No Metal backend.
- The hosts do not use `IGpuBackend` objects yet; the chooser has no user-facing preference setting (forced backend, per-backend
  demotion) beyond what the player and editor already had.
- Capability numbers are a static table read off the code, not probed from the hardware. "MSAA 4" is what the hosts request.
  The software renderer's light count was not audited (reads 0).
- UNVERIFIED: the player and editor changes were compiled for Linux only. The Direct3D 11 / 12 branches (`_WIN32`) and any real GPU
  run were not exercised.
