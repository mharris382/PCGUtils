# Point Selectors

## The idea

Many ways of selecting things only need points: is this point in a box, near a spline, passed by a filter. A
**point selector** is that and nothing more. It is given a set of points and answers one bit per point.

Each domain (Voxel, DynMesh, GC) knows how to show its own elements as points, and has its own selector type on
its own pins. A **wrapper** joins the two: it is an ordinary selector of one domain that holds a point selector.
So a `Selector | ...` node has no pin type of its own. Its **Domain** setting says what it selects, and what comes
out is a Voxel Selector, a DynMesh Selector or a GC Selection, which plugs in wherever that domain's own selectors
do and mixes with them by that pin's own rules.

In the palette each node is listed once per domain: `Selector | In Bounds (Voxel)`, `(DynMesh)`, `(GC)`. An entry
only sets Domain; it can be changed on the node afterwards (the output pin changes with it, so its wires drop).
The node shows its domain under its title.

## The pieces and who owns them

| Piece | Module | What it is |
|---|---|---|
| `FPCGUtilsElementPoints` | PCGUtilsCore (`Points/PCGUtilsElementPoints.h`) | "Elements as points": a flat list of points (world transform, local bounds; no bounds is the zero box) and, per element, the run of points that stand for it. Also the match rule and `Reduce`. |
| Conversion struct and function | each domain module | Fills an `FPCGUtilsElementPoints` from that domain's elements. Public, so anything else that wants a domain's elements as points can call it. |
| `UPCGVoxelSelectorData` | PCGUtilsVoxel (`Selectors/PCGVoxelSelector.h`) | The voxel domain's own selector type, "Voxel Selector" (pin colour `D2B48C`). Per cell: prepared against a grid, then `Test(Cell)` for any cell, filled or empty. |
| `UPCGUtilsPointSelectorData`, `FPCGUtilsPointSelectorOperation` | PCGUtilsSelectors (`Selectors/PCGUtilsPointSelector.h`) | The point selector. No pin type, never on a wire. One job: `Select(Points, Context, OutMask)`. |
| The three wrappers | PCGUtilsSelectors (`Selectors/PCGUtilsPointSelectorWrappers.h`) | `UPCGVoxelPointSelectorData`, `UPCGUtilsDynMeshPointSelectorData`, `UPCGUtilsGeometryCollectionPointSelectorData`: each a selector of its domain holding a point selector and that domain's conversion settings. |
| `UPCGUtilsPointSelectorProviderSettings` | PCGUtilsSelectors (`Elements/PCGUtilsPointSelectorProvider.h`) | Base of every `Selector \| ...` node: Domain, the three conversion settings, Invert, Priority, the per-domain palette entries. |
| `Selector \| In Bounds` | PCGUtilsSelectors | The first point selector: data, operation, box maths and node. |

Dependencies run one way: PCGUtilsCore, then the domain modules, then PCGUtilsSelectors. No domain module knows
point selectors exist, and PCGUtilsCore holds no selector.

Every wrapper evaluates the same way: the domain's conversion makes the points, the point selector answers once
for all of them, the match rule reduces that to one answer per element. Invert is copied into the domain
selector's own invert flag, so the domain applies it once, over the same candidates.

## Conversion: how each domain shows its elements

All points are in the world. DynMesh and GC data are target-actor-local, so their conversions place points by the
target actor transform.

| Domain | Setting | Options |
|---|---|---|
| Voxel (`FPCGVoxelPointConversion`) | Cell Points | **Center With Bounds** (default): the cell centre with the cell's box. **Center**: the centre alone. |
| DynMesh (`FPCGUtilsDynMeshPointConversion`) | (vertex) | Always the vertex itself, no bounds. |
| | Edge Points | **Midpoint**, **Midpoint With Bounds** (default; the box round its two ends), **Vertices** (its 2 vertices). |
| | Triangle Points | **Center**, **Center With Bounds** (default; the box round its three corners), **Vertices** (its 3 vertices). |
| GC (`FPCGUtilsGeometryCollectionPointConversion`) | Bone Points | **Center** of the piece's bounds, **Center With Bounds** (default), **Bounds Corners** (8 points), **Vertices** (every vertex of the piece: exact, heavier). |

DynMesh works on Triangle topology (vertices, edges, faces); Polygroup topology is not supported and says so. GC
candidates are pieces with valid bounds, never clusters; Invert gives the other pieces.

**Match** applies where an element stands as several points (Vertices, Bounds Corners): **Any** (default) selects
the element when at least one point is selected, **All** when every point is, **At Least Fraction** when at least
`Match Fraction` (0 to 1) of them are. An element that is one point ignores it.

A point "with bounds" matters to selectors that test extent. In `Selector | In Bounds`, `Containment` (Center,
Any Part, Fully Inside) says how a point with bounds is tested; for a point without bounds all three are the same.

## Selector | In Bounds

Input pin `Bounds` (points, required). Selects what lies in the bounds of any point on it, each point's Bounds
Min / Max placed by its transform. `Expand` grows every bound in world units (negative shrinks; a bound shrunk
inside out is skipped with a warning). No points on the pin selects nothing (with Invert, everything).

`Priority` is passed to the domain's selector and means what that domain makes of it. It never changes what is
selected: DynMesh tests higher Priority first where several selectors share a pin; Voxel and GC join several
selectors on a pin as "any of them", so it has no effect there.

## Writing a new point selector

1. Derive a data class from `UPCGUtilsPointSelectorData`. Give it its settings as properties, add them in
   `AddToCrc`, do one-time work in `Prepare`, and return an operation from `CreateOperation`.
2. Derive the operation from `FPCGUtilsPointSelectorOperation` and implement `Select`: set `OutMask` to one bit
   per point. Read `Points.GetTransforms()` and `GetLocalBounds()` when that is enough. Call
   `Points.GetOrBuildPointData(Context)` only when real point data is needed (attributes, an engine or PCGEx
   filter); it is made on the first call and kept. `Select` is const and may run on several threads.
3. Derive the node from `UPCGUtilsPointSelectorProviderSettings`. Override `CreatePointSelector` (build the data
   from the node's settings and pins; call `AddDataDependency` for any pin data it holds), `GetDefaultNodeTitle`
   (`Selector | [NAME]`), the tooltip and `InputPinProperties`. Write no per-domain code: the base wraps the
   selector, shows the conversion settings and adds the three palette entries.
4. Add the node to `PCGUtils.Palette.SearchContract` (its counts) and test it on a point set, as
   `PCGUtils.Selectors.InBounds.Points` does.

A selector in another plugin (the PCGEx filter selector in PCGUtilsPCGExInterop) follows the same steps and
depends on PCGUtilsSelectors; everything above is exported.

## How a new domain joins

1. Give the domain its own selector type and pins, if it has none.
2. In the domain's module, add a conversion struct and one public function that fills an `FPCGUtilsElementPoints`
   (one `BeginElement` per element, then its points; set `Match` and `MatchFraction`). It may call
   `SetPointDataBuilder` to make its points carry attributes; none of the three does today.
3. In PCGUtilsSelectors, add a wrapper deriving from the domain's selector class, a value in
   `EPCGUtilsSelectorDomain` (append, the value is saved in graphs), and the domain's conversion property and
   cases in `UPCGUtilsPointSelectorProviderSettings`.

Tests: `PCGUtils.Selectors.*` (element points, In Bounds on points, each wrapper, the node), and
`PCGUtils.Voxel.Patterns.*` for how Apply Patterns uses voxel selectors.
