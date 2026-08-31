# PCGUtilsFracture Development Conventions

Module-specific conventions for `PCGUtilsFracture`. Repository-wide rules are in `AGENTS.md`.

---

## What this module is for

`PCGUtilsFracture` uses Unreal's Geometry Collection / Fracture stack as a **transient procedural modelling
backend inside PCG**. It is not a runtime Chaos destruction feature.

Geometry Collections expose solid-mesh decomposition operations that are very hard to reproduce robustly with
Dynamic Mesh processing alone. PCG is already good at producing spatial inputs - points, transforms, bounds,
filters. The module's whole value is joining the two:

```
DynMesh -> GC -> fracture -> bones as points -> ordinary PCG/PCGEx filtering
        -> bone selection -> prune -> GC -> DynMesh
```

Nothing here creates an asset, actor, component, package or transaction. Every collection lives and dies inside
one graph execution.

---

## Dependency direction

```
PCGUtilsEditor -> PCGUtilsFracture -> PCGUtilsDynMesh -> PCGUtils
```

`PCGUtilsDynMesh` must remain completely unaware of `PCGUtilsFracture`. Do not add a reverse dependency, and do
not "temporarily" move a fracture type into `PCGUtilsDynMesh` to avoid one.

Engine dependencies are deliberately minimal. `FGeometryCollection`, the collection facades and
`GeometryCollectionAlgo` live in **Chaos**; the DynMesh converter in **MeshConversionEngineTypes**;
`FDataflowTransformSelection` in **DataflowCore** - all engine Runtime modules, not plugins. Only `FractureEngine`
and `PlanarCut` are plugins. Never depend on `FractureEditor` (editor tool mode) or `GeometryCollectionNodes`
(Dataflow wrappers); read those for reference and call the same backend they call.

---

## The immutability contract

`UPCGGeometryCollectionData` holds `TSharedPtr<const FGeometryCollection>`. There is no API that mutates one.

A node that changes a collection must:

1. `CreateMutableCopy()` to get a private `TSharedRef<FGeometryCollection>`,
2. mutate the copy,
3. publish it through `InitializeAsRevisionOf(InputData, Copy)`.

Never `const_cast` a collection, and never mutate an input to add a derived attribute - `GC Bones To Points`
computes hierarchy levels by walking `Parent` rather than calling `GenerateLevelAttribute()` for exactly this
reason.

Holding the *derived* `FGeometryCollection` rather than `FManagedArrayCollection` is safe and deliberate. It is
what lets one object satisfy both the `FManagedArrayCollection&` fracture APIs and the `FGeometryCollection&`
prune API with no conversion hop.

`CreateMutableCopy()` deep-copies via `FManagedArrayCollection::CopyTo` into a freshly-constructed
`FGeometryCollection`. `CopyTo` adds any groups/attributes the destination lacks and `InitFrom`s the ones it
already has, so the derived type's external `TManagedArray` members (registered by `Construct()`) are filled in
place rather than orphaned. Do not "simplify" this to `*Copy = SomeBaseRef` - the base `operator=` is not
visible through the derived type, and going out of your way to call it is strictly worse than `CopyTo`.

---

## Identity: three fields, one authoritative check

| Field | Meaning |
|---|---|
| `CollectionId` | Stable across a whole lineage. "Which collection." |
| `Revision` | Increments per topology change. Human-readable ordering for diagnostics. |
| `StateId` | Unique per exact state. **The authoritative staleness check.** |

Bone indices are meaningful against exactly one collection state - both fracture and prune reindex them. A
stale index does not fail loudly on its own; it silently selects the wrong piece. So:

- Any node emitting bone indices onto points writes `GC_BoneIndex`, `GC_SourceId`, `GC_SourceRevision` and
  `GC_SourceStateId` (see `PCGUtilsGCIdentity`).
- Any node consuming them compares `StateId` first, and treats a mismatch as a **graph error, not a warning**.
- `Revision` alone is insufficient: two different collections can both be at revision 1. `StateId` cannot
  collide.

On top of that, `FDataflowTransformSelection`'s bit-array length must equal the transform count - every
FractureEngine entry point enforces this, so it is a free structural cross-check. `ResolveSelectionFromPin`
verifies it too.

---

## Transform space

**Canonical space is the source DynMesh's own local space, entered at identity and never re-pivoted.**

- `DynMesh To GC` passes `MeshTransform = FTransform::Identity` to `AppendMeshToCollection`, so vertices land
  verbatim and the bone transform is identity.
- Fracture operations pass `FTransform::Identity` and supply sites already in collection space.
- `GC To DynMesh` uses `FToMeshOptions::Transform = Identity` and never centres the pivot.

The only thing that converts is **incoming PCG spatial data**, which is world-space by PCG convention. Use
`PCGUtilsDynMeshSpaceHelpers::ResolveMeshActorTransform` for that - do not scatter bespoke target-actor code.
This is what makes the module correct at non-identity source transforms rather than only at identity.

Stored bone transforms are **parent-relative**. Anything spatial must go through
`PCGUtilsGCHelpers::ComputeGlobalTransforms` (`GeometryCollectionAlgo::GlobalMatrices`).

---

## Factories: behaviour and target are separate

Two factory families, both rooted at `UPCGUtilsGCFactoryData`:

- `UPCGUtilsFractureFactoryData` - **how** to fracture. Receives a mutable collection plus an already-resolved
  target selection. Owns no selection of its own.
- `UPCGUtilsGCSelectionFactoryData` - **which** bones an operation affects.

Executors (`Fracture GC`, `Prune GC`) combine them. A new fracture type is a new factory, never an edit to the
executor: `Fracture GC` contains no Voronoi-, plane- or cutter-specific code and must stay that way.
`Uniform Voronoi Fracture` and `Voronoi Fracture From Points` were added this way and required no executor
change - that is the architecture working as intended, and the bar any future cutter should clear.

### Mirroring Fracture Mode is a goal

A stated aim of this module is to make Fracture Mode's functionality available inside PCG. When adding an
operation, prefer the parameter names, defaults and grouping Epic uses for the equivalent tool (compare against
`FUniformFractureDataflowNode` and friends in `GeometryCollectionFracturingNodes.h`) so someone who knows
Fracture Mode recognises the node. Diverge only where PCG genuinely differs - a points-driven variant of an
operation is a worthwhile *addition* alongside the Fracture Mode equivalent, not a replacement for it.

Every noise-capable operation must route its noise through `FPCGFractureNoiseSettings::ApplyTo` rather than
filling an `FNoiseSettings` by hand; see the trap documented below.

### GC selection is set-valued, not a per-element predicate

This deliberately differs from `PCGUtilsDynMesh`, whose selection operations implement `TestElement(int32)`.
That shape suits vertex/edge/face predicates but is wrong for bones: the useful bone selectors
(`FCollectionTransformSelectionFacade::SelectContact`, `SelectLevel`, `SelectSiblings`, `SelectByPercentage`)
compute over the whole hierarchy at once. `Evaluate()` therefore returns a whole `FDataflowTransformSelection`.

Before writing a new selector, check `FCollectionTransformSelectionFacade` (in **Chaos**) - it already ships
bounds, sphere, plane-side, volume, size, contact and hierarchy selection, plus cross-domain conversion. Most
future selectors are ~30-line wrappers over it, not new algorithms.

---

## Prefer Epic's backend over reimplementation

| Task | Call |
|---|---|
| Fracture | `FFractureEngineFracturing::VoronoiFracture` / `PlaneCutter` / `SliceCutter` / `BrickCutter` |
| Prune | `FFractureEngineEdit::DeleteBranch` |
| Bone selection | `GeometryCollection::Facades::FCollectionTransformSelectionFacade` |
| DynMesh <-> GC | `UE::Geometry::FGeometryCollectionToDynamicMeshes` |
| Site sampling | `FFractureEngineSampling` |

Three behaviours worth knowing because they are easy to fight:

- **`VoronoiFracture` always sets `FInternalSurfaceMaterials::NoiseSettings`**, and PlanarCut gates its
  expensive meshing path on `NoiseSettings.IsSet()` rather than on the amplitude. So the cheap
  `CreateMeshesForBoundedPlanesWithoutNoise` path is unreachable through that entry point, and every cut face
  is remeshed to `PointSpacing` (default 1cm) even at zero amplitude - 553k triangles for a fractured 100cm
  box, versus ~1k. `UPCGVoronoiFractureFactoryData` works around it by passing a no-subdivision spacing when
  `bAddSurfaceNoise` is off, since `PointSpacing` is only a target edge length for
  `RemeshForNoise(SplitsOnly)`. Any future cutter factory built on `FFractureEngineFracturing` must do the
  same. `PCGUtils.Fracture.RoundTrip.CarvesCavity` guards this with a triangle-count bound.

- **`DeleteBranch` never deletes a root bone** (`RemoveRootNodes`). This is why `DynMesh To GC` always adds an
  explicit cluster root above the geometry bone - without it, a single-bone collection is silently unprunable.
- **Fracture entry points narrow a selection to leaves internally** (`ConvertToLeafSelection`). So the default
  fracture target is `SelectAll()`, not a hand-rolled leaf set. Do not duplicate that hierarchy logic.

---

## PolyGroup layers are the bridge back to DynMesh

`GC To DynMesh` writes two named PolyGroup layers, and both matter:

- `GC_Bone` - source bone index per triangle, written by us during the append (no engine option does this via
  `FGeometryCollectionToDynamicMeshes`). Keeps fracture-piece identity.
- `GeometryCollectionInternalFaces` - the engine's interior/exterior tagging, kept by leaving
  `bInternalFaceTagsAsPolygroups` on.

`UPCGDynMeshPolygroupSelectionFactoryProviderSettings` resolves layers **by name**, so both are immediately
selectable with the existing DynMesh Select by PolyGroup node with no new code. "Select only the walls of the
cavity I just carved" works because of the second layer. Do not drop either to simplify the append loop.

Note `AppendWithOffsets` only carries attributes the destination already has - call `EnableMatchingAttributes`
first or the layers silently vanish.

---

## Node conventions

- Every element derives from `UPCGUtilsFractureElementBaseSettings`, which supplies `GetType()`. Deriving
  straight from `UPCGSettings` silently lands the node in the `Generic` palette bucket and nothing fails to
  compile.
- Settings are `PCG_Overridable` by default, matching the DynMesh module.
- Pin labels are `GC`, `Fracture`, `Selection`, `Points`, `DynMesh`, `Sites`. `Factory` never appears on a graph
  surface; use `GC` rather than `Geometry Collection` in titles, and add the spelled-out form as a title alias.
- All three fracture-domain data types share the domain colour `#2F7FA3`
  (`PCGUtilsFracture::DomainColorHex`). Colour identifies the domain; the icon identifies the semantic type. Do
  not invent per-type shades.

---

## Logging

Summary diagnostics, never per-bone spam. Executors log one `Log`-level line with before/after counts
(`PCGUtilsGCHelpers::DescribeCollection`); conversions log at `Verbose`. Anything that would silently produce
the wrong geometry - a stale selection, a prune that removed nothing - is a graph error or warning, not a log
line.
