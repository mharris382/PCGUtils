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

**DynMesh | Realize Builders** preserves the Seeds input's dataset-wide `@Data` metadata on every per-seed
output. `Single` and `Per Builder` outputs preserve it when all realized seeds come from one Seeds dataset; when
several datasets are combined there is no unambiguous `@Data` value to inherit. Seed element-domain attributes
are not copied onto DynMesh data because they describe source points rather than the realized mesh.

Which Builders apply to which seeds is a caller decision, not a setting. `Realize` takes an optional
`FSeedBuilderFilter`; with no filter every Builder is evaluated for every seed, which is what makes several
Builders on the pin compose one compound shape. A filter narrows that pairing, and outputs it empties - a seed
that selected no Builder, a Builder no seed selected - are dropped rather than emitted as empty meshes. The
sibling `PCGUtilsPCGExInterop` plugin uses this for `DynMeshEx | Realize Builders`, which drives the filter from
PCGEx match rules. PCGUtilsDynMesh does not depend on PCGEx.

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
from a separate **Select | By PolyGroup** node (set **Group Layer Name** to that name and **Group IDs** to `1`).

Reusing an explicit name replaces that region. Empty result regions clear the named layer and produce an empty
Selector result; there is no fallback to the highest group. The emitted Selector also selects nothing on
meshes that lack its named layer. One descriptor serves all outputs and all Builder seeds without owning a mesh.
The **Result** pin is always declared and keeps its Selector type when `Out` becomes a Builder pin, so
property overrides may enable it. It emits data only when enabled and a primary output was produced.

**Output pin type.** By default a process node's `Out` pin shows the type it will emit: purple Dynamic Mesh,
or DynMesh Selection when **Output Selection Data** is on, and a Builder pin when a Builder is wired in. This
applies even when nothing is connected and on nodes that cannot take Builders. Turn off **Show Concrete DynMesh
Output Pin Types** (Project Settings > Plugins > PCG Utils DynMesh) to get the old grey union pin back.

This is a mesh-attribute region, not an arbitrary topology-history system. Surviving membership follows the
engine's attribute propagation through subsequent edits. Deleting faces removes them; operations that discard
or regenerate PolyGroup layers can lose the region. Automatic names change if the node/asset path changes.
Named regions are independent of default group IDs, so assigning a later default group does not erase an earlier
named region. Each retained region adds one extended integer PolyGroup layer.

### Extrude, Inset and Bevel

- **DynMesh | Extrude Faces** uses Geometry Script's underlying linear-extrude operation and options. Its result
  defaults to the extruded cap faces. Choose side faces or cap and border faces when needed. `Distance` is an
  input shorthand: a constant, or one value per input read from `@Data` (a path's `PathHeight`, say). In `@Data`
  mode Builder inputs are refused. See `PCGUtilsInputShorthands.md`.
  `Direction Mode` is `Fixed Direction` (default), `Average Face Normal` (one area-weighted normal per
  extruded area), or `Per Face Normal`: each face moves Distance along its own normal while the area stays
  connected, using the engine's shell offset (`SelectionTriNormalsAngleWeightedAdjusted`), so a box's top and
  side go up and out by the same amount and stay parallel. `Max Corner Stretch` (advanced, default 4) caps how far
  a vertex at a sharp fold may be pushed to keep that; past it those faces end up nearer than Distance. For
  separate prisms instead of a connected shell, use `Average Face Normal` with `Area Mode` Per Triangle or Per
  Polygroup. Direction Mode used to be Geometry Script's two-value enum; nodes saved before
  `FPCGUtilsCustomVersion::ExtrudePerFaceNormal` are migrated in PostLoad, under the same override-pin label.
- **DynMesh | Inset Faces** uses the corresponding inset/outset operation, with inner faces as its default result.
  Border-only and combined results are also available. Negative distance outsets. UV Scale applies to the border.
  `Distance` is an input shorthand (default attribute `InsetDistance`); its old Geometry Script options struct is
  flattened into separate settings, as Extrude's was.
- **DynMesh | Extrude To Paths** separates the selected faces and joins them back with a strip, as the two nodes
  above do, but puts the separated rim on paths you supply instead of moving it along a direction or by a
  distance. Whether the result is an inset or an extrude depends only on where the paths are. `Matching` picks
  how rim and paths correspond:
  - **By Shape** (default) treats each path input as one closed outline: any point count, start, order or
    winding, and no attributes. Each rim loop of the selection (an area with a hole has two) takes the outline
    nearest it in shape (mean distance both ways), closest pairs first; `Max Match Distance` (0 = any) bounds
    that. Rim and outline are then walked round together: each rim vertex pairs with an outline corner, never
    going backwards, minimising total distance, with a cost of one average outline edge for two rim vertices
    sharing a corner. A few start corners are tried, because the corner nearest the rim's first vertex is not
    always its partner. The strip joins each rim edge to the run of corners between its ends, each corner
    fanning from the nearer rim vertex. Every outline corner becomes a vertex, so the result has exactly the
    outline's shape; this is what makes Clipper2 offsets and booleans usable as targets.
    `Inner Faces` decides what the selected faces become. **Keep Faces** keeps them: their rim vertices move onto
    the outline (vertices sharing a corner are spread along the outline beside it) and the other corners are
    split into the rim edges, so interior vertices, UVs and a shaped surface survive. **Refill From Path** cuts
    them out and fills the outlines flat (constrained Delaunay, even-odd so holes stay open); clean for a flat
    top, but there are no interior vertices. If the outlines cannot be triangulated (they cross), that area falls
    back to Keep Faces with a warning. Unmatched rim loops stay where they are; unused paths are logged.
    The geometry is in `Geometry/PCGUtilsDynMeshRimToPath.h` (no PCG dependency), covered by
    `PCGUtils.DynMesh.ExtrudeToPaths.ByShape.*`.
  - **By Vertex ID** is the original one-for-one mode: points are matched to rim vertices by vertex ID
    (`Vertex Index Attribute`), so the round trip is `DynMesh | Selection To Paths` with `Output Vertex Index`
    on, any path nodes, then this node. A rim vertex no point names stays in place; points that name no rim
    vertex are ignored. Both are logged with counts. Nodes saved before By Shape existed
    (`FPCGUtilsCustomVersion::ExtrudeToPathsByShape`) load in this mode.

  `Interior Vertices` (Keep Faces and By Vertex ID) chooses whether vertices inside the rim stay (inset) or follow
  the rim's average move (extrude).
- **DynMesh | Bevel Edges** now reports the newly created bevel faces through the same base, replacing invalidated edge IDs.
  Its existing bare-mesh behavior (bevel every edge) is retained. It now supports result Selection data and a
  reusable Selector on its **Result** pin. `Bevel Distance` is a non-negative input shorthand (default attribute
  `BevelDistance`); a negative `@Data` value is an error, not clamped.

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

### Mirror and Array modifiers

- **DynMesh | Mirror** mirrors across a plane expressed in Actor Local, World, DynMesh Local, or Builder Local
  space. It exposes Geometry Script's cut-side and seam-weld options. A selected triangle region is extracted,
  mirrored, and welded back; a Builder defers the same operation per seed.
- **DynMesh | Array** supports four placement modes. **Count** combines Blender-style Constant and Relative
  offsets; **Points** places copies at point transforms; **Spline** samples tangent-oriented frames along a PCG
  spline; and **Fit Bounds** fills one oriented point bound. **Fit Bounds Perfectly** retains the copy count that
  fits at the requested step, then expands that step so the last copy reaches the opposite usable bound exactly.
  Point, spline, and bound inputs are snapshotted by value, so deferred Builders do not retain PCG data objects.

Both operations preserve mesh overlays, PolyGroups, material IDs, weight layers, and the DynMesh material list.
Their topology changes invalidate an incoming active selection. Builder Local falls back to the DynMesh bounds
center for immediate DynMesh inputs, matching **DynMesh | Transform**.

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

## Materials by selection

**DynMesh | Set Material** assigns a material to a whole mesh, or to a selection of it (a Selection input or a
connected Selector).

With a selection, only the selected triangles change:

- They move to the slot that already holds the material. A slot is added, at the end of the list, only when no
  slot holds it. Running the node again with the same material never grows the list.
- Every other triangle keeps its slot and every existing material stays where it is.
- A mesh that arrives with no material list is given **Default Material** for the triangles outside the
  selection, not the selection's material.
- A mesh whose triangles use slots its material list does not have is not an error. The list is extended to
  cover them, with empty slots.

With no selection the whole mesh is set to the one material.

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

## Projecting points

**DynMesh | Project Points** (Query) casts a ray from every point and moves it onto the first DynMesh surface hit.
It is the directional counterpart of the closest-point projection the surface-pathing nodes use. Pins follow the
mutation rule: `In` (the points, emitted mutated on `Out`) first, then `Mesh`, then the optional `Selector`.

- **Direction** is a constant or a per-point Vector selector (`$Rotation.Up` by default). **Direction In Point
  Space** rotates it by each point's rotation, and **Invert Direction** flips it. **Max Distance** is `0` for
  unlimited and is measured in the points' space. **Bidirectional** also casts backwards and keeps the nearer hit,
  reporting it with a negative distance. **Ignore Back Faces** skips triangles reached from behind.
- **Several mesh inputs act as one scene**: each ray keeps the nearest hit across all of them. A Selection or
  Selector limits the surface to the selected triangles. Reported triangle IDs are always the source mesh's, and
  per-corner data (normals, UVs, colors) is read from the source mesh too.
- **Apply.** **Project Position** writes the hit back on the axes in **Position Axes** (X, Y and Z by default;
  clear X and Y to only drop points vertically), lifted by **Surface Offset** along the normal. **Project
  Rotation** turns **Align Axis** onto the hit normal by the smallest rotation, so heading around the normal
  survives. **Prune Misses** removes points that hit nothing; otherwise they pass through unmoved.
- **Attributes.** Each is opt-in with its own name: hit flag, signed distance, hit location, hit normal
  (interpolated shading normal or face normal, per **Normal Source**), back face, triangle ID, barycentrics, UV
  (per channel), vertex color, PolyGroup ID (default groups or a named layer), material ID, material, and which
  mesh input was hit.

The output is the input point data, mutated: it is initialized from the input, so every attribute in every domain,
every tag and the target actor survive, and pruning keeps each surviving point's own metadata entry.

## Structured surface generation

`DynMesh | Loft Paths` builds a quad grid between two paths: one column per path point and a set number of rows
across, with each row's height following a profile (`Linear`, `Smooth`, `Level at A`, `Level at B`, or a custom
curve). The second rail is either an offset of Path A or a path supplied on the `Path B` pin.

It exists because Remesh is the wrong tool for a slope. Remesh is isotropic, so its edges point in arbitrary
directions; on a curved or creased surface that shows up as faceting. A loft's edges follow the rails and the
profile by construction.

Path points are used exactly as given. That is the seam contract: a loft whose path was read off a mesh boundary
(`DynMesh | Selection To Paths`) lands its edge vertices on that boundary's vertices, so the two meshes can be
appended and welded with no boolean. Only `Path B` under `Arc Length` correspondence is resampled; use `Index`
correspondence when both edges must weld.

The geometry lives in `Geometry/PCGUtilsDynMeshLoft.h` with no PCG dependency (`BuildLoft`, `OffsetRail`,
`AlignRail`, `ResampleByArcLength`) and is covered by `PCGUtils.DynMesh.Loft.*`. The node is a creation node with
no DynMesh input, so it derives from `UPCGSettings` directly and has no Selector pin.

Per-path settings: Offset Distance, Offset Height and Rows can each be read from a `@Data` attribute on Path A
(one flag and one exposed attribute name each), so a single node lofts many paths with different shapes.

`Cap Rail B` fills a closed loft's far rail with a flat polygon that reuses the loft's own vertices, in its own
PolyGroup. An inward loft plus a cap is a plateau or bowl with one seam instead of two.

`Offset Smoothing` averages each point's offset direction along the path, over that multiple of the offset
distance. A rail read off a remeshed boundary is dense, and a mitered offset reverses any segment shorter than the
corner beside it can absorb; smoothing is what stops that. 0 restores mitered corners, which only suits sparse
paths. If segments still fold, the node warns with the path index, segment count and first point.

`DynMesh | Match Loops To Paths` is the companion node. A path that cut a hole no longer shares its points with the
hole, so the loop has to be read back off the mesh and then identified among every other loop. For each Target
path it emits the closest Loop; both outputs are ordered by Target, so an N:N Copy Attributes downstream carries
each path's data attributes onto its loop. A target with no loop within `Max Average Distance` goes to
`Unmatched Targets` with a warning naming the target and the distance.

When welding a loft to its mesh, keep the merge distance far below the loft's own vertex spacing (0.01, not 1).
The seam vertices are coincident, but the loft's inner rail is dense and a loose weld collapses it.

## Filling paths

`DynMesh | Path To Mesh` fills each path with a triangulated mesh, optionally extruded into a solid. It
replaces the chain `PCGEx | Create Spline` -> engine `Spline To Mesh` -> `Copy Attributes` (from the path with
`IsClosed` deleted). `DynMesh | Spline To Mesh` is the same class with `Input Splines` on: the `In` pin takes
splines and 2D polygons exactly like the engine node, so it can replace that node too. The pin label is `In` in
both, matching the engine node.

- **No input requirements beyond the engine node's.** The `@Data` Boolean `Closed Attribute Name` (default
  `IsClosed`) is optional: a path without it counts as closed, and one with it false is open. An open path or spline
  follows `Open Curves`, as in the engine node: `Treat As Closed` (default) joins its ends and fills it; `Offset`
  makes a band of `Curve Offset` width along it with `End Shapes`, and needs a Flatten Method and a Curve Offset.
  Filling needs at least 3 points; a band needs 2. 2D polygons are always closed.
- **@Data carries over.** `Copy Data Attributes` (on) copies the input's whole `@Data` domain onto the mesh, value
  by value. Inherited `@Data` (an input made from other data by an upstream node) copies the same as its own; the
  engine's domain copy refused it with "Invalid parenting on the domain 'Data'", which this replaced.
  `Copy Closed Attribute` (off) decides whether `IsClosed` comes along.
- **Frame.** Vertices are stored relative to a frame that is written, in world space, to `@Data.MeshTransform`
  (name exposed), as the engine node does. The frame is the first of: the input's `@Data` Transform
  `Transform Attribute Name` (default `RelativeTransform`, as written by Get Component Data) when present; the
  path centroid (`Use Centroid`, on); the spline/polygon's own transform, or the world origin for a path.
- **Facing.** The triangulation faces the way the path winds. `Face Up Direction` (on) turns each mesh so its
  area-weighted normal points toward `Up Direction` - a Vector shorthand, constant `(0,0,1)` or `@Data.Normal`.
  `Flip Result` then reverses the result. Facing is settled before extrusion, so `Thickness` (a DoubleAbs
  shorthand) always grows the solid toward the faces.

The triangulation is the engine's `FTriangulateCurvesOp`, with the engine node's settings (flatten, offset,
extrusion vector, open curves). Coverage:
`PCGUtils.DynMesh.PathToMesh.*`.

## Separating meshes

**DynMesh | Separate Islands** splits each mesh into its connected islands and emits each island as its own DynMesh
on **Islands**, like Blender's Separate by Loose Parts. Islands are numbered by their lowest triangle ID.

| Setting | Meaning |
|---------|---------|
| Connectivity | Shared Vertices (default, as Blender) joins triangles that share a vertex. Shared Edges needs a shared edge, so a bowtie becomes two islands. Proximity also joins vertices within Connect Distance, for parts that touch but were never welded. Intersecting also joins parts whose triangles cross each other (leaves pushed into a trunk), plus vertices within Connect Distance when it is above 0; parts that only overlap flat (coplanar) or sit wholly inside another without crossing its surface stay apart |
| Island Index Attribute | Optional, on by default. `@Data` int32 named `IslandIndex` that receives the island's number, counted per input mesh |
| Island Count Attribute | Optional, off by default. `@Data` int32 named `IslandCount` that receives the input mesh's island count |
| Output Properties | The DynMesh &#124; Properties outputs, written to each island |

Every island keeps the input's metadata (`@Data` included), tags and target actor, plus every per-element value:
UVs, normals, colors, material IDs, polygroup IDs (not renumbered) and weight layers.

With a selection or a Selector, only the selected triangles are split, and they connect only through each other:
two selected patches joined by unselected faces are two islands. The rest of the mesh comes out whole on
**Unselected**, which is empty without a selection. **DynMesh | Separate Selection** is the two-way split by
selection. Coverage: `PCGUtils.DynMesh.SeparateIslands`.

## Baking meshes to assets

Three nodes take a set of meshes from assets, through processing, back to assets in one graph, with no loop
subgraph. The engine's Mesh To Dynamic Mesh reads one mesh from a setting, and its Save Dynamic Mesh To Asset
saves only the first mesh it is given.

```
asset list -> DynMesh | From Static Mesh -> (processing) -> Get Asset Save Parameters (Input Data) -> DynMesh | Save Static Mesh
```

**DynMesh | From Static Mesh** makes one DynMesh per mesh its input names. `Mesh Attribute` (a selector,
default `@Last`) is a soft object path or string to a static or skeletal mesh, read on every element of the domain
it selects: each attribute set entry or point gives one DynMesh, and `@Data.<Name>` gives one per input. Each
DynMesh keeps its input's tags and `@Data` (`Copy Data Attributes`), and with `Copy Element Attributes` (on) the
attributes of the entry that named it become `@Data`, Mesh Attribute included, so a mesh still knows which asset
it came from. The read settings (materials, override materials, LOD, synchronous load) are the engine node's, and
the meshes read are dynamically tracked so the graph regenerates when one changes. An empty entry warns and is
skipped; a missing attribute, an asset that will not load or is not a mesh, and an override-material count
mismatch are errors naming the input and element. **DynMesh | From Static Mesh Points** remains the way to place
meshes by point transforms and append them.

**Get Asset Save Parameters**, in `Input Data` mode, runs once per input instead of once per node. `Asset
Attribute` (a selector, default `@Last`) must hold exactly one soft object path or string per input, normally a
`@Data` attribute. Each input comes out as a copy with the save name and folder written to its `@Data`; the output
pin takes the input's type. `Asset Name Attribute Name` and `Asset Path Attribute Name` (defaults `AssetName`,
`AssetPath`) name the attributes written, in every mode.

**DynMesh | Save Static Mesh** saves every DynMesh on `In`, with the engine node's options (materials from the
DynMesh, Copy Mesh To Asset Options, Mesh Write LOD, save on export). `Asset Name` and `Asset Path` are String
shorthands, constant or `@Data` (defaults `@Data.AssetName`, `@Data.AssetPath`), so either can come from each
mesh. A constant `Asset Name` given more than one mesh appends the mesh's index on the pin: `Rock_0`, `Rock_1`; a
single mesh is saved under the name as given, as the engine node does. Names are all checked before anything is
written: an empty name or folder, a missing `@Data` value, and two meshes that would be saved as the same asset
are each an error naming the input. `Open Save Dialog` is off by default and is ignored, with a warning, for more
than one mesh. `OutAssetPath` gives one attribute set per saved mesh, in input order, with the asset in
`Output Asset Attribute Name` (default `AssetPath`, the engine's) and the mesh's tags and `@Data`. Editor only.

### LODs

The chain carries a whole LOD chain when asked:

```
asset list -> From Static Mesh (All LODs) -> Paint Vertex Color (Paint LODs From Highest Detail)
           -> Get Asset Save Parameters (Input Data, Asset Attribute @Data.SourceMesh) -> Save Static Mesh (Save LODs From Attribute)
```

- **From Static Mesh** writes `@Data.LOD` (int32, the LOD read, 0 the most detailed) and `@Data.SourceMesh` (soft
  object path, the asset read) on every DynMesh, by default; both names are settings with toggles. With `All
  LODs` each mesh gives one DynMesh per LOD, LOD 0 first, and `Requested LOD Index` is unused. Source Model reads
  every stored source LOD, Render Data every render LOD, and Max Available a source LOD where the asset stores one
  and the render LOD where the LOD is auto-generated (it has no source mesh). Hi-Res Source Model has one LOD. A
  LOD that cannot be read is an error naming its LOD; the others are still made.
- **Paint Vertex Color**'s `Paint LODs From Highest Detail` paints each mesh's LOD 0 only and copies its colors
  onto the other LODs. See `PCGUtilsPainter.md`, "LOD groups".
- **Save Static Mesh**'s `Save LODs From Attribute` turns meshes that would save to the same asset into its LODs,
  each written to the LOD its `LOD Attribute` (default `LOD`, int32 or int64) names, instead of an error. A
  constant Asset Name is then not numbered. One asset's LODs must run 0, 1, 2... : a gap, two meshes for one
  LOD, a missing or non-integer LOD attribute, and a negative LOD are each an error naming the inputs, and that
  asset is not saved. LOD 0 sets the material slots; the asset is rebuilt once after every LOD is written, and
  LODs an existing asset had past the last one written are removed. `Mesh Write LOD` is unused in this mode.
  Outputs stay one per mesh, in input order. Coverage: `PCGUtils.DynMesh.SaveStaticMesh.LODsRoundTrip`.

All three copy `@Data` value by value through `PCGUtilsDataDomain` (PCGUtilsCore), as Path To Mesh does, so inputs
whose `@Data` is inherited from upstream data work. **From Static Mesh Points** uses it too; it used the engine's
domain copy before, which can fail the same way. Coverage: `PCGUtils.DynMesh.FromStaticMesh`,
`PCGUtils.AssetManagement.GetAssetSaveParameters`, `PCGUtils.DynMesh.SaveStaticMesh`.

## Simplification

**DynMesh | Simplify Planar** defaults to Geometry Script's planar simplifier and also exposes Polygroup
Topology, Triangle Count, Vertex Count, Geometric Tolerance, Edge Length, and Cluster Edge Length modes. It uses
the standard unified DynMesh/Selection input plus optional Selector, and the same immutable operation supports
deferred Builder chains. Selection regions are restored before optional ID compaction.

**DynMesh | Merge by Distance** replaces the deprecated legacy PCGUtils element. It can weld the whole mesh or
only vertices resolved by a DynMesh Selection/Selector, and quietly discards duplicate triangles created as a
normal consequence of welding rather than turning successful execution into a warning.

**DynMesh | Decimate** offers Blender's three Decimate modifier modes on the standard DynMesh/Selection input
and Selector, with Builder support:

- **Collapse** reduces to a fraction (`Ratio`) of the triangles with Geometry Script's quadric simplifier. The
  optional `WeightMapName` names a vertex weight map that steers where detail is kept, like Blender's vertex group,
  scaled by `WeightFactor` (negative inverts it). A missing map is an error naming the map. Selections run on an
  extracted region that is welded back. Blender's Symmetry and Triangulate options are not implemented.
- **Un-Subdivide** reverses grid subdivision: each pass merges every 2x2 block of quads into one quad. It pairs
  triangles into quads, labels each vertex as an original vertex, an edge midpoint or a face center, and removes
  the face centers and edge midpoints. Labels are vertex classes, not grid directions, because the two grid
  directions swap around a pole such as a cube corner. Each connected patch tries all four labelings and keeps the
  one that puts poles on original vertices. A face center needs four nearly coplanar quads, so creases survive.
- **Planar** merges faces whose normals differ by less than `AngleLimit` into regions, removes every vertex inside a
  region and every outline vertex whose bend is under the limit, then flips edges toward a Delaunay triangulation.
  Delimit by Material (on), Polygroup, UV Seams (on) and Normal Seams. `bAllBoundaries` drops all removable outline
  vertices regardless of angle.

Un-Subdivide and Planar live in `PCGUtilsDynMeshDecimate` (`Geometry/PCGUtilsDynMeshDecimate.h`). They are written
from Blender's documented behaviour, not translated from its GPL source, so they ship under the plugin's MIT
license. Every change goes through `FDynamicMesh3` collapses and flips, so UVs, normals, colors, material IDs and
polygroups stay consistent. Unselected triangles are locked. When a straight-outline vertex cannot collapse
because its triangle fan reaches other vertices on the same line, flips inside the region reduce its fan first.

On the test meshes, Planar at 5 degrees gives the same triangle count and smallest angle as Geometry Script's
Simplify Planar (box 432 to 12, grid 288 to 2, capped cylinder 384 to 92). The differences are the extras: delimits,
selections, `bAllBoundaries`, and an angle limit that also merges curved surfaces at large angles. Coverage:
`PCGUtils.DynMesh.Decimate`.

**Dark triangles after Simplify** usually come from earlier in the chain. In the tidepool Builder
(`Builder_Box_Tidepool`: Box with 2/2/6 steps, then Bevel 21, Taubin Smooth, Simplify Planar), the Bevel is wider
than half the step spacing. Neighbouring bevels overlap and fold, giving about 110 triangles that face inward
before Simplify runs. Simplify adds none. Keep a bevel under half the smallest step spacing, or simplify before
bevelling. The Properties node's inverted-normal count shows the fold.

The simplification itself lives in `FPCGDynMeshSimplifyParameters` / `PCGUtilsDynMeshSimplify::ApplySimplify()`
(`Geometry/PCGUtilsDynMeshSimplifyParameters.h`), so code outside a graph, such as the Static Mesh Brush cache,
runs exactly the same simplifier as the node.

## Mesh properties

**DynMesh | Properties** writes facts about each mesh to its `@Data` domain and passes the mesh through. The
properties are split by cost, and each has its own `bOutput<Thing>` flag and `<Thing>AttributeName`:

- **Free** (`FPCGUtilsDynMeshFreeProperties`, constant time): triangle, vertex and edge counts, Euler
  characteristic (V − E + T; 2 per closed piece without handles), whether IDs are compact, material slot count and
  slot materials, UV channel count, whether normals / vertex colors / default PolyGroups exist, and the extended
  PolyGroup layer count. Triangle and vertex count are on by default on this node.
- **Computed** (`FPCGUtilsDynMeshComputedProperties`, one pass): bounds min/max, surface area, volume, closed,
  open edge count, connected pieces, distinct PolyGroup count, and quality checks: degenerate triangles (area at
  or below a threshold), smallest triangle angle, triangles whose winding disagrees with their stored normals
  (*inverted normal* triangles — the dark facets left by a fold-over or stale normals), hard (normal seam) edges,
  and bowtie vertices. Only enabled properties are computed, and properties sharing a pass share it.

Properties always describe the whole mesh; a selection input passes through and does not narrow them.

**Inline writes.** The reading and writing live in `PCGUtilsDynMeshProperties` (`Geometry/PCGUtilsDynMeshProperties.h`),
not on the node. A process node exposes a `FPCGUtilsDynMeshFreeProperties` UPROPERTY and returns it from
`GetInlineFreeProperties()`; the process base validates the names once and writes after each successful input.
Simplify, Remesh, Self Union, Merge by Distance, Delete, Planar Cut, Mirror, Array, Separate (both outputs) and
every topology-base node (Extrude, Extrude to Paths, Inset, Bevel) offer this under *Output Properties*, all off by
default. Default names match the Properties node, so a renamed write before an operation (`PrevTriCount`) and a
default-named write after it can be subtracted. Builder results are the exception: realizing a Builder keeps the
mesh but not intermediate `@Data`, so a node given a Builder with inline writes on logs a note instead. Boolean
uses its own operand executor and does not offer inline writes yet. Coverage: `PCGUtils.DynMesh.Properties`.

## Node colors

Every DynMesh-family node draws in `PCGUtilsDynMeshNodeColor::Title` (`PCGUtilsCore`). PCGUtils nodes report
PCGUtils category types, which the PCG editor has no color for, so a node without an override is colorless. The
process base and the Selector/Builder/Painter provider base return the color; other nodes return it directly.
`PCGUtils.DynMesh.NodeColor` checks every node class in the DynMesh and Painter modules.

## Static Mesh Brushes and the brush cache

Hand-placed `UStaticMeshBrushComponent`s are editor-only Static Meshes that feed PCG as DynMesh data. Processing
a brush (reading an LOD, baking scale, simplifying) and merging brushes (self union) only needs redoing when its
inputs change, so it happens outside the graph and is cached. **DynMesh | Get Static Mesh Brush Data** then only
copies and places the cached result.

**Per brush.** Each component has `Processing`: the source LOD, and an optional simplification. The cached mesh is
the Static Mesh with the component's world scale baked in, then simplified, so simplify lengths and tolerances
are world units. Moving or rotating a brush does not invalidate it; placement is applied when the node reads it.

**Per actor.** A `UStaticMeshBrushCombinerComponent` (an actor component, not a scene component) appends every
brush on its actor, self-unions the result with its `SelfUnionOptions`, and can simplify the union. The combined
mesh is stored in the actor's rigid frame (rotation and location, no scale), so moving the whole actor keeps it;
moving, rescaling, re-meshing, adding or removing a brush, or changing any brush's `Processing`, rebuilds it. Brush
order does not matter. With `Recombine When Brushes Change` on (the default), the combiner rebuilds on the next
editor tick after a brush finishes moving, after a property commit, and after a brush's Static Mesh is edited.
With it off, it rebuilds only when the node asks and finds the cached result out of date. Adding or removing a
brush is always picked up on the next request.

**The cache.** `FStaticMeshBrushCache` is keyed by content, not by component: the asset, the world scale and the
exported `Processing` text for a brush; every brush key plus its placement in the actor frame plus the combine
settings for a combined mesh. That matters because Blueprint construction scripts re-create SCS components on
every move; a re-created component with the same inputs finds the same entry. Entries are dropped when their
Static Mesh broadcasts `OnMeshChanged` (reimport, mesh editor edits, Geometry Script writes), evicted
least-recently-used past `pcgutils.StaticMeshBrushCache.MaxEntries` (256 of each kind), and never saved to disk:
the first read in an editor session builds them. `pcgutils.StaticMeshBrushCache.Clear` drops everything. The
brush's `Rebuild Processed Mesh` and the combiner's `Recombine Now` buttons force a rebuild.

**The node.** `Source` picks the meshes per actor:

| Source | Actor with a combiner | Actor without one |
| --- | --- | --- |
| Automatic (default) | combined mesh | each brush |
| Combined | combined mesh | warning, actor skipped |
| Individual | each brush | each brush |

Automatic never emits both for one actor. Combined meshes get the `Combined Tag` (default `Combined`), meaning
they are already one self-unioned solid, so a graph can skip DynMesh | Self Union for them. Every output carries
the actor's tags plus the brush's (or combiner's) component tags. The inherited component filter applies to
separate brushes only; a combined mesh always holds every brush on its actor. `Convert World To Actor Local`
(on by default) puts the result in the PCG target actor's space, matching the module's local-space convention.
Materials are the Static Mesh asset's slot materials, not the brush's visualisation override material. Coverage:
`PCGUtils.DynMesh.StaticMeshBrush.Cache`.

## Selection modifiers

**Select | Interior Faces** exposes the first Blender Select by Trait predicate. It selects faces whose three
geometric edges each have more than two face users. Because `FDynamicMesh3` cannot store a topological edge with
three incident triangles, coincident split vertices are clustered by the configured tolerance before face users
are counted.

**Select | By Mesh Occlusion** is Select | By Self Occlusion with the blockers on a separate **Occluders** pin.
It fires a ray from each vertex (or triangle centroid) along **Trace Direction** and selects the elements whose ray
hits one of the occluder meshes (**Result** = Occluded) or reaches **Maximum Distance** clear (Unoccluded; 0 means
unlimited). Several occluders act together. Each mesh, tested and occluder, is placed by its own target actor
(**Meshes Are Actor Local**, on by default), so rays and distances are in world space. **Include Self** also lets
the tested mesh block its own rays. **Normal Offset** and **Direction Offset** move each ray's start off the surface.
The ray tests live in `PCGUtilsDynMeshOcclusion::FOccluderSet` (`Geometry/PCGUtilsDynMeshOcclusion.h`), which the
PCGEx point filter **Filter : Mesh Occlusion** in PCGUtilsPCGExInterop shares. Coverage:
`PCGUtils.DynMesh.MeshOcclusion`.

Every reusable DynMesh selector exposes **Invert Selection**. Inversion is applied by the shared selector
operation, so it works consistently inside Selection Logic and other nested selector graphs. Selectors with a
native domain are automatically adapted to the consumer's vertex, edge, or face domain. **Select | By Normal**
still evaluates vertex normals directly for vertex consumers and face normals for face consumers; an edge-only
consumer such as Realize Builders receives the converted face result without a domain error.

### Direction comparisons

**Select | By Normal** and the normal comparison of **Select | In Bounds** share one group of settings for how
closely two directions must agree. The settings and their Details panel preview are adapted from
PCGExtendedToolkit's dot comparison.

| Setting | Meaning |
|---------|---------|
| Domain | Whether thresholds are dot products (1 same, 0 perpendicular, -1 opposite) or angles in degrees (0 same, 90 perpendicular, 180 opposite) |
| Comparison | The operator. The default, greater or equal, passes directions at least as close as the threshold |
| Unsigned Comparison | Compare magnitudes, so a direction and its opposite count as the same |
| Threshold Input | Constant, or read from an attribute |
| Threshold (Attr) | The attribute, a number read in the chosen domain. Required when Threshold Input is Attribute |
| Scalar / Degrees | The constant threshold. Default 0.9 |
| Tolerance | For the Nearly Equal comparisons only |

The preview arc above the settings shows which directions pass. A threshold attribute holding a value outside
its domain is an error naming the attribute, the value and the index.

Where a threshold attribute is read from depends on the node. **Select | By Normal** has no points, so it reads
a Data-domain attribute (`@Data.Name`) of the mesh it is evaluated on. **Select | In Bounds** reads it from its
Points input, one value per point.

A **Select | By Normal** node saved with the single `Dot Threshold` setting keeps that threshold: it is moved
into the comparison settings when the node loads. Its `Dot Threshold` override pin no longer exists.

### Normals inside bounds

**Select | In Bounds** has **Compare Normal**. When on, an element is selected only if it lies in a point's
bounds *and* its normal agrees with a direction that point supplies. The reason this is one node, and not
Select In Bounds combined with Select By Normal through Selection Logic, is that each point brings its own
direction: one node can select the faces facing one way under one point and the faces facing another way under
the next.

| Setting | Meaning |
|---------|---------|
| Normal Direction Attribute | Attribute or property of the Points input giving the direction, in world space. Any vector-convertible value; must not be zero. Defaults to `$Rotation.Forward` |
| Normal Comparison | The direction comparison above. A threshold attribute is read per point |
| Normal Overlap Mode | For an element lying in several points' bounds: `Any` selects it if it passes for one of them, `All` only if it passes for every one. An element in no bounds is never selected |

A face is judged by its face normal and a vertex by its vertex normal. An edge is judged by the faces on it and
agrees with a direction if either of them does.

### PolyGroup Selector

**Select by PolyGroup** emits a reusable `Selector`. Connect it to **Build DynMesh Selection** or any process
node's optional `Selector` input, including processes in deferred Builder chains.

- **Group IDs** selects the union of one or more IDs (default `0`). Duplicate or unknown IDs have no additional
  effect; an empty list selects nothing. **Invert Selection** complements the resolved selection.
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

### Inside Mesh Selector

**Select | Inside Mesh** selects the vertices, edges or triangles of the tested mesh that lie inside the volume
enclosed by the DynMeshes on its `Volume` pin. **Invert Selection** selects what lies outside instead.

- Inside is the generalized winding number (`TFastWindingTree`) above **Winding Threshold** (0.5, advanced),
  so volumes with small holes or overlapping parts still have a sensible inside. Each element is tested at its
  center (vertex, edge midpoint, triangle centroid). Several Volume meshes act as their union.
- **Offset** grows (positive) or shrinks (negative) the volume by a distance from its surface, measured in the
  volume mesh's units. A small positive Offset counts elements lying on the surface as inside. A negative Offset
  keeps only elements deeper than that. Run on a boolean result against the original operand, this picks out
  the faces the boolean carved into the operand and leaves the operand's own surface out.
- **Meshes Are Actor Local** (advanced, default on) places the tested mesh and each Volume mesh by its own PCG
  target actor before testing. Disable it when they already share one space.
- A Volume input with no triangles is ignored with a warning; no usable volume is an error.

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

- Run several inputs at once. Today every input is processed one after another on the game thread; see
  [Processing several meshes at once](PCGUtilsDynMesh/ParallelInputs.md) for the investigation and staged plan.

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
