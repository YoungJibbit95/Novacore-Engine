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

## M1 Placeholder

M1 provides:

- Renderer create/shutdown.
- Frame begin/end API.
- Clear color settings.
- Null fallback when Vulkan SDK is absent.
- Backend name reporting.
- SDL debug rectangles, lines, and bitmap text for early tools.
- Mesh/scene asset validation and placeholder mesh handles.

M2 replaces the placeholder with actual Vulkan instance, surface, swapchain, and clear.
Mesh parsing/upload remains the next renderer step after the current handle shim.

## Vulkan Device Plan

Device selection priorities:

1. Discrete GPU preferred.
2. Required queue families: graphics, present.
3. Required extensions: swapchain.
4. Optional extensions: debug markers, maintenance features.

The engine records selected adapter name and supported features at startup.

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

- HLSL authored shaders.
- DXC compiles to SPIR-V.
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
- Resize path survives.
- A basic mesh renders.
- Mesh asset ids resolve to stable handles before upload.
- GPU/CPU timing is visible.
- Null fallback still lets server/tools builds work.







