# PCGUtilsVoxel (plan)

Status: **phase 1 built** (To DynMesh, Pack Points, Compress, Expand; module `PCGUtilsVoxel`), tested headless in a
scratch project and in the VRMonkeyGolf Development build, not yet tried in a real graph. To DynMesh speed is in
section 3.8. Decisions D1-D4 are settled (section 6). Where the build differs from
the plan it says so below ("Built:").

A new runtime module of voxel operations. It adds no PCG data type. A voxel set is ordinary point data, the way
PCGExtendedToolkit treats clusters as points with known attributes. Every engine and PCGUtils point node keeps working on
it (filters, attribute ops, Copy Points, Debug), and the engine's Volume Sampler output can go straight in.

Palette category: `Voxel`. Node titles: `Voxel | <Element>`.

## 1. The voxel contract

One point is one occupied cell, or one chunk of cells (section 1.1).

| Where | Name (default, editable on every node) | Type | Required | Meaning |
|---|---|---|---|---|
| `@Data` | `VoxelSize` | Vector | yes* | Cell size along the grid's X, Y and Z. Every component must be > 0. |
| `@Data` | `VoxelGridTransform` | Transform | no | Grid space to world: where the grid's origin is and how it is rotated. Scale must be 1 (size lives in `VoxelSize`). |
| point | `$Position` | | yes | The cell centre (a chunk's centre, section 1.1). |
| point | `VoxelSpan` | Vector | no | Whole cells covered along X, Y, Z. Its presence makes the set chunked (section 1.1). |
| point | `IsSurrounded` | Boolean | no | The point's cell or chunk shows no face (every cell sharing a face with its box is filled). `Voxel \| Cell Properties` writes it; every node that changes cells rewrites it when the input has it (section 1.3). Name: `Surrounded Attribute` in Grid; None leaves it alone. |
| point | `$BoundsMin` / `$BoundsMax` | | no | The cell's surface offset (section 3.5). Volume Sampler writes exactly ±VoxelSize/2. |
| point | anything else | | no | Per-cell values: material, color, UV tile, etc. |

\* Read through an input shorthand (`FPCGUtilsInputShorthandVector`), in `@Data` mode by default. Graphs add the
`VoxelSize` attribute themselves (for example after a Volume Sampler, which writes no `@Data`); switching the
shorthand to Constant is the fallback.

**Cell index.** `Index = Round(GridInverse(Position) / VoxelSize)`, so cell centres sit at `Index * VoxelSize`. The grid
origin is a cell **centre**, not a corner. This is the Volume Sampler's rule
(`PCGVolumeSampler.cpp`: `SampleLocation = (X * VoxelSize.X, ...)`), so its output is already on-grid.

**Missing `VoxelGridTransform`.** The Volume Sampler samples in **world** space with the grid origin at world 0.
It does not use actor space. Default: missing transform = identity (world), which matches the sampler exactly.
A `Missing Grid Transform` setting (`World` / `Target Actor`) covers graphs that build voxels in actor space.

**Validation** (CLAUDE.md: one exact invariant per message):
- `VoxelSize` not found; or its X, Y or Z is ≤ 0 (name the axis and value).
- `VoxelGridTransform` has the wrong type; or its scale is not 1 (give the scale).
- Malformed input only fails: a missing or non-positive voxel size, a bad grid transform, a span that is not whole.
  Points are free to go where a graph puts them, so a point's position never fails the input by default (designer
  decision 2026-11-13: failure is for catastrophic, malformed input only).
- Off-grid points (further from a cell centre than `Grid Tolerance`): `Off Grid Points: Snap | Error`; default
  **Snap** (read as the nearest cell). Error reports the point index, the distance and the tolerance.
- Two points in one cell: `Duplicate Cells: Keep First | Move To Free Cell | Keep Last | Error`; default **Keep First**.
  A grid stores occupancy only, so a shared cell is just a filled cell; the setting decides which point the cell reads
  its values from. Keep First / Keep Last ignore the other point quietly (it covers no cell, and nodes that rewrite
  the set leave it out); no warning, since Offset Surface writes carved cells this way on purpose. An ignored point on
  the same cell (same first cell and span) still counts toward the cell's surface offsets: the point that has the cell
  reaches as far as any of them, so a cell written as several boxes reads as the box around them
  (`ComputeSurfaceOffsets`), except in `Voxel | To DynMesh` with Exact offsets from point bounds, which meshes every
  box as it is (section 3.5). Move To Free Cell moves each later point to the free cell
  nearest its own centre within `Move Radius` (default 1; a chunk's whole box must fit), after every point that kept its
  place has claimed it, so a moved point never takes a later point's own cell; a point with no room is ignored with a
  warning. Error fails, naming both points and the cell.

Shared code lives in `PCGUtilsVoxel/Public/Voxel/PCGVoxelGrid.h`. `FPCGVoxelGrid::Build(PointData, Settings, Context)`
validates the contract once and returns:
- the grid frame,
- a cell lookup (index → point),
- each point's cell index.

Every node uses it. Built: the cell lookup is a dense array over the occupied bounding box when that box is at most
8 times the cell count (or under 65,536 entries, capped at 64M); otherwise a hash map. Both give the same answers;
`FPCGVoxelGrid::IsDense()` says which one a set got. The per-point pass of `Build` runs in parallel; errors are still
reported for the lowest failing point index.

### 1.1 Chunks (compressed voxel sets)

One point may stand for a box of cells: a chunk. This is how a voxel set is compressed. Large solid regions
become a few points instead of thousands.

**Format.**
- A set is chunked when its points have the `VoxelSpan` attribute (name exposed on every node). Nothing else marks it.
- `VoxelSpan` is a Vector of whole numbers ≥ 1: how many cells the chunk covers along grid X, Y and Z. A span of
  (1, 1, 1) is a plain cell, so chunked and plain points mix freely in one set.
- `$Position` is the chunk's centre. For an even span that lands on a cell corner rather than a cell centre, and
  that is expected. The chunk's first cell is `Round(GridInverse(Position) / VoxelSize − (Span − 1) / 2)`.
- Bounds follow the cells: natural extent ±Span × VoxelSize / 2, so PCG's debug view draws the chunk at its true
  size. The surface offset (3.5) is measured from that extent.
- Every cell of a chunk has the chunk's attribute values. A chunk can only exist where all its cells were equal.

**One meaning, either format.** `FPCGVoxelGrid` expands chunks when it builds the cell lookup: every covered cell
maps to its chunk's point. A node that reads cells (neighbours, faces, colors, materials) gets the same answer
from a chunked set as from the expanded one. **A chunked input must never change any node's result**, unless
that node's docs give a chunk rule. Tests check this per node: run on a set and on its compressed form, and
compare the outputs.

**Nodes that output voxels state their chunk rule.** Each has a `Chunks` setting from this list, with the default
in its docs:
- **Keep:** a chunk stays one point when the operation does not split it.
- **Expand:** the output is plain cells.

**Validation** (on top of section 1):
- `VoxelSpan` is not a Vector (give the type).
- Point N's span has an axis below 1 or not whole (give the axis and value).
- Point N's centre does not line up with whole cells for its span (give the distance and `Grid Tolerance`).
- Two points cover the same cell (give both point indices and the cell). This is the duplicate-cell rule;
  `Duplicate Cells` applies cell by cell.

### 1.2 Voxel | Compress and Voxel | Expand

**Compress** turns plain cells (or an already chunked set) into chunks.
- `Shape: Cubes | Boxes | Random`.
  - **Cubes** (default) makes N×N×N chunks only.
  - **Boxes** also allows uneven boxes, which usually means fewer points.
  - **Random** makes boxes of random size, as a stylistic choice rather than an optimization (below). The palette
    lists it separately as **`Voxel | Compress Random`**: the same node with Shape preset to Random, titled to match.
- `Max Span` (default 16, one value for every axis; Cubes and Boxes). An input shorthand: a constant or
  `@Data.CompressMaxSpan` (one per set, at least 1).
- Method: an aligned octree. Power-of-two cubes on grid-aligned positions are merged level by level while all eight
  children are full and equal. This is fast, deterministic, and the same input always gives the same chunks.
  Boxes mode then merges neighbouring equal chunks greedily along X, then Y, then Z.
- **Random** method: cells are visited in Z, Y, X order. Each cell not yet taken starts a box, sized from that start
  cell's values (first come, first served: cells an earlier box took are gone; the start cell does not look at the
  values of the cells it takes). Settings (added 2026-11-10 on request; span and chance are point shorthands):
  - `Random Size`: **Up To** (default, the original behaviour, below), **Exact** (the box is exactly `Random Span`,
    or the start cell stays a single cell when that box does not fit; never a smaller box) or **Cubes Up To** (a side
    drawn from 1 to the smallest axis of `Random Span`; the box is the largest cube up to that side that fits).
  - `Random Span` (default 3×3×3; was `Random Max Span`, an IntVector, carried over in `PostLoad`): a constant, a
    point attribute (default `CompressSpan`, a Vector; read from the start cell) or `@Data`. Every component must be
    a whole number ≥ 1, or the input fails naming the point and value.
  - `Random Chance` (default 1, 0 to 1): the chance a start cell becomes a box when one bigger than a cell fits there;
    otherwise it stays a single cell. Point attribute default `CompressChance`, read from the start cell, so an
    attribute steers where boxes form. Rolled only when below 1 and only when there is a box to make, so graphs
    with chance 1 keep the exact tiling they had.
  - **Up To**: the box draws a target size per axis uniformly from 1 to `Random Span` and grows one axis at a time,
  in a random axis order, while the next slab of cells is occupied, not taken and equal. Every cell earlier in that
  order is already taken, so the start is always the box's lowest corner and the boxes tile the set exactly, with
  staggered joints. Boxes come out smaller than their target where they meet empty, taken or unequal cells. An axis
  set to 1 keeps boxes one cell thick (flat tiles on a wall). Seeded from the node seed and the input index, so the
  same seed gives the same tiling. It does not start from the maximal compression: that state's power-of-two seams
  would show through any random split of it.
- **Equal** means equal in:
  - every point attribute, or only those listed in `Match Attributes` (`All | Listed`, default All). Listing fewer
    attributes compresses more, but cells keep only the first cell's values for unlisted ones, and the node says
    so in its tooltip;
  - the surface offset (bounds relative to the cell), so the mesh stays the same;
  - the `Surrounded Attribute` (`IsSurrounded`) and `VoxelSpan` are not compared: they describe the box, and are
    rewritten for every chunk, so surface and inside cells merge alike;
  - the seed is not compared. A merged chunk is seeded `PCGHelpers::ComputeSeed` of its first cell (the Volume
    Sampler's seed for that cell); a point that comes out unchanged keeps its seed.
- Output: one point per chunk, initialized from the input so `@Data` and tags carry over. `VoxelSpan` is written
  on every point. Points are ordered by their first cell.

**Expand** writes one point per covered cell.
- Each point copies its chunk's attributes and properties.
- Seeds are recomputed per cell the way the Volume Sampler does it (`PCGHelpers::ComputeSeed(X, Y, Z)`), so an
  expanded set matches a freshly sampled one.
- `VoxelSpan` is removed by default (`bRemoveSpanAttribute`).
- Expand of a plain set returns it unchanged.

### 1.3 `IsSurrounded` through the pipeline

Added 2026-11-10: graphs filter on `IsSurrounded` to spawn only visible voxels as instances, but nodes that changed
cells left a stale value (a chunk kept its first cell's, an offset cell its old one). Now, when the input carries the
Grid's `Surrounded Attribute` (default `IsSurrounded`) as a Boolean, every node that changes cells rewrites it on its
output: true when every cell sharing a face with the point's box is filled (`PCGVoxelPointIO::RefreshSurrounded`).
Nodes: Compress (and Random), Expand, Boolean, Offset Surface (both pins), Match Rules (both pins), Pack Points
(Unfilled), Cell Properties (before its own outputs). "Filled" is the whole voxel set the output belongs to, not
only the cells on that pin (Match Rules, Pack Points). Shift Seams and Transform Grid keep the cells, so the value
carries over. A non-Boolean attribute of that name is left alone; Surrounded Attribute None turns it all off.

Round trip: Expand(Compress(x)) equals x, except that point order follows cells and unlisted attributes take the
first cell's values. A test checks it.

## 2. Nodes

| Phase | Node | Does |
|---|---|---|
| 1 | `Voxel \| To DynMesh` | The main node (section 3); Shape Bevel in 3.5b. |
| 1 | `Voxel \| Compress` | Merges equal neighbouring cells into chunks (section 1.2). |
| 1 | `Voxel \| Compress Random` | The same node preset to Shape Random: random-size boxes for looks (section 1.2). |
| 1 | `Voxel \| Shift Seams` | Slides interior seams so boxes become uneven but fill the same space (section 4c). |
| 1 | `Voxel \| Transform Grid` | Moves, turns and scales whole voxel sets about a per-set pivot; values per set from @Data (section 4e). |
| 1 | `Voxel \| Separate Islands` | One voxel set per connected island; optionally re-anchors each island's grid at its own centre cell so the grid origin is its pivot (section 4n). |
| 1 | `Voxel \| Slice` | Cuts each set into slabs across one grid axis (chunks split at the cuts); writes slice index and 0-1 fraction for twists (section 4o). |
| 1 | `Voxel \| Air Flow` | Wind around a voxel set: a smooth flow field on air cells (a voxel set of its own), wind shadows, exposure per solid cell (section 4f). |
| 1 | `Voxel \| Rasterize` | A DynMesh or a closed point path to cells: Fill, Intersect and Surround modes, on its own grid or another set's (section 4g). |
| 1 | `Voxel \| Boolean` | Union, intersect or subtract voxel sets by cell index; Out is In, mutated (section 4h). |
| 1 | `Voxel \| Offset Surface` | Moves the surface by Amount cells: whole layers, then part cells (no box leaves its cell); inside corners split; Min Thickness (section 4i). |
| 1 | `Voxel \| Fill Gaps` | Fills thin gaps (what a closing fills) and, optionally, enclosed cavities; Out is In with the cells added, Gaps has only them (section 4l). |
| 1 | `Voxel \| Extrude` | Grows each cell along Direction × Amount (whole cells, rounded up), or with a negative Amount removes it and the cells beyond; separate runs per axis or a box; added cells numbered by step (section 4k). |
| 1 | `Voxel \| Match Rules` | Rule tiles: ordered rules (open faces, thin) that may turn to fit; turns each cell's point and writes a mesh index for a collection downstream (section 4j). |
| 1 | `Voxel \| Pattern` | One authored pattern (probes, boxes with mesh index and mesh turn, turns, joining) for Apply Patterns; presets Top Edge, Upright Corner, Inside Corner Fill (section 4p). |
| 1 | `Voxel \| Apply Patterns` | Runs the connected patterns in order: each claims cells as oriented chunks (or adds cells), one point per chunk with a mesh index; span stays in grid axes (section 4p). |
| 1 | `Voxel \| Expand` | Turns chunks back into one point per cell (section 1.2). |
| 1 | `Voxel \| Pack Points` | Packs loose points into free cells of a voxel grid, fully or part way, without overlaps (section 4). |
| 1 | `Voxel \| Cell Properties` | Writes per-cell attributes: open sides, surrounded, inside corners, neighbourhood, depth, column, components and shape (protrusions, thickness, gaps, cavities) (section 4b). Replaces the planned `Voxel \| Neighbors`. |
| later | `Voxel \| Resample` | Change cell size. (`From DynMesh` became `Voxel \| Rasterize`.) |

## 3. Voxel | To DynMesh

Input pins: `In` (voxel points, one mesh per input). Output: `Out` (DynMesh, one per input).

### 3.1 Faces
A face is emitted for every cell side whose neighbour cell is empty. Faces between two occupied cells are never made,
even when the two cells have different materials. A voxel set is a solid; inner walls are not built.
- Vertices are shared across faces, so the result is closed and manifold.
- Two cells that touch only along an edge or at a corner get split vertices there. That keeps the mesh manifold.
- Normals are flat per face, with hard edges between face directions.

### 3.2 Material
`Material Source: None | Constant | Attribute`.
- **None:** no material slots.
- **Constant** (default): one soft material reference, defaulting to the engine's default surface material
  (`UMaterial::GetDefaultMaterial(MD_Surface)`).
- **Attribute:** a soft object path or string point attribute (default name `Material`). Each distinct value becomes
  one material slot, in the order first seen. Each face gets its cell's slot.
- An unloadable or empty path is a per-point error naming the point index and the value.
- The output DynMesh's material list is written to match the slots.

### 3.3 Collapse coplanar faces (default false)
`bCollapseCoplanarFaces`. Merges neighbouring faces that share a plane and have the same:
- material,
- vertex color, when hard transitions are used (3.4),
- UV tile (3.6),
- polygroup.

Each merged region is triangulated from its outline, **keeping every boundary vertex** another region uses as a
corner. Neighbouring regions therefore share every edge vertex, and there are no T-junctions or cracks. Built: an
outline of N vertices becomes N − 2 triangles by ear clipping (exact integer collinear tests, so no zero-area
triangles), with a fan round an added centre vertex only as a fallback that has never been needed in tests.
- Faces under `Smooth` color transitions merge only where all corner colors match, so gradients survive.
- Faces whose surface offset (3.5) is not uniform across the region don't merge.

### 3.4 Vertex color
`Vertex Color Source: None | Attribute`. Default None. The attribute defaults to `$Color`; Vector4, Vector or scalar
values are accepted.

`Color Transition` sets what happens where neighbouring cells on the surface have different colors:
- **Hard** (default). Each face has its cell's exact color. The vertices still share positions; only the color
  elements are split, so the mesh stays watertight.
- **Smooth.** Each surface corner takes the average color of the cells on that surface that meet at the corner.
  This makes gradients across cells.
- **Smooth Within Material.** Same as Smooth, but the averaging stops at material boundaries, which stay hard.

### 3.5 Surface offset from point bounds (manifold-extrude style)
`Surface Offset: None | From Point Bounds | Constant`. Default None.

A cell's natural extent is ±VoxelSize/2. The offset is how far a cell's point bounds reach past that extent, measured
per side. For example, `+X` offset = `BoundsMax.X − VoxelSize.X/2`. `Constant` uses one inset or outset distance for
every face (an input shorthand).

The offset changes only the **outer surface**:
- Internal faces don't exist, so the bounds of fully enclosed cells do nothing.
- Each surface vertex moves along the normals of the surface faces that meet at it, by those faces' offsets. This
  works like Blender's Manifold Extrude: the shell grows or shrinks and stays closed, and its topology is the same as
  the un-offset mesh.
- Where neighbouring cells have different offsets, `Offset Blend` decides (added 2026-11-13):
  - **Exact (Point Boxes)** (default): every face keeps its own point's offsets, so the mesh has the shape you get by
    spawning each point's bounds as a box instance. A Rasterize Best Fit/Trim surface keeps its steps.
    With `Surface Offset` = From Point Bounds (rebuilt 2026-11-16) the mesh is the union of every point's box, built
    directly (`PCGVoxelMesher::BuildExactBoxMesh`, `PCGVoxelBoxMesher.cpp`): points Duplicate Cells ignored on the same
    cell count as boxes of their own, so a cell Offset Surface wrote as several boxes keeps its notch instead of
    becoming the box around them, and a face a neighbour's shrink exposes (the underside of a cell over a pulled-in one)
    is there. Each box face is cut, cell by cell, against the boxes in front of it and against earlier boxes' faces on
    the same plane; pieces are welded on a 1/2^20-cell lattice (all comparisons exact), every edge is split at each
    vertex lying on it (no T-junctions), the four faces at a pinch edge are paired by the solid quarter they bound
    (a later pair still sharing both ends gets a midpoint vertex), and each polygon is ear clipped clipping corners next
    to straight runs first, so there are no zero-area triangles and no risers or fans. Collapse joins neighbouring
    pieces of one plane, direction, material, color and polygroup value into rectangles. A box with no thickness on an
    axis (Offset Repair Zero Thickness or One-Sided Plane) falls back to the cell path below with merged boxes.
    The cell path (Constant offsets, or the fallback): corners join only
    where they land at the same place. Each edge whose corners part from its partner's gets a stitch: a riser quad, or a
    triangle when one end still meets. Where three or more stitches meet at one lattice point, the small hole left
    is fanned shut. The mesh stays closed and manifold. Stitches along a step's side line, and those fans, are
    zero-area slivers; their normals are set from their direction or a neighbour. Faces with a stitch are never
    collapsed. Corner rule: along its own axis a face moves by its point's offset; along each in-plane axis a corner
    follows that side's edge: convex (own side face) uses the point's own offset, concave (wall in front) the wall's
    offset, flat (the next cell continues) stays on the cell boundary. With equal offsets everywhere this is identical
    to Average.
  - **Average (Smoothed)**: the shared vertex takes the average, so the surface slopes between cells instead of
    stepping. Topology never depends on the offset.
- A point turns inside out only when its two faces on one axis cross: `BoundsMax ≤ BoundsMin` (after scale) with both
  sides showing, or a shown face reaching past the natural side of a hidden one. A single face passing the cell's
  centre is fine (Max.Z 0, Min.Z −20 is a valid 20-unit slab). Crossing points are repaired, never refused (changed
  2026-11-13; it used to be an error that dropped the whole set, and it wrongly tested each side against the centre):
  `Offset Repair` picks what a crossing point becomes, always placed halfway between the crossed faces (or against
  the hidden side when only one side shows):
  - **Thin Slab** (default): closed, 1/1000 of a cell thick. Renders as a two-sided plane; safe downstream.
  - **Zero Thickness**: closed, both faces exactly together; the side walls are zero-area triangles, which DynMesh
    Boolean, Bevel and normal recompute can trip over.
  - **One-Sided Plane**: only the + face of that axis (the - face if only that one shows) is kept; the opposite face
    and side walls are dropped (`FPCGVoxelMeshInput::DroppedFaces`), so the mesh is open there and the plane is seen
    from one side. A point crossed on several axes falls back to Zero Thickness.
  One warning per input gives the count, the repair used, and the first five points with axis, face positions and
  thickness. Added 2026-11-13 so the designer can compare the three in a graph.

### 3.5b Shape: Bevel
`Shape: Blocks | Bevel`. Default Blocks. Bevel cuts the surface's edges at an angle while the set is still voxels,
which gives cleaner, more angular corners than running DynMesh | Bevel on the finished mesh. Convex (outside) edges
are cut away; concave (inside) edges are filled in. Collapse and surface offsets are hidden and not used in this mode.

Amounts are fractions of a cell, 0 to 1. At 1 the cut runs a whole cell along each face (a full cell diagonal). With
a non-cubic Voxel Size the cut follows the cell's proportions, so it is not 45°.

`Bevel Controls` picks how many amounts to set:
- **One Amount**: `Bevel Amount` for every edge.
- **Convex / Concave**: `Convex Bevel`, `Concave Bevel`.
- **Convex / Concave x Top / Side / Bottom**: six amounts. An edge faces **Top** when one of its two faces points up
  (grid +Z), **Bottom** when one points down, otherwise **Side** (vertical edges). A concave Top edge is a floor
  meeting a wall; a concave Bottom edge is a ceiling meeting a wall.

`Use Bevel Scale Attribute` / `Bevel Scale Attribute` (default `BevelScale`, a float, 0 or greater, optional)
multiplies the amounts per point. A convex edge uses its cell's value; a concave edge averages the two cells beside
it. A band whose amount changes along its length tapers over one cell.

How it is built (`PCGVoxelBevelMesher.cpp`): the same closed quad surface as Blocks, then
- **Trim**: each cell face is pulled back from its beveled edges.
- **Strip**: each beveled edge becomes a sloped band between the two trimmed faces.
- **Fill**: the hole left round each surface corner is closed with one polygon (a triangle at a box corner).

Rules that keep it closed:
- Widths are kept per (surface corner, axis), the narrowest beveled edge there, so bands along long edges are
  continuous.
- Where two bevels would overlap on one face (opposite edges of a one-cell-wide face, or trims passing each other
  along a square edge), both are scaled down to meet. So a single cube at 1 becomes the octahedron through its face
  centres, and a one-cell wall at 1 gets a pointed ridge.
- Where a face's own two edges at a corner are square but a third edge there is beveled (top face over a beveled
  vertical edge), the face's corner is cut diagonally to meet the band's end.
- Where bevels meet exactly, faces shrink to lines or points; corners that land together along a polygon edge are
  welded and the collapsed polygons dropped.
- Edges where cells touch only along the edge count as convex on both cells.

Output: normals are flat per polygon (hard edges everywhere). UVs project each polygon on its dominant axis (Grid
Projection keeps tiling across the bevels; Per Face and Atlas start each polygon at its own corner). Colors work as in
Blocks (Smooth averages the cells round each surface corner). Polygroups Per Face Direction puts bands in group 6 and
corner fills in group 7.

Not handled: no collapsing of coplanar faces yet (every cell face is its own polygon); amounts that differ a lot
between neighbouring edges can give slightly folded corners; bevel 0 at a pinched edge has the Blocks limitation in
3.10.

Speed (2026-11-07, Development, every edge at 0.5): solid 64³ 27 ms (plain 11), terrain 1.3M cells 390 ms (plain
124), random 50% of 48³ 325 ms (plain 88). All three closed with no rejected triangles.

### 3.6 UVs
`UV Mode`:
- **None.**
- **Grid Projection** (default). Each face is projected on its own axis in grid space and scaled by `UV Scale`.
  The default is 1 UV unit per cell. UVs then tile seamlessly across cells and collapsed regions, and don't change
  when the actor moves.
- **Per Face.** Each cell face is 0..1. Collapsed regions span 0..N in each direction, so a tiling texture still
  shows one repeat per cell.
- **Atlas From Attribute.** A point attribute picks the cell's tile in a texture atlas: an Integer tile index with
  `Atlas Columns`/`Atlas Rows`, or a Vector4 UV rectangle. Per Face UVs are mapped into that tile. Faces in different
  tiles never collapse together.

Also:
- `UV Channel` (default 0).
- Fixed U/V axes per face direction, so textures stay upright on side faces.

### 3.7 Polygroups, space, metadata
- `Polygroups: None | Per Face Direction | Per Material | Per Collapsed Region | Per Attribute Value`. Default Per Face
  Direction.
- **Per Attribute Value** (added 2026-11-09): every cell sharing a value of `Polygroup Attribute` (an attribute
  selector, default `Polygroup`, required in this mode) lands in one group, and collapsed faces never merge across
  groups. With `Integer Values Are Group IDs` (default on) an int32/int64 attribute's value is the group ID itself
  (must be 0 to 2^31-1; the error names the point and value). Otherwise, or for any other type, the value is read as a
  string and distinct strings are numbered 0, 1, 2... in order of their first point, so values that print the same
  count as equal. In Bevel mode a band or corner fill takes the group of one of the cells beside it.
- Output space follows the DynMesh convention: vertices in the PCG target actor's local space by default, with the
  space helpers in `PCGUtilsDynMeshSpaceHelpers.h`.
- `bCopyDataAttributes` (default true). Copies the input's `@Data`, including `VoxelSize` and `VoxelGridTransform`,
  onto the mesh with `PCGUtilsDataDomain::CopyDataAttributes`, as DynMesh | Sample does.

### 3.8 Speed
How it is built (`PCGVoxelMesher.cpp`, stages as reported by `FPCGVoxelMeshStats::StageSeconds`):
- **faces** (parallel): a 6-bit exposed-face mask per cell, a prefix sum, then the faces. A cell's faces are stored
  together, so face (cell, direction) is found from the cell index and the mask, with no hash lookup.
- **vertices**: each face edge's partner (flat, convex or concave continuation) is found in parallel; a serial
  union-find then joins the corners. Cells touching only along an edge or at a corner get split vertices with no
  special case.
- **colors** (parallel): face corners are bucketed by vertex and averaged over distinct cells, no sort.
- **regions**: with collapse, faces are bucketed by (direction, plane), which keeps each plane in row order, and the
  planes are swept in parallel; regions are joined in plane order, so the result is deterministic.
- **emit**: outlines and triangulation in parallel; then the bare mesh (vertices, triangles) is appended with no
  attributes attached, which is the cheapest the engine allows; then the attribute set is enabled and overlays are
  filled from flat arrays keyed by (vertex, direction).

Measured 2026-11-07 in the Development editor build, best of three runs
(`Automation RunTests PCGUtilsPerf.Voxel`, `Private/Tests/PCGVoxelBenchTests.cpp`):

| Shape | Cells | Plain | Collapse | Smooth color + collapse | Whole node, defaults | Before (whole node) |
|---|---|---|---|---|---|---|
| Solid 64³ | 262k | 11 ms | 4 ms | 10 ms | 15 ms | 62 ms |
| Terrain 256×256, 4–36 high | 1.3M | 124 ms | 117 ms | 168 ms | 167 ms | 881 ms |
| Random 50% of 48³ (worst case) | 55k | 86 ms | 96 ms | 138 ms | 88 ms | 361 ms |

What is left is mostly the engine: appending the terrain's 518k triangles to an `FDynamicMesh3` costs 34 ms bare and
65 ms with attributes enabled, before any overlay is filled. Reading 1.3M points into the grid takes 34 ms.
The serial vertex join (23 ms on the terrain) is the largest stage still under our control.

Later: skipping chunk interiors in the face pass.

### 3.9 Chunks
The mesh is the same for a chunked set and its expanded form, vertex for vertex. Faces, collapse, colors (Smooth
averages count cells, not points), UVs and offsets all read the expanded cell lookup. No `Chunks` setting.
Later speed-up: a chunk's inside has no faces, so the face pass can skip it.

### 3.10 Known limitation: pinched edges under an overhang
Found 2026-11-07 while testing Bevel. Two cells touching only along an edge, with empty cells on both other sides
(for example the ceiling of a pocket meeting the back wall of the cell behind it, with open space above), make the
surface pass along that edge twice between the same two corners. Blocks gives 4 open edges there (cells
`(0,0,0) (2,0,0) (0,1,0) (1,1,0) (2,1,0) (0,0,1) (1,0,1) (2,0,1)`). Bevel avoids it whenever the corners there are
trimmed, which keeps the two passes apart; it shows only when every amount at that edge is 0. Fix: give one of the
two passes its own corner vertices (or a midpoint), not yet done.

## 4. Voxel | Pack Points

A creative node. Each input point is a voxel that has "fallen out" of a grid. The node puts it back into a free
cell, fully or only part way. A typical result is a wall of cubes that is neatly packed at the bottom and scatters
into loose, tilted cubes further up.

### 4.1 Pins
- **In** (first, mutated). Any point data, any count. The output matches it point for point and keeps all of its
  metadata, tags and other properties (CLAUDE.md rule).
- **Grid** (required, exactly one point data). A voxel set (section 1). Its points are the **target cells** to fill.
  Zero or several Grid inputs is an error giving the count.
- **Out**. The In points, moved.
- **Unfilled Cells**. The Grid points that nothing claimed. Feed them to another Pack Points or a To DynMesh.

### 4.2 Assignment
Points are handled in input order; sort upstream to change priority. Each point:
1. Looks for the nearest unclaimed target cell whose centre is within `Max Radius` (world units, > 0). Ties go to
   the lower cell index, so results are deterministic.
2. Claims that cell. Each cell takes at most one point, so fully packed points never overlap.
3. A point with no free cell in range is **unpacked**. `Unpacked Points: Keep | Remove` (default Keep). Kept points
   are not moved and get `Packed = false`.

The lookup is a spatial hash over the target cells. Claimed cells leave the hash.

### 4.3 Packed pose
The fully packed pose is:
- Position at the cell centre.
- Rotation the grid's rotation, or the closest equivalent cube orientation. A cube looks the same under 24
  rotations, so the box turns the shortest way into its slot instead of spinning. With a non-cubic `VoxelSize`,
  only the rotations that swap equal-length axes count.
- `bSetBounds` (default true): bounds = ±VoxelSize/2 and scale = 1. Bounds are never blended. Every point is
  cell-sized whatever its settle amount.

### 4.4 Partial settle
Each point gets a settle amount from 0 (stays where it was) to 1 (fully in its cell). The node blends position
(lerp) and rotation (slerp) from the point's own pose to the packed pose.
- `Position Settle` and `Rotation Settle` are separate. Each is `Constant | Random | Attribute`.
  - Constant defaults to 1.
  - Random draws between Min and Max from the point seed plus the node seed.
  - Attribute reads a point attribute (name exposed), for example a height falloff made upstream.
- An attribute value outside 0..1 is an error naming the point index and the value. The node does not clamp it.

### 4.5 No overlaps
Required when `bSetBounds` is on, and on by default (`bPreventOverlaps`). Every output box is treated as a
cell-sized oriented box, and no two boxes may intersect.
- Fully settled boxes are placed first. They sit in distinct cells, so they never collide.
- Partial boxes are then placed from most settled to least. Each box is tested against those already placed
  (oriented-box separating-axis test through a spatial hash).
- A box that collides is pushed further toward its cell: its settle amount is raised in steps toward 1 until it
  is free.
- If it collides even fully settled (an earlier partial box drifted into its slot), `Unresolved Overlap` decides:
  - **Remove** (default);
  - **Keep** (writes `Overlapping = true`);
  - **Return** (put back at settle 0 if that is free, otherwise removed).
- The settle amount actually used is written out, so the change is visible.

### 4.6 Attributes written (names exposed, each can be turned off)
- `Packed` (bool).
- `VoxelTargetIndex` (int32): the claimed Grid point's index, or −1.
- `Settle` (float): the position settle actually used.
- `Overlapping` (bool): only with Unresolved Overlap = Keep.

### 4.7 Validation
- Grid contract errors (section 1).
- `Max Radius` ≤ 0 (give the value).
- `Random` with Min > Max (give both).
- A settle attribute is missing or not numeric (give its name and type).

### 4.8 Chunks
- **Grid** chunks: each covered cell is a separate target.
- **In** points with a `VoxelSpan`: `In Chunks: Whole Piece | One Cell` (default Whole Piece).
  - Whole Piece claims a free box of that span: a 2×1×1 brick fits only where two neighbouring cells are free,
    and its bounds cover both.
  - One Cell ignores the span and treats it as a single voxel.
- **Unfilled Cells** output: `Chunks: Keep | Expand` (default Expand). A partly claimed chunk always splits.

## 4b. Voxel | Cell Properties

Per-cell properties written as point attributes, in the style of PCGEx Path Properties: every output has its own
toggle and an editable attribute name, and all outputs are optional. A side is **open** when the face neighbour on
that side is empty. Directions are grid axes; grid +Z is up.

| Output (default name) | Type | On by default | Value |
|---|---|---|---|
| `IsSurrounded` | Boolean | yes | All six face neighbours filled. |
| `OpenSides` | Integer32 | yes | Bitmask: +X = 1, −X = 2, +Y = 4, −Y = 8, +Z = 16, −Z = 32. |
| `OpenSideCount` | Integer32 | yes | Open sides, 0 to 6. |
| `PositiveOpenSides` | Vector | yes | 1 on each axis whose + side is open, else 0. |
| `NegativeOpenSides` | Vector | yes | −1 on each axis whose − side is open, else 0. Positive + Negative = sum of the open sides' outward directions. |
| `OpenNormal` | Vector | no | That sum normalized: a rough surface normal; zero when nothing is open or open sides cancel. `Open Normal Space: Grid / World` (default World). |
| `OpenAxisCount` | Integer32 | no | Axes with any open side: 0 inside, 1 face, 2 edge, 3 corner. |
| `IsInnerCorner` | Boolean | no | No open side, but at least one concave edge or corner: the cell hides behind its face neighbours in an inside corner. |
| `ConcaveEdges` | Integer32 | no | Bitmask of concave edges. XY: +X+Y = 1, −X+Y = 2, +X−Y = 4, −X−Y = 8; XZ: 16, 32, 64, 128 in the same order; YZ: +Y+Z = 256, −Y+Z = 512, +Y−Z = 1024, −Y−Z = 2048. |
| `ConcaveCorners` | Integer32 | no | Bitmask of concave corners: bit value 1 << (1 if −X, + 2 if −Y, + 4 if −Z), so +X+Y+Z = 1 and −X−Y−Z = 128. |
| `ConcaveCount` | Integer32 | no | Concave edges plus concave corners, 0 to 20. |
| `ConcaveDirection` | Vector | no | Normalized sum of the directions towards the concave edges and corners (into the inside corner); zero when none or they cancel. `Concave Direction Space: Grid / World` (default World). |
| `NeighborCount` | Integer32 | no | Filled cells among the 26 around (faces, edges, corners). |
| `Depth` | Integer32 | no | Face steps to the nearest empty cell: a cell with an open side is 1, the next layer 2. |
| `CellsAbove` | Integer32 | no | Filled cells stacked directly above without a gap (layering: grass / dirt / stone). |
| `IsSkyVisible` | Boolean | no | No filled cell anywhere above in the column. |
| `ComponentID` | Integer32 | no | Connected component, numbered from 0 in cell order (Z, Y, X). `Connectivity: Faces / Edges / Corners` (default Faces). |
| `ComponentSize` | Integer32 | no | Cells in that component (for removing floaters). |
| `IsProtrusion` | Boolean | no | No box of 2 x `Protrusion Radius` + 1 cells fits around the cell inside the set: part of a spike, fin, thin wall or arm. Exactly what an opening (Offset Surface -R then +R) removes. |
| `CoreDistance` | Integer32 | no | Face steps through filled cells to the nearest core cell (one whose box of `Protrusion Radius` is all filled); 0 on the core, -1 when the cell's part has no core. Long protrusions have large values. |
| `Thickness` | Vector | no | Cells in the unbroken row through the cell along X, Y and Z, the cell included. |
| `BordersGap` | Boolean | no | A face neighbour is a gap cell: empty, but filled by a closing by `Gap Radius` (Offset Surface +R then -R): a slot, notch or hole narrower than 2 x R + 1. |
| `BordersCavity` | Boolean | no | A face neighbour is an enclosed empty cell (no face path through empty cells leads out of the set's padded box). |
| `CellIndex` | Vector | no | The whole-number grid index. |

**Concave** means an inside corner that face checks cannot see. A concave edge is an empty edge neighbour whose two
faces (between it and the cell) are filled. A concave corner is an empty corner neighbour whose three faces and three
edges are all filled; a corner behind an empty edge counts only as that edge, so a crease along Z reports one edge, not
edge plus two corners. Concave diagonals are reported for open cells too (a wall cell under a notch has a concave
edge); `IsInnerCorner` is the case with no open side. Use: offsetting a surface by its open sides misses these cells;
they are where a negative offset would have to split the box, or a positive offset of the neighbours closes the gap.

- **Shape outputs** (added 2026-11-15, item 3 of the Offset Surface queue, from the designer's analysis sketch
  `Monkey Golf Docs/Excalidraw/Attachments/Pasted image 20261115122454.png`): `Protrusion Radius` and `Gap Radius`
  (default 1, cells) set the feature size; `Shape Axes` (default X | Y | Z) the axes they are measured along, so X | Y
  finds thin features and slots in plan (a one-cell plate is solid in plan, all protrusion in 3D). The sketch's
  protrusion test B ("distance to any cell with depth 2 is greater than X") is `CoreDistance > X` with radius 1.
  These four (not Thickness) work on a dense box of the set's bounds padded by max(Protrusion R, 2 x Gap R) + 1, capped
  by `Max Shape Cells` (default 64M; over it the input fails with an error naming the box). The morphology is the
  shared `Voxel/PCGVoxelMorphology` library (section 5). Thin gaps are flagged on the cells next to them; the gap
  cells themselves become points in item 4 (not built).
- **Output:** the input with the attributes added: points, metadata, tags and `@Data` carry over (written through
  `PCGVoxelPointIO::WriteCells`).
- **Chunk rule:** every property is per cell, so a chunked input comes out **expanded** to one point per cell (each
  copying its chunk), with the span attribute removed. The values equal those of the expanded set, cell for cell.
- Points dropped by `Duplicate Cells: Keep First / Keep Last` are left out, as in Expand.
- **Validation:** an enabled output with an empty name, or two enabled outputs with the same name, is an error naming
  the settings involved; nothing is written.

## 4c. Voxel | Shift Seams

Makes the boxes of a voxel set uneven while they still fill exactly the same space: input a clean grid of boxes,
output non-uniform boxes in the same volume. Typical chain: cells → `Voxel | Compress Random` (decides which seams
are long and which are short) → `Voxel | Shift Seams` (moves them). Plain cells work too, but then every seam is the
face between two single cells and the result looks like a jittered grid.

**Seam.** On one grid axis, the faces of some boxes that lie on one plane (the Low boxes, whose high face is there)
and the faces of the boxes across from them (the High boxes), joined through every unit face they share. Moving a
seam by d grows the Low boxes by d and shrinks the High boxes by d (or the reverse), so no gap or overlap appears at
the seam itself. A seam with any unit face on the outside never moves, so the outer shape never changes.

**When a seam may move.** Seams move one at a time, in a seeded random order. A seam moves only when, at that moment,
its two sides cover exactly the same area of its plane (sum of Low face areas = sum of High face areas = sum of their
overlaps). Then the slab it sweeps belongs wholly to the boxes it shrinks, and no other box is touched. Moving a seam
can make a crossing seam's sides stop matching (for example in a 2×2 grid, once one row's X seam moves, the Y seam
between the rows no longer matches); that seam then stays. In practice 90 to 100% of boxes change (benchmark:
330k boxes of terrain in about 0.4 s, 8k boxes in 10 ms).

An earlier rule ("a seam is pinned if any edge-adjacent face touches the outside") was safe but froze almost every
seam of a lumpy shape; the area test replaced it.

| Setting | Default | Meaning |
|---|---|---|
| `Shift X / Y / Z` | all on | Which axes' seams move. Bricks: Compress Random with spans N×1×1, then shift X only (or all). |
| `Strength` | 0.5 | How far a seam may move, as a fraction of its `Limit` (0 to 1). A point shorthand (2026-11-10): a point attribute (default `SeamStrength`) gives each box its own, and a seam uses the weakest box on either side (0 pins every seam of that box), for masking; `@Data` one per set. |
| `Limit` | Thinnest Box, Either Side | Added 2026-11-10 on request: the reach of one seam. **Thinnest Box, Either Side**: the full thickness (along the seam's axis, current sizes) of the thinnest box on either side, so chunks against chunks move far and anything next to a single cell moves at most a cell. **Half the Thinnest Shrinking Box**: the first version (half the thinnest box on the side that shrinks). |
| `Source` | Random | Random: a value per seam from the node seed and the seam's lowest face cell. Noise: 3D Perlin noise at the seam's centre (world space), so nearby seams move alike. |
| `Noise Scale` | 400 | Noise only. Feature size in world units; must be > 0 (error naming the value). Input shorthand: constant or `@Data.SeamNoiseScale`. |

- **Output:** the input point for point (metadata, tags, `@Data`, positions kept); only bounds change. Cells and
  spans are unchanged, so every other Voxel node still reads it. Bounds are written along each point's local axes,
  matched to grid axes through the point's rotation (unrotated or 90° steps against the grid).
- The moved faces are hidden, so `Voxel | To DynMesh` gives the same shell. Its inside-out offset check now looks only
  at sides that show, so shifted seams never trip it.
- Running it twice is valid (each pass reads the current bounds).

## 4e. Voxel | Transform Grid

Built 2026-11-09 from the designer's idea (a corridor split into slices, each slice turned a little more than the
last, like a twisting tiled tunnel). The graph splits a grid into parts first and writes each part's values to its
`@Data`; this node then moves, turns and scales each input set as a whole. One output per input.

- The set's grid transform (`Grid Transform Attribute`, written even when the input had none) and, when scaled, its
  `Voxel Size` attribute are updated, and every point moves with them (position, rotation, scale). Cells keep their
  indices, spans and per-cell values, so the output is a valid voxel set at any angle and every Voxel node reads it
  (a turned slice meshes on its own grid).
- The output is the input copied point for point (metadata, tags, `@Data` kept), per the mutated-input rule.
- `Space: Grid | World` (default Grid) for Translation, Rotation and a Location pivot. Grid means the set's own axes,
  so "roll about X" turns about the corridor whatever the grid's world rotation.
- `Translation` (Vector), `Rotation` (Rotator), `Scale` (Vector, grid axes, every axis > 0): Constant or `@Data`
  shorthands (defaults `@Data.Translation`, `@Data.Rotation`, `@Data.Scale`). Order: scale and turn about the pivot,
  then translate. Scale multiplies Voxel Size, so cells stay cells; anything but 1 needs Voxel Size read from `@Data`
  (it writes the new size there), else an error. Point scale follows the grid axis each point axis lies on (exact for
  points at 90° steps against the grid).
- `Pivot: Bounds | Location`, per set:
  - **Bounds** (default): `Pivot Alignment`, a fraction of the set's cell bounds per grid axis (0 min side, 0.5
    centre, 1 max side, outside 0-1 allowed), Constant or `@Data.PivotAlignment`. This covers the justification
    choices (Min / Center / Max / Custom) as one value that can differ per set.
  - **Location**: `Pivot Location` in Space, Constant or `@Data.Pivot`.
- Errors: each shorthand's own (missing or wrong-typed `@Data`), `Scale {axis} is {v}; every axis must be greater
  than 0.`, and scaling with a Constant Voxel Size.

The math, in the input grid's space with pivot P, rotation R, scale S and translation T: `x -> P + R(S*(x - P)) + T`.
The new grid has rotation `G.R * R`, origin `G(P - R(S*P) + T)` and Voxel Size `S * VoxelSize`; a point at grid
position x goes to `NewGrid(S * x)`. World-space values are converted first: `R = G.R^-1 * Rw * G.R`, `T = G.R^-1 * Tw`.

## 4n. Voxel | Separate Islands

Built 2026-11-20 on request, for spiralling columns and anything else that needs islands apart. The designer chose a
separate node over a "per island" mode on Transform Grid: islands turned by different amounts cannot share one grid,
so a fused node would emit one set per island anyway, which is this node followed by Transform Grid.

- **One output set per island**, input by input, islands numbered in cell order (Z, then Y, then X, by each island's
  lowest cell). Each set is the island's points copied unchanged (chunks stay chunks, nothing moves) from the input,
  with the input's metadata, `@Data` and tags (initialized from the input, per the mutated-input rule).
  `IsSurrounded` stays valid: a cell's face neighbours are always in its own island.
- `Connectivity: Faces | Edges | Corners` (default Faces), the same enum as Cell Properties' components.
- `Min Cells` (default 1): smaller islands are dropped; the rest are numbered without gaps.
- A point Duplicate Cells ignored goes with the island of the cell at its first cell (Offset Surface's split cells
  stay together).
- **Re-anchor Grid** (default on): each island's `Grid Transform Attribute` origin moves to a cell of the island picked
  by `Anchor Alignment` (Constant or `@Data.AnchorAlignment`, default 0.5 = centre; same meaning as Transform Grid's
  Pivot Alignment). No point moves, only the cell numbering. The origin then *is* the island's pivot: Transform Grid
  with `Pivot = Location`, `Space = Grid`, `Pivot Location = 0` turns each island about its centre, and any set later
  cut from an island (slices of a column) keeps the island's grid and so turns about the same axis. This is why there
  is no `@Data.Pivot` output (designer, 2026-11-20).
  - The origin must be a cell centre, so on an axis where the island is an even number of cells wide the anchor is half
    a cell off the true centre (rounded toward the max side). Agreed to accept for now; if twisted columns wobble, add
    an optional residual-offset attribute (0 or 0.5 cells per axis) or an offset on a Transform Grid "Grid Origin" pivot.
- Optional `@Data` outputs, each with its own toggle and name: `IslandIndex` (Integer32, on), `IslandCount`
  (Integer32, islands from that input after Min Cells), `IslandCellCount` (Integer32), `IslandCenter` (Vector, world,
  exact bounds centre, not snapped).
- Errors: an enabled output with an empty name, two outputs sharing a name, an output named like the grid transform or
  Voxel Size attribute, Re-anchor Grid with an empty Grid Transform Attribute, and the Anchor Alignment shorthand's own.

Island labelling is the shared `Voxel/PCGVoxelIslands` library (`PCGVoxel::FindIslands`, `HalfNeighbourSteps`);
Cell Properties' Component ID / Component Size now use it too. New nodes that need islands must use it.

## 4o. Voxel | Slice

Built 2026-11-20 on request, the second half of spiralling columns: Separate Islands (re-anchored) -> Slice ->
something that sets `@Data.Rotation` from `SliceFraction` (or `SliceIndex`) -> Transform Grid with `Pivot = Location`,
`Space = Grid`, `Pivot Location = 0`. Every slice keeps its input's grid transform, so all slices of one island turn
about that island's centre.

- **One output set per slice that has cells**, input by input, lowest slice first. Points are copied from the input
  (metadata, `@Data`, tags, grid kept; initialized from the input per the mutated-input rule).
- `Axis: X | Y | Z` (grid axis, default Z), `Thickness` in cells (default 1; below 1 is an error naming the value).
- `Start: Set Bounds | Grid Origin`. Set Bounds (default): slice 0 starts at the set's lowest cell. Grid Origin:
  boundaries at multiples of Thickness in cell index, indices may be negative; after re-anchoring, slice 0 holds each
  island's centre cell.
- **Chunks crossing a cut are split.** Each piece is a copy of the chunk (all attributes, unique metadata entry) with
  its `VoxelSpan`, position and bounds cut to the slice. Outer sides keep the chunk's surface offset; a cut side gets
  the natural extent. Only the point's local axis nearest the grid axis changes (points are assumed aligned with the
  grid, as `ComputeSurfaceOffsets` assumes). Pieces get the Volume Sampler seed of their first cell; whole points keep
  their seed. Written by hand rather than `PCGVoxelPointIO::WriteCells`, which would put surface offsets on cut faces
  and rewrite Offset Surface's several-boxes-per-cell points as one box.
- A point Duplicate Cells ignored goes to the slice(s) of its own box, like any other point.
- `IsSurrounded` (when the input has it) is rewritten: `Cut Faces Are Open` (default on) judges each slice alone, so
  cells on the cuts show (slices are usually turned apart); off judges against the whole input set.
- Optional `@Data` outputs, each with a toggle and name: `SliceIndex` (Integer32, on), `SliceFraction` (Double, on: 0
  lowest to 1 highest slice of that input, by index, 0 if only one), `SliceCount` (Integer32, lowest to highest index,
  empty slices counted), `SliceDistance` (Double: the slice's middle along the axis from the grid origin, cell index x
  Voxel Size; equal heights give equal values across sets sharing a grid). Same name checks as Separate Islands.
- Not built: a node that writes a Rotation from a fraction (any PCG attribute math does it today), and a combined
  Twist mode on Transform Grid (only if the chain proves tedious).

## 4f. Voxel | Air Flow

Built 2026-11-09 from the designer's request: a smoothed vector field of air flowing around a voxel set, using the
dense grid. Design choices were left to me; these are them.

**What it is.** Potential flow plus a wind shadow, on a staggered (MAC) grid of air cells:
1. **Air box.** The set's cell bounds, cut into air cells of `Air Cell Voxels` voxels per side (1 to 16), plus
   `Padding` air cells on every side. With `Ground At Bottom` the box stops at the lowest cells and its floor is a wall
   (terrain); otherwise there is air all round (floating shapes). An air cell is solid when more than `Solid Fraction`
   of its voxels are filled (default 0: any filled voxel, so thin walls still block). Over `Max Air Cells` (default 4M,
   about 60 bytes each while running) is an error that names the box size.
2. **Sealed air.** Air that cannot be reached from the open edge of the box (inside a hollow shape) is still, with
   shelter 1.
3. **Wind shadow.** From each air cell the node marches upwind, one air cell per step along the wind's main axis, up
   to `Wake Length` steps. Hitting a solid at step s gives `Shelter = 1 - (s - 1) / Wake Length` (1 right behind it).
4. **Starting wind.** `Wind * (1 - Wake Strength * Shelter)` plus optional turbulence: the curl of a three-channel
   Perlin field (no sources or sinks), size `Turbulence Size`, strength `Turbulence Strength` times the wind speed,
   seeded by the node seed.
5. **Smoothing.** `Smooth Passes` of a 1-2-1 blur along each axis over moving air only.
6. **Solve.** Face velocities are averaged from the cells; a face touching a solid (or the floor) is closed and holds
   0, so no air ever enters a solid. A pressure solve (conjugate gradient, Jacobi preconditioner, pressure 0 outside
   the box, so its edges are open) removes every source and sink; the air shadowed in step 3 is pushed over and round
   instead, which is what makes air speed up over tops and corners. It stops at `Tolerance` (fraction of the starting
   error) or `Max Iterations`, with a graph warning in that case. Measured: 32 to 160 steps, so the default cap of 300
   is rarely hit.
7. Cell velocity = the mean of its two faces per axis.

**Wind** is a vector shorthand (Constant or `@Data.Wind`, default constant (500, 0, 0)): direction, with its length as
the speed, in `Wind Space` (World or Grid). Zero is an error unless the Sources pin has data (sources alone drive the
air; no wind shadow then).

**Sources pin** (added 2026-11-09, the designer's idea: points as force fields, instead of or on top of the wind).
Optional, last pin. Any point data, not a voxel set; every source acts on every voxel set whose air box it overlaps.
Each point can carry any mix of three optional attributes (names are settings); a sources input with none of them is an
error naming all three:
- `FlowForce` (Vector): a push, the speed added at the centre (fan, jet), in `Force Space`: World, or Point (the
  point's rotation turns it, so aiming the point aims the fan). Added to the starting air, so the solve keeps its
  swirling part and sends air round in a loop: a jet also pulls air in behind it.
- `FlowOutflow` (Double): air made (positive, a vent) or taken away (negative, suction), given as the outward speed
  at the Radius with nothing in the way. This one cannot go in the starting air, because the solve removes every source
  and sink. It goes into the solve's right-hand side instead (total `Outflow × 4πR²`, spread over the air cells in
  reach by falloff weight, or into the one cell holding the source if it is smaller than a cell), so the air really
  leaves through the open box edges. Sources in solid or sealed air do nothing.
- `FlowSwirl` (Double): swirl speed about the point's up axis, counter-clockwise from above for positive values; tilt
  the point to tilt the vortex. Solid-body inside 0.3 R, so the centre has no spike.

Reach: `Radius From Bounds` (default on) uses the point's largest scaled half-size; off reads `FlowRadius` (Double,
required then, > 0). `Source Falloff`: Smooth `(1 - (d/r)^2)^2` (default), Linear, Constant. `Source Strength`
multiplies all three. Sources are applied after turbulence and before smoothing. Errors: no effect attribute, zero
bounds radius, missing or non-positive radius attribute, wrong attribute types.

**Flow pin.** One point per non-solid air cell (only those within `Max Distance` face steps of a solid when that is
above 0), ordered Z, Y, X. It is a voxel set: `@Data` gets the air cell size in the Voxel Size attribute name and a grid
transform whose cell c centre is c × air cell size, so Compress, To DynMesh and the rest read it. Points are fresh
(they are not a modification of the input), but carry the input's tags. Attributes, each name a setting:
`Flow` (Vector, always), `FlowSpeed` (Double), `Shelter` (Double), `SolidDistance` (Integer32, off by default). Vectors
are in `Output Space` (World default).

**Cells pin.** Only made when connected (or when there is no node, in tests). The input copied point for point, with:
- `WindExposure` (Double, 0 to 1), for the most hit open side: the starting air just outside it (wind less its
  shadow, plus source pushes and swirls, after smoothing, before the solve) blowing into the side, over the reference
  speed (the wind speed, or the strongest source when there is no wind). It uses the starting air and not the solved
  flow, because the solved flow into a wall is 0 by construction. With only wind and Wake Strength 1 this is how square
  the side faces the wind times `1 - Shelter`.
- `SurfaceFlow` (Vector): the mean flow in the air just outside the point's open sides. Air cells that are solid only
  because a coarse cell also holds other voxels are stepped past (up to 3 air cells).

**Speed** (`PCGUtilsPerf.Voxel.AirFlow`, turbulence on, ground on): 1.3M-cell terrain 2.7 s at 1 voxel per air cell
(1.5M air cells), 0.76 s at 2, 0.58 s at 4; a 64³ solid 0.16 s at 1.

**Not done (ideas).** No time stepping or vortex shedding (it is not a fluid simulation); no upwind lift of shadows
off the ground; no multigrid (CG cost grows with box size, fine at the sizes measured); no thermal or per-set source
terms. A `Voxel | Sample Flow` node to sample the field at arbitrary points (trilinear) would make it easier to drive
foliage that is not on the grid.

## 4g. Voxel | Rasterize

Built 2026-11-09 from the designer's request, for the Color Theory course: embed organic (non-voxel) green geometry in a
voxel grid that surrounds it. The graph rasterizes the green onto the surrounding grid, cuts those cells out with
Voxel | Boolean, meshes what is left, and the DynMesh boolean against the green makes it sit flush. The name is
Rasterize, not Voxelize, because the palette contract bans the category word inside the element name; "voxelize" is in
its search keywords.

**Source** (`Mesh | Path`, default Mesh). The In pin takes both data types; an input of the other type is an error.
- **Mesh**: DynMesh data, read in the target actor's space by convention (`Mesh In Local Space`, default on; off reads it
  as world).
- **Path**: point data read as one closed loop, points in order, the last joined back to the first (a repeated first
  point is dropped). It is filled in the grid's XY plane, over the grid layers its points span, plus `Cells Below` and
  `Cells Above`. Fewer than 3 distinct points is an error.

**Path height** (`Path Height`, added 2026-11-12). Every mode but Flat makes the flat one-layer mask first, then gives
each output column the Z of the nearest point on the path: nearest in the grid's XY plane, on any segment of the path
or of its overlapping holes, Z interpolated along that segment (distances in grid units; segments bucketed on a coarse
2D grid searched ring by ring).
- **Flat** (default): the old behaviour, the mask stacked over the layers the points span plus `Cells Below` / `Cells Above`.
- **Snap**: one full cell, the layer whose centre is nearest the path's Z.
- **Trim**: one cell, the one holding the path's Z (`ceil(z - 0.5)` in cells), its `BoundsMax.Z` lowered so its top face
  is exactly at the Z. Between just over 0 and 1 cell thick.
- **Best Fit**: the Trim cell plus one full cell under it, so the column is 1 to 2 cells thick with its top on the path.
- `Extrude Down` (non-Flat, default off): every column filled down to the lowest cell of the whole shape.
- `Extra Cells Below` (`Integer32Abs` point shorthand, default constant 0, attribute `ExtraCellsBelow`): that many more
  cells under every column's bottom, any height mode (in Flat it adds to `Cells Below`). `@Data` gives one value per
  path. A per-point attribute warns and uses 0; planned: read it per column from the nearest segment, interpolated
  between the segment's two points (TODO in the element).
- `Path Z Attribute` (optional Double, default `PathZ`, off by default): the world Z of that nearest path point, the
  same for every cell of a column, for refining the surface downstream. Must differ from `Mode Attribute`.

**Hole paths** (`Use Hole Paths` + `Hole Tag`, default `Hole`, added 2026-11-12). Path inputs carrying the tag are holes:
each is cut from every shape it overlaps (shares a cell with the shape's Fill or Intersect cells). A cell whose centre
is in a hole loses Fill, and the shape's own line there too unless the hole's line also passes through it; the hole's
line is Intersect where it runs through the shape, and Surround grows after the cut. Holes take part in the nearest-
segment search, which is why they live on this node rather than in a later Boolean: a hole shapes the heights around
it. Holes output nothing; a Grid with one set per input then means one per shape. All inputs being holes warns and
outputs nothing; an empty Hole Tag, a hole that is not point data, or a hole with under 3 points is an error naming it.

**Modes** (bitmask, any mix, at least one; default Fill + Intersect):
- **Fill**: cells whose centre is inside. Rays along X through each row of cell centres, nonzero winding, so it does not
  care which way the faces point. Rays are nudged off the centre lines by a tiny odd amount so they never run along an
  edge. A mesh that is not closed leaves rows with more crossings one way than the other; those rows skip their open run
  and the node warns with the count (only when Fill or Surround is on).
- **Intersect**: cells the surface passes through (exact triangle-box test; segment-square for a path). `Touch Margin`
  (fraction of a cell, default 0) widens or narrows the cell for this test. At 0 a surface lying exactly on a cell face
  touches neither neighbour (1e-4 of a cell counts as on the face), so a mesh built on the grid gets one layer, not two.
- **Surround**: a shell `Surround Cells` thick (default 1) grown from the Fill and Intersect cells through cells that are
  neither. `Surround Neighbors`: Faces (6, or 4 for a path), Faces And Edges (18, or 8), All (26, or 8; default: no gaps).

Intersect and Fill are always worked out, so `RasterMode` (Integer32, optional, name a setting) holds the true bits per
cell: 1 inside, 2 touched (a surface cell with its centre inside is 3), 4 surround (only when Surround is on).

**Grid.** The optional Grid pin (last) takes a voxel set; its Voxel Size and grid transform are read through the Grid
settings (`@Data` by default) and its cells are ignored. One Grid set serves every input, or one per input. Without
it, `Voxel Size` (default 100) and `Grid Transform` (rotation and location; identity matches the Volume Sampler) place
the grid. A box of more than `Max Cells` cells (default 64M; the shape's bounds plus the shell) is an error naming its
size.

**Out.** One fresh voxel set per input (per shape, with hole paths): plain cells ordered Z, Y, X, bounds ±Voxel Size/2 (Best Fit and Trim lower the top cell's Max Z), Volume Sampler seeds, the
Voxel Size and grid transform written to `@Data`, the input's tags. It is not a modification of the input, so no
metadata is carried.

**Speed** (DebugGame build): a 260k-triangle sphere 50 cells across, all modes, 85k cells in 0.26 s.

## 4h. Voxel | Boolean

Built 2026-11-09 (planned as phase 2). The engine's Difference works on bounds overlap and is too sensitive for cells
(touching faces and rounding change the answer). This matches cells by index on In's grid, so a cell is simply in or
out.

- Pins: `In` (mutated, required), `Operand` (required). All Operand inputs together are one set of cells; one output per
  In input.
- `Operation`: Union, Intersect, Subtract (default).
- Operand sets are placed on each In set's grid: their Voxel Size, turned into In's grid axes, must match (error giving
  both), and every Operand cell centre must land within Grid Tolerance of an In cell centre (error naming the cell and
  point: "the two grids do not line up"). Any whole-cell offset or 90° turn between the grids is fine. Rasterize with In
  on its Grid pin always lines up.
- Out is In, mutated (CLAUDE.md rule): its points, metadata, tags and `@Data`. Chunks stay whole where none of their
  cells changed; a cut chunk becomes plain cells (Volume Sampler seeds for the new points).
- Union adds Operand cells In lacks, after In's own points, ordered Z, Y, X. `Added Values`:
  - **Operand, Then Nearest** (default): the Operand cell's attributes, color, density and steepness; attributes In has
    but the Operand lacks (or has with another type) come from the nearest In cell. Operand-only attributes are added
    to the output; In's own cells get their default.
  - **Nearest**: everything from the nearest In cell (by centre distance; hash grid, then a scan for far islands).
  - **None**: attribute defaults; properties from the nearest In cell.
  An In set with no cells has nothing to take values from, so Union outputs it unchanged with a warning.
- **Speed** (DebugGame): a 216k-cell block against the 85k-cell sphere, Subtract 0.31 s, Union 0.39 s.

## 4i. Voxel | Offset Surface

Built 2026-11-10 from the designer's request. Their graph offset cells by adding Cell Properties' open-side vectors to
the bounds (`BoundsMax += PositiveOpenSides * Amount`, `BoundsMin += NegativeOpenSides * Amount`). That is limited to
under one cell, and leaves inside-corner cells sticking out on a shrink. This node knows the grid, so the offset can
be any size and corners come out right. It takes over the planned phase-2 `Dilate / Erode`.

- `Amount` (default 0.25) x `Amount Scale` (default (1, 1, 1); a vector since 2026-11-16 on request, multiplying
  Amount per axis: (1, 1, 0) leaves tops and bottoms alone, (1, 1, 2) moves them twice as far, a negative axis
  shrinks along it while the others grow, and the sideways growth then takes the shrunk height; a number attribute is
  used on all three axes, and graphs saved with a number scale load it on all three). Both are point shorthands (changed 2026-11-10 on request; see
  `PCGUtilsInputShorthands.md`): a constant, a point attribute or property (one value per cell; a chunk's cells share
  its point's value; defaults `OffsetAmount` and `AmountScale`) or `@Data` (one value per set). Positive grows,
  negative shrinks. With one value for the set it is a box
  (L-infinity) offset of the whole set: the whole part adds or removes that many layers (corners stay square), and
  the fraction is part cells. **No box ever leaves its cell** (rule from the designer, 2026-11-15): `+3.5` adds three
  layers, then a layer of cells half deep. `-1.25` removes one layer and pulls the open faces of the cells left in a
  quarter cell; a 5x5x5 block becomes a 2.5-cell cube.
- `Directions` (bitmask +X, -X, +Y, -Y, +Z, -Z, default all six; replaced `Axes` on 2026-11-16 on request, and older
  graphs keep both directions of each axis they had): the directions the offset moves along. A direction left out
  gets no offset at all: no layers added or removed that way, its faces stay put, and inside corners are not carved
  towards it. -Z alone grows or shrinks the bottom faces only; the four sideways directions offset sideways only (one
  cell +1 is a 3x3 plate; -1 on a 3x3x3 keeps the middle column). A growing cell's spare reach is measured over the
  axes it grows along, in the direction of the cell it reaches.
- `Units` (added 2026-11-10 on request): **Cells** (default; 1 is one cell width per axis) or **Absolute** (world
  units, turned into cells per axis by Voxel Size, so non-cubic cells get per-axis layers and fractions).
- **Per-cell values** (either setting from a point attribute): falloff and masking (a scale of 0 leaves a cell
  alone, negative flips it). Each cell's own product decides: a shrinking cell goes when an empty cell is within its
  whole layers (box, per axis; a summed-volume table of the empty cells answers it in O(1)). A growing cell's
  **reach** is its own box grown by its amount on each axis; the reach is clipped to every empty cell it touches
  (stamped, so cost is cells times reach cubed). A cell covered whole is filled and copies the grower with the most
  reach to spare, then the closest. A cell covered only in part keeps the **union** of the parts reaching it: cut at
  every part's faces, each piece goes to the part with the most reach to spare there (then the closest, then input
  order), and pieces are merged greedily into boxes within one grower, so each box copies its own grower. An inside
  corner is an L of 2 boxes, one from each wall; a one-cell gap between two cells growing 0.75 is 2 boxes meeting
  where the reaches cross. This answers "which cell supplies the offset at an inside corner": every grower supplies
  its own part, with its own amount. Growth fills only cells empty in the input, not cells a neighbour removed. With
  one value everywhere this is exactly the box offset above. Until 2026-11-15 a growing fraction stretched the cell's
  own box over its open faces, so two cells could stretch into the same empty cell (inside corners, diagonal
  neighbours, gaps under twice the fraction) and overlap; the field bug that set the rule above.
- **Fraction.** Written to the point bounds: the point stays on its cell centre and its bounds cover the box (in the
  point's frame: its rotation and scale are allowed for). Any bounds the input had are replaced. A growing cell stays
  whole (its fraction is the part cells around it). A shrinking cell moves its open faces in; only a whole cell next to
  a face holds it (a part cell from a neighbour's growth does not). Shrinking with `Split Inner Corners`
  (default on) takes away everything within the fraction of every empty neighbour among the 26, so cells in inside
  corners and under notches are carved too: each cell is cut at its faces and the planes the fraction in from them
  (up to 3x3x3 pieces), and the kept pieces are merged greedily into the fewest boxes found (an inside edge gives 2, an
  inside corner 3). Each box is a point copied from the cell. Off: one box per cell, open faces only (the old look).
- **Min Thickness** (added 2026-11-15 on request, for thin floating slivers a shrink leaves; off by default:
  `Limit Thinness`). Along each axis the whole cells after the layers form runs, and only a run's two end faces move,
  so its thickness after the shrink is its length less its end cells' fractions. A run the fraction leaves under its
  minimum (in Units; the largest of its cells') gets `Thin Result`: **Clamp** (default; the end cells shrink less,
  sharing the cut by their fractions, at most back to no shrink) or **Remove** (its cells are deleted). Only runs the
  fraction made thinner count: whole layers removed stay removed, and a wall already thin in the input is left alone.
  `Min Thickness` is a point shorthand (default constant 0.25, attribute `MinThickness`): per cell, 0 means never
  clamp or remove this cell, which is the per-cell switch the designer asked for (a Boolean shorthand does not exist;
  a number covers it and allows different minimums). A cell clamped on some axis is flagged `WasClamped` (optional
  Boolean output, `Was Clamped Attribute`). Corner carving uses the clamped fraction. Growth is not limited (a grown
  part box is always attached to its grower).
- **Compression** (added 2026-11-15, items 5 and 5.2 of the queue; both off by default). `Compress Added` writes the
  cells the offset adds as chunks instead of a point per cell: whole cells added by one cell (they copy it, so nothing
  is lost) merge as octree cubes, then boxes; part boxes from growth join along the axes they fill, with part boxes of
  exactly their shape (a face's half-deep slabs become one chunk). `Compress Equal Cells` does the same for every
  whole cell and lone part box of the output, the input's own included, joining cells equal under `Match Attributes`
  (All, or Listed `ListedAttributes`: with only the offset attribute listed, cells join by offset value and other values
  come from one cell per chunk). `Compress Max Span` (default 16) caps a chunk per axis. Several boxes on one cell (split
  corners) stay single. The span attribute is written when any chunk forms; `Voxel | Expand` turns chunks back into
  cells. The merging is the shared `Voxel/PCGVoxelChunking` code Voxel | Compress uses. Measured: one cell +3 is 13
  points instead of 343; +1.5 is 33 instead of 125, and meshes to the same closed 4-cube.
- `Removed Cells` (negative, a cell or more): **Delete** (default) keeps each remaining cell's own values; **Push**
  gives each newly exposed cell the values of the nearest cell of the old surface (hash grid, then a scan), so the surface sinks with its
  look. The geometry is the same.
- `Added Values` (positive, a cell or more): **Nearest** (default; the cell that grew into it) or **None** (default
  attributes; native properties still from that cell).
- **One pin** (changed 2026-11-14 on request: the separate Spawn pin went once two points on one cell stopped being
  an error, section 1). **Out** is In, mutated (CLAUDE.md rule): metadata, tags and `@Data` kept, chunks expanded
  (the span attribute removed), cells ordered Z, Y, X. Each box is inside its own cell (bounds as surface offsets). A
  cell that takes more than one box (a grown inside corner, or a cell carved at an inside corner) is several points on
  one cell, the largest first, each with `WasSplit` = true (optional Boolean output, `Was Split Attribute`; a cell
  carved down to one box is flagged too). Other voxel nodes read such a cell as one filled cell; `Voxel | To DynMesh`
  (Surface Offset From Point Bounds, Exact offsets) meshes the union of all the boxes (section 3.5), so the mesh is the
  exact shape of the boxes, notches and exposed undersides included (tested for carved corners, part cells and a grown
  L, and by `OffsetSurfaceOutputRandomSets`: 297 random sets, exact volume and area, no slivers).
  `IsSurrounded` is rewritten (section 1.3): only whole cells hide a face, and it is false on carved and part boxes;
  filter on it to spawn only visible boxes (the old Spawn Only Visible). Growing makes more points than before
  2026-11-15: one cell +0.25 is 27 points (itself, 6 slabs, 12 bars, 8 corner cubes) where it was one stretched box.
- Error when the box worked over (bounds plus grown layers plus one) is over `Max Cells` (default 64M), naming its
  size.
- **Queued (designer, 2026-11-15):** (3, built 2026-11-15 as Cell Properties' shape outputs, section 4b); (4, built
  2026-11-15 as `Voxel | Fill Gaps`, section 4l); (5 and 5.2, built 2026-11-15 as Compression above); (5) a
  `Compress` flag that writes each added run of whole cells as one chunk instead of a point per cell; (5.2, default
  off) also join neighbouring cells with the same offset into box chunks in the same pass. The chunking must be the
  shared code `Voxel | Compress` uses, moved out of `PCGVoxelCompress.cpp` into a `Voxel/` library first (designer:
  no element reimplements compression).

## 4l. Voxel | Fill Gaps

Built 2026-11-15 (item 4 of the Offset Surface queue; the designer's sketch's "thin gap, option 1: fill it" and "flood
fill gap"). Uses the shared `Voxel/PCGVoxelMorphology` library.

- `Fill Gaps` (default on): fills the empty cells a closing by `Gap Radius` (default 1) fills, measured along `Axes`
  (default X | Y | Z; X | Y fills slots seen in plan): slots, notches and holes narrower than 2 x R + 1 cells. A slot
  three wide stays at radius 1 and fills at radius 2.
- `Fill Cavities` (default off): also fills enclosed empty cells of any size (no face path through empty cells leads
  out of the set's padded box). A cup open at the top is not a cavity.
- **Out** is In, mutated (CLAUDE.md rule): its own points as they were, chunks kept (span written when the input was
  chunked), plus one point per added cell. **Gaps** has only the added cells, as a voxel set of their own (same grid,
  metadata and tags). Added cells copy the nearest filled cell, found breadth-first by face steps through the added
  cells (a face neighbour where there is one), or the nearest by distance when no such path exists; `Added Values: None`
  gives them attribute defaults instead. Whole-cell bounds. `IsSurrounded` is rewritten on both pins.
- Outputs on both pins: `IsGap` (Boolean, default on: true on added cells) and `IsCavity` (Boolean, default off: true
  on added cells inside an enclosed cavity, also when Fill Gaps filled them because the cavity is small).
- Errors: an enabled output with an empty name, the two outputs sharing a name, and a working box (bounds padded by
  2 x Gap Radius + 1) over `Max Cells` (default 64M), each naming the setting or the box.
- Tests (`PCGUtils.Voxel.FillGaps`): a one-cell slot in plan comes back (4 cells, copied from a face neighbour,
  whole-cell bounds); a three-wide slot needs radius 2; a 3-wide hollow is no gap but Fill Cavities fills its 27, all
  flagged; a cup is no cavity; a one-cell hollow is filled as a gap and flagged a cavity; chunks are kept; both off
  passes through; shared output names fail.

## 4k. Voxel | Extrude

Grows or cuts each cell along a direction (added 2026-11-13, from the designer's spec). Out is In, mutated, and still
a voxel set: one point per cell (a chunked set is expanded), metadata, tags and @Data kept, `IsSurrounded` rewritten.
Cells are in Z, Y, X order.

- **Direction** (point shorthand, Vector, default constant (0, 0, 1), attribute `ExtrudeDirection`) and **Amount**
  (point shorthand, Double, default constant 1, attribute `ExtrudeAmount`). Per axis, steps = |Direction × Amount|
  rounded up to whole cells, with a 1e-6 tolerance so 2.0000001 stays 2. Fractions round away from zero for
  negatives too, so -1.5 removes 2. Each axis goes the way Direction's sign points, so one call never grows up and down.
- **Amount > 0** adds that many cells beyond the cell. **Amount < 0** removes the cell itself and that many beyond it
  (-1: itself and its neighbour). **0**, or Direction (0, 0, 0), does nothing, so no setting removes only the cell.
- **Separate Axes** (default on): each axis is its own straight run (1, 0, 1 makes an L: two runs, not the square
  between them). Off: the whole box between the cell and its steps.
- Added cells never replace filled cells. Where several cells grow into one, the smallest step wins (ties: the first in
  cell order). Removals win over additions: they are applied to the union of the input and the added cells.
- **Added Values**: Nearest (default) copies the cell it grew from, so all attributes and properties follow; None gives
  a fresh entry (attribute defaults; native properties still copied). Added cells get whole-cell bounds, so a trimmed
  top (Rasterize Best Fit/Trim) is not copied upward. The trimmed cell keeps its own bounds.
- **Extrude Step Attribute** (optional Integer32, default `ExtrudeStep`, off by default): 1, 2, 3... out along a run
  (with Separate Axes off: the largest axis distance), 0 for every cell that was already there.
- Error when the cells would add or remove more than `Max Cells` (default 64M) in total.
- Tests: `PCGUtils.Voxel.Extrude.Grow`, `.Remove`.

## 4m. Voxel | Build Nav Grid

Started 2026-11-16 from the designer's spec (vault note `Excalidraw/Voxel Element - Create Nav Grid`), for a baked
flying-cube effect in a tower. Builds a navigation graph from a voxel set as a **PCGEx cluster**. Plan: pass 1
(flying, one vertex per cell) and pass 2 (compression into free boxes, aerial collapse; 2026-11-17) are built; pass 3
adds Surface mode and Use Surface Offset. Digger
mode (caves, tunnels) is deferred.

**Pins.** In: `VoxelGrid` (required, voxel sets, one cluster per input) and `Bounds` (optional Spatial: one data for
all inputs or one per input). Out: `Vtx` and `Edges`.

**Cluster contract** (shared `Data/PCGUtilsClusterInterop.h` in PCGUtilsCore, moved there from PCGUtilsFracture on
2026-11-16 so both modules emit clusters without linking PCGEx): Vtx tagged `PCGEx/Vtx` with int64 `PCGEx/VData` =
(vertex id << 32) | degree, the id being the point index; each island's Edges tagged `PCGEx/Edges` with int64
`PCGEx/EData` = (A << 32) | B; every data of one input shares the value tag `PCGEx/Cluster:<id>`. One Edges data per
connected island, all sharing the one Vtx data. A cell with no edge is left out (PCGEx has no cluster of one). Edge
points sit midway along their edge with zero bounds; vertex points carry the grid rotation and the agent's box as
bounds. Input tags are kept on both outputs.

**Flying (pass 1).**
1. Filled = the set's cells; with `Ignore Gaps` (default on) also the gaps a closing by `Gap Radius` (default 1)
   fills (`PCGVoxelMorphology::Close`, as Fill Gaps uses).
2. Blocked = Filled grown by `Surface Clearance` (point shorthand, Double >= 0, default constant 1, attribute
   `SurfaceClearance`, per filled point so hazardous cells get a wider berth): a cell is blocked when its box lies
   closer than the clearance to a filled cell's box, in cells, Euclidean box to box (1: every face, edge and corner
   neighbour). Gap cells take the widest clearance of the filled points around them.
   (`PCGVoxelMorphology::DilateByDistance`.)
3. `Agent Size` (data shorthand Vector, default (1, 1, 2), attribute `AgentSize`, in cells, each axis > 0): each axis
   rounds up to whole cells; a vertex needs that box of unblocked cells (`PCGVoxelMorphology::ErodeBox`, uneven reach
   so an even size extends to the + side) and sits at the box's middle, so a 2 tall agent's vertex is on the boundary
   between its two cells.
4. Where: `Flying Space` Padded Box (default: the set's bounds plus `Padding` cells, default 2), Enclosed Only (empty
   space with no face path to the outside of Filled), Outside Only. A connected `Bounds` replaces the padded box: the
   cells whose box overlaps its bounds, kept only where the vertex position samples a density above 0.
5. Edges to the face neighbours; `Enable Diagonals` (default off) adds edge and corner neighbours where every cell of
   the box the move spans is a vertex too (no corner cut).

**Compression (pass 2, `Enable Compression`, default on).** The cells from step 4 are merged into boxes with
`PCGVoxelChunking::GrowBoxes` (shared, added for this node: greedy unaligned boxes grown along X, then Y, then Z, which
suit rooms and corridors better than the octree of `MergeCells`). Every edge is then a straight line inside one box of
free cells, so it never leaves free space and never cuts a corner (Enable Diagonals only affects the uncompressed grid).
- `Compression Size` (data shorthand Double >= 1, default 4, attribute `CompressionSize`, rounded to whole cells): the
  largest box side near surfaces, and the widest portal tile.
- Aerial cells: those whose box is at least `Aerial Cell Distance` (default 2, cells, Euclidean box to box,
  `DilateByDistance` again) from every filled cell. Aerial and near cells never share a box. With `Collapse Aerial
  Cells` (default on) aerial boxes grow up to `Aerial Cell Collapse Size` (default 16 x 16 x 16) instead.
- Each box gets a centre vertex (bounds: everywhere the agent can be inside the box). Each face two boxes share is cut
  into tiles no wider than the smaller box limit of the two, with a portal vertex at each tile's middle on the face,
  joined to both centres. Two boxes with the same face (the pair is itself a box) join centre to centre with no portal,
  so a tunnel becomes a chain of centres.
- Hub cubes (designer's sketch 2026-11-17, `Excalidraw/Attachments/Pasted image 20261117182737.png`; replaced a Link
  Portals option that joined every two portals of a box, rejected as thousands of edges where a path uses a few). In a
  box two or more portals open into, `Hub Cubes` (default 1, 0 to 3) nested cubes round the centre, cube c of N at
  (c + 1) / (N + 1) of the way from the centre to the box's extent (one cube: halfway). `Hub Subdivisions` (default 1:
  corners only) steps per side of each cube's surface lattice, joined along its lines. The innermost cube's corners
  join the centre, an outer cube's points join the same points of the cube inside, and each portal joins its centre
  plus the outermost cube's nearest point (all tied: a face-centre portal joins the 4 corners facing it). A cube flat
  on an axis (under a quarter cell of reach) collapses there; a cube with no reach at all is skipped. Paths then bend
  round a box's middle instead of all crossing one vertex; to drop portals from a found path, filter on `IsPortal`.
- A box of more than one cell that touches no other box (a sealed pocket) is split in two along its longest side, so it
  stays a cluster of two instead of a lone vertex PCGEx would drop.

**Outputs on Vtx** (each name exposed, each can be turned off): `IsAerial` (Boolean, on: a centre takes its box's cells,
a portal is aerial when both boxes are), `IsPortal` (Boolean, on: true on face portals), `ChunkSize` (Vector, on: the box a centre stands
for, (1, 1, 1) uncompressed, (0, 0, 0) on portals and hub points), `SurfaceDistance` (Double, on: world distance from the vertex to the closest of the nearest
filled cells of every cell it touches (exact on a cell centre; a vertex on a cell boundary touches two to eight),
ignoring gap fills; -1 when the set is
empty) and
`NearestVoxelIndex` (Integer32, off: input index of the point filling that cell, a chunk's own point for a chunked set;
`Nearest Voxel Axis Priority` settles equal distances, the cell further along the first axis winning). Both come from
`PCGVoxelMorphology::NearestSet`, an exact separable distance-to-box field with that tie rule.

**Errors and warnings.** Surface mode is an error (not built yet). An enabled output with an empty name, two outputs
sharing a name, an output named `PCGEx/VData`, Padding below 0, a Bounds count that is neither 1 nor the input count,
non-spatial or empty Bounds, an Agent Size axis that is not a number above 0 (names the axis), and a working box over
`Max Cells` (default 64M; names the box), Aerial Cell Distance below 0, an Aerial Cell Collapse Size axis below 1
(names the axis) a Compression Size below 1, Hub Cubes below 0 and Hub Subdivisions below 1 are errors. No two vertices connecting is a warning naming how many cells the
agent fits in.

**Tests** (`PCGUtils.Voxel.BuildNavGrid`): Flying checks a sealed 7 wide room (125 vertices and 300 edges at clearance
1, nearest wall 150 away; 343 at clearance 0; 100 for a 1 x 1 x 2 agent, vertices on cell boundaries, bounds the
agent's box; 1036 edges with diagonals), the padded box round one cell (124) and Outside Only round a room. Islands
checks two rooms (two Edges datas, one Vtx data), per point clearance and a Bounds box (both turn compression off to
count cells). Compression checks a 1 x 1 x 10 tunnel (three centres at 4, 4 and 2 cells, two edges, 50 from the walls),
a sealed 2 x 2 x 2 pocket (split into two vertices), a room with a pillar at 0, 1 and 2 hub cubes and 2 subdivisions (the
boxes cover every free cell once, portals exist, every edge sampled stays in free cells, hubs add only hub points), a
5 x 5 x 5 box with two portals (13 vertices, 32 edges, corners exactly halfway) and an open room (the 3 x 3 x 3 aerial
middle is one box). Every case also checks the
PCGEx contract: tags, ids, EData endpoints and every vertex's VData degree against its edges.

## 4j. Voxel | Match Rules (rule tiles)

Designed with the designer on 2026-11-10; first version built the same day. The 2D rule-tile idea in 3D, with a
designer-sized vocabulary: rules say which surface shape a mesh fits and how far it may turn to be reused, the node
finds the rotation, turns the point and writes a mesh index. Fitting and picking the actual mesh stay downstream
(for example a PCGEx mesh collection read by index, where several equivalent meshes can share one index and be
randomized). The mesh itself is never part of the mapping: the artist builds each mesh to one written convention
("the edge mesh is open on +Y and +Z") and gives it the matching index.

**Mesh space.** Every rule is written in the mesh's own frame: +Z is the mesh's up, and the side layouts below start
at +Y (the designer's "Y forward"; a wall mesh is open on +Y, with X along the wall). The node tests the rule against
the cell turned into that frame.

### 4j.1 Three layers, all plain structs

The customization belongs to the struct types, not to an asset, so rules work wherever a struct can live: node
settings, graph parameters, Blueprint variables, data assets.

| Layer | Struct | Holds |
|---|---|---|
| Rule | `FPCGVoxelRule` (base; instanced) | One condition, with `Invert`. Types so far: **Faces** and **Thin**. New types (Gap, attribute compare) are new structs, so the format does not change. |
| Compound rule | `FPCGVoxelCompoundRule` | Rules that must all pass, plus `Turns`, `Prefer Up` and `Spin`. All its rules are tested under one shared rotation (so a later "gap on +Y" check turns with the pattern). No rules = matches every cell. |
| Mapping | `FPCGVoxelRuleMapping` | Ordered entries: `Name`, `Enabled`, `Mesh Index`, compound rule. Tried top to bottom, first match wins. The index is on the entry, not the compound, so one compound can feed different indices in different mappings; OR is two entries with the same index. |

`UPCGVoxelRuleMappingAsset` (a data asset) only wraps one mapping for sharing.

### 4j.2 Rule types

- **Faces.** `Up (+Z)` and `Down (-Z)`: Any / Empty (open) / Filled (closed). `Sides`: Any, None, One, Two Adjacent,
  Two Opposite, Three, Four, or Custom (then `+Y`, `+X`, `-Y`, `-X` each Any / Empty / Filled). Canonical layouts:
  One = +Y open; Two Adjacent = +Y and +X; Two Opposite = +Y and -Y; Three = all but -X. Up/Down x Sides gives the
  24 shapes that are distinct when only turns about up are allowed; under all 24 rotations they fall into 10 (inside,
  face, edge, slab, corner, U, tube, fin, cap, lone cell).
- **Thin.** Both faces of one axis open. `Axis`: Any, Up/Down (Z), X, Y, or Either Side (X or Y). With `Invert` it
  excludes thin cells. Your "thin tiles get the default cube" test is one entry, Thin Any, placed first.

### 4j.3 Turns and orientation

- `Turns`: **Exact** (as drawn), **About Up** (4 yaws; default, floors stay floors), **About Up + Flip** (8: ceiling
  and floor share a mesh), **Any** (all 24 cube rotations: an edge mesh fits vertical edges too). Mirrors are not
  included (they would need negative scale).
- When several rotations match (a floor matches all 4 yaws), `Prefer Up` (default on) keeps those whose mesh up is
  nearest world up. That gives walls the convention for free: mesh +Y out of the wall, Z up, X along it.
- `Spin` settles what is left: **Fixed** (the first in a fixed order: identity, then the yaws, then the flips, then
  the rest) or **Random** (one of the tied rotations per cell, from the node seed and the cell's seed; breaks up
  tiling on floors and ceilings).

### 4j.4 Text form

Every layer round-trips to one line of text, for copy and paste, diffs, graph parameters and attributes (a string
travels anywhere). The node's `Rules Source: Text` parses it; parse errors name the line and the token.

```
# Index Name: rules | settings
0 Corner: +Z=E Sides=TwoAdjacent | Turns=Any
1 Thin: Thin=Any | Turns=Exact
2 Floor: +Z=E -Z=F Sides=None | Turns=Up Spin=Random
3 Wall: +Z=F -Z=F +Y=E +X=* -Y=F -X=* | Turns=Up
9 Default: | Turns=Exact
```

- Faces tokens: `+Z`, `-Z`, `+Y`, `+X`, `-Y`, `-X` = `E` (empty, open), `F` (filled) or `*` (any); `Sides=None |
  One | TwoAdjacent | TwoOpposite | Three | Four | Any`. Loose face tokens form one Faces rule; braces make more,
  `!{...}` inverts one.
- Thin: `Thin=Any | Z | X | Y | Sides`, `!Thin=...` inverted.
- Settings: `Turns=Exact | Up | UpFlip | Any`, `PreferUp=Yes | No`, `Spin=Fixed | Random`. Defaults: Turns Up,
  PreferUp Yes, Spin Fixed. The name is one word and optional (`3: ...`).

### 4j.5 The node

- In: voxel sets (chunks are expanded, one point per cell).
- `Rules Source`: **Inline** (a mapping struct on the node; default), **Asset** (`Rules Asset`) or **Text**
  (`Rules Text`).
- `Drop Inside Cells` (default on): cells with no open face are left out of every output (they are never seen).
- `Unmatched`: **Pin** (default; to the Unmatched output, for the default cube) / **Keep** (in Out with index -1) /
  **Drop**.
- Out: In, mutated: metadata, tags, `@Data` kept. Each matched point is turned to grid rotation x the rule's
  rotation, stays on its cell centre, and gets bounds covering its cell in its own (turned) frame, so non-cubic cells
  fit. `Mesh Index Attribute` (int32, default `MeshIndex`) and `Rule Index Attribute` (int32, default `RuleIndex`,
  the entry's position) are written, -1 when unmatched. Unmatched points keep their rotation and bounds.
- Speed: only the open-face mask (6 bits) decides a match today, so the node solves all 64 masks once and looks each
  cell up.
- Validation, each its own error: no asset, an empty rule (instanced struct with no type), a negative mesh index
  (naming the entry), a text parse error (line and token), empty or clashing attribute names.

### 4j.6 Editor

The compound rule and the mapping entry show their one-line text in the collapsed row header, wherever they appear.
Planned: a 3x3x3 neighbour picker on the Faces rule, the text line editable both ways, named shape presets, and a
preview with meshes in the asset editor.

### 4j.7 Next, after the designer sees it live

1. **Gap**: per open face, empty cells before the next filled one, up to `Max Gap` (one tile of air = 1); also as a
   Cell Properties output (`GapUp`, `GapDown`). Gap rules turn with the compound. Matching then needs more than the
   64-mask table.
2. Edge and corner neighbours in the Faces pattern (inner corners).
3. Attribute-compare rules (surface area, chunk volume, Depth, Component Size).

## 4p. Voxel | Pattern and Voxel | Apply Patterns

Built 2026-11-20 from the designer's sketches and the Sol handoff (`VoxelPatternFactory_UE58`). One small set of
right-angle wedge meshes (`/PCGUtils/Assets/Meshes`) scales to any chunk, so shaped edges and corners come down to
two decisions per place: which cells form a chunk, and how the mesh is turned in it. A pattern authors both. Fitting
the mesh to the chunk stays downstream (a PCGEx mesh collection read by `MeshIndex`, fitted to the point's bounds).

Match Rules (4j) answers "which mesh fits this one cell" from its six faces. Patterns answer "which group of cells
becomes which chunks": they look further than the faces, make chunks bigger than a cell, run in order, and can add
cells. Match Rules is unchanged.

### 4p.1 A pattern

`FPCGVoxelPattern`, a plain struct (inline on a `Voxel | Pattern` node, or in a `UPCGVoxelPatternAsset`). Everything
is written in the pattern's own frame, which is also the frame of the mesh it places.

| Part | Meaning |
|---|---|
| `Name`, `Debug Color` | For reading graphs and errors; the colour goes on the points the pattern makes. |
| `Operation` | **Claim** (default): the boxes take filled cells. **Add**: the boxes are new cells in empty space. |
| `Probes` | Cells around the origin that must be `Filled`, `Empty`, `Unclaimed` or `Solid Face` (with a `Face` side). Read only. Cells not listed do not matter. Each probe: `Offset`, `State`, and `Count` + `Step` to cover a row of cells with one probe (Count 3, Step -X: three cells running toward -X from Offset). |
| `Boxes` | The chunks made where the pattern fits, all or none. Each: `First`, `Span`, `Mesh Index`, `Mesh Forward` (+Y of the mesh) and `Mesh Up` (+Z of the mesh), and `Open Sides` (the sides the mesh cuts away), in the pattern's frame. |
| `Turns` | Exact / About Up (4) / About Up + Flip (8) / Any (24), as Match Rules. Probes and boxes turn together. |
| `Join Along`, `Max Joined Span` | Pattern axes along which neighbouring fits (same turn, same box, end to end) join into one longer chunk. Uses the shared chunking (`PCGVoxelChunking::JoinAlong`). |
| `Probes See Added Cells` | Off: probes read the input set only. On: cells added by earlier patterns count as filled. |

Rules that hold everywhere:

- **The origin cell (0,0,0) is always a filled cell.** A pattern is tried at every filled cell (Z, then Y, then X),
  under each of its turns in fixed order (as drawn first).
- **Claiming does not empty a cell.** A claimed cell cannot be claimed again, but every probe still reads it as
  filled. So a 2 x 2 corner taken first leaves no fake holes for the 1 x 1 pattern after it.
- **Unclaimed asks for plain wall** (added 2026-11-20, after the first barrier test made corners against other
  patterns' chunks, on lone blocks and on wall ends). An `Unclaimed` probe needs a cell of the set that no *earlier*
  pattern claimed; added cells never count. Cells the same pattern takes do count, so two fits of one pattern never
  block each other and the visiting order does not change the result. "How much room does a corner need" is the
  `Count` of its Unclaimed probes: N on one wall, M on the other.
- **A chunk is solid on its back and air on its cut sides** (added 2026-11-21 from the designer's sketch: a fill
  behind a 2 x 2 corner was wrongly refused because the bend cell was claimed). Each box lists its `Open Sides`. A
  `Solid Face` probe names one side of the probed cell (`Face`) and passes on a plain cell, or on a cell of an
  earlier pattern's chunk that does not list that side as open. Diagonal cells are not looked at unless a probe is
  put there. As with Unclaimed, a pattern's own chunks read as plain to it.
- **Exact boxes.** A box is its authored size or the pattern does not fit. Longer chunks come only from Join Along.
- **All or nothing.** Several boxes in one pattern are one layout; a fit takes every box or none.
- **Added cells appear after their pattern has finished**, so the order cells are visited in never changes what an
  Add pattern sees.
- An added chunk copies the point of the cell its pattern started on (metadata kept), with whole-cell bounds.

### 4p.2 Mesh convention

Measured from the meshes on 2026-11-20 (each a 200 unit cell, pivot at the centre):

| Mesh | Solid part | Open (cut) sides |
|---|---|---|
| `SM_Voxel_Edge` | Wedge along Y; whole faces on -X and -Z | +X and +Z (one slope across the +X / +Z edge) |
| `SM_Voxel_Corner_Ex_2Sides` | Pyramid on the -Z face, apex at (-X,-Y,+Z) | +X, +Y, +Z (slopes across +X/+Z and +Y/+Z: a hip) |
| `SM_Voxel_Corner_Ex_3Sides` | Corner tetrahedron at (-X,-Y,-Z) | +X, +Y, +Z (one diagonal face) |
| `SM_Voxel_Corner_In_2Sides` | Two wedges joined; only the (+X,+Y,+Z) corner missing | +Z (a valley) |
| `SM_Voxel_Corner_In_3Sides` | Cube minus the (+X,+Y,+Z) corner | only the +X+Y+Z diagonal |
| `SM_Voxel_Corner_Int_Out` | Upright half cube on the +X+Y side, top corner cut | -X, -Y upright diagonal; use not confirmed |

So the convention the presets use: **a mesh's cut sides face +X (then +Y), and +Z.** A pattern written as drawn for
the Edge mesh has `Empty` probes on (1,0,0) and (0,0,1). To use the same mesh another way round inside a pattern, set
the box's `Mesh Forward` / `Mesh Up`: the upright corner stands the Edge mesh on end with Mesh Forward -Z, Mesh Up +Y
(its +X stays +X, its +Z points along +Y).

### 4p.3 The nodes

`Voxel | Pattern`: one optional input pin, `Selector` (below); `Pattern Source` Inline or Asset; `Order` (lower runs
first; ties run in connection order); `Selector Scope`. Output pin `Pattern` (its own data type, so the pin takes
nothing else). Palette presets:

| Entry | Pattern |
|---|---|
| `Voxel \| Pattern` | Blank: one single-cell box, no probes. |
| `Voxel \| Pattern Top Edge` | +X and +Z open, About Up, joined along Y: one long edge per wall top. |
| `Voxel \| Pattern Upright Corner` | The origin is the corner cell; +X and +Y open; Edge mesh on end; joined along Z. Two `Unclaimed` probes, (-1,0,0) stepping -X and (0,-1,0) stepping -Y, Count 1: both walls must run on from the corner as plain cells. `Presets::UprightCorner(Size, WallCells)` in C++ makes the Size x Size version. |
| `Voxel \| Pattern Upright Corner 2 x 2` | The same with a 2 x 2 x 1 box behind the corner cell, four Empty probes, and four Unclaimed wall probes (each wall, both rows of the corner's width). |
| `Voxel \| Pattern Inside Corner Fill` | Add. The origin is the cell in the bend, diagonal to the empty corner; (1,0,0) `Solid Face` +Y and (0,1,0) `Solid Face` +X (the sides facing the fill), (1,1,0) empty, the bend cell free; adds (1,1,0); Edge mesh on end; joined along Z. |

The wall probes are what stop a corner being made on a lone block, on the end of a wall, or beside a chunk an earlier
pattern made. Raise their `Count` to ask for more wall; delete them to get the old behaviour (any outside corner).
The preset boxes set `Open Sides`: Top Edge +X and +Z; the upright corners and the fill +X and +Y.
Nodes placed before 2026-11-21 keep their old probes and no Open Sides: place the presets again or set them by hand.

**Where a pattern is applied** (added 2026-11-21, reworked 2026-11-22). The `Selector` pin of `Voxel | Pattern`
takes **Voxel Selectors**, the voxel domain's own selector type (light tan pin, `UPCGVoxelSelectorData` in
`Selectors/PCGVoxelSelector.h`). Nothing connected: the pattern is applied everywhere, as before. A voxel selector
is prepared against the grid and then answers for single cells, filled or empty (an Add pattern asks about the
empty cells it would fill). The nodes that make them today are the `Selector | ... (Voxel)` nodes
(`Docs/PCGUtilsSelectors.md`).

- Several selectors on the one pin: a cell is selected when **any of them** selects it. Each selector's Invert is
  applied first.
- `Selector Scope` says what must be selected. **Cells** (default): every cell the pattern takes or adds. **Origin**:
  only the cell the pattern starts on; its boxes may reach outside the selection. **Both**: the origin cell and every
  cell taken or added. Cells and Both differ when the origin cell is not in a box, as in an Add pattern.
- **Selectors never change what probes read.** An unselected cell still counts as filled, and a cell an earlier
  pattern claimed there is still claimed. Selectors only say where this pattern may start and which cells it may take.
- Selectors belong to the pattern, so two patterns on one Apply Patterns node can have different selections.

`Selector | In Bounds (Voxel)` (module `PCGUtilsSelectors`, palette category `PCGUtils|Selectors`): input pin
`Bounds` (points, required), output pin `Selector` (a Voxel Selector). It is the `Selector | In Bounds` node with
`Domain` set to Voxel. It selects the cells lying in the bounds of any point on the Bounds pin, each point's Bounds
Min / Max placed by its transform (rotation and scale included).

| Setting | Meaning |
|---|---|
| `Domain` | Voxel here. DynMesh or GC makes the same node put out that domain's selector instead. |
| `Voxel Conversion` > `Cell Points` | What stands for a cell. **Center With Bounds** (default): its centre with the cell's box (the cell's size, turned with the grid). **Center**: its centre alone. |
| `Containment` | How a cell with bounds is tested. **Center** (default): the cell's centre is inside a bound. **Any Part**: the cell overlaps or touches a bound. **Fully Inside**: the whole cell is inside one bound. With Cell Points on Center all three are the same. |
| `Expand` | Grows every bound on all sides, in world units, before testing. Negative shrinks. A bound shrunk inside out is skipped with a warning. |
| `Invert` | Selects every cell the test does not. |
| `Priority` (advanced) | No effect on voxel selectors: several on one pin select what any of them selects. |

No points on the Bounds pin is a selector that selects nothing (with Invert, everything).

`Voxel | Apply Patterns`: pins `In` (voxel sets), `Patterns`; outputs `Out`, `Unmatched`.

- Out: In, mutated. One point per chunk, at the chunk centre, rotation = grid rotation x the mesh turn, **scale 1**,
  bounds covering the chunk in the turned frame (surface offsets of claimed cells are kept, turned with it).
  `VoxelSpan` stays in **grid axes** however the point is turned.
- `Unmatched`: **Pin** (default; cells no pattern took, unturned, an untouched input chunk stays a chunk, a chunk
  that lost cells gives the rest one by one) / **Keep** (after the chunks in Out, Mesh Index -1) / **Drop**.
- `Set Point Color` (default on): the pattern's Debug Color goes to `$Color`.
- Attributes, every name a setting: `MeshIndex` (int32, required), `PatternIndex` (int32, on by default: position in
  run order), `PatternName` (Name), `PatternColor` (Vector4), `IsAdded` (Boolean); the last three off by default.
- Validation, each its own error naming the pattern and the box or probe: no boxes; a span below 1 or above 256;
  negative mesh index; Mesh Forward and Mesh Up on one axis; overlapping boxes; an Add box on the origin; an Empty
  probe on the origin; a probe Count below 1 or above 256; a probe that contradicts another probe or a box; empty or clashing attribute names.

Out is for spawning: a turned multi-cell point is not a clean voxel set for other Voxel nodes (they ignore point
rotation when reading bounds). Unmatched is a clean voxel set.

### 4p.4 The golf barrier and the centre patch

The barrier (Voxel Grid 2), three patterns on one Apply Patterns node:

1. Order 0, `Pattern Upright Corner 2 x 2`: outside corners that have a full 2 x 2 behind them.
2. Order 1, Upright Corner 1 x 1: the outside corners left over that still have plain wall on both sides, so none
   lands against a 2 x 2 chunk. **Square ends on thin extrusions** come from the same two wall probes: a wall end one
   cell thick has one wall missing, so it stays square. Delete the two probes to chamfer the ends too. That is the
   one switch between the two Pass 1 pictures. A wall end two cells thick does have two real 1 x 1 corners (each has
   one cell of wall both ways); set the wall probes' Count to 2 to leave those square as well.
3. Order 2, Inside Corner Fill: adds the corner cells of Pass 2 wherever both walls show a whole side to the fill,
   so also behind a 2 x 2 corner chunk, and never against a chunk's cut side.

Unmatched carries the straight barrier cells for the default mesh.

**Keeping open entrances square.** Where the barrier stops for a course entrance, the wall ends beside it can get
corners. To leave them square:

1. Make one point per entrance, with bounds that cover the entrance and the wall ends beside it.
2. Feed the points to `Selector | In Bounds (Voxel)` with **Invert on**: it selects every cell not at an entrance.
3. Connect its `Selector` output to the `Selector` pin of the corner patterns (the 2 x 2 and the 1 x 1 Upright Corner,
   and the Inside Corner Fill if it should stay out too). One selector node can feed all of them.

Leave `Selector Scope` on Cells so a corner never reaches into an entrance. Use `Containment` Any Part to also keep
out cells the bounds only touch, or `Expand` to widen every entrance by the same amount. Patterns with nothing on
their Selector pin still run at the entrances.

The centre patch (Voxel Grid 1) turned out not to be a pattern job as first set up (designer, 2026-11-20): a flat
one-cell-thick patch has no +X / +Z corner to find, so it is either built separately or extruded down first to make a
real corner, with the lower cells removed afterwards. Drawn by hand it still works: a pattern with Turns Exact, no probes, and the boxes drawn by hand: three
3 x 1 x 1 boxes for three ramp strips, or any other layout, each box with its own Mesh Forward / Mesh Up, which is
how the hip and ridge layouts in the sketches are made from the one Edge mesh.

Order matters at corners: a corner cell also fits an edge pattern (as drawn first), so put corner patterns before
edge patterns.

### 4p.5 Not built yet

1. **The rest of selective application.** Voxel selectors on `Voxel | Pattern` are built (4p.3), fed by point
   selectors (`PCGUtilsSelectors.md`). Next: the PCGEx filter selector in `PCGUtilsPCGExInterop` (any PCGEx point
   filter as a point selector), then more point selectors (by distance, inside a mesh). All reach patterns through
   the same seam: `PCGVoxelPatterns::FLimits` has `CanStart(pattern, origin cell)` and `CanEdit(pattern, cell)`,
   and neither changes what probes read.
2. Attribute equality when joining (today a joined chunk takes its first cell's values, as Compress `Listed` with
   nothing listed), and equal surface offsets.
3. A slice-grid editor for probes and boxes, and a text form.
4. Face patterns (`SM_Voxel_Plane_Inset`): a box that makes a point per open face rather than per chunk.
5. Mirrored turns.
6. Choosing which way a corner faces when two turns fit (a wall end three wide): today, Turns Exact and one pattern
   per direction. The designer expects this to wait for the editor layer (item 3).

## 4d. Shared voxel grid (design, not built)

Proposed 2026-11-08 by the designer. Today every Voxel node rebuilds its own cell lookup (`FPCGVoxelGrid`: a dense
int32 per cell over the bounding box, or a hash map, plus the cell and cell → point arrays). Large sets pay that once
per node, which is heavy on memory. The idea: build the grid once, keep it for the rest of the graph run, and link the
point set to it from `@Data`. Two wins:
- (a) nodes downstream skip the rebuild;
- (b) points can be both the cells and selections of cells: a node takes selection points and edits the grid and the
  cell points together.

The grid is an optional speed-up. Every node must still work, with the same result, when the grid is missing or
does not match its points.

### 4d.1 Grids are versions that never change
A PCG graph is dataflow: one point set often feeds two branches, PCG can run them in parallel, and it caches node
outputs. A grid edited in place would make one branch's result depend on whether the other branch ran first, race
when they run together, and leave cached outputs pointing at a grid that has since changed. So a grid is never edited
after it is built. A node that changes cells (Clear, Fill, Filter Cells) makes a new version, and its output points
link to the new version; the input points keep the old one.

New versions stay cheap by sharing storage: the grid is stored in bricks (16³ cells proposed), and a new version copies
only the bricks it touches and shares the rest with the version it came from (reference-counted, copy on write).
Clearing a few hundred cells of a 1.3M-cell set copies a few bricks.

### 4d.2 The grid stores occupancy, not point indices
Any ordinary node (Filter, Merge, Uber Filter, a Self Pruning) changes the points but keeps `@Data`, so point indices
cannot live in the grid; they would be wrong after the first filter. A brick holds one bit per cell (512 bytes per
16³ brick, against 16 KB for today's int32 per cell). Chunks (`VoxelSpan`) are written into the occupancy as the
cells they cover.

Nodes that read point attributes per cell (To DynMesh reads materials and colors only on surface cells) build a small
cell → point lookup over just the cells they need, from the points.

### 4d.3 Checking that a grid matches its points
Before trusting a grid, a node checks that it matches the points, which needs no allocation:
- the grid's cell count equals the number of cells the points cover (point count, or the sum of spans for chunks);
- every point's cells are occupied in the grid.

When no two points fill one cell, these two together prove the grid holds exactly the points' cells. Duplicate cells
(Keep First, the default, and Keep Last; section 1) make the counts differ, so such a set always
rebuilds. Cost: one occupancy read per cell, far less than building a grid. When the check fails, the node
builds its own lookup as it does today and gives a note naming the reason (missing grid, cell count A against B, or
the first point whose cell is empty). So a plain Uber Filter is always safe; the Voxel versions only save time.

### 4d.4 Lifetime: an ID and a registry
A soft object path to a transient object holds nothing, so garbage collection could free the grid during the graph
run. Instead:
- points carry `@Data.VoxelGridId` (name exposed as an FName setting on every node that reads or writes it);
- a subsystem (`UPCGVoxelGridSubsystem`) owns the grid versions, keyed by ID;
- the subsystem keeps a component's grids until that component's generation finishes, then releases them. A cached
  output from an earlier run then finds no grid, fails the check and rebuilds, which is correct.

Open: whether to keep grids across runs for the same component (faster re-runs in the editor, more memory), keyed by
a cell hash.

### 4d.5 Nodes
- `Voxel | Build Grid`: points in, the same points out with `@Data.VoxelGridId`. Optional; any Voxel node that builds
  a grid could also attach it (setting `Attach Grid`, default off at first).
- `Voxel | Clear Cells`: cell points first, selection points second; out the cell points without the selected cells,
  linked to a new grid version. Selection points pick cells by position (or by their own cells when they carry the same
  `@Data.VoxelSize`).
- `Voxel | Fill Cells`: the same pins; adds a cell point for every selected empty cell. Added points take metadata from
  the nearest cell (or the selection point, setting), following the mutated-input rule.
- `Voxel | Filter Cells` (PCGUtilsPCGExInterop): Uber Filter on the cell points, removing the cells that fail, with a
  new grid version.
- Every existing node reads the grid when it matches and writes nothing to it unless it changes cells.

### 4d.6 Order of work
1. Measure peak memory per node on the 1.3M-cell terrain. The pressure may be from Expand or chunk expansion rather
   than the lookup itself; that decides how much this saves.
2. Brick occupancy grid, the registry, the match check and the rebuild fallback. Existing graphs get faster with no
   changes.
3. Clear, Fill and Filter Cells.

## 5. Module and files

- `Source/PCGUtilsVoxel/PCGUtilsVoxel.Build.cs`. Dependencies: Core, CoreUObject, Engine, PCG,
  PCGGeometryScriptInterop (`UPCGDynamicMeshData`), GeometryCore, DynamicMesh, PCGUtilsCore (input shorthands, data
  domain helpers) and PCGUtilsDynMesh (space helpers, material assignment).
- `.uplugin`: `PCGUtilsVoxel`, Runtime, PreDefault.
- `Public/Voxel/PCGVoxelGrid.h` holds the contract, validation and cell lookup.
- `Public/Voxel/PCGVoxelMorphology.h` (added 2026-11-15) is the shared dense-grid library: `FPCGVoxelDenseMask` (a
  box of cells, a byte each), `FPCGVoxelBoxSum` (set cells in any box in O(1), a summed-volume table) and
  `PCGVoxelMorphology::Erode / Dilate / Open / Close / FloodOutside` with per-axis reach. Offset Surface and Cell
  Properties use it; new nodes must too, rather than writing their own (designer rule, 2026-11-15, which applies to
  compression as well: the chunking in `PCGVoxelCompress.cpp` moves to a shared header before anything else chunks).
- `Public/Voxel/PCGVoxelIslands.h` (added 2026-11-20) is the shared island library: `FPCGVoxelIslands` (island per
  cell and per point, cell count and cell bounds per island) and `PCGVoxel::FindIslands`. Separate Islands and Cell
  Properties use it. `EPCGVoxelConnectivity` moved to `PCGVoxelGrid.h` for it.
- `Public/Voxel/PCGVoxelChunking.h` (added 2026-11-15) is the shared compression library: `EPCGVoxelChunkShape`,
  `EPCGVoxelRandomSize`, `EPCGVoxelMatchAttributes`, `FPCGVoxelPointEquality` (which points may share a chunk) and
  `PCGVoxelChunking::MergeCells` (octree cubes, then boxes), `JoinAlong`, `RandomBoxes` and `GrowBoxes` (greedy
unaligned boxes with a per-label limit, added 2026-11-17 for Build Nav Grid). Voxel | Compress and Voxel
  | Offset Surface use it; it came out of `PCGVoxelCompress.cpp` unchanged in behaviour (every Compress test passes).
- `Public/Voxel/PCGVoxelMesher.h` holds the faces → mesh core. It is plain C++, so it can be tested without a graph.
  The bevel path is `Private/Voxel/PCGVoxelBevelMesher.cpp` (`PCGVoxelMesher::BuildBeveledMesh`).
- `Public/Elements/PCGVoxelToDynMesh.h`, `PCGVoxelPackPoints.h`, `PCGVoxelCompress.h`, `PCGVoxelExpand.h`, `PCGVoxelCellProperties.h`, `PCGVoxelShiftSeams.h`, `PCGVoxelTransformGrid.h`, `PCGVoxelAirFlow.h`, `PCGVoxelRasterize.h`, `PCGVoxelBoolean.h`, `PCGVoxelOffsetSurface.h`, `PCGVoxelMatchRules.h`, `PCGVoxelExtrude.h`, `PCGVoxelFillGaps.h` (15 palette entries; the palette test counts them). All derive from `UPCGSettings`, so each overrides
  `GetType()` (or they land in `Generic`). Add the `Voxel` category to the `PCGUtils.Palette.SearchContract` test.
- `Public/Voxel/PCGVoxelRules.h` holds the rule structs (rule, compound rule, mapping, mapping asset), the 24
  rotations, the 64-mask match table and the text form. `PCGUtilsEditor` depends on `PCGUtilsVoxel` only for the rule
  header customization (`PCGVoxelRuleCustomization`).
- `PCGUtilsDynMesh` must not depend on `PCGUtilsVoxel`; the dependency only runs Voxel → DynMesh.
- Tests in `Private/Tests/`:
  - contract errors, one per invariant;
  - a single cell gives a closed 12-triangle box;
  - two cells touching only at an edge stay manifold;
  - collapse leaves no T-junctions (the mesh is closed and the volume is the same with and without collapse);
  - Hard vs Smooth colors;
  - Per Attribute Value polygroups: string values numbered, integer values as IDs or numbered, collapsed faces never
    cross a seam between values, bevel stays closed with one group per value; negative ID and missing attribute errors;
  - the offset keeps the mesh closed and moves only surface vertices;
  - UV tiling continues across cells;
  - `@Data` is carried over;
  - chunks: To DynMesh gives the same mesh for a set and its compressed form; Expand(Compress(x)) gives back x;
    a cell that differs in one attribute or in its offset stays out of a chunk; span validation errors;
  - Pack Points: every cell is claimed at most once; Max Radius is respected; points keep their metadata; with
    random partial settle, no two output boxes intersect;
  - Cell Properties: open sides, depth, neighbours and column values on a block; components by connectivity; concave
    edges and corners of a notch and a crease; a chunked set gives the same values as its expanded form; output names must be set and distinct;
    shape: a block's spike is a protrusion and its core distance counts out to the tip, thickness rows, a thin plate
    is all protrusion in 3D but solid in plan (X | Y), a slot's walls border a gap, a hollow borders a cavity but a cup
    does not, Max Shape Cells stops the input;
  - Compress Random modes: Exact 2 tiles a 6-cube in 27 cubes, Exact 4 makes one cube and leaves the rest single,
    Exact 3x1x1 makes 72 bricks, Cubes Up To makes only cubes, chance 0 compresses nothing, chance 0.5 mixes cubes
    and cells, a per-cell chance limits boxes to where it is 1, a per-cell span, a bad span names its point; Max Span
    from `@Data` (and 0 refused).
  - `IsSurrounded` (`Voxel.Surrounded.KeptByNodes`): Compress merges inside and surface cells and flags only the
    hidden chunk (also from a stale input), Expand restores the inner 64, Boolean Subtract of a middle cell gives 57,
    Surrounded Attribute None leaves values alone.
  - Shift Seams reach: two 4-cell chunks move more than a cell, a chunk against a cell at most a cell, the old limit
    at most half a chunk; a strength of 0 on a box pins its seams; a strength of 2 is refused naming the point.
  - Offset Surface split cells on Out: an L's Out has 4 points with the corner's 2 boxes `WasSplit`; Out feeds another
    offset as 3 cells; the corner reads as its face box; meshed it is closed with the carved boxes' exact volume; a
    notched block's Out is 31 points (middle 3 boxes); stale `IsSurrounded` rewritten.
  - Offset Surface Axes: X | Y grows a quarter sideways only, +1 is a 3x3 plate, -1 on a 3x3x3 keeps the middle
    column, the L corner carves into 2 full-height boxes; Z only grows a column; no axis leaves the cell alone; -Z
    alone grows only the cell below and lifts only the bottom face; all but +Z keeps the top; Amount Scale (1, 1, 0)
    is a plate, (1, 1, 2) reaches twice as far vertically, (1, 1, -1) grows sideways and shrinks vertically with the
    side growth at the shrunk height; a per point vector attribute works.
  - Offset Surface grow never overlaps: every grow test checks each box stays inside its cell and no two overlap; an L
    +0.25 is the exact box offset (5.25 x 1.5) with its inside corner an L of 2 boxes; diagonal neighbours; a one-cell
    gap grown 0.75 from both sides is filled exactly once, each box copying its side; a plate of mixed amounts.
  - Offset Surface grow meshes: one cell +0.25 and +1.5 and the L +0.25 mesh closed with the boxes' exact volume.
  - Offset Surface Compression: +3 compressed is a few chunks with the 7-cube's volume and box, no overlaps, spans
    written, the input cell alone; +1.5 compressed is at most 40 points and meshes closed as the 4-cube; Compress Equal
    Cells with All keeps distinct Ids apart, Listed $Density makes one 4-cube; a missing listed name names it.
  - Offset Surface Min Thickness: a 1-thick plate -0.4 keeps 0.5 (edges still pull in 0.4), all flagged; Remove deletes
    it; a lone cell -0.5 stays a quarter cube; a thick block is unchanged and unflagged; a run of 2 shares the cut; a
    per-cell 0 leaves that cell unclamped; a missing attribute names the setting.
  - Compress Random: boxes tile every cell exactly once, stay within Random Span, never mix unequal cells, and
    depend only on the seed; the palette alias presets the shape;
  - Bevel: a cube's chamfer volume matches the formula at 0.1, 0.25, 0.4 and 1 (octahedron); each of the six Full
    controls touches only its own edges (frustum, octagonal prism, L, step); the scale attribute and its errors;
    random lumpy shapes and touching pairs stay closed; every filling of a 3×2×2 block under ten bevel setups is
    closed and valid. `PCGUtilsPerf.Voxel.ToDynMesh` also reports bevel timings and open edges on the big shapes;
  - Transform Grid: a quarter turn moves cells where expected and keeps attributes and tags; an odd turn plus
    translation still reads with no off-grid points, same cells and same mesh volume; two sets each use their own
    `@Data` rotation and pivot alignment; a `@Data` Location pivot in world space; grid space and the matching world
    rotation agree on a turned grid; scale writes Voxel Size and keeps the pivot fixed; scale and missing-value errors;
  - Air Flow: past a wall the wind slows in front, speeds up round the edge and over the top, keeps symmetry and is
    near the free wind far off; the flow output is a voxel set with the input's tags; exposure is 1 square on to the
    wind and 0 downwind; wind shadow values behind a cube; sealed air inside a hollow box is still; Ground At Bottom
    leaves no air under the set; coarse air cells sit over the right voxels; Max Distance and Solid Distance;
    turbulence changes the flow, repeats with the seed and has no NaN; errors for zero wind, too many air cells, a bad
    Air Cell Voxels, clashing names and a missing `@Data.Wind`; the warning when the solve hits Max Iterations.
    Sources: a vent with no wind blows out every way and slows with distance, a negative one sucks in; a vortex turns
    counter-clockwise about Z; a World fan and a turned Point-space fan agree; a vent adds to the wind; a fan exposes
    the cell it is aimed at; errors for no effect attribute, zero bounds radius and a missing radius attribute.
    `PCGUtilsPerf.Voxel.AirFlow` reports timings;
  - Rasterize: a bar inside three cells gives three Fill cells, the same three Intersect cells (mode 3), a 42-cell
    Surround shell (14 with Faces), and 45 cells with every mode; a box with faces on the grid fills its 3 cells and
    touches none, and Touch Margin 0.2 touches both sides; a sphere's Fill matches the lattice count, no cell is in a
    mode it cannot be in and the three modes together leave no gap; a square path fills 4x4, touches the ring of 12,
    surrounds with 20 (16 with Faces), stacks with Cells Above and ignores a repeated first point; the Grid pin's size
    and offset are used; errors for no modes, the wrong data type, a short path and Max Cells; an open quad intersects
    quietly and warns under Fill. `PCGUtilsPerf.Voxel.Rasterize` reports timings (with Boolean);
  - Boolean: Subtract and Intersect on a row keep Ids, tags and `@Data`; Union values from the Operand, the nearest In
    cell or defaults; a chunk stays whole when untouched and becomes 7 cells when one is cut; a whole-cell grid shift
    lines up; errors for a different Voxel Size and a half-cell shift; Rasterize on a block's grid then Subtract leaves
    the block with the mesh's 8 cells gone;
  - Offset Surface: a cell grows a quarter on every side keeping Id and tags; a plate shrinks on its open sides
    only; -0.5 removes a lone cell; an L's corner cell splits in 2 and a notched block's middle cell in 3, with the exact
    inset volume, and no split keeps the notch; -1 on 5x5x5 leaves the inner 27 (Delete keeps Ids, Push gives the new
    surface the old surface's), -1.25 gives a 2.5 cube, +1.5 on a cell a 4 cube with Ids copied (None: defaults);
    a chunk expands with the shared face unmoved; Max Cells error; Absolute 25 = 0.25 cells; Amount Scale 2;
    per-cell scales 0 / 1 / -1 on a row; per-cell whole layers grow only the scaled cell and copy it; @Data scale;
    missing scale attribute error; per-point Amount 0 / 0.25 / -0.25; Amount x Scale both per point; Amount from
    `$Density`; a NaN amount names its point; a text attribute is the wrong type; override pin labels kept;
  - Match Rules: corner, edge and face rules under Any cover a 3x3x3 block (8 / 12 / 6, inside dropped) and every
    point's turned pattern lands on its cell; a wall faces out on mesh +Y with Z up; a chunk gives the same 26; About
    Up matches only the 4 top corners (Unmatched pin 22; Keep and Drop), About Up + Flip all 8; Spin Fixed keeps
    floors unturned, Random spreads yaws about up only; Thin Any / X / inverted; turned bounds on 100 x 200 x 300
    cells cover the cell; the text form round-trips and each parse error names its token and line; errors for bad
    text, no asset, no entries, a negative index, an untyped rule and clashing names; a rule-less entry catches all;
  - Shift Seams: in a row the ends stay and neighbours still meet; a 2×2 grid stays consistent; random lumpy shapes
    after Compress Random and two passes (random, then noise) have no overlaps, no box outside the shape and the same
    total volume; the shifted set meshes to the same shell. `PCGUtilsPerf.Voxel.ShiftSeams` reports timings and how
    many boxes changed.

## 6. Decisions

- **D1.** A missing grid transform means world space, matching the Volume Sampler. `Missing Grid Transform` offers
  Target Actor.
- **D2.** No Set Grid node. Graphs add `@Data.VoxelSize` themselves; the shorthand's Constant mode is the fallback.
- **D3.** Material Source defaults to Constant with the engine's default surface material.
- **D4.** Mixed offsets: Exact (default since 2026-11-13) keeps each point's box with riser stitches; Average shares averaged vertices (smooth slope, unchanged topology).

## 7. Fix queue (from the designer, 2026-11-10)

In order. The designer asked for one at a time.

1. **Done 2026-11-10.** Point shorthands (constant, or a per-point attribute, or `@Data`) in PCGUtilsCore; Offset
   Surface `Amount` and `Amount Scale` use them.
2. **Done 2026-11-10** (section 1.3). **`IsSurrounded` kept correct by every node.** It is the attribute graphs filter on to spawn voxels as instances
   (only surface cells and chunks). The designer reports Compress loses it. Audit every node that changes the cells (Compress,
   Compress Random, Expand, Offset Surface, Boolean, Shift Seams...). When the input carries it, each such node should rewrite it
   for its output: a chunk is surrounded when every cell around its box is filled.
3. **Done 2026-11-10** (section 4i, Out and Spawn). **Offset Surface keeps the grid.** Out stays a valid voxel set (whole layers applied; the cells split at inside
   corners flagged, e.g. `WasSplit`); a second pin carries the spawn points (the moved-face boxes and the split
   pieces). Today split cells make Out unusable for other voxel nodes. **Superseded 2026-11-14:** one pin; split cells
   are several points on one cell, which every voxel node now reads (section 1, Duplicate Cells).
4. **Done 2026-11-10** (section 1.2: Random Size, Random Span, Random Chance). **Compress Random controls.** A fixed chunk shape or size (compress to exactly this, or not at all) instead of
   only "anything up to the maximum"; a chance per chunk; chance and size as point shorthands read from the chunk's
   starting cell (first come, first served: cells already taken by another chunk are gone).
5. **Done 2026-11-10** (section 4c, Limit; read as "the thinnest box on either side, full thickness"; the old limit
   is an option). **Shift Seams across chunk sizes.** The shift limit of a cell or chunk on an axis becomes the smaller of its own
   size and the smallest neighbouring cell or chunk on that side, not one voxel cell.
6. **Partly done 2026-11-10.** **Shorthands across the rest of the voxel nodes**, wherever a per-point or per-set
   value makes sense. Done: Shift Seams `Strength` (per box) and `Noise Scale` (per set), Compress `Max Span` (per
   set). Left as plain settings for now, each a candidate on request: To DynMesh bevel amounts and UV settings (the
   mesher would need per-cell bevels to make per point mean anything), Rasterize margins and cell counts (per mesh),
   Air Flow solver and wake settings (one field per set), Pack Points radius and value ranges.
