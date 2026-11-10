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
| 1 | `Voxel \| To DynMesh` | The main node (section 3); Shape Bevel in 3.5b. |
| 1 | `Voxel \| Compress` | Merges equal neighbouring cells into chunks (section 1.2). |
| 1 | `Voxel \| Compress Random` | The same node preset to Shape Random: random-size boxes for looks (section 1.2). |
| 1 | `Voxel \| Shift Seams` | Slides interior seams so boxes become uneven but fill the same space (section 4c). |
| 1 | `Voxel \| Transform Grid` | Moves, turns and scales whole voxel sets about a per-set pivot; values per set from @Data (section 4e). |
| 1 | `Voxel \| Air Flow` | Wind around a voxel set: a smooth flow field on air cells (a voxel set of its own), wind shadows, exposure per solid cell (section 4f). |
| 1 | `Voxel \| Rasterize` | A DynMesh or a closed point path to cells: Fill, Intersect and Surround modes, on its own grid or another set's (section 4g). |
| 1 | `Voxel \| Boolean` | Union, intersect or subtract voxel sets by cell index; Out is In, mutated (section 4h). |
| 1 | `Voxel \| Offset Surface` | Moves the surface by Amount cells: whole layers added or removed, the fraction on the open faces (bounds), inside corners split (section 4i). |
| 1 | `Voxel \| Expand` | Turns chunks back into one point per cell (section 1.2). |
| 1 | `Voxel \| Pack Points` | Packs loose points into free cells of a voxel grid, fully or part way, without overlaps (section 4). |
| 1 | `Voxel \| Cell Properties` | Writes per-cell attributes: open sides, surrounded, inside corners, neighbourhood, depth, column and components (section 4b). Replaces the planned `Voxel \| Neighbors`. |
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
- Where neighbouring cells on one flat surface have different offsets, the shared vertex takes their average. The
  surface then slopes between them instead of stepping. Topology never depends on the offset.
- An offset ≤ −VoxelSize/2 on any side would turn the shell inside out. That is an error naming the point index,
  the side and the value.

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
| `Strength` | 0.5 | How far a seam may move, as a fraction of half the thinnest box it shrinks (0 to 1). At 1 a box can shrink to nothing; at 0.5 every box keeps at least half its size per move. |
| `Source` | Random | Random: a value per seam from the node seed and the seam's lowest face cell. Noise: 3D Perlin noise at the seam's centre (world space), so nearby seams move alike. |
| `Noise Scale` | 400 | Noise only. Feature size in world units; must be > 0 (error naming the value). |

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

**Out.** One fresh voxel set per input: plain cells ordered Z, Y, X, bounds ±Voxel Size/2, Volume Sampler seeds, the
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

- `Amount` (cells, default 0.25): positive grows, negative shrinks. It is a box (L-infinity) offset of the whole set:
  the whole part is a box dilation or erosion by that many layers (separable passes over a dense box, so corners
  stay square), and the fraction then moves the open faces of the cells that are left. `-1.25` removes one layer and
  pulls the new surface in a quarter cell; a 5x5x5 block becomes a 2.5-cell cube.
- **Fraction.** Written to the point bounds: the point stays on its cell centre and its bounds cover the box (in the
  point's frame: its rotation and scale are allowed for). Any bounds the input had are replaced. Growing never needs
  splitting (at an inside corner the two grown faces overlap inside the solid). Shrinking with `Split Inner Corners`
  (default on) takes away everything within the fraction of every empty neighbour among the 26, so cells in inside
  corners and under notches are carved too: each cell is cut at its faces and the planes the fraction in from them
  (up to 3x3x3 pieces), and the kept pieces are merged greedily into the fewest boxes found (an inside edge gives 2, an
  inside corner 3). Each box is a point copied from the cell. Off: one box per cell, open faces only (the old look).
- `Removed Cells` (negative, a cell or more): **Delete** (default) keeps each remaining cell's own values; **Push**
  gives each cell of the new surface the values of the nearest cell of the old surface, so the surface sinks with its
  look. The geometry is the same.
- `Added Values` (positive, a cell or more): **Nearest** (default; nearest old surface cell, by centre distance: hash
  grid, then a scan) or **None** (default attributes; native properties still from the nearest).
- Out: In, mutated (CLAUDE.md rule): metadata, tags and `@Data` kept, chunks expanded to one point per cell (the span
  attribute removed), cells ordered Z, Y, X. Without split cells it is still a valid voxel set whose bounds are surface
  offsets (`Voxel | To DynMesh` with Surface Offset From Point Bounds meshes it). With split cells a cell has several
  points, so other Voxel nodes reject it as duplicate cells: it is for spawning meshes.
- Error when the box worked over (bounds plus grown layers plus one) is over `Max Cells` (default 64M), naming its
  size.

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
(allowed under `Duplicate Cells: Keep First | Keep Last`, section 1) make the counts differ, so such a set always
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
- `Public/Voxel/PCGVoxelMesher.h` holds the faces → mesh core. It is plain C++, so it can be tested without a graph.
  The bevel path is `Private/Voxel/PCGVoxelBevelMesher.cpp` (`PCGVoxelMesher::BuildBeveledMesh`).
- `Public/Elements/PCGVoxelToDynMesh.h`, `PCGVoxelPackPoints.h`, `PCGVoxelCompress.h`, `PCGVoxelExpand.h`, `PCGVoxelCellProperties.h`, `PCGVoxelShiftSeams.h`, `PCGVoxelTransformGrid.h`, `PCGVoxelAirFlow.h`, `PCGVoxelRasterize.h`, `PCGVoxelBoolean.h`, `PCGVoxelOffsetSurface.h`. All derive from `UPCGSettings`, so each overrides
  `GetType()` (or they land in `Generic`). Add the `Voxel` category to the `PCGUtils.Palette.SearchContract` test.
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
  - Compress Random: boxes tile every cell exactly once, stay within Random Max Span, never mix unequal cells, and
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
    a chunk expands with the shared face unmoved; Max Cells error;
  - Shift Seams: in a row the ends stay and neighbours still meet; a 2×2 grid stays consistent; random lumpy shapes
    after Compress Random and two passes (random, then noise) have no overlaps, no box outside the shape and the same
    total volume; the shifted set meshes to the same shell. `PCGUtilsPerf.Voxel.ShiftSeams` reports timings and how
    many boxes changed.

## 6. Decisions

- **D1.** A missing grid transform means world space, matching the Volume Sampler. `Missing Grid Transform` offers
  Target Actor.
- **D2.** No Set Grid node. Graphs add `@Data.VoxelSize` themselves; the shorthand's Constant mode is the fallback.
- **D3.** Material Source defaults to Constant with the engine's default surface material.
- **D4.** Mixed offsets on one flat surface are averaged at shared vertices (smooth slope, unchanged topology).
