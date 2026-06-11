# Shaders

Development shaders currently use GLSL and compile to SPIR-V through `glslc` when the Vulkan SDK is visible to CMake.

Current shader paths:

- `debug_triangle.*`: earliest swapchain/pipeline smoke shader.
- `world_box.*`: depth-tested 3D greybox primitive shader.

The long-term production path can move to HLSL plus DXC once descriptor reflection and cooked shader assets exist.







