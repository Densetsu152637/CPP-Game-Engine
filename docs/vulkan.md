# Vulkan rendering

Build with `make VULKAN=1` (Windows: GNU Make from an x64 Native Tools prompt,
with `VULKAN_SDK` set to the SDK directory). `make VULKAN=1 run` starts the sample,
and `make VULKAN=1 smoke-validation` builds the GLSL shaders and runs the GPU
smoke test with the Khronos validation layer. Put the SDK `Bin` directory on
`PATH`; if its layers are not registered, set `VK_LAYER_PATH` to that directory.
Builds without `VULKAN=1` retain backend-neutral shader and uniform support;
attempting Vulkan initialization or drawing reports a disabled-backend error.

Desktop rendering goes through `rendering::RenderDevice`. Its backend worker
creates and owns Vulkan resources and submits GPU frames. Callers create shader,
mesh, and texture handles from copied descriptions, then record complete draws
and uniform values in a `RenderCommandBuffer`. Frame recording may run on several
CPU threads. Use `record(draw, DrawOrderKey{producer, drawIndex})` when draws are
recorded concurrently and their order matters: submission sorts those keys;
duplicate keys and mixing keyed with serial records are rejected. Serial
`record(draw)` keeps call order. `submit` seals the frame; `wait(ticket)` and
`waitIdle()` establish completion before dependent work or shutdown. The device
retains resources referenced by queued frames, so temporary CPU descriptions
may be released after handle creation. Window events still run on the window
owner's thread through the surface provider.

`device.capabilities().feature(RenderFeature::SampledTextures)` and the other
named feature queries return support plus a reason when unsupported.
`diagnostics()` prints all feature statuses. The authored preview requires an
initialized backend, sampled textures, and a depth attachment and reports the
specific missing feature before submitting a draw. CPU-parallel recording does
not imply parallel GPU command encoding; the Vulkan backend encodes on one
render worker.

The Vulkan backend records a render pass, binds a graphics pipeline and descriptor
sets, submits to the graphics queue, and presents the result. `render(shader,
uploads)` retains the procedural triangle used as a renderer smoke fixture.
`drawMesh(shader, vertices, layout)` records a non-indexed triangle-list draw from
explicit CPU vertex bytes. The layout supports `Float2`, `Float3`, and `Float4`
attributes with caller-declared locations, stride, and offsets. Invalid layouts,
device-limit violations, and data whose byte count does not match its stride are
rejected. Vertex bytes are copied to a frame-owned host-visible vertex buffer.
There is no index buffer, culling, blending, or mipmapping yet. Each swapchain
image has a depth target; pipelines use depth test/write with `LESS`. The renderer
chooses a supported D32 or D24 depth attachment format. Viewport and scissor follow
the swapchain extent.

Supply a `vulkan::VulkanShaderProgram` with exactly one SPIR-V vertex source and
one SPIR-V fragment source. Compile GLSL before rendering (for example, with
`glslc assets/shaders/triangle.vert -o build/shaders/triangle.vert.spv`, and likewise
for the fragment shader). GLSL text and compute/tessellation/geometry stages are
rejected by this graphics path.

Every entry in `shader.uniforms` becomes a uniform-buffer descriptor, visible to
both vertex and fragment stages, at its declared set and binding. Set gaps are
supported; duplicate set/binding pairs, empty buffers and values exceeding device
descriptor/range limits are rejected. The caller must match the SPIR-V descriptor
interface and block byte layout, including std140 padding and matrix order.
Uniform names are CPU-side identifiers, not shader reflection. The textured mesh
overload adds one combined image sampler at set 0, binding 3; this binding is
reserved for that draw. Other descriptor types, arrays, storage buffers and push
constants are not exposed.

The sample shaders use a column-major model matrix (64 bytes) at set 0 binding 0,
a four-float `MaterialUniform::baseColor` (16 bytes) at set 0 binding 1, and a
column-major view-projection `CameraUniform::viewProjection` matrix (64 bytes) at
set 0 binding 2. Upload all statically used shader bindings before the first draw.
Uniform values can be supplied through `shader.bindComponent<T>(name, {set,
binding})` or `renderer.upload(shader, name, value, frame, slot)`. Components do not
define GPU layouts: use the explicit camera/material uniform structs or pack other
shader values into a matching GPU-side structure.

`rendering::loadMeshAsset` reads the small `CGMESH 1` text format: one `vertex x y
z u v` record per vertex, with a non-indexed triangle list, finite floats, and a
maximum of 1,000,000 vertices. `MeshAsset::layout()` describes position at
location 0 (`Float3`) and UV at location 1 (`Float2`).
`rendering::loadTexturePpm` accepts ASCII PPM P3 with max value 255 and converts
RGB pixels to opaque row-major RGBA8. The loader caps each dimension at 8192 and
the image at 16,777,216 texels. Vulkan currently samples `R8G8B8A8_UNORM` with
nearest filtering and repeat addressing. Texture bytes are uploaded synchronously
by the Vulkan backend and retained through frame completion. The persistent
`RenderDevice` path caches texture resources by handle for reuse across frames.
The authored sample stores `asset:player-mesh` at
`examples/first-project/assets/player.mesh` and `asset:player-texture` at
`examples/first-project/assets/player.ppm`.

Each draw snapshots all current uniform bytes into its own coherent host-visible
buffers and descriptor sets. Multiple entities using one shader in one frame
therefore keep distinct transforms/colors, even after later uploads change the
registry. Shader objects may be destroyed after their draw is recorded: GPU
pipelines use a key containing source bytes, entry points, descriptor bindings and
vertex layout, not a shader object's address. Pipelines are reused until swapchain
recreation or shutdown. Changing a shader's source, descriptor bindings, texture
mode or vertex layout selects a new pipeline.

There is one graphics frame in flight. A fence guards command-buffer and per-draw
resource reuse; each swapchain image has its own presentation semaphore. Resize
and out-of-date/suboptimal results rebuild dependent resources after device idle.
Minimized windows skip acquisition. Cancellation discards draws and submits a
clear frame to consume the acquire semaphore and release the acquired image.
Initialization/submission failures clean up the device state; initialize again
before retrying. Keep the GLFW window alive until renderer shutdown, and run frame
coordination on its owning thread after all render jobs have finished when using
`VulkanRenderer` directly. With `RenderDevice`, poll window events on that thread
and use `wait` or `waitIdle` before destroying the surface.

Enable `VulkanRendererConfig::enableValidationLayers` to request the installed
`VK_LAYER_KHRONOS_validation` layer. This is deliberately an explicit requirement:
initialization reports an error if that layer is unavailable.
An optional `validationCallback`/`validationUserData` receives Vulkan debug-report
warnings and errors throughout initialization, drawing and teardown. The callback
and its user data must remain valid until shutdown completes. The standalone smoke
test uses this callback to fail on any validation error, including resource leaks.
On surfaces that support transfer-source usage and the sample SRGB formats, the
smoke test also reacquires a tracked rendered image and reads it back with explicit
GPU synchronization. It checks the separate orange/cyan triangles and clear
background pixels. Farther red/green overlays must fail the depth test and leave
the foreground colors visible. The test writes `build/vulkan-readback.ppm`. Unsupported surfaces
explicitly report that pixel verification was skipped.
For visual inspection, run `build/debug-vk1/bin/VulkanSmoke` (add `.exe` on Windows)
with `build/debug-vk1/shaders --validation --visual`. Each round holds the final
frame for 20 seconds: an orange triangle on the left and a cyan triangle on the
right demonstrate independent transform/color snapshots from a shared shader.
