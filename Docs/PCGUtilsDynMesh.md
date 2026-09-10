# PCGUtilsDynMesh

`PCGUtilsDynMesh` is the PCG-native DynMesh processing layer in PCGUtils. It is intended to be more than a set of
Geometry Script wrappers: it provides a consistent graph contract for mesh ownership, selections, reusable
selectors, domains, coordinate spaces, and operations that Geometry Script only exposes for complete meshes.

## Goals

1. Make DynMesh workflows feel native to PCG, including flexible typed pins, overrides, graph composition, and
   consistent transitions between PCG world space and DynMesh local space.
2. Expose Geometry Script's mesh-processing surface comprehensively through PCG.
3. Port useful procedural operations from Blender when no Geometry Script equivalent exists, following the
   source algorithm and applicable open-source license requirements.
4. Make practically every operation selection-aware unless applying it to only part of a mesh is semantically
   invalid.

## Unified process contract

`UPCGUtilsDynMeshProcessBaseSettings` is the settings contract for nodes that process or query an existing mesh.
Its primary input accepts either:

- `UPCGDynamicMeshData`, representing the whole DynMesh; or
- `UPCGDynamicMeshSelectionData`, representing a materialized selection tied to its source DynMesh.

It also exposes one optional `Selector` input. For a whole-mesh input, the selector materializes the effective
selection. For an incoming materialized selection, the selector is evaluated in the effective domain
and intersected with that selection.

`FPCGUtilsDynMeshProcessFunctions::ResolveInput()` is the common resolver for specialized/query executors.
`FPCGUtilsDynMeshProcessBaseElement` provides the default deep-copy-and-`ProcessMesh()` mutation executor.

### Required selections and domains

The process base exposes **Require Selection**. A derived operation can also enforce the requirement by
overriding `RequiresSelection()`. Either materialized Selection data or a connected Selector satisfies it.
A bare DynMesh without either produces a graph warning and is skipped. Builder requirements are checked when
the Builder is evaluated, because its child may supply an active selection. An explicit empty selection is valid
and means no work, not the whole mesh.

An operation with a native vertex, edge, or face domain overrides `GetRequiredSelectionDomain()`. Incoming
selections and selectors are converted centrally using Geometry Script-compatible inclusive conversion. Override
`AllowPartialSelectionDomainInclusion()` only when full incident-element inclusion is required.

## Geometry Script API shapes

| Geometry Script shape | PCGUtilsDynMesh execution |
|---|---|
| Whole-mesh and selection overloads | Dispatch to the appropriate overload using the resolved selection. |
| Optional selection/no-selection behavior | Pass the resolved selection and explicit no-selection policy. |
| Selection-only operation | Override `RequiresSelection()` and the required domain. |
| Whole-mesh-only operation | Operate through a Mesh Target handle and restore only the selected result domain. |

## Processes that produce topology

`UPCGUtilsDynMeshTopologyProcessBaseSettings` **inherits from the process base**. It adds a result-selection
contract; it does not implement a second mesh/Builder executor. Derived operations implement
`FPCGUtilsDynMeshTopologyOperation::Apply()` and report the result triangle IDs explicitly. The base then:

1. Replaces the active selection with those faces, bound to the processed mesh. This applies to immediate
   `Output Selection Data` and to the internal selection carried by a Builder, independent of pin presentation.
2. Optionally assigns the result faces to one fresh default PolyGroup with **Assign Result Polygroup**.
3. Optionally emits a reusable Selector on the **Result** pin, using the existing PolyGroup Selector and its
   domain adapter. **Output Result Selector** automatically enables group assignment. Both options default off.

The reusable Selector cannot capture a default numeric group ID: a deferred Builder has not allocated one yet,
and separate input meshes can allocate different IDs. The base therefore also records a named extended PolyGroup
layer, with `1` for result faces and `0` for other faces. **Result Polygroup Name** specifies this name; `None`
generates a name from the authoring node's object path, stable across PCG property overrides and saved-graph
reloads. A new or renamed/copied node has its own automatic name. Use an explicit name when referencing a region
from a separate **Select|By PolyGroup** node (set **Group Layer Name** to that name and **Group IDs** to `1`).

Reusing an explicit name replaces that region. Empty result regions clear the named layer and produce an empty
Selector result; there is no fallback to the highest group. The emitted Selector also selects nothing on
meshes that lack its named layer. One descriptor serves all outputs and all Builder seeds without owning a mesh.
The **Result** pin is always declared and keeps its Selector type when `Out` becomes a Builder pin, so
property overrides may enable it. It emits data only when enabled and a primary output was produced.

This is a mesh-attribute region, not an arbitrary topology-history system. Surviving membership follows the
engine's attribute propagation through subsequent edits. Deleting faces removes them; operations that discard
or regenerate PolyGroup layers can lose the region. Automatic names change if the node/asset path changes.
Named regions are independent of default group IDs, so assigning a later default group does not erase an earlier
named region. Each retained region adds one extended integer PolyGroup layer.

### Extrude, Inset and Bevel

- **DynMesh|Extrude Faces** uses Geometry Script's underlying linear-extrude operation and options. Its result
  defaults to the extruded cap faces. Choose side faces or cap and border faces when needed.
- **DynMesh|Inset Faces** uses the corresponding inset/outset operation, with inner faces as its default result.
  Border-only and combined results are also available. Negative distance outsets. UV Scale applies to the border.
- **DynMesh|Bevel Edges** now reports the newly created bevel faces through the same base, replacing invalidated edge IDs.
  Its existing bare-mesh behavior (bevel every edge) is retained. It now supports result Selection data and a
  reusable Selector on its **Result** pin.

Extrude and Inset enable **Require Selection** by default; Bevel leaves it off for compatibility. All three accept
DynMesh, Selection and Builder inputs and the optional Selector. The shared resolver converts input selection
domains and intersects incoming Selection data with an explicit Selector. Empty effective selections are no-ops.
Geometry Script group options remain available on Extrude/Inset; shared result grouping takes precedence for the
chosen result faces when enabled. Directions and distances use the mesh's coordinate space.

To author **Select Up Face → Extrude → Inset → Extrude** with concrete meshes:

1. Connect an upward normal Selector to the first Extrude's `Selector` input, and the mesh to `In`.
2. Enable **Output Result Selector** on the first Extrude and Inset. Keep `Out` as DynMesh if downstream nodes
   require full mesh data.
3. Connect each node's `Out` to the next node's `In`, and its `Result` pin to the next node's `Selector`.

Alternatively, enable **Output Selection Data** and chain those results directly. In Builder mode the same
operation settings and Selector wiring work, while `Out` remains a Builder. The Builder also carries the result
selection internally, so the next operation can use it without an additional Selector wire. A supplied Selector
still intersects that active selection, following the normal process contract.

## Whole-mesh processes with an operand

`UPCGUtilsDynMeshOperandProcessBaseSettings` extends the process base for two-input whole-mesh operations.
Its `InA`, optional `InB`, and `Out` pins share a single type: all DynMesh or all DynMesh Builder. Connecting
either input or the output constrains all three pins, including connected operand-process nodes. Mixed data
types are rejected at execution as well. This is an explicit selection exception: these solid operations do not
support partial mesh application, so there is no Selection input, Selector pin, or selection setting/override.

The base owns pairing, tag inheritance, validation, mesh duplication, and deferred Builder composition. Derived
nodes implement `CreateProcessOperation()` and read `Invocation.OperandMeshData`; operations never capture
the authoring context. Both Builder children evaluate against the same seed, with the primary Builder frame
retained. Active selections in child Builders do not scope the operation and are cleared after processing.

### DynMesh Boolean

**DynMesh Boolean** mirrors the engine Boolean Operation, including Geometry Script operations/options,
pairwise N:N/N:1/1:N broadcasting, sequential operands, Cartesian pairing, and A/B/Both tag inheritance.
Both meshes must already be in the same coordinate space (normally target-actor-local).

- When `InB` supplies **no data**, every primary is passed through by reference with its original tags, including
  in B-only tag mode. No geometry work or Builder wrapper is created. This applies to every operation and pairing
  mode, so optional subtraction cutters do not require a graph branch.
- A **present empty mesh** still runs the selected Geometry Script operation. `Allow Empty Result` retains the
  engine default of false; enable it if a boolean should be allowed to erase the primary mesh. Invalid data or a
  failed Builder evaluation is an error/failure, not a missing operand to silently ignore.
- **Assign Operand Polygroup** (off by default, Union and Subtract only) assigns surviving operand faces to one
  fresh default-layer PolyGroup per operand application. For subtraction this identifies operand-derived cut
  faces. Polygroups label triangles, not individual vertices; downstream selections can convert domains. Existing
  primary groups are retained. No group is created if no operand faces survive. Automatic hole-repair faces use
  Geometry Script's normal repair behavior and are not guaranteed to be part of the operand group.
- **Self Union Operand** (off by default) applies Geometry Script self-union to each operand before the boolean,
  with overrideable self-union options. Group assignment runs afterward, including any self-union repair faces.
  Both conveniences prepare a private operand copy and never change shared upstream data.
- **Separate Operand Contributions** (off by default) is a structural switch: it replaces the single `Out` pin
  with `Out A` and `Out B`, and the node stops accepting Builder inputs (it reproduces the boolean directly, so
  it cannot defer). It is not a runtime override. One boolean still runs per pairing; its result is then
  partitioned by which operand each triangle came from. `Out A` is the InA-derived surface (for Subtract, the
  surviving InA shell); `Out B` is the InB-derived surface, including generated cut and cavity walls. Provenance
  is read from the boolean itself (`FMeshBoolean::TrackPerTriangleSourceMesh`), never from PolyGroups, so
  pre-existing groups on either operand have no effect on the split. Post-boolean hole-fill triangles are
  attributed to whichever operand owns the majority of their boundary loop's rim (ties go to InA); no result
  triangle is dropped. Every final triangle lands on exactly one of `Out A` / `Out B`. Single-mesh operations
  (`Trim*`, `New PolyGroup*`) and a missing InB put the whole result on `Out A` and leave `Out B` empty; an
  empty result with **Allow Empty Result** disabled passes InA through on `Out A`. Sequential folds report the
  partition for the last operand only. **Assign Operand Polygroup** is hidden in this mode — the split supersedes
  it. This mode runs immediately on concrete DynMesh data; the auxiliary `Out B` result travels the generic
  `FPCGUtilsDynMeshProcessOutcome::AuxiliaryMeshOutputs` channel, so it is a reusable secondary-output mechanism
  rather than a Boolean-specific one.

Material handling follows the vanilla element: primary material slots are retained, and this node does not merge
or remap separate operand material-slot tables. Operand material IDs should already use the primary's slot layout.

## Mesh Target handles

`FPCGUtilsMeshTargetHandle` separates selection resolution from working-mesh preparation and restoration.
Handle-based elements still derive from the process settings base and pass their settings to
`FPCGUtilsMeshTargetFunctions::CreateTarget(..., Settings)`, ensuring that Selector inputs are honored.

## PolyGroup attribute processes

Both derive from `UPCGUtilsDynMeshProcessBaseSettings`, override `GetRequiredSelectionDomain()` to `Face`
(the base converts any incoming selection to triangles), are topology-preserving, and support deferred Builder
processing.

- **Set DynMesh PolyGroup** — assigns one `Polygroup ID` (or a freshly allocated one) to the selected
  triangles via Geometry Script `SetPolygroupForMeshSelection`. With no selection the whole mesh is used
  (`CreateSelectAllMeshSelection`). The default PolyGroup layer is enabled if absent; an extended layer at
  the configured index is grown into existence.
- **Clear DynMesh PolyGroups** — resets the selected triangles' PolyGroup IDs to `Clear Value`. Geometry
  Script `ClearPolygroups` has no selection option and clears a whole layer, so this node extracts the
  selection into a region (`FPCGUtilsMeshTargetFunctions::CreateTargetInPlace(Invocation, Region)`), clears
  it, and welds it back with `RestoreRegion` — unselected triangles keep their groups. With no selection the
  whole layer is cleared.

## Shared geometry utilities

`Geometry/PCGUtilsDynMeshSurfaceCorrespondence.h` is a standalone GeometryCore-only helper (no PCG element,
Painter, or Static Mesh Component dependency) for closest-point surface correspondence between two Dynamic
Meshes: `ProjectPoints` / `ProjectMeshVertices` return, per destination sample, the nearest source triangle,
its barycentric coordinates, and the squared distance, and `SampleColorOverlay` / `TransferColorChannels`
resample a source colour overlay through that correspondence. The source AABB tree defines the reference
space; `FProjectionOptions::DestinationToSource` (default identity) maps destination points into it. Used by
the Painter Static Mesh target for LOD0 → lower-LOD colour transfer; intended for reuse by any future
attribute-transfer system.

## Surface pathing

Two conversion nodes bridge PCG paths/points and DynMesh surface topology using Unreal's geodesic solvers. Both
derive from `UPCGUtilsDynMeshProcessBaseSettings`, override `GetRequiredSelectionDomain()` to `Face`, and accept
a Selection or Selector that restricts pathing to the selected triangle region (extracted as a submesh). Both
are read-only with respect to the mesh and emit ordered `UPCGPointArrayData` paths on a `Paths` pin, matching
what **DynMesh Selection To Paths** produces — same closed-loop `@Data` Bool contract, first point never repeated
at the end — with one addition: points carry a real frame (X = path tangent, Z = surface normal, the Sample
DynMesh convention), because a surface path is normally fed straight into spline conversion or mesh spawning.

- **Route Path On DynMesh** takes a sparse guide path on its `Path` pin — first, ahead of `Mesh`, because the
  output is that path mutated onto the surface — projects every guide point onto the mesh once, and joins each
  consecutive pair with the shortest surface path, concatenated into one path. It routes the closing segment too
  when the input reports itself closed through **Is Closed Attribute Name**. **Max Projection Distance**
  (`0` = unlimited) bounds how far a guide point may float off the geometry.

  The routed path keeps the guide path's identity: it is initialized from the guide path, so its attributes in
  every metadata domain (`@Data` included), its tags and its target actor survive. Routing inserts a point at
  every triangle crossing, so most output points fall between two guide points; each one records which pair
  brackets it and how far along by arc length. **Metadata Inheritance** decides what those in-between points get
  — `Interpolate` (default) weights attributes that allow interpolation between the bracketing pair and takes
  everything else from the nearer guide point, `Nearest Source Point` copies wholesale from the nearer one, and
  `None` emits a standalone path that inherits nothing. Transform, Steepness and Seed are always the node's own
  regardless of mode.
- **Trace Surface Path** takes seed points on its `Seeds` pin and traces a straight surface path from each, in
  the seed's own direction, until **Max Path Length** or a mesh/selection boundary. Direction comes from a
  transform axis by default, or from a Vector attribute selector. Each seed produces its own output path;
  optional `@Data` attributes record the seed index and whether the trace ended on a boundary.

  A traced path belongs to the seed that produced it, so **Inherit Seed Attributes** (on by default) initializes
  it from the seed data — the dataset's attributes in every metadata domain, its tags and its target actor — and
  gives every traced point that one seed point's own attributes, along with its Density, Color and Bounds. Those
  per-point attributes stay on the **element domain** rather than being hoisted to `@Data`: `@Data` already holds
  the seed dataset's own attributes plus Seed Index and Reached Boundary, so hoisting would collide by name.
  Nothing is interpolated — every point of one path shares a single seed — and Transform, Steepness and Seed are
  the node's own, as with Route Path On DynMesh.

Neither node competes with PCGEx's cluster pathfinding: there are no heuristics, costs, obstacles or flood
fills. The value is solving directly on the real triangle surface with no cluster conversion step.

`Geometry/PCGUtilsDynMeshSurfacePathing.h` holds the geometry half — a standalone GeometryCore/DynamicMesh-only
helper with no PCG dependency, like `PCGUtilsDynMeshSurfaceCorrespondence.h`, which it uses for projection.
`FSurfaceRouter` runs the same algorithm as `GetShortestSurfacePath()` (Dijkstra seed path, then
`FDeformableEdgePath::Minimize`) but makes **one** working mesh copy per guide path instead of one per segment,
by poking every anchor into that copy up front; the Geometry Script wrapper deep-copies the whole mesh on every
call. `TraceSurfacePath()` drives `FMeshGeodesicSurfaceTracer` against a const mesh and copies nothing.

## Selection modifiers

### PolyGroup Selector

**Select by PolyGroup** emits a reusable `Selector`. Connect it to **Build DynMesh Selection** or any process
node's optional `Selector` input, including processes in deferred Builder chains.

- **Group IDs** selects the union of one or more IDs (default `0`). Duplicate or unknown IDs have no additional
  effect; an empty list selects nothing. **Invert Selection** complements the face region before domain conversion.
- **Group Layer** chooses default triangle groups (including those assigned by **DynMesh Boolean**) or an extended
  PolyGroup layer by index. Missing layers report an error; evaluation never creates or changes mesh groups.
- **Group Layer Name**, when set, resolves an extended layer by name instead of using the index/default-layer
  setting. This can retrieve a topology process's named result region using group ID `1`.
- **Highest Group ID** selects the largest ID currently used by triangles in the chosen layer, evaluated separately
  for each mesh. This can isolate a recently added boolean operand group, but it does not track provenance: if the
  boolean passed through or produced no operand faces, it will select the highest remaining primary group instead.
  Hole repair or subsequent edits can also create higher groups. Use explicit IDs when that distinction matters.
- Face membership converts automatically to vertex or edge selections. **Allow Partial Inclusion** defaults to
  true (any incident selected face); disable it to require all incident faces. Existing incoming selections are
  intersected by the standard process resolver.

### Selection operations

Selection-transforming nodes derive from `UPCGUtilsDynMeshSelectionOperationSettings`. Their `Operation Mode`
switch provides two graph forms from the same element:

- `Selection` consumes and emits materialized DynMesh Selection data.
- `Selector` decorates an upstream Selector and emits another reusable Selector.

Expand Selection, Contract Selection, Select Connected, and Select Boundary follow this contract. Older
standalone/provider duplicates remain loadable only for graph compatibility and are deprecated.

Selection Logic groups evaluate their child Selectors from highest to lowest **Priority**, preserving connection
order between equal priorities. AND stops testing an element after the first child excludes it; OR stops after the
first child includes it. Put cheap or highly selective predicates at a higher priority to avoid lower-priority work.
NOT has exactly one child, so priority only matters when that NOT group is nested inside another group.

### Preparation and restoration

- `Region` extracts the selected triangle region. Use it when the operation may change topology and can safely be
  welded back. Finalize with `RestoreRegion()`.
- `FullMeshCopy` preserves source vertex IDs. Use it for whole-mesh-only deformations or selection-aware Geometry
  Script calls. Finalize deformations with `RestoreVertexPositions()`.
- A selection-aware operation that directly and completely edits the full copy may need no compositor, but this
  is only valid when its own Geometry Script selection scopes every changed result domain.

Attribute-only whole-mesh operations may eventually need dedicated compositors for colors, UVs, normals,
materials, polygroups, or weight maps. Do not misuse vertex-position restoration for non-position results.

## Coordinate spaces

PCG points, splines, and bounds are normally authored in world space. DynMesh geometry in this module is normally
target-actor-local. Nodes that combine these representations use `PCGUtilsDynMeshSpaceHelpers` or
`PCGUtilsSplineHelpers`; conversion should not be reimplemented per node.

Unreal's `UPCGDynamicMeshData` currently carries geometry and materials but no coordinate-space provenance.
Therefore automatic conversion is reliable only where the node knows the provenance of the external PCG data and
the module's local-space convention. The explicit To World/To Local nodes remain necessary for arbitrary inputs.

## Naming

PCGUtils-owned terminology uses `DynMesh` to keep node names compact and distinguish them from vanilla Unreal
Dynamic Mesh nodes. Official Unreal API types retain their engine names, including `UDynamicMesh`,
`FDynamicMesh3`, and `UPCGDynamicMeshData`.

Existing reflected element classes are not renamed merely for terminology, because that can invalidate serialized
references. Their node names, titles, data display names, and other user-facing text should use `DynMesh` now;
future C++ renames require Core Redirects.

## Painter fields

The Painter feature family — Axis Gradient Painter, Paint from Points, Painter Math, Combine Painters, Points to
Painter, and the `Paint DynMesh Vertex Color` consumer — moved into its own module, **`PCGUtilsPainter`**, which
depends on `PCGUtilsDynMesh`. See `Docs/PCGUtilsPainter.md`.

`Paint DynMesh Vertex Color` and `Points to Painter` remain DynMesh-specific and still derive from
`UPCGUtilsDynMeshProcessBaseSettings`; they simply live in the Painter module now. Class, struct and enum names
are unchanged — Core Redirects in `Config/DefaultPCGUtils.ini` keep existing graphs loading.

## Extension checklist

When adding an operation:

1. Classify its Geometry Script selection shape and result domains.
2. Derive from `UPCGUtilsDynMeshProcessBaseSettings` or a more specific selection-authoring/filter base.
3. Override `RequiresSelection()` and `GetRequiredSelectionDomain()` where applicable.
4. Use the default process executor when direct mutation is sufficient; otherwise use the shared resolver or Mesh
   Target handle and pass the settings object.
5. Choose a restoration strategy for every result domain the operation modifies.
6. Use shared coordinate-space helpers for world/local interaction.
7. Confirm the graph exposes a DynMesh-or-Selection input and a functional optional Selector pin, unless
   the exception is documented.
8. Use `DynMesh` in PCGUtils-owned display names and make normal settings PCG-overrideable.
9. Build the module and visually inspect the node's pins and overrides.

## Recommended base-class evolution

The current base can be improved further:

- Replace the Boolean `RequiresSelection()` with an explicit policy enum: `Optional`, `Required`, or `Unsupported`.
  `Unsupported` would document rare whole-object semantic operations such as coordinate-space conversion.
- Separate selection input/selector policy from mutation-output policy so query/conversion nodes do not inherit an
  irrelevant `bOutputSelectionData` setting.
- Add a result-domain declaration (topology, positions, normals, UVs, colors, materials, and so on) so the target
  layer can validate or select the correct compositor automatically.
- Add a space policy once PCGUtils can represent coordinate-space provenance on DynMesh data. The process base
  could then normalize inputs and outputs automatically instead of relying on per-node switches.
- Cache one resolved selector per execution and reuse it for every mesh input rather than parsing the selector pin
  for each input.
- Add automation tests covering whole mesh, materialized selection, selector selection, intersection, empty
  selections, required-selection failures, domain conversion, and each restoration strategy.
