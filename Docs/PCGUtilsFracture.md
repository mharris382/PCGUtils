# PCGUtilsFracture

Uses Unreal's Geometry Collection / Fracture stack as a **transient procedural modelling backend inside PCG**.

This is not a runtime Chaos destruction feature. Geometry Collections expose robust solid-mesh decomposition
that is genuinely hard to reproduce with Dynamic Mesh boolean/topology work; PCG is already good at producing
the spatial inputs that drive it. The module joins them, and nothing it creates ever becomes an asset, actor,
component or package.

```
Source DynMesh
      |
      v
DynMesh To GC ------------------------+
                                      |   Voronoi site points
                                      |          |
                                      |   Voronoi Fracture
                                      v          v
                              Fracture GC
                                      |
                              Fractured GC
                                      |
             +------------------------+------------------------+
             |                                                 |
      GC Bones To Points                                       |
             |                                                 |
      PCG / PCGEx filters                                      |
             |                                                 |
    Select Bones From Points --------> Prune GC <--------------+
                                          |
                                    GC To DynMesh
                                          |
                                    Result DynMesh
```

---

## Nodes

All under the `Dynamic Mesh` palette category (the engine has no Geometry Collection settings type).

| Node | In | Out |
|---|---|---|
| **DynMesh To GC** | `DynMesh` | `GC` |
| **Fracture GC** | `GC`, `Fracture`, `Selection` (optional) | `GC` |
| **Uniform Voronoi Fracture** | *(none)* | `Fracture` |
| **Voronoi Fracture From Points** | `Sites` (points) | `Fracture` |
| **GC Bones To Points** | `GC` | `Points` |
| **Select Bones From Points** | `Points` | `Selection` |
| **Prune GC** | `GC`, `Selection` | `GC` |
| **GC To DynMesh** | `GC` | `DynMesh` |

`GC`, `Fracture` and `Selection` pins all use the fracture-domain colour `#2F7FA3`; the icon distinguishes the
type. A blue selection icon is a Geometry Collection bone selection, a purple one a DynMesh element selection.

---

## Building the V1 prototype graph

### 1. Source geometry

Any closed solid: a cube, stone block, rock, or thick wall. `Create Primitive` (Box) works fine. The mesh only
needs to be a watertight solid — the fracture backend cuts volume, not surfaces.

### 2. `DynMesh To GC`

Connect the DynMesh. Defaults are correct:

- **Merge Inputs Into One Collection** (on) puts every input DynMesh in one collection as its own bone.
- **Split Islands** (off) — only needed when a single mesh contains separate shells. If you already have
  separate DynMeshes, feed them separately instead; that is cheaper and more predictable.

The node always adds a cluster root above the geometry, so the geometry bones are always prunable.

### 3. Choose a fracture operation

Two nodes produce a `Fracture` operation. Both feed the same `Fracture GC` executor.

**`Uniform Voronoi Fracture`** — start here. The direct equivalent of Fracture Mode's Uniform button: set
**Min/Max Voronoi Sites** and it scatters its own sites through the bounds of whatever it is fracturing. No
point input, no coordinate space to get wrong. Setting Min and Max to the same value asks for an exact piece
count.

| Setting | Notes |
|---|---|
| **Min / Max Voronoi Sites** | Count is chosen at random in this range. Set both equal for an exact count. |
| **Group Fracture** | On: one pattern spanning all targeted bones. Off: each bone fractured independently, with its own sites and seed. |
| **Random Seed** | Drives site placement, the count within the range, and Chance To Fracture. |
| **Chance To Fracture** | 1.0 fractures every targeted bone. |
| **Grout** | Gap between pieces. Start at 0. |
| **Split Islands** | On by default; splits a piece the cut left disconnected. |
| **Add Surface Noise** | Off by default. See the warning below before turning it on. |

**`Voronoi Fracture From Points`** — when you want to *place* the cells rather than have them scattered for
you: one piece per point, so the fracture pattern follows a PCG/PCGEx scatter, a density field, or any filtered
point set. This is the Houdini-style primitive, and it is what makes fracture composable with the rest of PCG.
It takes everything above except Min/Max Sites and Group Fracture, plus:

| Setting | Notes |
|---|---|
| **Sites Are World Space** | On by default — PCG points are world-space, the collection is mesh-local. |

Scatter **30–100 points** through the volume of the mesh; `Create Points Grid` clipped to the mesh bounds, or
any PCG/PCGEx scatter, works. Sites outside the geometry contribute no cell, and the node says so if too few
land on the mesh.

### 4. Wire it up

> **Add Surface Noise is expensive, and not for the reason you would guess.** Unreal's fracture entry point has
> no way to disable noise — PlanarCut decides between its cheap and expensive meshing paths on whether a noise
> struct was supplied at all, not on the amplitude — so enabling noise also subdivides every cut face down to
> **Surface Resolution**, a target edge length in centimetres. On a 100cm object at the engine's 1cm default
> that is the difference between ~1,000 and ~550,000 triangles. With noise off, this module passes a
> no-subdivision spacing and the cuts stay planar and cheap. If you do want noise, start with a Surface
> Resolution around a tenth of the object's size and work down.

This node produces a `Fracture` *operation*; it does not fracture anything by itself.

### 5. `Fracture GC`

`GC` from step 2, `Fracture` from step 3. Leave `Selection` unconnected — everything is targeted and the
backend narrows to leaves.

With `Uniform Voronoi Fracture` the minimum working graph is just three nodes: a DynMesh source,
`DynMesh To GC`, and `Fracture GC`.

The log line reports before/after bone counts. Expect roughly one bone per site that landed inside the mesh.

### 6. `GC Bones To Points`

Fractured `GC` in. One point per fracture piece, at the piece's centre, carrying the piece's bounds and
orientation, plus:

Every attribute has its own toggle and its own name field in the details panel, so what this node can produce
is visible without running the graph. Only identity is written unconditionally - it is the contract with
`Select Bones From Points`, which cannot resolve a selection without it:

```
always:   GC_BoneIndex  GC_SourceId  GC_SourceRevision  GC_SourceStateId
opt-in:   GC_ParentIndex  GC_HierarchyLevel  GC_GeometryIndex  GC_BoundsVolume
opt-in:   GC_IsExterior  GC_ExposureRatio
          GC_ExteriorArea  GC_InteriorArea
          GC_ExteriorFaceCount  GC_InteriorFaceCount
```

Names shown are defaults; rename any of them in the details panel to avoid a collision. Leaving every surface
toggle off skips the per-face pass entirely.

The surface attributes split each piece's faces into surface it inherited from the source mesh and surface a
fracture cut created. The collection tracks this per face, so it is exact rather than inferred.

| Attribute | Meaning |
|---|---|
| `GC_IsExterior` | the piece has at least one original-surface face |
| `GC_ExteriorArea` / `GC_InteriorArea` | surface area of each set, in collection space |
| `GC_ExteriorFaceCount` / `GC_InteriorFaceCount` | face counts of each set |
| `GC_ExposureRatio` | `ExteriorArea / TotalArea`, in [0,1] |

**Prefer `GC_ExposureRatio` for choosing pieces.** It is scale-invariant, so a threshold that works on one mesh
works on the next: 0 is fully buried, a value near 1 is a piece the fracture barely touched, and the middle is
a chunk with real exposure. An absolute `GC_ExteriorArea` threshold has to be retuned per mesh size.

Before any fracture every face came from the source mesh, so every bone reads as fully exterior; the breakdown
only becomes interesting after the first cut.

`Output To World Space` is on by default so the points compare correctly against world-space PCG geometry.

> `GC_BoundsVolume` is bounding-box volume, not true mesh volume. The collection's real `Volume` attribute
> requires convex-hull generation, which is far too heavy for a points node.

### 6b. Random damage, safely

The surface attributes exist to make this workflow correct. Pruning a *buried* piece is doubly wrong: nothing
visible changes, and interior faces nobody will ever see are left behind. So filter them out first:

```
GC Bones To Points                         (tick Is Exterior + Exposure Ratio)
  -> filter GC_IsExterior == true          (exclude buried pieces)
  -> filter GC_ExposureRatio > ~0.15       (exclude pieces that merely graze the surface)
  -> random subset of N points
  -> Select Bones From Points
  -> Prune GC
```

The middle filter is what separates believable damage from a piece vanishing with almost no visual effect. On a
4x4x4 fracture of a cube, 56 of 64 pieces are exterior and 8 are buried, so a naive random pick has a ~12%
chance of doing nothing at all.

### 7. Filter the points

Ordinary PCG/PCGEx. This is the whole point of the design — no GC-specific filtering node exists or is needed.
Bounds overlap against a volume, distance from a point, `Difference`, an attribute filter on
`GC_BoundsVolume`, or any PCGEx filter. To carve a localised cavity, select the pieces near a chosen location.

### 8. `Select Bones From Points`

Filtered points in. Duplicate, negative and out-of-range indices are dropped with a warning.

**Points must come from the same collection state being selected against.** If you fracture again between
steps 6 and 8, the indices become meaningless and the node fails with an error naming both revisions rather
than silently selecting the wrong pieces. Re-run `GC Bones To Points` on the collection you actually intend to
select on.

### 9. `Prune GC`

Fractured `GC` plus the `Selection`. The selected bones and their children are genuinely removed —
this is not a visibility flag or a conversion-time filter.

Root bones are never deleted (Epic's `DeleteBranch` refuses). If the node warns that it removed nothing from a
non-empty selection, you selected only root/cluster bones; select the geometry-bearing children instead.

### 10. `GC To DynMesh`

Pruned `GC` in. Defaults are what you want:

- **Set Polygroup Per Bone** writes each source bone index into the named PolyGroup layer `GC_Bone`.
- **Tag Internal Faces** keeps the engine's `GeometryCollectionInternalFaces` layer.

### Expected result

An irregular cavity in the solid, with valid fracture-generated interior surfaces around it — the thing that
would otherwise take significantly more complex DynMesh boolean/topology work.

---

## Re-entering the DynMesh ecosystem

The two PolyGroup layers from step 10 are the payoff, and both are readable by the existing
**Select by PolyGroup** node via its **Group Layer Name** field:

| Layer | Selects |
|---|---|
| `GC_Bone` | one specific fracture piece, by its source bone index |
| `GeometryCollectionInternalFaces` | fracture-generated interior surfaces vs. original exterior ones |

So "assign a different material to the walls of the cavity I just carved" is a `Select by PolyGroup` +
`Set Material` away, with no fracture-specific node involved.

---

## Transform space

The canonical space is **the source DynMesh's own local space**, entered at identity and never re-pivoted.
Nothing in the round trip recentres the mesh.

The only conversion is on incoming PCG spatial data, which PCG authors in world space — Voronoi sites and bone
points both handle this through the target actor transform, the same convention `PCGUtilsDynMesh` uses. This
is why the graph works at translated, rotated and scaled source transforms rather than only at identity.

---

## Collection identity

Every `GC` data carries `CollectionId` (the lineage), `Revision` (a counter for diagnostics) and `StateId`
(unique per exact state). Fracture and Prune both publish a new state, because both reindex bones.

`StateId` is the authoritative check. A bone index from one state applied to another does not fail on its own —
it silently addresses a different piece — so `Select Bones From Points` treats a mismatch as a graph error.
Leave **Validate Source Identity** on.

---

## Troubleshooting Fracture GC

Unreal's fracture backend reports every failure the same way — a bare `INDEX_NONE` — so `Voronoi Fracture`
checks each condition itself, before calling it, and names the one that actually applies. If you see a generic
message, that is a bug worth reporting.

| Message | Cause | Fix |
|---|---|---|
| *needs at least 2 sites to define a cut* | One site. A Voronoi diagram of a single site has no dividing planes at all. | Scatter one point per piece you want, or use Uniform Voronoi Fracture. |
| *the sites are in the wrong coordinate space* | The sites miss the geometry as interpreted, but would hit it under the other interpretation. | Toggle **Sites Are World Space**, as the message says. |
| *only N of M site(s) inside the geometry* | Sites genuinely miss the mesh in both spaces. Bounds for both are printed. | Scatter sites through the mesh's volume. |
| *missing the attribute(s) it requires* | A malformed collection reached the node. `DynMesh To GC` validates its own output, so this should be impossible from a normal graph. | Report it — it is a bug, not a setting. |
| *cut nothing … produced no new pieces* | Inputs were all valid but the cut still yielded nothing: sites clustered nearly on top of each other, or a Grout large enough to consume every piece. | Spread the sites out, or lower Grout. |

The minimum input that fractures anything is **two sites inside the geometry**.

---

## Known limitations

- **Noise cannot be cheap.** Surface displacement and surface tessellation are the same switch in the engine
  API, so noised cuts are always dense. See the warning in step 4.
- **Internal face UVs are unscaled.** `FInternalSurfaceMaterials::GlobalUVScale` defaults to 1 and the engine's
  own fracture entry point never calls `SetUVScaleFromCollection`. Same for us.
- **Internal material override is a post-pass.** `Fracture GC`'s `Override Internal Material` retags *every*
  face marked internal, including any produced by an earlier fracture in the same chain. The fracture backend
  exposes no internal-material parameter at all.
- **Fracture cannot be time-sliced.** A large Voronoi cut is one non-interruptible call and occupies a PCG
  worker for its whole duration. Correctness over parallelism for V1.
- **Three deep copies per fracture.** Ours, plus the two `FFractureEngineFracturing::VoronoiFracture` performs
  internally (`NewCopy<FGeometryCollection>` and the assign-back). Not avoidable without forking engine code.
- **`GC_Bone` values are post-prune indices.** Pruning reindexes bones, so the layer written by `GC To DynMesh`
  refers to the collection state it converted, not the pre-prune one.
- **Chained fracture drops an authored selection.** If a second `Fracture` operation runs on the same node
  after the first changed the bone count, the authored `Selection` no longer addresses the new collection, so
  the second operation targets everything and warns.

## Not in V1

Plane / Slice / Radial / Brick fracture, Mesh Cutter, clustering tools, hierarchy editing beyond prune cleanup,
GC-specific distance/bounds/volume selectors, runtime Chaos simulation, Geometry Collection assets/actors/
components, and the cross-domain `PCGUtilsSelections` bridge.

Most of those are small: every `FFractureEngineFracturing` cutter has the same signature shape as Voronoi and
drops in as a new `Fracture` operation with no executor change, and `FCollectionTransformSelectionFacade`
already implements bounds/sphere/plane/volume/size/contact/hierarchy bone selection for future selectors.
