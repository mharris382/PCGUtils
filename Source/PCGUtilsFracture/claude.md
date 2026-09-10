# PCGUtilsFracture Development Conventions

Module-specific conventions for `PCGUtilsFracture`. Repository-wide rules are in `AGENTS.md`.

## Editor-gated Dataflow exception

`GC | Dataflow Processor` is the explicit exception to the direct-backend-only convention below. Its settings,
element, and DataflowCore bridge structs are runtime-loadable; DataflowEngine execution is gated with
`WITH_EDITOR` and its asset reference with `WITH_EDITORONLY_DATA`. Outside the editor it errors and emits no
output. It uses a private transient UGeometryCollection solely as the engine's variable-override owner; no
package, saved asset, actor, component, or terminal write is performed. Do not extend this into a runtime graph host.

The processor deliberately excludes selections, accepts named GC/point inputs, and publishes every GC output
as a new lineage with regenerated bone identities. All batches use fresh contexts and N:N/N:1 cardinalities;
PCG evaluation caching is disabled. Dataflow node and variable edits invalidate loaded settings. The plugin's
GeometryCollectionPlugin dependency is editor-target-only; individual Dataflow node wrappers are not a new
implementation layer for ordinary fracture elements. See `Docs/PCGUtilsDataflowProcessor.md` for the interface.

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

## Vocabulary: bone, piece, cluster

A collection's Transform group is a tree of **bones**. Not every bone carries usable geometry, and conflating
the two is the single easiest way to write a subtly wrong node here.

| Term | Test | Meaning |
|---|---|---|
| **Bone** / **Transform** | `0 <= i < NumTransforms` | an element of the Transform group; what a GC Selection addresses |
| **Piece** | `IsPiece` = rigid **and** has geometry | a fracture piece: what renders, converts, prunes and gets cut |
| **Cluster** | `IsCluster` | structural; its shape is the union of the pieces beneath it |
| **Root** | `IsRoot` | no parent; `DynMesh To GC` always adds one |

`PCGUtilsGeometryCollectionHierarchy` owns all of these, plus `GetLevel`, `GatherPieces`, `GatherPiecesUnder`,
`GetAncestors`, `GetDescendants` and `LowestCommonAncestor`. Use it rather than reading `Parent`,
`SimulationType` or `TransformToGeometryIndex` directly.

**"Has geometry" is not "is a piece".** Unreal's cutters do not delete the shape they replace: they mark every
one of its faces invisible and leave it on the bone, which is now a cluster (`CutMultipleWithPlanarCells`,
`bRemoveOldGeometry = false`). Epic's own converter skips it - "cluster geometry is typically just there for
legacy reasons" - and so must we. The publisher removes it by default; `Fracture GC`'s
**Keep Hidden Source Geometry** opts back in.

User-facing text keeps saying **bone** (Fracture Mode does, and every attribute is named `GC_BoneIndex`), but
should say **piece** wherever it means one.

---

## The immutability contract

`UPCGGeometryCollectionData` holds `TSharedPtr<const FGeometryCollection>`. There is no API that mutates one.

A node that changes a collection must:

1. `CreateMutableCopy()` to get a private `TSharedRef<FGeometryCollection>`,
2. mutate the copy,
3. publish it through `PCGUtilsGeometryCollectionRevisionPublisher::PublishRevision(...)` - or
   `PublishNewLineage(...)` when authoring one.

Never `const_cast` a collection, and never mutate an input to add a derived attribute.

**Publish, do not call `InitializeAsRevisionOf` directly.** The publisher is where a collection acquires the
guarantees the rest of the module relies on, and adding a fourth place that half-normalises is how those
guarantees rot. A published collection always has:

- a `Level` attribute consistent with its hierarchy (**not** part of the collection schema - see below),
- a `PCGUtils_BoneId` per bone,
- material sections consistent with its face `MaterialID`s,
- geometry bounds consistent with its vertices,
- no stale `Proximity`,
- no hidden cluster geometry, unless the caller asked to keep it.

Every mutating operation says what it changed through `FPCGUtilsGeometryCollectionMutationResult`, which is
what the publisher uses to decide which of those steps to run - and what the future geometry-view cache will
use to decide what it may keep. Under-reporting is a correctness bug, not a performance one; when in doubt use
`Everything()`. Fracture factories report through the `OutMutation` parameter of `Fracture()`; `Fracture GC`
accumulates them with `Accumulate()`.

### `Level` is optional, and that is why the publisher exists

`FTransformCollection::Construct` registers `Transform`, `BoneName`, `BoneColor`, `Parent` and `Children` -
**not** `Level`. But `FCollectionTransformSelectionFacade::SelectLevel`, `GetBonesByLevel`,
`SelectContact` and `FGeometryCollectionProximityUtility::EnumerateNeighbors` all need it, and silently return
nothing or `ensure` without it. The publisher materialises it via
`FGeometryCollectionClusteringUtility::UpdateHierarchyLevelOfChildren(&Collection, INDEX_NONE)` whenever the
hierarchy changed or the attribute is missing.

### Identity: BoneId answers a different question from StateId

`StateId` asks *are these bone indices still valid* and correctly rejects everything after any change.
`PCGUtils_BoneId` (a non-persistent Transform-group `FGuid`) asks *is this the same bone as before* and
survives reindexing, because a managed-array attribute travels with its element through `RemoveElements` and
`ReorderElements`. Existing ids are never reassigned. Keep using `StateId` for selection validity; `BoneId` is
for following a bone across revisions.

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

Every fracture authoring node derives from `UPCGUtilsFractureProviderSettings`, which owns two things no
individual operation should re-implement:

- **`Priority`.** The executor's Fracture pin is multi-connection and runs its operations in priority order, so
  an operation that cannot state its priority can only be sequenced by wiring order.
- **The `Result` pin.** A second output carrying a Selection of the bones that operation created, so a graph can
  fracture and then keep working on the fragments. It is a *deferred* selector, exactly like Extrude's Result
  Selector: the bones do not exist when the node runs, so the data carries the name of a tag rather than a set
  of indices. `UPCGUtilsFractureProviderSettings::CreateFactory` is final in spirit - override
  `CreateFractureFactory` instead, or the shared plumbing is skipped.

How "the bones this operation created" is identified generically, with no cooperation from the operation: bone
ids are minted by the *publisher*, after fracture, so the executor mints ids for everything that exists
immediately before each operation (`PCGUtilsFractureResultTagging::PrepareForOperation`) and afterwards any bone
still lacking one was created by that operation. That survives the reindexing the cutters do, which an index-range
comparison would not. Minting early is safe because `EnsureBoneIds` never reassigns an existing id.

The tag is a Transform-group `int32` attribute (1 = created by this operation), named per authoring node and
exposed as a setting like every other attribute this module writes. Tagging happens only when the Result pin is
enabled - an operation whose result nobody reads must not alter the data it produces.

Executors (`Fracture GC`, `Prune GC`) combine them. A new fracture type is a new factory, never an edit to the
executor: `Fracture GC` contains no Voronoi-, plane- or cutter-specific code and must stay that way.
`Uniform Voronoi Fracture`, `Voronoi Fracture From Points` and then `Planar`, `Slice` and `Brick` were all
added this way and required no executor change - that is the architecture working as intended, and the bar any
future cutter should clear. The three planar cutters additionally share `FPCGPlanarFractureCommonSettings`,
since every `FFractureEngineFracturing` plane-based entry point takes the same trailing block of fracture,
island-split and noise arguments.

Two engine behaviours the planar cutters do *not* share with Voronoi, both found by assertion rather than by
reading:

- **`PlaneCutter` appends.** It seeds its plane list with the `InCutPlaneTransforms` you supply and *then*
  generates `InNumPlanes` more, so a node offering both must pass zero for the generated count or it silently
  adds random cuts to a deliberate pattern.
- **Plane, Slice and Brick gate noise on `InAmplitude > 0`** and leave `FInternalSurfaceMaterials::NoiseSettings`
  unset otherwise, so they reach PlanarCut's cheap meshing path on their own. The suppressing point spacing
  `FPCGFractureNoiseSettings::ApplyTo` returns is belt-and-braces for these three and load-bearing only for the
  Voronoi path.
- **Slice counts are cutting planes, not divisions.** `GenerateSliceTransforms` steps the extent by
  `(Slices + 1)`, so N planes give N+1 divisions per axis and 0 leaves an axis uncut.

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

### Base selectors decide, decorators move

Two kinds of selector, and keeping them apart is what stops the selector layer sprawling:

- A **base selector** answers "which bones match", from the collection alone. `GC Select Bones` (All, None,
  Root, Pieces, Clusters, At Level), `Select Bones From Points` and `GC | Select | By Mesh Predicate` are the
  current ones; any future geometric predicate - bounds, sphere, volume - is the same kind and evaluates
  **pieces**. Note that `By Mesh Predicate` is the general case of all of them: it runs any *DynMesh*
  selector over each piece's surface, which is why this module ships no bounds, normal, colour or occlusion
  selector of its own. Before writing a geometric bone selector, check whether an existing DynMesh selector
  plus that adapter already expresses it.
- A **decorator** takes a selection and returns another one, derived from
  `UPCGUtilsGeometryCollectionSelectionDecoratorFactoryData`. `GC Selection Hierarchy` (Parent, Children,
  Siblings, Ancestors, Descendants, To Pieces, To Clusters, Same Level, To Level, Invert) and `Select Contact`.
  A decorator implements only `TransformSelection`, rewriting a bone array; the base handles resolving the
  children, validating indices and applying `bIncludeOriginal`.

**Do not give a geometric selector a bone-depth setting.** That was considered and rejected during the
architecture investigation: it puts hierarchy handling into every predicate, and it still cannot express the
distinction that actually matters - "clusters where *any* piece matched" versus "clusters where *every* piece
matched" - which `Select Parent` does with one enum. A predicate tests pieces; a decorator decides what to do
with the answer.

Several selectors on one Selection pin mean their **union**, everywhere, and `EvaluateAndUnion` is the single
implementation of that. Anything else - intersection, difference, exclusive or - is the `GC Selection Logic`
node.

New operations are preconfigured entries on an existing settings class rather than new classes, so one node
family covers a whole Fracture Mode button group. Preconfigured indices are serialized into saved graphs;
append, never reorder.

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

## Crossing to DynMesh: one canonical view, many presentations

Going from a Geometry Collection to a Dynamic Mesh is a normal capability of this module, not something each
node reimplements. Two layers:

**The canonical view** (`FPCGUtilsGeometryCollectionPieceMeshView`) is one piece as an `FDynamicMesh3`, built
by `PCGUtilsGeometryCollectionPieceMesh::BuildPieceMeshView` and cached on the collection data. It is
deliberately raw:

- **Bone-local.** A bone's transform belongs to the collection state, not to its geometry, so a node that only
  moves bones invalidates nothing.
- **Unwelded, uncompacted, every face including hidden ones.** Which is what makes provenance arithmetic:
  `GC face == TriangleID + FaceStart` and `GC vertex == VertexID + VertexStart`. Non-manifold input is the one
  exception and records its splits in `DuplicatedVertexSource`.
- **Classified.** Interior/exterior and visibility travel as PolyGroup layers under the engine's own names and
  its `1 + flag` encoding, so anything that already reads a converted collection keeps working.

Do **not** use `UE::Geometry::FGeometryCollectionToDynamicMeshes` for anything a selector will read. It is a
presentation conversion: it bakes the global transform in, welds, drops isolated vertices, compacts, skips
invisible faces, and copies the vertex normals into the tangent overlay (an engine bug). Every one of those
destroys the correspondence. It remains the right call for *authoring* a collection - `DynMesh To GC` still
uses `AppendMeshToCollection`.

**Presentation** (`PCGUtilsGeometryCollectionMeshPresentation`) is everything a user would recognise as a
setting on a conversion node - skip hidden faces, weld, keep isolated vertices, bake a transform, combine
pieces, write the `GC_Bone` layer - applied on the way out. It is never stored, so the cache holds one form
rather than one per combination of settings. Presentation renumbers everything, so read provenance from the
view *before* presenting.

### Weld before evaluating a vertex or edge predicate

The canonical view is unwelded, and where an original surface meets a fracture cut there is a hard normal
seam - so the collection stores those corners as *separate vertices*. On the raw view no vertex and no edge is
ever shared between the two surfaces, which means:

- vertex and edge counts are inflated by the split corners, so "every vertex passes" is a different question
  than a user thinks they are asking, and
- the exterior-biased boundary rule in `PCGUtilsGeometryCollectionSurface` has nothing to decide.

Both only become meaningful after welding. Anything evaluating a **vertex- or edge-domain** predicate must
therefore run against a welded presentation, not the cached view. A **face-domain** predicate is fine either
way - triangles are 1:1 with collection faces before welding, which is also the only form in which provenance
is exact. `PCGUtils.Fracture.PieceMesh.SurfaceClassification` pins down both halves of this.

### The cache is safe because the data is immutable

`UPCGGeometryCollectionData::GetPieceMeshCache()` can only grow within a state, so there is no invalidation to
get wrong and no way for one consumer to affect another. `DuplicateData` shares it.

Crossing revisions is the only judgement call, and `PublishRevision` owns it: views are carried over
**only when the mutation reports no geometry change and no structural change**, re-keyed by `BoneId`. Note that
"everything below the first new bone survived a fracture" is *false* - the cutters hide the faces of the bone
they cut, and normalisation then removes that geometry entirely. A future cutter that reports
`DirtyGeometryIndices` could let more through; until then, conservative is correct.

### `GC To DynMesh` is a consumer, not a converter

It gathers pieces, asks the cache for each view, presents, and either combines or emits one data per piece. It
contains no conversion code of its own, and the next node that needs meshes out of a collection should be
written the same way. Two things it owns that the layers below deliberately do not:

- **Which PolyGroup layers survive.** The view always carries both; the node removes the internal-face layer
  when `Tag Internal Faces` is off, and the visibility layer whenever hidden faces were excluded (where it
  would say nothing anyway).
- **Per-piece identity**, written on each output's *data domain*: `GC_BoneIndex` plus the source id/revision/
  state trio, unconditionally, for the same reason `GC Bones To Points` writes them - a selection cannot be
  resolved against the collection without them.

Surface attributes are measured with `GetBoneSurfaceInfo` on the collection, **not** on the emitted mesh, so
`GC_ExposureRatio` describes the piece and matches what `GC Bones To Points` reports for the same bone. Two
nodes disagreeing about a piece would be a trap; the test asserts they agree.

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

## Attributes

Follow the repository rule in `AGENTS.md`: every attribute exposes its name, and each optional one has its own
toggle with the name hidden behind it. `GC Bones To Points` is the reference implementation - its
`FAttributeWriters` resolves the whole set once, and a null pointer means "not requested" so the write sites
stay a flat list of guarded assignments.

Identity (`GC_BoneIndex` and the source id/revision/state trio) is the one thing written unconditionally,
because it is the contract with `Select Bones From Points` - a selection cannot be resolved without it. Its
names are still exposed so they can be matched against that node.

## Cluster interop

`GC Bones To Points` can emit the bone adjacency graph as a PCGEx-compatible cluster. `PCGUtilsFracture` must
**not** gain a dependency on PCGExtendedToolkit to do it - the whole contract is two int64 attributes and three
tags, reproduced in `PCGUtilsClusterInterop` (`Data/PCGGeometryCollectionClusterData.h`) with the source files
it was verified against listed in the comment.

Two things to preserve if you touch this:

- **The vertex id is the bone index, not the point index.** PCGEx's `BuildEndpointsLookup` builds a
  VtxId -> point-index map, so any unique id is legal, and using the bone index is what lets a selection
  survive a PCGEx round trip and still resolve through `Select Bones From Points`.
- **The declared degree must match the emitted edges.** PCGEx sizes its adjacency from the degree packed into
  the vtx attribute; a mismatch corrupts the cluster rather than failing loudly.

- **Both halves are plain `UPCGPointArrayData` on plain Point pins.** Do not introduce a data subtype for
  vtx or edges. A cluster is point data carrying tags and attributes, not a distinct type; subtyping narrows
  the pin, stops other point nodes accepting the output, and buys nothing. PCGEx declares its own cluster pins
  with `PCGEX_PIN_POINTS` on inputs *and* outputs for exactly this reason. The test asserts the output class is
  `UPCGPointArrayData` precisely so this cannot creep back in.

Adjacency uses `FGeometryCollectionProximityUtility::ComputePreciseProximity` - the static const overload,
because the instance methods cache a Proximity attribute onto the collection and ours are immutable. Note
`DeleteBranch` (Prune) strips that attribute anyway, so proximity is always recomputed rather than reused.

## Naming

C++ spells out `GeometryCollection`; user-facing text uses `GC`. So `UPCGGeometryCollectionBonesToPointsSettings`
in code, "GC | Bones To Points" in the palette, `GC` on pins, `GC_` on attributes. See `AGENTS.md` for why.

Every palette entry in this module is prefixed `GC | `, and every bone selection `GC | Select | `, with the name
itself carrying none of `GC`, `select`, `selection` or `selector` - `GC | Select | Contact`, not
`GC | Select | Select Contact`. The prefix is not decoration: PCG derives the palette category from
`EPCGSettingsType` alone, so with no `GeometryCollection` value in that enum the title prefix is the only
thing separating this module from the DynMesh nodes it shares the `Dynamic Mesh` bucket with. `AGENTS.md`
has the full rule and the search-text mechanics; `PCGUtils.Palette.SearchContract` enforces them.

## Node conventions

- Every element derives from `UPCGUtilsFractureElementBaseSettings`, which supplies `GetType()`. Deriving
  straight from `UPCGSettings` silently lands the node in the `Generic` palette bucket and nothing fails to
  compile.
- Settings are `PCG_Overridable` by default, matching the DynMesh module.
- Pin labels are `GC`, `Fracture`, `Selection`, `Points`, `DynMesh`, `Sites`. `Factory` never appears on a graph
  surface; use `GC` rather than `Geometry Collection` in titles, and put the spelled-out form in `Keywords`
  (not a title alias, which would add a duplicate palette entry).
- The two DynMesh bridges draw compact with the standard convert icon, so they carry no title text on the
  canvas at all. Their palette titles are `GC | From DynMesh` and `GC | To DynMesh`, and their keywords include
  `To`/`From` so that dragging off a pin and typing "to GC" finds them.
- All three fracture-domain data types share the domain colour `#2F7FA3`
  (`PCGUtilsFracture::DomainColorHex`). Colour identifies the domain; the icon identifies the semantic type. Do
  not invent per-type shades.

---

## Logging

Summary diagnostics, never per-bone spam. Executors log one `Log`-level line with before/after counts
(`PCGUtilsGCHelpers::DescribeCollection`); conversions log at `Verbose`. Anything that would silently produce
the wrong geometry - a stale selection, a prune that removed nothing - is a graph error or warning, not a log
line.
