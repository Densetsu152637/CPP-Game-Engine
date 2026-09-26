# Vulkan rendering

Build with `make VULKAN=1` (Windows: GNU Make from an x64 Native Tools prompt,
with `VULKAN_SDK` set to the SDK directory). `make VULKAN=1 run` starts the sample,
and `make VULKAN=1 smoke-validation` builds the GLSL shaders and runs the GPU
smoke test with the Khronos validation layer. Put the SDK `Bin` directory on
`PATH`; if its layers are not registered, set `VK_LAYER_PATH` to that directory.
Builds without `VULKAN=1` retain backend-neutral shader and uniform support;
attempting Vulkan initialization or drawing reports a disabled-backend error.

The Vulkan backend records a render pass, binds a graphics pipeline and descriptor
sets, draws three vertices, submits to the graphics queue, and presents the result.
Each `render(shader, uploads)` call draws one procedural triangle. Vertex shaders
must derive positions from `gl_VertexIndex`; there is currently no mesh or vertex
layout API. The pipeline uses filled triangles, no culling, no depth testing, and
an opaque color attachment. Viewport and scissor follow the swapchain extent.

Supply a `vulkan::VulkanShaderProgram` with exactly one SPIR-V vertex source and
one SPIR-V fragment source. Compile GLSL before rendering (for example, with
`glslc assets/shaders/triangle.vert -o build/shaders/triangle.vert.spv`, and likewise
for the fragment shader). GLSL text and compute/tessellation/geometry stages are
rejected by this graphics path.

Every entry in `shader.uniforms` becomes a uniform-buffer descriptor, visible to
both vertex and fragment stages, at its declared set and binding. Descriptor
arrays, samplers, storage buffers and push constants are not exposed by this API.
Set gaps are supported; duplicate set/binding pairs, empty buffers and values
exceeding device descriptor/range limits are rejected. The caller must match the
SPIR-V descriptor interface and block byte layout, including std140 padding and
matrix order. Uniform names are CPU-side identifiers, not shader reflection.

The included triangle shaders expect a column-major 4x4 float matrix (64 bytes)
at set 0 binding 0 and a four-float color (16 bytes) at set 0 binding 1. Bind those
component values using `shader.bindComponent<T>(name, {set, binding})` or upload
them explicitly through `renderer.upload(shader, name, value, frame, slot)`.
Upload all statically used shader bindings before the first draw.

Each draw snapshots all current uniform bytes into its own coherent host-visible
buffers and descriptor sets. Multiple entities using one shader in one frame
therefore keep distinct transforms/colors, even after later uploads change the
registry. Shader objects may be destroyed after their draw is recorded: GPU
pipelines use a key containing source bytes, entry points and descriptor bindings,
not a shader object's address. Pipelines are reused until swapchain recreation or
shutdown. Changing a shader's source or descriptor bindings selects a new pipeline.

There is one graphics frame in flight. A fence guards command-buffer and per-draw
resource reuse; each swapchain image has its own presentation semaphore. Resize
and out-of-date/suboptimal results rebuild dependent resources after device idle.
Minimized windows skip acquisition. Cancellation discards draws and submits a
clear frame to consume the acquire semaphore and release the acquired image.
Initialization/submission failures clean up the device state; initialize again
before retrying. Keep the GLFW window alive until renderer shutdown, and run frame
coordination on its owning thread after all render jobs have finished.

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
background pixels, and writes `build/vulkan-readback.ppm`. Unsupported surfaces
explicitly report that pixel verification was skipped.
For visual inspection, run `build/debug-vk1/bin/VulkanSmoke` (add `.exe` on Windows)
with `build/debug-vk1/shaders --validation --visual`. Each round holds the final
frame for 20 seconds: an orange triangle on the left and a cyan triangle on the
right demonstrate independent transform/color snapshots from a shared shader.
