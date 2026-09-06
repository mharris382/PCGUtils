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

## Canonical target model

Every Painter graph evaluates against a canonical `FDynamicMesh3`, whatever the original target domain. The
`FPCGUtilsPainterTarget` abstraction (`Target/PCGUtilsPainterTarget.h`) owns target conversion, LOD
propagation, and write-back; **no Painter factory contains Static Mesh, Geometry Collection, or other
target-specific logic**, and there are no per-factory LOD modes or capability flags.

```text
Original target
    -> canonical FDynamicMesh3
    -> EvaluatePainterGraphOntoTarget()  (one traversal, seam-consistent write to the primary color overlay)
    -> Target::Commit()                  (write the painted colors back; propagate to lower LODs)
```

- `FPCGUtilsPainterDynMeshTarget` — the canonical mesh *is* the input `UPCGDynamicMeshData`; evaluation writes
  in place and `Commit()` is a no-op. `Paint DynMesh Vertex Color` routes through this.
- `FPCGUtilsPainterStaticMeshTarget` — `Prepare()` builds a transient `FDynamicMesh3` of LOD0 by
  **position-welding** the render buffers: render vertices whose positions are within `CanonicalWeldTolerance`
  become one canonical base vertex, while per-render-vertex color / normal / UV differences stay in overlays.
  It records the exact render-vertex ↔ canonical-vertex ↔ color-element correspondence and builds the LOD0
  AABB tree once. `Commit()` writes LOD0's override colors straight from that correspondence, then transfers
  only the painted write-channels to every lower LOD by closest-point surface projection.

### Canonical topology: what position welding can and cannot do

Position welding merges render vertices by proximity alone. It has no access to the authored mesh's vertex
identity, so it cannot distinguish:

- **render-buffer duplicates at a UV / hard-normal / tangent / material seam** — the static-mesh build
  produces these at *bit-identical* positions, so any positive tolerance welds them back into one geometric
  vertex. This is the desired behaviour and the reason the node uses welding.
- **two authored-distinct vertices that happen to sit within tolerance** — e.g. two separate mesh pieces
  touching at a shared corner. Position welding **will merge these into one canonical vertex**, joining what
  the artist authored as separate geometry.

There is no reliable runtime signal in UE 5.8 to tell these apart. `FStaticMeshLODResources::WedgeMap`
(wedge → render-vertex) is only populated for some build configurations, and the authored `FMeshDescription`
(editor-only) would move the ambiguity into a render-vertex ↔ source-vertex position match rather than
removing it. V1 therefore accepts the ambiguity and keeps welding, because:

- seams are the overwhelmingly common case and weld perfectly (distance 0);
- the tolerance is exposed (`CanonicalWeldTolerance`, default `0.01` asset units ≈ 0.1 mm) and validated —
  negative or non-finite values are rejected with a graph warning and fall back to `0` (no welding);
- component override-color write-back is unaffected either way (it is keyed by render vertex, not canonical
  vertex).

Consequence for topology-dependent Painters (`Random Value by Mesh Island`): islands are the connected
components of this canonical mesh. A false weld can join two islands; a `0` tolerance splits every seam into
its own island. Leave the tolerance at its default unless a specific mesh needs otherwise.

**Non-manifold triangles** in LOD0 render data are *retained*: their three corners are split onto separate
canonical vertices and color elements so the canonical mesh stays a complete representation of the render
surface. A graph warning reports the count; mesh-island results near those triangles may treat the split
corners as separate islands. Only genuinely degenerate or duplicate triangles are dropped, again with a
counted warning, and their render vertices keep their base color deterministically.

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

### Operation lifecycle: Initialize -> Prepare -> Evaluate

`FPCGUtilsDynMeshPainterOperation` runs once per operation instance, per root operation and target:

1. **`Initialize(Context)`** — validate configuration, resolve pin inputs, create and `Initialize` child
   operations. A composite (Painter Blend, Combine Painters, Selection Painter Switch) builds its children here.
2. **`Prepare(Context)`** — the optional one-off, mesh-wide topology pass a factory needs before per-vertex
   evaluation (connected components, one selector evaluation, a cached vertex lookup). The default is a no-op,
   so every existing stateless Painter is unaffected. A composite MUST call `Prepare` on its children.
   `Context.Mesh` (the canonical Dynamic Mesh) is always valid here, for every target domain.
3. **`Evaluate(Sample)`** — per vertex sample. `const`, and after `Prepare` the operation is immutable and safe
   for concurrent calls.

A failed `Initialize` or `Prepare` logs on the graph and discards the whole Painter, like any invalid factory
input. `FPCGUtilsDynMeshPainterEvaluationContext` now always carries the canonical `FDynamicMesh3` and a
`UPCGDynamicMeshData` view of it (real graph data for a Dynamic Mesh target, a transient wrapper for a Static
Mesh target); `bIsNativeDynMeshTarget` is the narrower "participates in DynMesh<->Points dataset pairing" flag
that only `Painter by Vertex ID` requires.

The Painter providers:

- **Bounds Brush Painter**: prepares world-space PCG points as independently sized brushes. Point Bounds mode fits an
  oriented ellipsoid to each point's transformed bounds, including non-uniform extents; Attribute mode reads a
  uniform world-space radius from a normal input selector (default `Radius`). Values use a selector defaulting to
  `$Density`. An optional inner-radius selector (default `InnerRadius`) creates a solid homothetic core, after which
  Hard/Linear/Smooth falloff begins. Linear and Smooth falloff support a per-point power selector defaulting to
  `$Steepness`, or a constant power. Max/Min/Add/Multiply overlap reduction is followed by the explicit `Clamp
  Value` option, which clamps the final result to `[0,1]`.
- **Axis Gradient Painter**: evaluates a clamped projection between Start and End along a normalized axis, in
  either DynMesh-local or world space, with optional inversion.
- **Painter Blend**: blends base `A` with blend `B` using Add, Subtract (`A-B`), Multiply, Darken (Min),
  Lighten (Max), Mix (Normal), or Screen. Scalar/scalar inputs return a scalar; mixed/color inputs return color.
  Scalars broadcast to the color operand's valid channels. The base defines output channels; undefined blend
  channels preserve the base rather than writing zero. Alpha is an ordinary channel, not implicit opacity.
  `Factor` and optional scalar `Mask` are independently clamped to `[0,1]` and multiplied; non-finite weights
  act as zero. The result is `lerp(A, blend(A,B), weight)` in linear value space, without output clamping.
  Default Multiply with Factor 1 preserves old scalar behavior. No intermediate points are made.
- **Combine Painters**: accepts optional `R`, `G`, `B`, and `A` Painter pins and produces one color Painter. A scalar
  child supplies the channel represented by its pin; a color child supplies that channel only when it defines it.
- **Painter by Vertex ID**: maps point values to explicit DynMesh vertex IDs using the configurable `Vertex ID
  Attribute` (default `VertexIndex`, an int32 attribute matching DynMesh To Points). Scalar mode defaults to
  `$Density`; Color mode defaults to `$Color`. Points may be reordered or filtered. IDs must be unique and valid
  in the consuming mesh; duplicate or invalid IDs reject initialization. Missing IDs return zero scalar influence
  or undefined color channels. Point positions and bounds are irrelevant. Multiple datasets still pair one-to-one
  with consuming DynMesh inputs. This is explicit correspondence, not surface projection or brush evaluation.
  Old serialized graphs retain legacy point-order mapping; new nodes default to `Use Vertex IDs` enabled. This
  is the one Painter that requires a **native** Dynamic Mesh target and is rejected on a Static Mesh Component.
- **Random Value by Mesh Island**: a topology-dependent scalar Painter. Its `Prepare()` pass finds the
  connected vertex components of the whole canonical mesh (`FMeshConnectedComponents::FindConnectedVertices`,
  edge connectivity) and gives each one a deterministic value in `[Min Value, Max Value]`, hashed from the
  node `Seed` and the component's smallest canonical vertex ID (so inserting geometry elsewhere does not shift
  other islands). `Evaluate` returns the cached per-vertex value. Islands are canonical base-topology
  components: UV / normal / colour-overlay seams and material boundaries never split one, and the outer paint
  Write Selection never redefines them. Empty meshes and sparse IDs are safe; an edge-isolated loose vertex
  becomes its own single-vertex island keyed by its own ID (a real per-island value, never a silent floor).
  Topology rebuilding or vertex-ID reassignment can change which value an island gets.
- **Selection Painter Switch** / **Selection to Painter**: two presentations of one binary Painter
  multiplexer. A **Value Selection** (any DynMesh Selector, including composite Selection Logic) classifies
  every canonical-mesh vertex during `Prepare()` — evaluated once, in the vertex domain, with the Selector
  library's own domain conversion for vertex / edge / triangle native selectors — and each vertex returns its
  Selected branch or its Unselected branch. Each branch is independently a **Constant** scalar or a connected
  **Painter**; only the chosen branch is evaluated per vertex (no evaluate-both-and-lerp), and constant
  branches are normalised into a shared constant Painter operation so all four combinations share one path.
  A branch set to Painter with nothing connected is a graph error. `Selection to Painter` is the same
  implementation preset to constant `1` / `0` with the Painter branch pins hidden — the plain
  "selection -> scalar mask" case.

  The Value Selection is independent of the outer paint node's DynMesh **Write Selection**: the Value
  Selection decides *what value* a vertex gets over the whole mesh, the Write Selection decides *whether* that
  vertex is written.

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

`DynMesh To Points -> native PCG point processing -> Painter by Vertex ID -> Painter consumer`

Enable the Vertex Index attribute on DynMesh To Points and preserve it during point processing. Painter by
Vertex ID does not infer identity from position or apply brush falloff. Legacy mode (`Use Vertex IDs` disabled)
requires the full vertex count and unchanged vertex-iteration order. Evaluation remains selection-scoped through
the consuming processor. DynMesh vertex IDs are not Static Mesh render-vertex IDs; this Painter remains
DynMesh-specific. Cross-target projection is intentionally outside this refactor.

### Graph examples

Scarlet-macaw palette coordinate:

`Marker Points ($Density) -> Bounds Brush Painter (Smooth, Max) -> Paint DynMesh Vertex Color (Write Channels = R)`

The material reads `VertexColor.R` as its palette/gradient lookup coordinate.

Branch wind mask:

`Axis Gradient Painter -> Painter Blend (Multiply).A`

`Bounds Brush Painter -----> Painter Blend (Multiply).B -> Paint DynMesh Vertex Color (Write Channels = A)`

Combined color Painter:

`Bounds Brush Painter -> Combine Painters.R`

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
- **`Painter`** pin — exactly one, same contract as `Paint DynMesh Vertex Color`. `Painter by Vertex ID` is
  Dynamic Mesh-only and is rejected here with a graph error.
- **LOD Mode** is a *target policy*, not a per-Painter setting: the complete Painter graph is always evaluated
  exactly once, on the canonical LOD0 mesh. *All LODs* (default) then transfers the painted write-channels to
  every lower LOD by closest-point projection + barycentric interpolation — so randomized or topology-dependent
  Painters stay spatially consistent across LODs. *LOD 0 Only* skips the transfer.
- **Base Color**: *Modify Existing* (component override → asset colors → white), *Asset Vertex Colors*, *White*,
  *Black* — resolved once on LOD0 and used to seed the canonical color overlay (and to seed each lower LOD's
  preserved channels). `Write Channels` limits which channels the Painter may change; on lower LODs the other
  channels keep their existing value. `Convert To sRGB` (default off) — leave off for mask / gradient-lookup
  workflows.
- LOD transfer interpolates in linear float space; byte quantization and sRGB encoding happen only at the final
  `FColor` write. The LOD0 spatial index is built once per target execution and reused for every lower LOD and
  every channel.
- **Editor-authoring only.** Mutates components on the game thread inside a transaction; never cacheable.
- **Skipped with a graph warning:** Nanite-rendered components (the Nanite raster path ignores override vertex
  colors — use a Mesh Paint Texture) and ISM/HISM components (one override buffer is shared by every instance —
  use Per Instance Custom Data).

The low-level per-component override write path lives in `PCGUtilsPainterStaticMeshBackend` (Engine + RenderCore
only); it knows nothing about PCG, the Painter framework, LOD policy, or targeting. Background:
`PCGUtils_StaticMeshInstanceVertexPainting_Investigation.md`.

## Reusable surface correspondence

`PCGUtilsDynMeshSurfaceCorrespondence` (in **`PCGUtilsDynMesh`**, `Geometry/PCGUtilsDynMeshSurfaceCorrespondence.h`)
is a general DynMesh utility — it depends only on GeometryCore, not on the Painter framework, Static Mesh
Components, or PCG element execution, so it can transfer any per-corner mesh attribute between two
representations of one surface in future systems.

- `ProjectPoints` / `ProjectMeshVertices` build a `FMeshSurfaceProjection` per destination sample: closest
  source `SourceTriangleID`, `BarycentricCoordinates`, and `DistanceSquared`. `ProjectMeshVertices` is indexed
  by destination vertex ID and is safe for sparse Dynamic Mesh IDs.
- **Coordinate-space contract**: the source AABB tree defines the reference space; each destination point is
  transformed by `FProjectionOptions::DestinationToSource` (destination → source) before the query, default
  identity (two LODs of one asset share the asset's local space).
- `SampleColorOverlay` / `TransferColorChannels` interpolate a source primary color overlay at a projection in
  linear float space and copy only the requested channel bits into a caller-owned destination array.
- Empty/degenerate source, out-of-range `MaxDistance`, invalid triangles, and non-finite input all produce
  `bProjected == false` with success/failure counts — no crashes, no Painter-specific logging.

## Roadmap

Deferred: render-vertex selection, Mesh Paint Texture backend, per-instance ISM/HISM painting, Geometry
Collection backend, cached/persistent LOD correspondence, runtime/cooked traversal, spatially stable island
IDs across topology rebuilds, weighted / feathered Selector output for the switch, and dropping `DynMesh` from
the generic core Painter identifiers (cosmetic).
