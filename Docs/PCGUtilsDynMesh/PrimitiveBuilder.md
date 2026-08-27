# Primitive Builder

`Create Primitive` can now be driven two ways: its original single-inline-primitive behavior (**legacy
mode**, still the default), or a new **Builder** pipeline that supports per-primitive fitting, padding, and
a local pre-transform, and is designed to grow into composing several primitives per seed. This document
has two parts: a user-facing explanation of the feature as it exists today, and the implementation plan/design
record for the parts that are designed but not yet built. This whole feature was designed in one continuous
session, so part 2 exists to keep the reasoning behind each decision from being lost.

---

## Part 1 — User Guide

### Legacy mode vs. Builder mode

`Create Primitive`'s **Use Legacy Mode** checkbox (Advanced) is on by default, so every existing graph keeps
working unchanged: the primitive type is configured inline on the node, and optional seed points use the
original Transform/Bounds placement toggle.

Turn **Use Legacy Mode** off to switch the node into Builder mode:

- The inline `Primitive` property, `Use Seed Points`, and `Placement` disappear.
- `Seeds` becomes a required pin.
- A new required **Builder** pin appears. Connect a **Primitive Builder** node to it.

In Builder mode, `Create Primitive` generates one shape per seed point by evaluating whatever the connected
Builder describes, and appends all of them into a single output Dynamic Mesh.

### The `Primitive Builder` node

`Primitive Builder` (category `Utils DynMesh|Creation`) is where you configure one primitive:

- **Primitive**: the same inline, `Instanced` primitive-type picker `Create Primitive` has always had (Box,
  Sphere, Cylinder, ...). Nothing about the 11 existing primitive types changed.
- **Fitting**: controls how that primitive is sized, aligned, offset, and padded relative to each seed's own
  bounds. This is new and considerably more capable than the old Transform/Bounds toggle:

  - **Scale To Fit** — `None` (keep native size), `Uniform` (one scale factor for all axes), or `Individual`
    (pick a strategy per axis). Each axis's strategy is `None`, `Fill` (stretch to exactly match), `Min`
    (use the smallest of the three fill ratios, so nothing overflows), `Max` (use the largest, so at least
    one axis is filled exactly), or `Avg`.
  - **Justification** — per-axis alignment (`Min`/`Center`/`Max`/`Pivot`/`Custom`) for both where on the
    *primitive* to measure from and where in the *seed's bounds* to place it. `Custom` uses a fixed 0-1
    fraction rather than an attribute (see "Why constants-only" in Part 2).
  - **Padding** — insets (positive) or outsets (negative) the seed's bounds, per axis, before fitting runs.
    This exists specifically so two primitives built from the *same* seed can differ only by an inset amount
    - the motivating example is a window frame: a filled box, and a second filled box padded inward, combined
      with a boolean subtract (once the Boolean decorator exists - see Part 2) to leave a frame of consistent
      thickness.
  - **Local Transform** — a full offset/rotation/scale pre-transform applied to the primitive's own geometry
    before fitting is computed. Use it to nudge, spin, or pre-scale a primitive independent of the seed's own
    bounds (e.g. rotating a box 45° before it gets fit into a diamond orientation).

### What a Builder actually is

A Builder is a small reusable "recipe," not baked geometry — the same `Primitive Builder` node can feed
several `Create Primitive` nodes, or (once composition exists) be combined with other Builders into a bigger
assembly. It travels through its own pin type (colored distinctly from DynMesh/DynMesh Selection pins) so it's
visually obvious in the graph which pins carry "a plan for geometry" versus "geometry that already exists."

### Current limits (by design, for now)

Only a single `Primitive Builder` can feed `Create Primitive` today - there is no way yet to combine several
primitives into one compound shape, subtract one from another, or tag part of a result with a different
material/vertex color. All of that is designed (Part 2) but deliberately not built yet; see "Why V1 stopped
here."

---

## Part 2 — Implementation Plan & Design Record

### Status

- **Done**: shared Builder-factory scaffolding, the Fitting/Padding/Local-Transform math, the `Primitive
  Builder` leaf node, and the `Create Primitive` legacy/Builder split. See file list below.
- **Designed, not built**: the Combine/Boolean/Set-Attribute decorators, the bounds-deduplication
  optimization, and a larger open question about generalizing the module's process contract itself.

### Why this exists

`PCGCreatePrimitive` could only generate one hard-coded primitive type per node, with one hard-coded placement
mode. There was no way to build a compound shape (a column from a base + shaft + capital), subtract one
primitive from another (a window frame), or tag different parts of a result for downstream nodes to
distinguish. The fix is a composable **Primitive Builder** factory family that can eventually be combined,
decorated, and reused - including as saved subgraphs.

### Architecture: reused precedent, not new machinery

This follows the exact Factory Provider → Factory Data → Operation shape already used for DynMesh Selectors
(`Factories/PCGUtilsDynMeshFactoryProvider.h`, `PCGUtilsDynMeshFactoryData.h`, `PCGUtilsDynMeshOperation.h`),
itself a stripped port of PCGExtendedToolkit's factory pattern (see
`PCGExFactoryBoilerplate_ReplicationPlan.md` at the repo root for that prior stripping exercise - no task
manager, no `FFacade`, no `ManagedObjects`; plain `FPCGContext`, synchronous execution).

New shared scaffolding, mirroring `Factories/PCGUtilsDynMeshSelectionFactory.h` file-for-file:

- `Factories/PCGUtilsDynMeshPrimitiveFactory.h/.cpp` — `FPCGUtilsDynMeshPrimitiveFactoryDataTypeInfo`
  (`PCG_DataTypeDisplayName="Primitive Builder"`), `UPCGUtilsDynMeshPrimitiveFactoryData` (abstract factory
  base, `CreateOperation()`), `FPCGUtilsDynMeshPrimitiveOperation` (abstract runtime contract:
  `BuildMesh(SeedTransform, SeedLocalBounds) -> UDynamicMesh*`), pin constants (`Builder`/`Builders`),
  `PCGUtilsDynMeshFactories::GetPrimitiveBuilderFactoryTypes()`.
- A new pin color registered in `PCGUtilsDynMesh.cpp`'s `RegisterPinColors()` (copper/orange, distinct from
  the DynMesh-Selection yellow), per `CLAUDE.md`'s "no colorless custom data type" rule.

**Key design choice**: `BuildMesh` returns a private, freshly-allocated `UDynamicMesh*` already placed in the
seed's local space, rather than mutating a shared target. Every future composition primitive (append,
boolean, re-tag) becomes trivial without any partial-selection tracking, because each subtree owns a
self-contained mesh until something above it decides to combine meshes.

### Naming

`Factory` is implementation-only (per `CLAUDE.md`) and never appears in node titles/pins/tooltips - the
user-facing noun for the whole family is **Builder**, mirroring how Selection Factories are user-facing as
"Selector." Pins: `Builder` (single), `Builders` (multi-input, for future combining nodes). New files carry:

```
// Copyright Max Harris
// Fitting/alignment structures adapted from PCGExtendedToolkit, Copyright 2026 Timothe Lapetite and contributors (MIT).
```

### Fitting/Padding/Local-Transform math

`Elements/Creation/PrimitiveBuilder/PCGUtilsPrimitiveFittingDetails.h/.cpp`, ported from
`PCGExtendedToolkit/Source/PCGExCore/Public/Fitting/{PCGExFittingCommon.h,PCGExFittingOverrides.h}`, using the
already attribute-getter-free "Lean" structs there as the porting base (`FPCGExLeanScaleToFitDetails`,
`FPCGExLeanJustificationDetails`) - no `FFacade`/context dependency to strip, unlike the full PCGEx structs.

- **Why constants-only Justify** (no attribute-driven custom anchors): confirmed scope decision - PCGEx's full
  `FPCGExSingleJustifyDetails` supports reading a per-point attribute for the Custom From/To anchor via an
  input-shorthand-selector abstraction PCGUtilsDynMesh has no equivalent of. The "Lean" variant (fixed
  constants) covers the primary use cases (window/column construction) without that plumbing.
- `EPCGUtilsFitMode`/`EPCGUtilsScaleToFit`/`EPCGUtilsJustifyFrom`/`EPCGUtilsJustifyTo` and the free functions
  `PCGUtilsFitting::ScaleToFitAxes`/`JustifyAxis` are near-verbatim ports (renamed `PCGEx`→`PCGUtils`).
- **Padding** (`FVector` on `FPCGUtilsFittingDetails`) does not exist in PCGEx. It insets/outsets
  `SeedLocalBounds` before the fit math runs, clamped per-axis so an over-large value collapses to the bounds
  center rather than inverting. This is what makes a boolean-subtract window frame possible.
- **Local Transform** (`FTransform` on `FPCGUtilsFittingDetails`) mirrors PCGEx's already-precedented
  `FPCGExShapeConfigBase::LocalTransform` field (so it wasn't invented for PCGUtils, just carried over), but
  was explicitly generalized mid-design from an initial "padding + offset" idea into "padding + full
  offset/rotation/scale" per the user's direction.
- `ComputeLocalTransform(SeedTransform, SeedLocalBounds, CandidateBounds, OutTransform)` mirrors
  `FPCGExFittingDetailsHandler::ComputeLocalTransform`'s algorithm (local scale → fit → rotate AABB for
  justification → compose world transform) but takes `SeedTransform`/`SeedLocalBounds` directly as parameters
  instead of through a `PCGExData::FFacade`/`TargetIndex` lookup, since PCGUtilsDynMesh has no `FFacade`
  equivalent and seed data already arrives as plain `TPCGValueRange`s in `PCGCreatePrimitive.cpp`.

### The leaf node

`Elements/Creation/PrimitiveBuilder/PCGPrimitiveBuilderFactory.h/.cpp`:

- `UPCGPrimitiveBuilderFactoryData` carries `TObjectPtr<const UPCGCreatePrimitiveSettingsBase> Primitive`
  (reuses all 11 existing primitive subclasses **unchanged**) plus `FPCGUtilsFittingDetails Fitting`.
- `UPCGPrimitiveBuilderFactoryProviderSettings` derives **directly** from
  `UPCGUtilsDynMeshFactoryProviderSettings` (no intermediate base layer), matching how
  `UPCGDynMeshBoundsSelectionFactoryProviderSettings` derives directly rather than through an extra layer.
- The leaf operation generates the primitive once at identity to measure its native bounds (cached across
  every seed, since the config is static per node - mirrors what `PCGCreatePrimitive.cpp`'s legacy Bounds mode
  already did), then calls `Fitting.ComputeLocalTransform(...)` and appends into a fresh private mesh.
- **CRC**: `UPCGPrimitiveBuilderFactoryData::AddToCrc` hashes the primitive's class *and* calls
  `UObject::Serialize(Ar)` directly against the `FArchiveCrc32` to fold every reflected primitive parameter
  (Radius, Steps, Origin, ...) into the cache key - chosen over hand-listing every property across 11
  primitive subclasses, since `FArchiveCrc32` is itself a valid `FArchive` and `Serialize` already knows how
  to walk a UObject's tagged properties. `Fitting`'s fields are hashed by hand (it's a plain USTRUCT, not a
  UObject, so there's no `Serialize()` shortcut for it).

### `Create Primitive`'s legacy/Builder split

- `bUseLegacyMode` (Advanced, default `true`) is structural (`GetChangeTypeForProperty`), so toggling it
  rebuilds the node's pins.
- Legacy path is the original code, moved verbatim into `ExecuteLegacy` - zero behavior change for existing
  graphs.
- Builder path (`ExecuteBuilder`) resolves exactly one `Builder` input via
  `PCGUtilsDynMeshFactories::GetInputFactories<UPCGUtilsDynMeshPrimitiveFactoryData>(...)`, creates one
  `Operation`, and for each seed point calls `Operation->BuildMesh(SeedTransform, SeedLocalBounds)` then
  appends the result via the same `AppendMeshTransformed` call the legacy path already used (single-element
  transform array, so no new Geometry Script surface was introduced).

### Why V1 stopped here

Partway through planning the Combine/Boolean/Set-Attribute decorators, it became clear that a "decorator" is
structurally identical to any existing DynMesh **process** element (Smooth, Warp, SetVertexColor, every
Selection, ...) - the only difference is *when* it runs. A process element normally receives concrete
`UPCGDynamicMeshData`/`UPCGDynamicMeshSelectionData` and mutates it immediately; a Builder decorator instead
wants to receive a *promise* of mesh data (a Builder factory) and defer the same kind of mutation until a
seed's transform/bounds are known.

If `UPCGUtilsDynMeshProcessBaseSettings`'s contract could accept **either** concrete DynMesh/Selection data
**or** a Builder factory - producing a new, wrapped Builder factory instead of executing immediately when fed
one - then *every* existing process element becomes usable inside the Builder pipeline for free: `Smooth` a
Builder's output before it's ever materialized, run a Selection-based operation against a not-yet-built
primitive, etc. That's a much bigger payoff than three bespoke decorator classes, but it touches the shared
contract every existing derived element relies on - a different order of magnitude from adding new leaf-only
files. **This was deliberately deferred out of V1** rather than decided in the moment.

### Deferred: Combine / Boolean / Set Attribute decorators (reference design, not built)

Kept here in case the generalized process-base approach above turns out to be too large to land soon, so the
"obvious bespoke" version isn't lost:

- **Combine Builders**: multi-input `Builders` pin
  (`GetInputFactories<UPCGUtilsDynMeshPrimitiveFactoryData>`), `BuildMesh()` = allocate empty mesh, call each
  child's `BuildMesh()` in priority order, `AppendMesh` each in. Structurally
  `Elements/Selections/PCGDynMeshSelectionFactoryGroup.*` minus the AND/OR/NOT mode. Delivers "column from 3
  stacked primitives."
- **Boolean Builder**: exactly 2 required children on `Builders`, `Mode` enum (Union/Subtract/Intersect),
  executes via Geometry Script's mesh boolean function (exact entry point - expected
  `UGeometryScriptLibrary_MeshBooleanFunctions::ApplyMeshBoolean` - to be confirmed against the installed
  engine version when this is built). Delivers "window frame via box minus padded box."
- **Set Attribute decorator(s)**: wraps a single child `Builder`, stamps Material ID / Vertex Color / PolyGroup
  over 100% of that child's resulting mesh (safe - each subtree owns a private mesh, no partial-selection
  tracking needed). Reuse existing Geometry Script attribute calls already used in the module (see
  `Elements/Attributes/PCGSetVertexColor.cpp`).

Open questions to resolve before building either the bespoke version or the generalized one:

- Does `GetRequiredSelectionDomain()`/`RequiresSelection()`/domain-conversion still make sense against a
  Builder factory with no concrete mesh yet?
- Can a shared wrapper intercept "input was a Builder" once, in the base class, and re-invoke each element's
  existing `ProcessMesh(...)` override against each Builder's eventually-built mesh - meaning individual
  elements need zero code changes? (This is the outcome worth aiming for.)
- How does this interact with `FPCGUtilsMeshTargetHandle`/`CreateTarget()`, which currently assumes a concrete
  mesh is available up front?
- Does "Selector" (deferred selection-only predicate) and "Builder" (deferred mesh-producing pipeline) want to
  unify into one concept, or stay separate?
- CRC/caching: a Builder factory's `AddToCrc` needs to fold in whatever deferred chain is attached, the same
  way `UPCGDynMeshSelectionFactoryGroupData::AddToCrc` folds in child CRCs today.

### Planned optimization: dedup identical seed bounds (opt-out, default on)

Raised late in design: many seed sets place large numbers of points that share exactly the same bounds (a
grid of identically-sized cells, a copy of one templated point, etc.). Running the full Builder chain per
point in that case is wasted work - the *shape* the chain produces only depends on the seed's bounds and
scale, never its position or rotation.

**Why that's true, precisely**: `ComputeLocalTransform(SeedTransform, SeedLocalBounds, CandidateBounds,
OutTransform)` only reads `SeedTransform.GetScale3D()` before the final compose step - `SeedTransform`'s
rotation and translation are applied last, purely as a rigid transform on top of already-fitted, already-scaled
geometry. So the *canonical* (unrotated, untranslated) shape a Builder produces is a pure function of exactly
two inputs: `SeedTransform.GetScale3D()` and `SeedLocalBounds` (`Min`/`Max`). Everything else that varies
per-seed (position, rotation) can be applied afterward via one cheap `AppendMeshTransformed` call against a
cached mesh.

**Design** (not yet implemented):

1. Add `bool bDeduplicateSeedBounds = true;` (Advanced, Builder-mode only) to `UPCGCreatePrimitiveSettings`.
2. In `ExecuteBuilder`, key a per-execution `TMap<FKey, TObjectPtr<UDynamicMesh>>` cache on the **exact**
   tuple `(SeedTransform.GetScale3D(), SeedLocalBounds.Min, SeedLocalBounds.Max)` - i.e. memoize `BuildMesh`'s
   two real shape-affecting inputs verbatim, not a derived/pre-multiplied "effective size." This is
   deliberate: folding scale into a single combined size value (e.g. `Size * Scale`) was considered and
   rejected, because `ScaleToFit::None`-mode axes inherit the seed's raw scale directly rather than fitting
   against bounds, and Padding is applied to the *unscaled* bounds before scale is factored in inside the
   existing fit math - pre-folding would silently change both behaviors. Keying on the exact literal inputs
   instead makes this a pure memoization with **zero behavior change**, on or off - purely a performance
   toggle, not a semantics toggle.
3. On a cache miss, call `Operation->BuildMesh(FTransform(FQuat::Identity, FVector::ZeroVector, RealScale),
   SeedLocalBounds)` (identity rotation/position, but the *real* per-seed scale, since scale is part of the
   cache key/shape) and store the result.
4. Whether hit or miss, append the canonical mesh into the output via
   `AppendMeshTransformed(OutputMesh, CanonicalMesh, {FTransform(SeedRotation, SeedPosition,
   FVector::OneVector)}, Identity)` - note `Scale = 1` here, since scale was already baked into the canonical
   mesh's vertices during the cache-miss build.
5. Cache scope is a local variable inside one `ExecuteBuilder` call (not persisted across node executions or
   frames) - simplest option, avoids any GC/lifetime complexity with caching `UDynamicMesh*` objects across
   PCG's own execution/caching boundaries. A future refinement could hoist this further if profiling justifies
   it, but that's out of scope for the initial version.
6. This composes for free with the deferred Combine/Boolean/Set-Attribute decorators once they exist: because
   the cache wraps the single top-level `Operation->BuildMesh()` call, it automatically covers the entire
   composed tree's result for a given (scale, bounds) pair - no per-decorator caching plumbing needed.

Exact-value matching (no epsilon/quantization) is the intended default: it is trivially correct and the common
real-world case (grid generators, copied template points) tends to produce bit-identical bounds already.
Epsilon-tolerant bucketing was considered but deferred as a possible future enhancement, not required to
deliver the win described above.
