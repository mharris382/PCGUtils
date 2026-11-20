# Mesh Repair

`Select | Mesh Issues` locates problematic faces without changing the mesh. `DynMesh | Repair Mesh` repairs
small affected patches, and optionally removes redundant planar topology. Neither operation performs a global remesh.

## Checks

- **Intersections**: select both faces of each crossing or positive-area coplanar overlap, including folded
  neighbours that share vertices or an edge. Normal contact along a common boundary is excluded. Disconnected,
  geometrically coincident seam edges also count as normal contact when no face interiors overlap.
- **Degenerate Triangles**: select faces at or below **Minimum Area**. On by default.
- **Sliver Triangles**: select faces below **Minimum Quality**, including long-edged needles with appreciable area.
  Quality is `4 * sqrt(3) * area / sum(squared edge lengths)`: 1 for an equilateral triangle, approaching 0 as
  it collapses. Off by default because some thin faces are intentional. Try 0.01 first.

Intersection Tolerance and Planarity Tolerance are mesh-local distances; Minimum Area uses squared mesh-local
units. Normal Unreal meshes use centimetres. Thresholds support constant values and strict per-input `@Data`
attributes. Missing or mistyped attributes produce a graph error; they never silently use the constant.

## Inspect before repairing

Set `Select | Mesh Issues` to **Selection** representation and connect the mesh to **Candidates** to inspect
the detected triangle cluster. **Selector** representation produces the same predicate for another node's
Selector pin. Face membership adapts to vertex and edge consumers; inversion uses the shared selector rules.

For the first repair attempt, connect the whole mesh directly to Repair Mesh. A Selection or Selector is a hard
limit on edited faces. A narrowly selected set of bad faces may not include enough surrounding faces to repair
the patch. Expand that selection upstream if needed, or repair the whole mesh: only detected problem patches
are touched unless Planar Cleanup is enabled.

## Repair behaviour

Related intersections and adjacent bad faces form one repair cluster. The node tries its boundary first, then
up to **Maximum Expansion Rings** neighbouring rings (default 2), limited by **Maximum Patch Triangles** (256)
and any input selection. A successful patch must have one simple boundary and stay within **Planarity
Tolerance** (default 0.1) of its boundary plane. Its interior is retriangulated using the existing boundary
vertices. No boundary vertices move and no replacement vertices are added. Unused interior vertices are removed.

Each candidate is made on a copy. It is accepted only if the result retains the exact original mesh boundary
edges, has valid mesh/overlay structure, introduces no intersection involving the new faces, and leaves none of
the requested defects on the replacement faces. Unrelated triangles and their attributes remain unchanged.

**Preserve Attribute Seams**, **Preserve Materials**, and **Preserve Polygroups** default on. Internal UV,
normal, tangent and colour seams can therefore prevent a patch from being replaced. Material and group boundaries
are protected independently, including extended PolyGroup layers. Fully populated overlays retain their boundary
samples; entirely unset layers stay unset. Partially populated layers cause the patch to be skipped. New faces
interpolate their interior attribute values from the retained boundary corners; removal of interior vertices can
therefore change a nonlinear UV/color field inside the patch. Disabling a preservation option permits that kind
of boundary to be replaced. Material/group values then come from the lowest-ID source face of the patch.

The **Out** pin normally emits the repaired DynMesh, preserving PCG tags, metadata, materials and target actor
through the shared process executor. **Unresolved** always emits a Selection tied to that result, including an
empty selection when all requested issues were resolved. Enabling the inherited **Output Selection Data** makes
Out emit the same unresolved selection rather than bare mesh data. Unsafe patches remain unchanged and produce
a graph warning when detected issues remain. This node does not offer deferred Builder processing because its
Unresolved companion is tied to a concrete mesh.

This is a conservative repair for nearly planar surface defects, not a general reconstruction of intersecting
closed solids. Curved patches, intersecting independent surfaces, multiple boundary loops and ambiguous
boundaries can remain unresolved. A screenshot cannot establish which case a real mesh contains.

## Planar Cleanup

**Planar Cleanup** is separate and off by default. It uses the existing PCGUtils planar dissolve algorithm to
remove redundant interior topology from planar regions, with the preservation switches and **Planar Angle**
(default 0.1 degrees). It can affect the entire supplied mesh/selection, even where no defect was detected.
The pass is accepted only if the original boundary edges remain exact and the result has no intersections or
collapsed faces. Existing unresolved intersections therefore block this extra pass. To remove thin but valid
coplanar faces, enable Sliver Triangles; use Planar Cleanup when broader removal of redundant flat topology is
also desired.

## Verification

Native coverage is under `PCGUtils.DynMesh.RepairMesh`: coplanar/folded and 3D crossings, split boundary contact,
collapsed and long-edged sliver detection, local repair with boundary/overlay retention, selection and planarity
limits, material protection, optional planar cleanup, selector domain adaptation/cache identity, and the PCG
Selector-to-Repair Mesh pipeline. Synthetic fixtures do not establish the result on the mesh shown in a screenshot.
