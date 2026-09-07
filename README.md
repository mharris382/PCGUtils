# PCGUtils

PCGUtils extends Unreal Engine's PCG framework with composable Dynamic Mesh processing, procedural vertex
painting, editor asset-authoring workflows, and artist-facing PCG authoring utilities.

Unreal Engine **5.8** · MIT licensed · <https://github.com/mharris382/PCGUtils>

---

## Overview

PCGUtils began as a set of conveniences for building reusable PCG tools: an actor base that manages its own
bounds and asset save paths, splines and markers that carry procedural control data, and native nodes for
pulling that authored data into a graph. Those utilities are still here and still supported.

What the plugin has grown into is larger. The majority of the codebase is now **PCGUtilsDynMesh**, a layer that
makes Dynamic Mesh geometry behave like ordinary composable PCG data — meshes flow between operations, subsets
of geometry are described by reusable selectors, and mesh work can be deferred into recipes that are realized
against points. **PCGUtilsPainter** builds on that to author vertex colors and vertex masks procedurally, and
**PCGUtilsFracture** borrows Unreal's Geometry Collection stack as a transient modelling backend.

The consistent goal across all of it is that these workflows should feel *native to PCG*: data flows between
operations, behaviour is reusable, point processing participates in mesh work, and repetitive editor
bookkeeping is automated where practical.

| Module | Role |
|---|---|
| `PCGUtilsDynMesh` | PCG-native Dynamic Mesh processing: selections, selectors, builders, topology, attributes, conversion |
| `PCGUtilsPainter` | Procedural vertex-color and vertex-mask authoring for Dynamic Meshes and Static Mesh Components |
| `PCGUtils` | Actors, components, provider interfaces, component-query nodes, bake settings, save parameters |
| `PCGUtilsCore` | Shared factory foundation used by the Selector, Builder and Painter families |
| `PCGUtilsFracture` | **Experimental.** Geometry Collection fracture as a step in a procedural geometry pipeline |
| `PCGUtilsDataCache` | Saving and reloading PCG data as cache assets |
| `PCGUtilsMaterialCache` | Shared material-variant cache and the node that resolves against it |
| `PCGUtilsEditor` | Component visualizers, details customizations, editor styling, Graph Batch asset |

---

## Dynamic Mesh Processing

### One contract for every operation

PCGUtilsDynMesh is not a set of Geometry Script functions re-exposed one node at a time. Almost every node that
touches an existing mesh derives from a single settings contract, and that contract is what makes the nodes
compose.

A process node's main input accepts **either** a Dynamic Mesh **or** a DynMesh Selection, and it exposes one
optional **Selector** pin:

- Given a whole mesh, a connected Selector produces the effective selection.
- Given a selection, a connected Selector is intersected with it.
- Given neither, the operation applies to the whole mesh — unless it is one that requires a selection, in which
  case the graph warns and skips rather than silently doing something surprising.

Selection pins are domain-agnostic. A triangle selection handed to a vertex-domain operation is converted
centrally, using Geometry Script's inclusive conversion semantics, rather than being rejected or quietly
producing nothing. You never have to rebuild an upstream selection to satisfy a downstream node's internal
element type.

Operations that change topology go a step further: they report the geometry they created. Extrude, Inset and
Bevel can hand their result back as selection data, or as a reusable **Result Selector**, so the classic
modelling chain works directly in a graph:

```
Select Up Faces ─▶ Extrude ─▶ Inset ─▶ Extrude ─▶ Bevel
                     └── Result Selector ──┘
```

### Selectors

Many mesh operations should only apply to *part* of a mesh. Rather than every node inventing its own targeting
settings, PCGUtilsDynMesh separates the question "which geometry?" from the question "what do I do to it?".

A **Selector** is a reusable predicate on mesh elements, travelling on its own colored pin. It carries no mesh:
the same Selector can drive several operations, sit inside a subgraph, or be evaluated against different meshes.

If you have used PCGEx, the mental model transfers directly:

> **PCGEx Filters → subsets of PCG points**
> **PCGUtils DynMesh Selectors → subsets of mesh geometry**

The architecture is deliberately modelled on PCGEx's factory pattern, but there is no dependency in either
direction and no API compatibility is implied.

Selectors currently available include selection by **normal**, **distance**, **PolyGroup**, **vertex color**,
**self-occlusion**, **point bounds**, **vertex IDs**, **proximity to a spline**, **sharp edges**, **edge
direction**, and **triangle properties**; plus the modifiers **Expand**, **Contract**, **Expand to Connected
Region**, and **Extract Selection Boundary**. A **Selection Logic** node nests them into AND / OR / NOT groups,
with a per-child priority that controls short-circuit order — put cheap or highly selective predicates first
and the expensive ones never run on excluded elements.

Every selection operation offers two presentations of the same implementation: a **Selection** mode that
materializes concrete selection data, and a **Selector** mode that decorates an upstream Selector and stays
reusable. There is one node per algorithm, not two.

`[ADD SCREENSHOT/GIF HERE - Dynamic Mesh selection created using point data and PCG/PCGEx composite filters]`

### DynMesh ↔ Points

This is one of the defining ideas of the module.

PCG already has a mature ecosystem for querying, filtering, transforming, sorting, sampling and randomizing
points — and PCGEx extends it considerably further. PCGUtilsDynMesh does not attempt to recreate any of that
for mesh elements. It instead makes mesh information available *as points*, lets the existing ecosystem do the
work, and then turns the result back into something a mesh operation can consume.

```
Dynamic Mesh
   └─▶ DynMesh To Points / Sample DynMesh / Selection To Points
          └─▶ PCG + PCGEx point filtering, attribute math, scatter, sorting
                 └─▶ Select from Vertex IDs / Select in Bounds  (Selector)
                        └─▶ any Dynamic Mesh operation
```

The nodes that bridge the two representations:

| Node | Direction |
|---|---|
| **DynMesh To Points** | Vertices → points, preserving position, vertex color and normal-derived rotation. Optionally writes the source vertex ID to a named attribute. |
| **Sample DynMesh** | Surface → points, matching the vanilla Mesh Sampler's sampling modes and metadata conventions, but scoped by a selection or Selector. |
| **DynMesh Selection To Points** | The vertices a selection touches → points. |
| **DynMesh Selection To Paths** | Selection boundaries → path data. |
| **Select from Vertex IDs** | Points carrying vertex IDs → Selector. |
| **Select in Bounds** | Point bounds → Selector. |
| **Apply Points To Dynamic Mesh** | Points → vertex positions, normals, vertex colors (whole or per-channel) and UV channels. |
| **Painter by Vertex ID** | Points → per-vertex paint values, by explicit vertex correspondence. |

Because these are ordinary PCG point data on ordinary point pins, anything that consumes points can sit in the
middle of that pipeline.

### Builders

A **Builder** is a recipe for geometry rather than geometry itself — a plan that has not been executed yet.

Eleven Builder nodes cover the Geometry Script primitives (Box, Sphere, Capsule, Cylinder, Cone, Torus,
Rectangle, Rounded Rectangle, Disc, Linear Stairs, Curved Stairs). Each exposes its parameters as normal
overrideable PCG properties, plus a shared **Fitting** block that decides how the shape is sized and placed
against a seed point's own bounds:

- **Scale To Fit** — none, uniform, or a per-axis strategy (Fill, Min, Max, Avg)
- **Justification** — per-axis alignment of the primitive within the seed bounds
- **Padding** — per-axis inset or outset applied before fitting (two boxes from one seed, differing only by
  inset, is what makes a window frame a boolean subtract away)
- **Local Transform** — an offset/rotation/scale pre-transform on the primitive itself

Builders become genuinely useful because processing nodes understand them. Feed a Builder into a supporting
operation and it does not run: it wraps itself around the recipe and hands back a *larger* Builder. Nothing is
evaluated until a materializer realizes the whole expression against seed points.

```
Box Builder ─┐
             ├─▶ Transform ─▶ Boolean Subtract ─▶ Set PolyGroup ─┐
Box Builder ─┘  (still a Builder the whole way)                  │
                                                    PCG Points ──┴─▶ Create Primitive ─▶ Dynamic Mesh
```

`Create Primitive` is the materializer. Several Builders can feed it at once to compose one compound shape per
seed, and its output mode controls the split: one mesh per seed, one mesh for everything, one per Builder, or
one per Builder per seed.

Builder support is opt-in per operation, and currently covers the families where deferral is meaningful:

| Supports Builders | Does not |
|---|---|
| Topology — Extrude, Inset, Bevel, Boolean, Delete Selection, Remesh | Painter consumers (Builder integration is deferred by design) |
| Deform — Smooth, Warp, Transform, Deform Along Spline | Conversion and query nodes |
| Attributes — Set Material, Set Vertex Colors, Set/Clear PolyGroup, Project UVs | Write DynMesh LODs |

Builders also carry their own rigid frame and an internal active selection, so a topology result flows to the
next operation in the chain without an extra Selector wire.

`[ADD SCREENSHOT/GIF HERE - Compound Dynamic Mesh builder recipe being instantiated from PCG points]`

### World space and local space

PCG authors points, splines and bounds in world space; Dynamic Mesh geometry in this module is target-actor
local. Nodes that combine the two handle the conversion themselves through shared space helpers — point-bounds
selectors, spline selectors, Create Primitive seeds, painter brush centres and fracture site points all compare
correctly without you wiring transform maths into the graph.

That said, this is honest rather than magical. Unreal's `UPCGDynamicMeshData` carries no record of which space
a mesh is in, so conversion can only be automatic where a node knows the provenance of the data it was given.
The explicit **ToWorld** and **ToLocal** nodes remain part of the toolkit for arbitrary meshes, and operations
that take directions or distances work in the coordinate space of the mesh they are given.

### Operation families

A representative sample rather than a full node list — see *Examples and documentation* below.

**Creation** — `Create Primitive`, eleven primitive `… Builder` nodes, `SplineMesh To DynMesh` (deforms a
static mesh along every segment of a PCG spline), `Get Spline Mesh Data` (extracts already-authored Spline Mesh
Component geometry from actors in the level).

**Topology** — `DynMesh Boolean` (full Geometry Script boolean set, N:N / N:1 / 1:N broadcasting, sequential
operands, optional per-operand PolyGroup tagging, and a mode that splits the result into separate A-derived and
B-derived outputs), `Extrude DynMesh Faces`, `Inset DynMesh Faces`, `Bevel DynMesh Edges`, `Delete DynMesh
Selection`, `Separate DynMesh Selection`, `Remesh DynMesh` (uniform and adaptive).

**Deform** — `Smooth DynMesh`, `Warp DynMesh` (bend, twist, flare/squish), `Transform DynMesh`, `Deform DynMesh
Along Spline`, `ToWorld` / `ToLocal`.

**Attributes** — `Set DynMesh Material`, `Set DynMesh Vertex Colors`, `Set DynMesh PolyGroup`, `Clear DynMesh
PolyGroups`, `Project DynMesh UVs`.

**Selection** — the Selector and Selection families described above, plus `Build DynMesh Selection` to
materialize a Selector against a mesh.

**Conversion and output** — `DynMesh To Points`, `Sample DynMesh`, `DynMesh Selection To Points`, `DynMesh
Selection To Paths`, `Write DynMesh LODs`.

Also included: brush actors and components (`DynMeshBrushActor`, `StaticMeshBrushActor`,
`StaticMeshBrushComponent`) that expose additive/subtractive brush configuration and convert static-mesh
geometry for Dynamic Mesh workflows, and a reusable closest-point **surface correspondence** utility for
transferring per-corner attributes between two representations of the same surface.

`[ADD SCREENSHOT/GIF HERE - A complete Dynamic Mesh graph and its resulting geometry, showing selectors and topology operations chained together]`

---

## Procedural Vertex Painting

Vertex colors are cheap procedural data embedded directly in a mesh: material masks, texture and material
blending, per-instance colour variation, surface effects, environment blending, and reusable mesh variation all
fall out of having the right values in the right channels.

PCGUtilsPainter is an authoring system for those masks, not a node that sets vertex colors. The point is to
make complex vertex data practical to produce procedurally, instead of hand-painting every asset.

### How a Painter graph is built

A **Painter** is a reusable field expression evaluated per mesh sample. It produces either an untargeted scalar
or a color with explicit valid channels; the consuming node decides where the result is written.

**Providers** produce values:

| Painter | What it evaluates |
|---|---|
| **Axis Gradient Painter** | A clamped projection between Start and End along a normalized axis, in mesh-local or world space, optionally inverted |
| **Bounds Brush Painter** | PCG points as independently sized world-space brushes — oriented ellipsoids fitted to point bounds, or a uniform radius from an attribute — with an optional solid inner core, Hard/Linear/Smooth falloff, and Max/Min/Add/Multiply overlap reduction |
| **Painter by Vertex ID** | Explicit point → vertex correspondence, reading values from an attribute. Not projection, not falloff |
| **Random Value by Mesh Island** | A deterministic value per connected component of the mesh, seeded stably so adding geometry elsewhere does not reshuffle existing islands |
| **Selection Painter Switch** / **Selection to Painter** | A DynMesh Selector classifies every vertex, and each branch is a constant or another Painter |

**Composition** combines them: **Painter Blend** (Add, Subtract, Multiply, Darken, Lighten, Mix, Screen, with a
Factor and an optional scalar Mask), and **Combine Painters**, which takes optional R / G / B / A Painter inputs
and produces a single color Painter.

**Consumers** write the result: **Paint DynMesh Vertex Color** and **Paint Static Mesh Vertex Colors**. Both
expose Write Channels, so a Painter graph can author the red channel without disturbing what is already in
green, blue or alpha — and both scope their write by the usual selection/Selector contract.

Whatever the target, the whole Painter graph is evaluated exactly once, against one canonical mesh, in a single
traversal. Color writes are seam-aware: a vertex's result is written to every color element attached to it, so
UV and normal seams do not show up as painting artifacts.

### Targets and limits

Two target types are supported today:

- **Dynamic Mesh** — the mesh in the graph is painted in place.
- **Static Mesh Components** — targets are resolved from a soft-object-path attribute (the `ComponentReference`
  that `Get Static Mesh Data` emits by default). Painting writes the component's **override vertex colors**;
  the `UStaticMesh` asset itself is never modified. The Painter is evaluated on a canonical LOD0 mesh, and the
  painted channels are transferred to lower LODs by closest-point projection, so randomized and
  topology-dependent Painters stay spatially consistent across the LOD chain.

Current limitations, stated plainly:

- Static Mesh painting is an **editor-authoring** operation — game thread, transacted, never cached.
- **Nanite-rendered components are skipped** with a graph warning; the Nanite raster path ignores override
  vertex colors.
- **ISM / HISM components are skipped** with a graph warning; one override buffer is shared by every instance.
- `Painter by Vertex ID` requires a native Dynamic Mesh target and is rejected on Static Mesh Components.
- Geometry Collections, Mesh Paint Textures and per-instance painting are not supported.

### What you get out of it

**Ground blending.** An Axis Gradient Painter along the vertical axis produces a mask that a material reads to
dissolve the base of an asset into the terrain — no per-asset painting, and the same graph works on every mesh
it is pointed at.

`[ADD SCREENSHOT/GIF HERE - Axis Gradient Painter creating a ground-blending vertex mask]`

**Reusable mesh variation.** Because brushes come from PCG points, spatial regions can author parts of a mask
procedurally. Placing PCG Markers over a bird mesh and feeding them to a Bounds Brush Painter writes a palette
coordinate into the red channel; the material reads `VertexColor.R` as a gradient lookup. The same underlying
mesh then produces dramatically different species — a scarlet macaw and a very different bird are the same
asset with different marker placement.

`[ADD SCREENSHOT/GIF HERE - Bounds Painter and PCG Markers turning one bird mesh into multiple species/color variants]`

**Composite masks.** Painters combine like any other expression — an axis gradient multiplied by a brush field,
written to alpha as a wind mask, while a separate brush field writes red as a blend mask, all in one traversal.

---

## PCG Authoring Utilities

The original layer of PCGUtils: components and actors that make reusable PCG tools cleaner to author, and
native nodes that pull their authored data into a graph.

### PCGActorBase

`APCGActorBase` is a reusable actor base for PCG-driven tools. It owns a PCG component and an editor bounds
box, computes bounds from supported components and splines, and can regenerate when its utility components are
edited. It also provides:

- A stable seed and standardized bake settings.
- Save name, save path, asset group label, and baked-asset metadata.
- Pre-bake and post-bake override graphs.
- A Call in Editor action to recenter the actor pivot without moving attached spline geometry.
- Blueprint hooks for custom bounds, editor color, and remapping other local-space data after recentering.

Its purpose is to remove manual asset bookkeeping from tools that generate persistent assets.

### Splines and paths

**PCG Spline Component** extends Unreal's spline component with the `PCGPathProvider` interface and shared
`PathData`: a path group ID, **PathDensity**, **PathColor**, **PathWidth**, **PathHeight**, and a per-path
override graph. Spline-authored tools therefore carry procedural control data with the spline itself, instead
of requiring a parallel set of parameters somewhere else. It also supports optional curved-segment
subdivision, editor colors, and edit-triggered regeneration.

**PCG Child Spline Component** copies a normalized section of an attached spline, or of a tagged spline
component on another actor. It handles open and closed sources, forward wrapping across a closed-loop seam,
configurable endpoint tangent modes, preserved editable points before and after the copied section, a Z offset,
automatic source-change detection, and manual refresh. Its visualizer marks the copy range on the source.

**Shape Path Component** produces an ordered path from an instanced shape generator rather than hand-edited
control points — Circle, Arc, Rectangle, Polygon and Star are built in. **PCG Shape Spline Component**
generates a real spline component from the same generators, optionally preserving manual point offsets as
generator settings change.

![](https://i.imgur.com/utLnXaE.png)

### PCG Marker

A **PCG Marker Component** is a manually positioned PCG point represented as a *component* rather than an
actor. It carries bounds and standardized point data — group ID, density, steepness, color, and a per-point
override graph — and is retrieved into a graph with **Get Marker Data**.

The component-based approach is deliberate. Markers belong to and move with the PCG tool that owns them,
instead of scattering large numbers of independent marker actors through the World Outliner. The editor
visualizer supports configurable selected/unselected colors, fill opacity, wireframe display, and ellipsoid
drawing.

![](https://i.imgur.com/usURBXv.png)

### Provider interfaces and shared data

Blueprint-compatible interfaces decouple the PCG nodes from concrete component classes:

- `PCGPathProvider` — ordered path points, local/world-space indication, path metadata, closed-loop state.
- `PCGPointProvider` — point collections with shared point metadata.
- `PCGBoundsProvider` — actor-relative bounds contribution.
- `PCGComponentProvider` — access to a primary PCG component and its regeneration policy.

`FPathComponentData`, `FPointComponentData`, bake settings and override-graph types give these systems
consistent metadata and extension points. Implement an interface on your own component and the existing query
nodes pick it up.

### Component query nodes

- **Get PCG Path Data** — collects any `PCGPathProvider` as point array data or PCG spline data, preserving path
  metadata and tags.
- **Get PCG Point Data** — collects points from `PCGPointProvider` components.
- **Get Marker Data** — retrieves marker components and their point metadata.
- **Get PCG Spline Data**, **Get Shape Path Data**, **Get Spline Data With Overrides** — the specialized path
  component workflows.
- **Get Static Mesh Data** — one point-data collection per matching static-mesh component, emitting a
  `ComponentReference` soft object path by default (this is what the Static Mesh Painter consumes).
- **Get Override Graph Sets** — routes data according to serialized override-graph metadata.
- **Capture / Restore** aliases for Density, Position Z and Bounds — stash a point property, do something to
  the points, put it back.

![](https://i.imgur.com/f2wDGJI.png)

---

## Procedural Asset Authoring

PCGUtils is useful for runtime generation, but a lot of it exists to support the *editor* case: using PCG to
process authored geometry and commit an optimized result as a real asset.

The recurring friction there is bookkeeping. A graph that produces assets needs to decide a name and a path
every time it runs, and doing that by hand — or through a save dialog on every execution — makes the workflow
unusable. PCGUtils is built so the graph can determine asset name, save path and actor-specific identity on its
own.

### Save parameters and bake identity

**Get Asset Save Parameters** derives the `AssetName` and `AssetPath` strings that PCG's asset-saving nodes
expect, from an existing asset reference. It works from a soft object reference set on the node, or in bulk
from a soft-object-path attribute on an Attribute Set. By default it appends a suffix (`_Copy`) so the derived
name can never collide with the source asset; enable **Use Actual Asset** when you intend a downstream node to
modify or replace the source.

**Bake settings** are convention-based rather than interface-based. Bake subgraphs read a visible actor
property named exactly `BakeSettings` using the vanilla **Get Actor Property** node. `APCGActorBase` already
provides it; to integrate a custom actor, expose an `FPCGUtilsBakeSettings` property with that name. The
struct supplies the baked asset save name, the output directory, a group label (the final name is
`{GroupLabel}_{SaveName}`), and pre/post-bake override graphs. The default output directory comes from the
plugin's project settings, `/Game/_Generated` out of the box.

When baking several meshes from one graph, give each Dynamic Mesh data set a distinct `@Data.Label` so the
derived save names stay unique.

### A representative workflow

1. An artist places editor-only rock meshes to block out a cliff section.
2. A PCG graph gathers those components and processes them — boolean union, cleanup, selection-driven material
   assignment, vertex painting.
3. The merged geometry is optimized.
4. A persistent Static Mesh asset is written to an automatically determined path and name.
5. The authored source geometry stays editor-only while the baked result is what ships.

`[ADD SCREENSHOT/GIF HERE - Editor-authored cliff rocks processed by PCG and baked into a single optimized Static Mesh asset]`

Note on scope: the asset *creation* step uses Unreal's own PCG asset-saving nodes. What PCGUtils contributes is
the identity around them — name, path, group, actor association — plus the LOD commit step below.

### Committing LODs

**Write DynMesh LODs** writes each incoming Dynamic Mesh into a specific LOD slot of a target Static Mesh
asset, mapping inputs either by connection order or by an `LOD<N>` tag. It can create the target asset if the
path does not resolve, sets each written LOD to "no further reduction" with explicit screen sizes so the
engine's reducer does not re-touch geometry you already decimated, and reports per-LOD status on an output pin.

It marks the package dirty and deliberately **does not save it** — a batch run should save once at the end
rather than once per asset. The node is editor-only, main-thread, and never cached, because it writes to state
that PCG's cache cannot see.

### Batch runs and data caching

**PCG Graph Batch** is an editor-only Data Asset holding an ordered list of standalone PCG graphs (Asset usage
context). Press Run Batch and they execute in sequence, with Stop-on-Failure, a cancel button, and Data
Validation integration that reports missing references, wrong usage contexts and duplicate entries. It controls
execution order; it does not pass data between entries.

**Save PCG Cache Data** / **Get PCG Cache Data**, with the `PCGDataCacheComponent`, persist PCG data into cache
assets — a runtime folder for data intended to be cooked and loaded in game, and an offline folder for
editor-only results. The save path is resolved from the component and project settings and written without a
save dialog, which is what makes it usable inside a graph that runs repeatedly.

---

## Experimental — Procedural Fracture

> **`PCGUtilsFracture` is experimental.** The pipeline works end to end and is covered by round-trip tests, but
> the node set is deliberately small and the module should be treated as a prototype.

Unreal's Chaos Fracture / Geometry Collection stack does robust solid decomposition that is genuinely hard to
reproduce with Dynamic Mesh boolean and topology work, and PCG is already good at producing the spatial inputs
that drive it. `PCGUtilsFracture` joins the two — as a **transient procedural modelling backend**, not as a
runtime destruction feature. Nothing it creates becomes an asset, actor, component or package.

```
Source DynMesh ─▶ DynMesh To GC ─▶ Fracture GC ─▶ GC Bones To Points ─▶ PCG / PCGEx filters
                                        ▲                                        │
                          Voronoi Fracture (uniform or from points)              ▼
                                                             Select Bones From Points
                                                                        │
                            Result DynMesh ◀── GC To DynMesh ◀── Prune GC
```

- **Fracture behaviour is a factory family.** `Uniform Voronoi Fracture` scatters its own sites through the
  target's bounds; `Voronoi Fracture From Points` places one cell per PCG point, so the fracture pattern can
  follow a scatter, a density field, or any filtered point set. Both feed the same `Fracture GC` executor.
- **Bone selection is a separate family**, driven by ordinary points. `GC Bones To Points` emits one point per
  piece with optional per-piece attributes — parent, hierarchy level, bounds volume, and an exact
  original-surface vs. fracture-cut breakdown including an `ExposureRatio` that makes "pick visible pieces"
  scale-invariant. Filter those points with any PCG or PCGEx node, then `Select Bones From Points` → `Prune GC`.
- **Collection state is validated.** Every collection carries a lineage id, revision and state id; applying
  bone indices from one state to another is a graph error rather than a silent mis-selection.
- **It re-enters the DynMesh ecosystem cleanly.** `GC To DynMesh` writes a `GC_Bone` PolyGroup layer per source
  bone and keeps the engine's internal-faces layer, both readable by `Select by PolyGroup` — so "assign a
  different material to the walls of the cavity I just carved" needs no fracture-specific node.
- **Optional cluster output.** `GC Bones To Points` can also emit the piece adjacency graph on an Edges pin,
  shaped to the convention PCGEx uses to recognize a cluster, so flood fill and pathfinding can expand a
  selection to neighbouring pieces. The contract is reproduced, not depended on — there is no link against
  PCGExtendedToolkit.

Known limitations: only Voronoi cutters exist today (no plane, slice, radial, brick or mesh cutter); surface
noise is expensive because the engine couples displacement to cut-face tessellation; internal-face UVs are
unscaled; fracture cannot be time-sliced; and there is no Geometry Collection asset output.

`[ADD SCREENSHOT/GIF HERE - Procedurally generated column fractured through a compact PCG graph]`

---

## Also in the plugin

**PCGUtilsMaterialCache** — a world-scoped cache of material variants, keyed on (parent material + parameter
overrides), so a graph that needs many parameterized materials reuses them instead of creating a dynamic
instance per element. A request with no overrides resolves to the parent asset directly. The
**Resolve Material Variants** node is the graph-facing entry point.

**PCGUtilsDataCache** — described under *Procedural Asset Authoring* above.

---

## Installation

1. Clone or download this repository into your project's `Plugins` directory:

   ```
   YourProject/Plugins/PCGUtils
   ```

2. Ensure the required engine plugins are enabled (see below). PCGUtils enables them through its descriptor,
   so this normally happens automatically.
3. Regenerate project files and build. PCGUtils contains C++ modules, so the project must be a C++ project —
   a Blueprint-only project needs to be converted first.
4. Restart the editor. PCGUtils nodes appear in the PCG graph palette under the Dynamic Mesh category and
   PCGUtils' own categories.

---

## Requirements and dependencies

- **Unreal Engine 5.8** (`"EngineVersion": "5.8.0"`).
- Engine plugins, all enabled by the PCGUtils descriptor: `PCG`, `GeometryScripting`,
  `PCGGeometryScriptInterop`, `Fracture`, `PlanarCut`, `ChaosCaching`.
- Dynamic Mesh functionality additionally uses Unreal's Geometry Framework, Geometry Core and Modeling modules
  through the module dependencies declared in `Source/`.

**PCGExtendedToolkit is not required.** PCGUtils does not link against it in any module. Some bundled example
graphs under `Content/` do use [PCG Extended Toolkit](https://github.com/PCGEx/PCGExtendedToolkit), and the
point and cluster workflows described above are designed to interoperate with it, but the plugin builds and
runs without it.

---

## Examples and documentation

Sample levels and graphs ship under `Content/Samples/` — mesh baking, a multi-mesh sampler, PCG markers, and
path utilities — alongside reusable templates and subgraphs in `Content/PCG/`. These are binary Unreal assets;
open them in the editor rather than reading them here.

Deeper written documentation lives in `Docs/`:

| Document | Covers |
|---|---|
| `Docs/PCGUtilsDynMesh.md` | The process contract, selection domains, topology results, Boolean, PolyGroups, coordinate spaces |
| `Docs/PCGUtilsPainter.md` | Painter architecture, the canonical target model, Static Mesh painting and LOD transfer |
| `Docs/PCGUtilsFracture.md` | The full fracture pipeline, node reference, cluster interop and troubleshooting |
| `Docs/PCGUtilsDynMesh/PrimitiveBuilder.md` | Builder fitting, padding and alignment |
| `Docs/PCGUtils/PCGGraphBatch.md` | Running batches of standalone PCG graphs |

Full node and API reference documentation is planned for a dedicated documentation site; this README is an
overview of what the plugin is and how its pieces fit together.

---

## License, acknowledgements and support

PCGUtils is released under the **MIT License** — see [LICENSE.md](LICENSE.md).

The Selector, Builder and Painter families follow the factory architecture pioneered by
[PCGExtendedToolkit](https://github.com/PCGEx/PCGExtendedToolkit) (Timothé Lapetite and contributors, MIT), and
the Builder fitting/justification maths and its inline enum icons are adapted from it with permission. There is
no runtime or compile-time dependency between the two plugins in either direction.

Dynamic Mesh processing is built on Epic's Geometry Script and `PCGGeometryScriptInterop`; the fracture module
builds on the Chaos Fracture and PlanarCut stack.

Issues and feature requests: <https://github.com/mharris382/PCGUtils/issues>
