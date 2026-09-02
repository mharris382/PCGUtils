# PCGUtilsPainter

The Painter feature family — reusable procedural scalar/color field expressions and the nodes that apply them to
geometry. Extracted from `PCGUtilsDynMesh` into its own module because the family is growing and now targets
output surfaces beyond Dynamic Mesh.

## Module position

```text
PCGUtilsDynMesh        dynamic mesh data, selections, DynMesh processing, shared geometry utilities
      ^
      |
PCGUtilsPainter        Painter framework, generic evaluation, spatial providers,
                       DynMesh Painter integrations, Static Mesh Component Painter
```

`PCGUtilsPainter` **depends on** `PCGUtilsDynMesh` (and `PCGUtils`, `PCG`, `Engine`, `RenderCore`, the
GeometryProcessing modules). `PCGUtilsDynMesh` must never depend on `PCGUtilsPainter`.

The module exists for feature organisation and target expansion, not to make Painting independent of the DynMesh
toolkit. The core Painter *evaluation* API (`FPCGUtilsDynMeshPainterSample` / `...PainterValue` /
`...PainterOperation::Evaluate`) is kept geometry-agnostic so both DynMesh traversal and Static Mesh
render-vertex traversal can evaluate the same Painter expressions — but legitimate DynMesh dependencies elsewhere
in the module are expected and fine.

> Migration note: the Painter classes, structs and enums kept their names when they moved from
> `/Script/PCGUtilsDynMesh.*` to `/Script/PCGUtilsPainter.*`. Core Redirects in `Config/DefaultPCGUtils.ini`
> keep existing graphs loading. A later pass may drop `DynMesh` from the genuinely generic identifiers.

## Painter fields

Painters are reusable field expressions. A Painter evaluates either an untargeted scalar or a color with explicit
valid channels for a mesh sample containing local/world position, local/world normal, and geometric vertex
ID. A scalar does not choose its destination channel; a consuming node decides where to broadcast it. A color does
identify channels, so a consumer writes only the intersection of its requested channels and the channels supplied
by the Painter.

The Painter providers:

- **Paint from Points**: prepares world-space PCG points as independently sized brushes. Point Bounds mode fits an
  oriented ellipsoid to each point's transformed bounds, including non-uniform extents; Attribute mode reads a
  uniform world-space radius from a normal input selector (default `Radius`). Values use a selector defaulting to
  `$Density`. An optional inner-radius selector (default `InnerRadius`) creates a solid homothetic core, after which
  Hard/Linear/Smooth falloff begins. Linear and Smooth falloff support a per-point power selector defaulting to
  `$Steepness`, or a constant power. Max/Min/Add/Multiply overlap reduction is followed by the explicit `Clamp
  Value` option, which clamps the final result to `[0,1]`.
- **Axis Gradient Painter**: evaluates a clamped projection between Start and End along a normalized axis, in
  either DynMesh-local or world space, with optional inversion.
- **Painter Math**: evaluates two child Painters directly and combines them with Add, Subtract, Multiply, Min, or
  Max. Its inputs must be scalar Painters. Nested expressions do not create intermediate point data.
- **Combine Painters**: accepts optional `R`, `G`, `B`, and `A` Painter pins and produces one color Painter. A scalar
  child supplies the channel represented by its pin; a color child supplies that channel only when it defines it.
- **Points to Painter**: turns vertex-aligned PCG point datasets back into a Painter. Scalar mode reads a normal
  input selector defaulting to `$Density`; Color mode reads one defaulting to `$Color`. Multiple point datasets
  pair one-to-one, in order, with the DynMesh inputs evaluated by the consuming process. Every point count must
  equal the matching mesh's full vertex count and point order must remain unchanged, even when only a vertex
  selection will be painted. This provider is DynMesh-specific — it depends on `FDynamicMesh3` vertex iteration
  order — and has no Static Mesh equivalent.

The Painter consumers:

- **Paint DynMesh Vertex Color**: accepts one required `Painter` pin and exposes `Write Channels`. Its output pin is
  typed as `Dynamic Mesh` (or DynMesh Selection when `Output Selection Data` is enabled), preserving direct
  connections and context-sensitive graph search. It makes one
  primary vertex traversal, resolves the Painter result against the requested channels once per geometric vertex,
  and writes the final color once. A scalar is broadcast to all requested channels; a color preserves unrequested
  or undefined channels.

`Paint DynMesh Vertex Color` uses the module's actor-local DynMesh convention by default. It resolves the PCG target
actor transform once per mesh, populates both local and world sample fields, and therefore compares Paint from
Points' world-space brush centers against world-space mesh samples without mixing coordinate systems. Disable
`Mesh Is Actor Local` only when incoming mesh coordinates are already world space.

Color writes use shared seam-aware overlay helpers. A newly required overlay is initialized seamlessly; for an
existing split overlay, the base color is read from an attached element and the evaluated result is written to
every color element associated with that geometric vertex. This deliberately makes a vertex's Painter result
consistent across color seams.

The point round-trip pattern is:

`DynMesh To Points -> native PCG point processing -> Points to Painter -> Painter consumer`

Points to Painter retains a full one-point-per-vertex interface so point index remains aligned with DynMesh vertex
iteration order. This costs more storage than a native Painter. Evaluation remains selection-scoped: the Painter
consumer reads values only for selected vertices, so an inexpensive Selector can still protect high-poly meshes
from expensive per-vertex Painter work.

### Graph examples

Scarlet-macaw palette coordinate:

`Marker Points ($Density) -> Paint from Points (Smooth, Max) -> Paint DynMesh Vertex Color (Write Channels = R)`

The material reads `VertexColor.R` as its palette/gradient lookup coordinate.

Branch wind mask:

`Axis Gradient Painter -> Painter Math (Multiply).A`

`Paint from Points -----> Painter Math (Multiply).B -> Paint DynMesh Vertex Color (Write Channels = A)`

Combined color Painter:

`Paint from Points -> Combine Painters.R`

`Axis Gradient ----> Combine Painters.A -> Paint DynMesh Vertex Color (Write Channels = R, A)`

The Painter providers deliberately leave spatial acceleration, per-point radii, non-spherical brushes,
curves/remapping, noise, splines, textures, curvature, arbitrary named attributes, GPU evaluation, and destination
blend modes for later versions.

## Static Mesh Component Painter

`Paint Static Mesh Vertex Colors` applies a Painter to the per-component override vertex colors of existing
`UStaticMeshComponent`s (`FStaticMeshComponentLODInfo::OverrideVertexColors`) without touching the `UStaticMesh`
asset.

- **Targets** are resolved from a soft-object-path attribute on the `Target` input (default: the standard
  `ComponentReference` attribute, which `Get Static Mesh Data` now emits by default). Duplicate paths are
  de-duplicated; unresolved / unloaded paths are counted and warned once.
- **`Painter`** pin — exactly one, same contract as `Paint DynMesh Vertex Color`. `Points to Painter` is
  Dynamic Mesh-only and is rejected here with a graph error.
- **LOD Mode**: *All LODs* (default) evaluates the Painter independently against every LOD's own render vertices —
  no cross-LOD correspondence, matching the engine's runtime `FMeshVertexPainter`. *LOD 0 Only* writes just LOD 0.
- **Base Color**: *Modify Existing* (component override → asset colors → white), *Asset Vertex Colors*, *White*,
  *Black*. `Write Channels` limits which channels the Painter may change. `Convert To sRGB` (default off) — leave
  off for mask / gradient-lookup workflows.
- Render vertices are read directly from `FStaticMeshLODResources` — **no `FDynamicMesh3` round-trip**. World
  normals use the component matrix inverse-transpose (correct under non-uniform scale).
- **Editor-authoring only.** Mutates components on the game thread inside a transaction; never cacheable.
- **Skipped with a graph warning:** Nanite-rendered components (the Nanite raster path ignores override vertex
  colors — use a Mesh Paint Texture) and ISM/HISM components (one override buffer is shared by every instance —
  use Per Instance Custom Data).

The reusable write path lives in `PCGUtilsPainterStaticMeshBackend` (Engine + RenderCore only); it knows nothing
about PCG, the Painter framework, LOD policy, or targeting. Background:
`PCGUtils_StaticMeshInstanceVertexPainting_Investigation.md`.

## Roadmap

Deferred: LOD0→lower-LOD transfer / `RemapPaintedVertexColors`, render-vertex selection, Mesh Paint Texture
backend, per-instance ISM/HISM painting, Geometry Collection backend, runtime/cooked traversal, and dropping
`DynMesh` from the generic core Painter identifiers (cosmetic).
