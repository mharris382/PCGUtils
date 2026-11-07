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
- Point N is off-grid: distance to the nearest cell centre is greater than `Grid Tolerance`. Report the point index,
  the distance and the tolerance. A setting `Off Grid Points: Error | Snap` exists; default Error.
- Points N and M fill the same cell. A setting `Duplicate Cells: Error | Keep First | Keep Last`; default Error.

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
- `Max Span` (default 16, one value for every axis; Cubes and Boxes).
- Method: an aligned octree. Power-of-two cubes on grid-aligned positions are merged level by level while all eight
  children are full and equal. This is fast, deterministic, and the same input always gives the same chunks.
  Boxes mode then merges neighbouring equal chunks greedily along X, then Y, then Z.
- **Random** method: cells are visited in Z, Y, X order. Each cell not yet taken starts a box; the box draws a
  target size per axis uniformly from 1 to `Random Max Span` (per axis, default 3×3×3) and grows one axis at a time,
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

Round trip: Expand(Compress(x)) equals x, except that point order follows cells and unlisted attributes take the
first cell's values. A test checks it.

## 2. Nodes

| Phase | Node | Does |
|---|---|---|
| 1 | `Voxel \| To DynMesh` | The main node (section 3). |
| 1 | `Voxel \| Compress` | Merges equal neighbouring cells into chunks (section 1.2). |
| 1 | `Voxel \| Compress Random` | The same node preset to Shape Random: random-size boxes for looks (section 1.2). |
| 1 | `Voxel \| Expand` | Turns chunks back into one point per cell (section 1.2). |
| 1 | `Voxel \| Pack Points` | Packs loose points into free cells of a voxel grid, fully or part way, without overlaps (section 4). |
| 1 | `Voxel \| Cell Properties` | Writes per-cell attributes: open sides, surrounded, inside corners, neighbourhood, depth, column and components (section 4b). Replaces the planned `Voxel \| Neighbors`. |
| 2 | `Voxel \| Boolean` | Union, intersect or subtract voxel sets by cell index; attributes come from the first input. |
| 2 | `Voxel \| Dilate / Erode` | Grow or shrink by N cells. Added cells copy the nearest source cell's attributes (the CLAUDE.md inheritance rule). |
| later | `Voxel \| Resample`, `Voxel \| From DynMesh` | Change cell size; voxelize a mesh. |

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
- Where neighbouring cells on one flat surface have different offsets, the shared vertex takes their average. The
  surface then slopes between them instead of stepping. Topology never depends on the offset.
- An offset ≤ −VoxelSize/2 on any side would turn the shell inside out. That is an error naming the point index,
  the side and the value.

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
- `Polygroups: None | Per Face Direction | Per Material | Per Collapsed Region`. Default Per Face Direction.
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
| `CellIndex` | Vector | no | The whole-number grid index. |

**Concave** means an inside corner that face checks cannot see. A concave edge is an empty edge neighbour whose two
faces (between it and the cell) are filled. A concave corner is an empty corner neighbour whose three faces and three
edges are all filled; a corner behind an empty edge counts only as that edge, so a crease along Z reports one edge, not
edge plus two corners. Concave diagonals are reported for open cells too (a wall cell under a notch has a concave
edge); `IsInnerCorner` is the case with no open side. Use: offsetting a surface by its open sides misses these cells;
they are where a negative offset would have to split the box, or a positive offset of the neighbours closes the gap.

- **Output:** the input with the attributes added: points, metadata, tags and `@Data` carry over (written through
  `PCGVoxelPointIO::WriteCells`).
- **Chunk rule:** every property is per cell, so a chunked input comes out **expanded** to one point per cell (each
  copying its chunk), with the span attribute removed. The values equal those of the expanded set, cell for cell.
- Points dropped by `Duplicate Cells: Keep First / Keep Last` are left out, as in Expand.
- **Validation:** an enabled output with an empty name, or two enabled outputs with the same name, is an error naming
  the settings involved; nothing is written.

## 5. Module and files

- `Source/PCGUtilsVoxel/PCGUtilsVoxel.Build.cs`. Dependencies: Core, CoreUObject, Engine, PCG,
  PCGGeometryScriptInterop (`UPCGDynamicMeshData`), GeometryCore, DynamicMesh, PCGUtilsCore (input shorthands, data
  domain helpers) and PCGUtilsDynMesh (space helpers, material assignment).
- `.uplugin`: `PCGUtilsVoxel`, Runtime, PreDefault.
- `Public/Voxel/PCGVoxelGrid.h` holds the contract, validation and cell lookup.
- `Public/Voxel/PCGVoxelMesher.h` holds the faces → mesh core. It is plain C++, so it can be tested without a graph.
- `Public/Elements/PCGVoxelToDynMesh.h`, `PCGVoxelPackPoints.h`, `PCGVoxelCompress.h`, `PCGVoxelExpand.h`, `PCGVoxelCellProperties.h`. All derive from `UPCGSettings`, so each overrides
  `GetType()` (or they land in `Generic`). Add the `Voxel` category to the `PCGUtils.Palette.SearchContract` test.
- `PCGUtilsDynMesh` must not depend on `PCGUtilsVoxel`; the dependency only runs Voxel → DynMesh.
- Tests in `Private/Tests/`:
  - contract errors, one per invariant;
  - a single cell gives a closed 12-triangle box;
  - two cells touching only at an edge stay manifold;
  - collapse leaves no T-junctions (the mesh is closed and the volume is the same with and without collapse);
  - Hard vs Smooth colors;
  - the offset keeps the mesh closed and moves only surface vertices;
  - UV tiling continues across cells;
  - `@Data` is carried over;
  - chunks: To DynMesh gives the same mesh for a set and its compressed form; Expand(Compress(x)) gives back x;
    a cell that differs in one attribute or in its offset stays out of a chunk; span validation errors;
  - Pack Points: every cell is claimed at most once; Max Radius is respected; points keep their metadata; with
    random partial settle, no two output boxes intersect;
  - Cell Properties: open sides, depth, neighbours and column values on a block; components by connectivity; concave
    edges and corners of a notch and a crease; a chunked set gives the same values as its expanded form; output names must be set and distinct;
  - Compress Random: boxes tile every cell exactly once, stay within Random Max Span, never mix unequal cells, and
    depend only on the seed; the palette alias presets the shape.

## 6. Decisions

- **D1.** A missing grid transform means world space, matching the Volume Sampler. `Missing Grid Transform` offers
  Target Actor.
- **D2.** No Set Grid node. Graphs add `@Data.VoxelSize` themselves; the shorthand's Constant mode is the fallback.
- **D3.** Material Source defaults to Constant with the engine's default surface material.
- **D4.** Mixed offsets on one flat surface are averaged at shared vertices (smooth slope, unchanged topology).
