# 02 Renderer Vulkan

## Renderer Goal

The renderer is Vulkan-first and built for modern competitive readability. It should support high frame rates before chasing photorealism. The initial goal is a stable clear-screen and mesh path; the long-term goal is Forward+ with readable realism.

## Backend Layers

- `Renderer`: public engine interface used by game code.
- `RenderWorld`: extracted renderable scene state.
- `VulkanDevice`: instance, physical device, logical device, queues.
- `Swapchain`: surface, images, format, resize path.
- `RenderGraph`: pass/resource dependency description.
- `GpuResourceManager`: buffers, images, samplers, descriptors.
- `MaterialSystem`: shader variants and material bindings.
- `DebugGpu`: labels, timings, counters.

## M1/M2 Active Foundation

The current foundation provides:

- Renderer create/shutdown.
- Frame begin/end API.
- Clear color settings.
- Null fallback when Vulkan SDK is absent.
- Backend name reporting.
- SDK-free Vulkan runtime and physical-device probing.
- SDL debug rectangles, lines, and bitmap text for early tools.
- Mesh/scene asset validation and placeholder mesh handles.
- CPU-side glTF/GLB scene info attached to mesh handles for early renderer diagnostics.
- CPU-side GLB vertex/index extraction attached to mesh handles for the coming upload path.
- Opt-in compiled Vulkan backend with SDL surface creation, physical device selection, logical device/queues, swapchain, image views, render pass, framebuffers, command buffers, semaphores, fences, clear, present, and a first debug triangle draw.
- CMake-driven GLSL-to-SPIR-V shader compilation through `glslc` when the Vulkan SDK is visible.

GPU upload and mesh draw submission remain the next renderer steps after the current CPU mesh extraction shim and debug triangle pipeline.

Current local runtime result:

- Vulkan loader/runtime is available.
- The current Vulkan SDK is exposed at `F:\VulkanSDK\1.4.350.0`.
- NovaCore CMake finds Vulkan headers/libs and `glslc`.
- `nemisis_game --vulkan-smoke-test` creates the Vulkan swapchain and debug triangle graphics pipeline.
- NovaCore runtime probe detects `NVIDIA GeForce RTX 3070 Ti (discrete)`.
- Nemisis keeps `sdl-debug` as the default visible UI/debug backend while `--vulkan` and `--vulkan-smoke-test` opt into the compiled Vulkan backend.

## Vulkan Device Plan

Device selection priorities:

1. Discrete GPU preferred.
2. Required queue families: graphics, present.
3. Required extensions: swapchain.
4. Optional extensions: debug markers, maintenance features.

The engine records selected adapter name and supported features at startup.
The current SDK-free runtime probe already records loader version and physical device names/types before full queue/surface/swapchain selection exists.

## Frame Model

Use two or three frames in flight:

- Per-frame command pool.
- Per-frame descriptor arena.
- Timeline semaphore where available.
- Fences only where needed.

Frame stages:

1. Acquire swapchain image.
2. Build render graph.
3. Record command buffers.
4. Submit.
5. Present.

## Render Graph

The render graph should describe:

- Pass name.
- Read resources.
- Write resources.
- Clear/load/store behavior.
- Queue requirements.

Early passes:

- Depth prepass optional.
- Main opaque pass.
- Transparent pass.
- UI pass.
- Present pass.

## Lighting Direction

M2/M3:

- Directional light.
- Point lights.
- Shadow map for directional light.

Later:

- Forward+ clustered light culling.
- Spot lights.
- Contact shadows or SSAO.
- Reflection probes if needed.

## Material Model

Use PBR-lite first:

- Base color.
- Normal.
- Roughness.
- Metallic.
- Emissive.

Fallback material must be deterministic and visually obvious.

## Shader Pipeline

- GLSL debug shaders compile to SPIR-V through `glslc` for the first backend slice.
- HLSL authored shaders and DXC are still the preferred future production direction.
- Reflection generates descriptor expectations.
- Debug builds keep shader names and compile diagnostics.
- Release builds use cooked shader artifacts.

## UI Composition

Game UI is rendered after world passes. The UI system is Vulkan-native but exposes a NanoVG-style API to game code:

- Rounded rectangles.
- Gradients.
- Blur regions.
- Glow.
- SDF text.
- Vector paths.
- Gamepad focus indicators.

## Performance Targets

- 144 FPS target at 1080p/1440p on mid-range PC hardware.
- Render settings must expose scalable quality.
- GPU timing per pass.
- CPU timing for extraction, culling, and submission.

## Acceptance

Renderer work is acceptable when:

- Clear color appears in a window.
- The opt-in Vulkan backend creates a swapchain, render pass, framebuffers, and debug triangle pipeline.
- Resize path survives.
- A basic mesh renders.
- Mesh asset ids resolve to stable handles before upload.
- Imported glTF assets expose scene counts before GPU upload.
- Imported GLB assets expose CPU primitive, vertex, and index counts before GPU upload.
- Vulkan runtime device detection is visible in logs/debug UI even before the compiled Vulkan backend is active.
- GPU/CPU timing is visible.
- Null fallback still lets server/tools builds work.







