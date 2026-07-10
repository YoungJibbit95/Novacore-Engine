# Shaders

Development shaders currently use GLSL and compile to SPIR-V through `glslc` when the Vulkan SDK is visible to CMake.

Current shader paths:

- `debug_triangle.*`: earliest swapchain/pipeline smoke shader.
- `world_box.*`: depth-tested 3D greybox primitive shader.
- `world_line.*`: depth-tested 3D debug line shader for aim rays, normals, and probes.
- `world_mesh.*`: vertex/index GLB mesh shader with simple normal-based shading.
- `sky.*`: renderer-owned full-screen sky gradient pass submitted before depth-tested world geometry.
- `contact_shadow.*`: soft analytic sun/contact shadows projected onto receiving ground planes.

The world mesh path consumes cooked UVs and evaluates a compact roughness/metallic material response with procedural micro-surface variation until descriptor-backed image sampling is enabled.

The long-term production path can move to HLSL plus DXC once descriptor reflection and cooked shader assets exist.







