# PCGUtils Fracture: Designer Guide

This guide explains how to fracture geometry with PCGUtils inside Unreal Engine. It is written for designers
who are comfortable working in the Level Editor and with Blueprint-style node graphs, but who are new to PCG.

The shortest useful version is:

```text
Mesh To Dynamic Mesh Element
    -> GC | From DynMesh
    -> GC | Fracture
    -> GC | To DynMesh
    -> Spawn Dynamic Mesh

Fracture | Uniform Voronoi
    -> GC | Fracture.Fracture
```

Start with this graph, verify that it works, and only then add selections, extra fracture stages, noise, or
asset saving.

> PCGUtils Fracture uses Geometry Collections as a procedural modelling tool. The data flowing through the PCG
> graph is transient. It does not become a Chaos-enabled asset or a placed destruction actor until you
> explicitly save or spawn it.

## Before you begin

This guide assumes:

- Unreal Engine 5.8.
- PCGUtils is installed and enabled.
- The PCG, Geometry Scripting, PCG Geometry Script Interop, Fracture, and Planar Cut engine plugins are enabled.
- You have a closed, watertight Static Mesh to test with. A cube, rock, pillar, or thick wall is ideal.

After enabling plugins, restart the editor. In a PCG graph, fracture nodes appear under the
`PCGUtils | GC` categories. Searching for `GC` shows the whole Geometry Collection family; searching for
`Fracture` narrows the list to fracture operations and the executor.

Avoid starting with a thin card, an open plane, or a visibly broken mesh. The fracture backend cuts a solid
volume. A surface with no inside and outside usually produces no useful pieces.

## The four data types in the graph

The pins are strongly typed, much like Blueprint pins. Understanding these four types is enough to build most
fracture graphs:

| Data | What it means | Typical node |
|---|---|---|
| `DynMesh` | Editable mesh geometry | `Mesh To Dynamic Mesh Element` |
| `GC` | A transient Geometry Collection containing bones and pieces | `GC | From DynMesh` |
| `Fracture` | Instructions describing how to cut a GC | `Fracture | Uniform Voronoi` |
| `Selection` | A reusable description of which GC bones to affect | `GC | Select | Pieces` |

A `Fracture` node is similar to a Blueprint struct or strategy object: it describes an operation, but does not
modify geometry on its own. `GC | Fracture` is the node that actually applies it. Selection nodes work the same
way: they describe a set, and another GC node evaluates that set against its input collection.

## Quick start: fracture one Static Mesh

### 1. Create and place a PCG graph

1. In the Content Browser, create a **PCG Graph** asset. Name it something like `PCG_Fracture_QuickStart`.
2. Place a **PCG Volume** in the level.
3. Size the volume so it is easy to find and select. This first graph does not need to sample the volume; the
   volume simply gives the graph a scene context and a place to preview its output.
4. Assign the new graph to the PCG component on the volume.
5. Open the PCG graph asset.

You can also put a PCG component on your own Blueprint actor. The fracture graph works the same way, but a PCG
Volume is the simplest setup for a first test.

### 2. Load the source mesh

Add **Mesh To Dynamic Mesh Element** and set its **Mesh** property to your test Static Mesh.

Leave **Extract Materials** enabled if you want the source material slots carried into the generated mesh or
saved Geometry Collection. The default LOD settings are suitable for a first test. Later, select a simpler LOD
if the source is too dense.

### 3. Convert the mesh to a Geometry Collection

Add **GC | From DynMesh** and connect the Dynamic Mesh output from the previous node to its `DynMesh` input.

Leave these defaults unchanged:

- **Merge Inputs Into One Collection:** on.
- **Split Islands:** off.

Enable **Split Islands** only when one source mesh contains several disconnected shells that should start as
separate pieces.

### 4. Create the fracture pattern

Add **Fracture | Uniform Voronoi**. This is the best first fracture operation because it creates its own sites
inside the target bounds and needs no point setup.

Use these starter settings:

| Setting | Starter value | Effect |
|---|---:|---|
| **Min Voronoi Sites** | 20 | Minimum requested cell count |
| **Max Voronoi Sites** | 20 | Match Min for a repeatable requested count |
| **Random Seed** | 0 | Changes the pattern without changing the graph |
| **Chance To Fracture** | 1.0 | Fractures every targeted piece |
| **Grout** | 0 | Leaves no gap between pieces |
| **Group Fracture** | on | Uses one pattern across all targets |
| **Add Surface Noise** | off | Keeps the first preview fast |

The requested site count is not a guarantee that the final collection will contain exactly that many visible
pieces. Sites that do not contribute a valid cell may not produce a piece.

### 5. Apply the fracture

Add **GC | Fracture**.

Connect:

- `GC | From DynMesh.GC` to `GC | Fracture.GC`.
- `Fracture | Uniform Voronoi.Fracture` to `GC | Fracture.Fracture`.

Leave `Selection` unconnected. An unconnected Selection pin means the operation targets the available geometry
pieces.

Leave **Keep Hidden Source Geometry** off. The old uncut shape is not useful in the normal pipeline and keeping
it increases memory and face counts after every fracture stage.

### 6. Preview the result in the level

Add **GC | To DynMesh** and connect the fractured `GC` to it. Keep **Output** set to **Combined**.

Add Unreal's **Spawn Dynamic Mesh** node and connect the resulting `DynMesh`. Generate the PCG component. The
fractured object should appear in the level as one generated Dynamic Mesh component.

`Combined` does not weld the fracture pieces into one solid. It places all surviving pieces in one DynMesh data
object while preserving per-piece PolyGroups. This is convenient for previewing and for later DynMesh work.

If nothing appears, work through [Troubleshooting](#troubleshooting) before adding more nodes.

## Other ways to provide source geometry

The quick start begins with a Static Mesh because it is easy to reproduce. Production graphs can enter the GC
pipeline in several ways:

| Source | Graph entry | When to use it |
|---|---|---|
| Static or Skeletal Mesh asset | `Mesh To Dynamic Mesh Element` -> `GC | From DynMesh` | Fracturing a content asset without preparing a GC first |
| Existing DynMesh graph | Connect directly to `GC | From DynMesh` | Fracturing geometry already generated or modified in PCGUtilsDynMesh |
| Geometry Collection asset | `GC | From Asset` | Starting from an authored collection or adding another fracture level |
| Placed Geometry Collection component | `GC | Get GC Data` | Reading selected actors or components from the current level |

`GC | From Asset` and `GC | Get GC Data` copy their source into transient PCG data. Fracturing or pruning that
data does not modify the original asset or placed component. Use **GC | Save Asset** when the edited result
should become persistent.

## Choosing a fracture operation

All fracture-operation nodes connect to the same multi-connection `Fracture` pin on **GC | Fracture**.

| Operation | Use it for | Important detail |
|---|---|---|
| **Fracture | Uniform Voronoi** | Rocks, concrete, irregular chunks, general testing | Easiest operation; generates its own sites |
| **Fracture | Voronoi From Points** | Art-directed damage, impacts, density-driven patterns | Each PCG point is a Voronoi site; normally keep **Sites Are World Space** on |
| **Fracture | Planar** | One or more deliberate flat cuts | A point transform can place each plane; the point's Z axis is the plane normal |
| **Fracture | Slice** | Tiles, panels, slabs, regular grids | Counts are cutting planes, so `1,1,1` produces up to `2 x 2 x 2` divisions |
| **Fracture | Brick** | Masonry bond patterns | Requires Grout greater than zero |
| **Fracture | Mesh** | Cutting with a custom closed shape | The cutter can be a DynMesh, points carrying a Static Mesh attribute, or both |

You may connect several fracture operations to one **GC | Fracture** node. They execute in their **Priority**
order. Use clearly separated priority values such as 0, 10, and 20 so another operation can be inserted later.

For a first production graph, prefer one operation per executor. It is easier to inspect the intermediate GC,
measure cost, and understand which stage caused a failure.

## Common fracture settings

### Random Seed

The same inputs and seed produce the same pattern. Expose the seed as a PCG override when a placed Blueprint or
PCG component should produce controlled variations.

### Chance To Fracture

At `1.0`, every selected target is processed. Lower values are useful when applying a second fracture pass to
only some existing pieces. They are less useful on the very first single-piece mesh, where a failed chance roll
can leave the whole object unchanged.

### Group Fracture

When enabled, one pattern spans all selected pieces. When disabled, each selected piece receives its own pattern
and seed. Disable it for a second-level pass when each large chunk should break independently.

### Grout

Grout creates space between neighbouring pieces. Start at zero. Increase it only when the visible separation is
part of the art direction; large values can consume small pieces and cause a cut to produce nothing.

### Split Islands

Keep this enabled on fracture operations unless you have a specific reason not to. It turns disconnected parts
left by a cut into separate pieces.

### Surface Noise

Surface noise is expensive because enabling it also subdivides cut faces. The cost comes mainly from the
tessellation required by **Surface Resolution**, not from moving vertices.

Use this workflow:

1. Build and approve the fracture pattern with noise off.
2. Turn noise on only for the final look-development pass.
3. Begin with a coarse Surface Resolution, roughly one tenth of the object's size.
4. Reduce the value gradually while watching generated triangle counts and editor responsiveness.

Do not begin with the engine's smallest resolution on a large object.

## Targeting only part of a collection

The `Selection` pin on **GC | Fracture**, **GC | Prune**, and **GC | Separate Selection** accepts GC bone
selectors. You can think of these as Blueprint functions that return a set of piece IDs.

Useful starting selectors include:

- **GC | Select | Pieces:** every visible geometry-bearing piece.
- **GC | Select | With Exterior:** pieces that contain original outer surface.
- **GC | Select | With Interior:** pieces that contain fracture-generated surface.
- **GC | Select | Random:** a deterministic percentage or count of pieces.
- **GC | Select | Bones At Level:** bones at one hierarchy depth.

Selection modifiers accept another Selection and move or grow it:

- **Parent**, **Children**, **Ancestors**, and **Descendants** move through the hierarchy.
- **Contact** grows to physically touching pieces. Increase **Iterations** instead of chaining several Contact
  nodes.
- **To Pieces** resolves selected clusters down to the visible pieces below them.
- **Invert** selects the complement.
- **OR** unions every selector connected to its single multi-connection `Selection` pin.
- **AND**, **XOR**, and **Subtract** combine two selections.

Selections do not modify the collection. Connect the final selector to the operation that should use it.

### Selecting the pieces created by a fracture stage

Enable **Output Result Selector** on **GC | Fracture** when the next stage should affect only bones created by
that executor. A `Result` output pin appears on the node. Connect it to a second **GC | Fracture**,
**GC | Separate Selection**, or another selection modifier.

This is useful for hierarchical damage: make a few large pieces in the first stage, capture the result, then
apply a denser second fracture only to those new pieces.

## Removing pieces to create damage

Fracturing creates pieces but does not remove them. To make a hole, chipped corner, or missing section, use this
pattern:

```text
Fractured GC
    -> GC | Bones To Points
    -> ordinary PCG point filters
    -> GC | Select | Bones From Points
    -> GC | Prune
    -> GC | To DynMesh
```

### 1. Convert pieces to points

Connect the fractured collection to **GC | Bones To Points**. It emits one point per fracture piece. Keep
**Output To World Space** on so normal PCG bounds, distance, and volume tests line up with the level.

For believable visible damage, enable these two optional attributes:

- **Is Exterior**, default name `GC_IsExterior`.
- **Exposure Ratio**, default name `GC_ExposureRatio`.

### 2. Filter the points

Use standard PCG filters just as you would filter spawn points. For example:

1. Keep points where `GC_IsExterior` is true.
2. Keep points where `GC_ExposureRatio` is greater than about `0.15`.
3. Intersect that result with an impact volume, distance field, or designer-placed bounds.
4. Optionally take a random subset.

Filtering exterior pieces avoids choosing a completely buried chunk whose removal would make no visible change.
Exposure Ratio is scale-independent: `0` is fully buried, while values approaching `1` have mostly original
outer surface.

### 3. Convert filtered points back to a selection

Connect the filtered points to **GC | Select | Bones From Points**, then connect its `Selection` output to
**GC | Prune** alongside the same fractured GC state.

Keep source-identity validation enabled. Piece indices can change whenever the collection is fractured or
pruned. If you fracture the GC again after creating the points, regenerate **GC | Bones To Points** from that new
state before selecting.

If the resolved selection is empty, **GC | Prune** is a silent no-op. This makes optional damage volumes safe:
the object passes through unchanged when the volume hits no pieces.

## Using the pieces separately

On **GC | To DynMesh**, set **Output** to **Per Piece** when each chunk should become its own output mesh.

- Use **Collection** space when the pieces should remain assembled where the fracture placed them.
- Use **Piece Local** when the pieces will be independently placed, instanced, or handed to another system.

Per-piece outputs carry GC identity attributes so they can be traced back to the source collection state. The
optional surface attributes use the same definitions as **GC | Bones To Points**.

In **Combined** mode, these PolyGroup layers are useful downstream:

| Layer | Purpose |
|---|---|
| `GC_Bone` | Identifies the source fracture piece |
| `GeometryCollectionInternalFaces` | Separates cut faces from the original exterior |

For example, a DynMesh **Select by PolyGroup** followed by a material operation can assign a concrete interior
material only to newly cut faces.

## Saving a Geometry Collection for Chaos

The quick-start graph previews a transient DynMesh. If the result must become a persistent Geometry Collection
asset, connect the final GC to **GC | Save Asset** instead.

Before saving:

1. Set the export path and asset name in the node's Asset settings.
2. Leave **Export Materials** enabled unless materials will be assigned later.
3. Review Nanite, collision, mass, clustering, and damage settings in **Bake Settings**.
4. Generate the PCG graph. If **Open Save Dialog** is enabled, choose the location when prompted; otherwise the
   node uses its configured asset path and name.
5. Locate and inspect the resulting Geometry Collection asset in the Content Browser.

Saving is an explicit authoring action. Treat the output asset like any other baked asset: review its collision,
damage thresholds, material slots, and level hierarchy before using it in gameplay.

To use it for destruction, place the saved asset as a Geometry Collection component or use
**GC | Spawn Component** from `PCGUtilsSimulation`. Runtime breakage, fields, solver configuration, and removal
behaviour belong to the Chaos setup; the fracture graph determines the authored pieces and hierarchy.

## Iterating safely in the PCG Editor

- Change one expensive setting at a time, then regenerate.
- Keep site counts low while building the graph. Increase them after selection and material logic works.
- Inspect the output after each major conversion: DynMesh, GC, fracture, prune, then final DynMesh.
- Use node debugging or data inspection to verify point counts after **GC | Bones To Points** and each filter.
- Expose art-direction controls with PCG overridable settings instead of duplicating a graph for every actor.
- Disable automatic regeneration on a heavy placed component while tuning high-site-count or noisy fractures.
- Keep a cheap preview branch when the final branch saves an asset or creates high-resolution geometry.

## Troubleshooting

### The graph generates, but nothing is visible

- Confirm **Spawn Dynamic Mesh** is connected to **GC | To DynMesh**.
- Confirm the PCG component or volume has been generated.
- Verify the source Static Mesh is assigned on **Mesh To Dynamic Mesh Element**.
- Try a known closed cube to rule out bad source topology.
- Set Uniform Voronoi Min and Max to 10, Grout to 0, and Surface Noise off.

### Fracture reports that it cut nothing

- The source may be open, paper-thin, or non-manifold.
- A selection may contain only roots or clusters instead of visible pieces. Add **To Pieces**.
- Grout may be large enough to consume the result.
- Point-driven sites or planar cuts may miss the mesh.
- A custom cutter may be open or may not overlap the target.

### Voronoi From Points says too few sites are inside

At least two useful sites are needed to define a cut. Check that the points overlap the mesh volume. Keep
**Sites Are World Space** on for ordinary PCG points. Disable it only when the points were deliberately authored
in the source mesh's local space.

### A point-based selection is stale

The points came from an earlier collection state. Reconnect **GC | Bones To Points** after the most recent
fracture or prune, then rebuild the filtered selection. Do not reuse recorded bone points across a topology
change.

### The editor becomes slow or the result has too many triangles

- Turn Surface Noise off first.
- Increase noise Surface Resolution.
- Reduce Voronoi sites, slices, planes, or cutter copies.
- Use a lower source-mesh LOD.
- Break a multi-stage graph into separately inspectable passes.
- Leave **Keep Hidden Source Geometry** off.

### The wrong pieces are removed

- Verify **GC | Prune.Invert Selection** is set as intended.
- Inspect points immediately before **Bones From Points**.
- Confirm the points and GC came from the same collection state.
- Convert hierarchy selections **To Pieces** before pruning when the graph selected clusters.

### Internal and exterior materials look the same

Enable **Override Internal Material** on **GC | Fracture** and provide a valid collection material-slot index, or
convert to DynMesh with **Tag Internal Faces** enabled and assign material by the
`GeometryCollectionInternalFaces` PolyGroup layer downstream.

## Production checklist

Before handing a fracture graph to another designer or using its baked result:

- [ ] The source mesh is a closed solid and uses an appropriate LOD.
- [ ] The fracture works with Grout at zero and Surface Noise off.
- [ ] Site, slice, plane, or cutter counts are appropriate for the asset's size and importance.
- [ ] Selections target pieces, not only roots or clusters.
- [ ] Point-based selections are generated from the same GC state they modify.
- [ ] Hidden source geometry is not being kept accidentally.
- [ ] Interior faces have the intended material treatment.
- [ ] The final output mode is correct: Combined, Per Piece, or saved GC asset.
- [ ] Saved assets have reviewed collision, mass, clustering, and damage settings.
- [ ] The graph has been tested after moving, rotating, and scaling its owning actor in the level.
- [ ] Generation time and triangle counts are acceptable with final settings.

## Where to go next

Once the quick-start graph is stable, useful extensions are:

- Drive **Voronoi From Points** with a designer-authored impact scatter.
- Use **Planar** points along a spline to art-direct cracks or slab cuts.
- Use **GC | Fracture.Result** to select the pieces created by one fracture stage.
- Grow damage through touching pieces with **GC | Select | Contact**.
- Emit cluster data from **GC | Bones To Points** for PCGEx flood fill or pathfinding.
- Convert back to DynMesh and use the `GC_Bone` and internal-face PolyGroups for materials or mesh operations.

For exhaustive settings, identity rules, cluster interoperability, and implementation limitations, see the
existing [PCGUtilsFracture reference](PCGUtilsFracture.md).
