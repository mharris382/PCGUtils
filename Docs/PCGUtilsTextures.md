# PCGUtilsTextures

Textures as inputs to PCG. The first slice turns images into paths: a greyscale sampling step, an image
vectorization library with three algorithms, and the **Image Path** component, a PCG path provider traced from a
texture.

## Module position

```text
PCGUtilsTexturesShaders   global compute shaders; loads at PostConfigInit; Core, RenderCore, RHI only
      ^
PCGUtilsTextures          sampling, vectorization, Image Path component; loads at Default
      depends on PCGUtils (path provider interface, path data) and PCG
```

Global shaders must be registered before the shader maps are built, so they live in their own module that loads at
`PostConfigInit` and knows nothing about UObjects, Engine or PCG. `PCGUtilsTextures` hands it a raw RHI texture.
The shader source is under `Shaders/Private/PCGUtilsTextures/`, mapped to `/Plugin/PCGUtils`.

## Sampling: texture to greyscale

`FPCGUtilsImageSampleOptions` reduces a texture to one float per pixel, in this order: resample so the longest side
is at most **Max Resolution** (box filter when shrinking, bilinear when enlarging), pick the **Channel**
(Luminance, R, G, B, A, Max RGB), **levels** (Black Point to 0, White Point to 1, clamped), **invert**, then a
separable Gaussian **blur** (sigma = Blur Radius / 2, out to 3 sigma).

**Read Source** decides where the pixels come from:

- **Source Data (CPU)**: the texture's uncompressed source, editor only. Exact; never touched by compression or the
  texture's max-size/LOD settings.
- **GPU**: the texture's GPU resource, processed by compute shaders (`SampleCS`, then two `BlurCS` passes) and read
  back. Works for render targets and in cooked builds. It reads the platform data, so block compression shows up in
  the result. It needs a renderer (not `-nullrhi`), and blocks until the GPU finishes, which is fine for rare
  editor-time rebuilds.
- **Auto** (default): source data when the texture has any, the GPU otherwise.

**Value Space**: *Authored* (default) reads an sRGB texture as it was painted, so a 0.5 threshold is the mid-grey the
artist picked. The CPU path skips the sRGB decode; the GPU path re-encodes after the sampler decodes. *Linear* reads
what the renderer shades with.

## Vectorization

Every algorithm takes a `FPCGUtilsGreyscaleImage` and an options struct, in the Geometry Script convention, and
returns `FPCGUtilsVectorPath`s in **image pixel coordinates** (X right, Y down, origin at the top-left corner). The
same structs drive the component, the Blueprint library (`UPCGUtilsVectorizationLibrary`) and any future consumer.
`FPCGUtilsVectorizeOptions` bundles an algorithm choice with all three structs.

| Algorithm | Options | Source | Output |
| --- | --- | --- | --- |
| Marching Squares | `FPCGUtilsMarchingSquaresOptions` | Lorensen & Cline marching squares, as in scikit-image `find_contours` | sub-pixel iso-contours, Douglas-Peucker simplified |
| Border Following | `FPCGUtilsBorderFollowingOptions` | Suzuki & Abe 1985, the algorithm behind OpenCV `findContours` | pixel-exact polygons through boundary pixel centers |
| Curve Fit | `FPCGUtilsCurveFitOptions` | Schneider, Graphics Gems 1990, on marching-squares contours | cubic Bezier curves with tangents, split at corners |

When to use which:

- **Marching Squares** follows greyscale. Anti-aliased or blurred masks give smooth, sub-pixel boundaries, and the
  iso level picks which grey to trace.
- **Border Following** is exact to the pixel grid, for pixel-art masks and anything where a pixel must be in or out.
  Its hierarchy comes straight from the algorithm, so even a one-pixel-wide ring nests correctly. Simple
  approximation keeps only direction changes; Douglas-Peucker simplifies to Epsilon.
- **Curve Fit** produces the fewest points and smooth splines (`CurveCustomTangent`), keeping turns sharper than
  Corner Angle as corners. Max Error trades fidelity for smoothness.

Potrace is the usual third choice but is GPL, which the MIT PCGUtils cannot include.

Shared behaviour, applied by `PCGUtilsVectorization::FinalizePaths`:

- The image is treated as surrounded by background, so a shape touching the border is still closed by it.
- Paths enclosing less than **Min Area** square pixels are dropped as speckles. A dropped path's children move to
  its parent.
- Nesting: `ParentIndex`, `Depth` (0 outer, 1 hole, 2 island in a hole, ...) and `bIsHole` (odd depth). Border
  Following uses its own exact hierarchy; the others nest by containment.
- Winding: outer boundaries have positive signed area in image coordinates, holes negative.
- **Retrieval** *Outer Only* keeps depth 0.
- Order: by depth, then by area, largest first. Path 0 is the largest outer boundary.

Curve paths store Hermite tangents per point (`ArriveTangents`, `LeaveTangents`); a segment's Bezier handles are
`P0 + Leave0 / 3` and `P1 - Arrive1 / 3`.

## Image Path component

`UImagePathComponent` (display name *Image Path*) implements `IPCGPathProvider` and `IPCGBoundsProvider`, alongside
`UPCGSplineComponent` and `UShapePathComponent`.

- **Texture**, **Sampling** and **Vectorization** decide the trace. **Image Width**, **Pivot** and **Path Data**
  only place and describe it.
- **Caching**: the component keys its trace on everything that affects it: the texture's identity and content id
  (`FTextureSource::GetId`, which changes on every import or edit), the sampling options, the algorithm, and *that
  algorithm's* options. Moving the component, changing Image Width or Pivot, editing Path Data, or editing an
  algorithm that is not selected reuses the cached paths. The cache is serialized, so loading a level or running a
  cooked build traces nothing. In the editor, a texture reimport or edit is caught through
  `OnObjectPropertyChanged` and retraces only if the content id changed. On register the key is checked, which
  catches a texture changed while the level was closed. A render target's content changes without notice: call
  **Rebuild Paths** with *Force*.
- **Placement**: the image lies in the component's XY plane, reading upright in the Top viewport (image right is +Y,
  image up is +X). It is Image Width centimetres wide, with Pivot (0..1 UV) at the component origin. This is a
  rotation plus scale, so winding is kept.
- **Status** shows the last result: path and hole counts and the traced resolution, or why the image could not be read.
- The editor visualizer draws the image rectangle, outer boundaries in green and holes in orange.

## Multiple paths through the path provider interface

`IPCGPathProvider` gained `GetPCGPathCount()` and `GetPCGPathPointsAt(PathIndex, ..., bIsHole)`. The defaults
(count 0, index 0 forwarding to `GetPCGPathPoints`) keep every existing provider, Blueprint providers included,
behaving as before. **Get PCG Path Data** emits one data per path for a provider with a count above 0, in point or
spline mode. Two optional @Data outputs tell the paths apart: **Path Index** (Integer32, default name `PathIndex`)
and **Is Hole** (Boolean, default name `IsHole`).

## Roadmap

A PCG element that vectorizes texture data inside a graph; GPU marching-squares cell classification for very large
images; asynchronous rebuilds; color-layer vectorization (one layer per palette color); open centerline tracing
(skeletons) for line art.
