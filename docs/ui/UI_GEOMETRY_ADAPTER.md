# UI geometry adapter

`RenderFrameInfo::ui` is an ordered, premultiplied-alpha indexed triangle list.
Coordinates and scissor rectangles use UiDrawData's logical width/height; the
backend maps both to the active swapchain extent. UiDrawData::valid rejects
non-finite vertices, invalid ranges/indices and malformed RGBA8 textures.

UiTexture is immutable after submission. The Vulkan backend owns all native
resources; each in-flight slot keeps shared sources until its fence completes.
Expired cache entries can then be retired safely. Resize rebuilds resources
only after the existing swapchain device-idle boundary. First-use atlas uploads
use the engine graphics queue and a synchronous queue wait; this spike does not
claim hitch-free streaming. Pools and draw packet sizes are bounded.

The renderer is private and has no game or RmlUi dependency. It implements the
basic textured geometry interface only, without stencil masks, render layers,
GPU gradients/filters or backdrop blur. Vertex gradients and font-atlas effects
are supported by ordinary geometry/textures. Null/headless behavior is retained.

`RenderBackendFrameStats::uiGeometryReady` reports successful pipeline setup;
`lastUiGeometryDrawCount` reports draws actually recorded, not requested draws.
The game should reject its modern UI launch profile if the capability is absent.

Integration points: create with existing physical/device/render pass, prepare
only after the current slot fence, record before vkCmdEndRenderPass, shutdown
inside existing idle swapchain teardown. Native handles remain engine-private.
