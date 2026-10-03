# Desktop 2D rendering

`rendering/sprite2d.h` and `text2d.h` supply backend-neutral geometry. The desktop
Vulkan frame backend consumes the same explicit mesh/texture draw handles as the
opaque 3D path. Existing draws default to opaque blending with depth test/write.
Set `DrawCommand::state` to `painter2DState(view)` for sprite and UI draws: straight
alpha blending (`src.rgb * src.a + dst.rgb * (1-src.a)`), alpha accumulation,
depth disabled, nearest sampling, and the authored camera viewport. The pipeline
cache keys include blend/depth state; viewport/scissor are dynamic and reset for
every draw. Submit serial draws in painter order or use unique `DrawOrderKey`
values after sorting by authored layer and stable entity ID. Sorting must not use
resource handle creation order or unordered-map iteration.

Textures are top-left, row-major RGBA8. `loadTexture` accepts PNG and the existing
P3 PPM format. PNG preserves straight alpha, including zero-alpha RGB; neither
loader premultiplies. Texture samples use the existing `R8G8B8A8_UNORM` format,
so these numeric channels participate in blending as linear values; the SRGB
swapchain encodes the final framebuffer for presentation. PNG decoding is capped
at 8192 per axis, 16,777,216 texels, and 64 MiB encoded input. The PNG-only
stb_image decoder is pinned and licensed in `third_party/stb/README.md`.

`makeSpriteQuad` accepts world position/size, normalized pivot measured from the
left/bottom, and a top-left source pixel rectangle. Atlas bounds and all floating
values are validated. Invisible sprites return no geometry. Upload the description's
`tint` using `MaterialUniform::baseColor` with `mesh_textured` shaders. Geometry
already contains world position, so use an identity transform uniform.
`animationFrame(elapsedSeconds,fps,count,loop)` derives a frame from deterministic
elapsed simulation time; looping wraps, one-shot animation clamps at its last
frame, and invalid times/frame lists fail.

A camera requires authored logical dimensions and pixels per world unit; no
production resolution is inferred. `makeCamera2DView` computes the largest whole
integer framebuffer scale that fits and centers the viewport, leaving letterbox
bars clear. When a framebuffer is smaller than the authored logical viewport, a
uniform fractional fit is used and pixel-perfect output is not claimed. Zero
framebuffer dimensions return scale zero: callers skip submitting those frames.
Use actual framebuffer dimensions, including DPI scaling, rather than window
client dimensions. World positive Y is upward; projection flips it for Vulkan's
positive-height viewport. Pixel snapping rounds the world sprite anchor and
camera center to the nearest `1/pixelsPerUnit`; pivot/size remain authored. A
resize rebuilds a stale swapchain before recording framebuffer-sized viewports.

UI uses `makeLogicalUiCamera(logicalWidth,logicalHeight)`, with top-left coordinates
and positive Y down. Font asset schema 1 uses a separately declared stable texture
ID and explicit Unicode glyphs:

```json
{"schema":1,"texture":"asset:font-atlas","lineHeight":12,"fallback":63,
 "glyphs":[{"codepoint":63,"source":[0,0,6,10],"advance":7,"bearing":[0,1]}]}
```

`loadFontAtlas(path,textureWidth,textureHeight)` validates metrics, duplicate
codepoints, atlas bounds, the explicit fallback glyph, and unknown fields.
`bearing` is an offset from the top of the line in logical pixels; `advance` is
the horizontal cursor distance. Empty rectangles allow spacing glyphs. JSON is
capped at 4 MiB and 65,536 glyphs. A quote/escape-aware preflight caps combined
object/array nesting at 64 containers before recursive JSON parsing. The containing
project must resolve `texture`
to a declared texture asset and diagnose missing/wrong-kind references.

`makeTextGeometry` decodes strict UTF-8, rejecting overlong encodings, surrogates,
truncated sequences and values outside Unicode. It lays out the supplied glyphs,
wraps at word boundaries (breaking oversized words by glyph), handles newlines,
applies scroll offset and clips each glyph quad with corresponding UV adjustment.
Unsupported supplied codepoints use the declared fallback and are returned in
`missingCodepoints`. This is atlas coverage, not a claim of universal font support
or complex-script shaping. Texture dimensions and glyph metrics must match the
font asset used by panel layout, so UI measurement and rendering agree.

CPU coverage is `src/test/rendering/desktop2d_tests.cpp` (`test_desktop2d_rendering`);
`DESKTOP2D_TEST_MAIN` builds it standalone. It verifies real RGBA PNG decoding,
failed imports, sprite pivots/atlas bounds/visibility, animation, camera fit/snap,
Unicode/font loading, wrapping, fallback and clipped UVs. The standalone
`desktop2d_gpu.cpp` uses the real Vulkan `RenderDevice` path and validation layer,
reacquires tracked swapchain images with explicit barriers/fences, and asserts
transparent-background preservation, translucent overlap, reversed insertion with
keyed painter order, clipped font geometry, integer/fractional resize and
letterbox pixels. GPU readback artifacts are written beneath `build/desktop2d`.
