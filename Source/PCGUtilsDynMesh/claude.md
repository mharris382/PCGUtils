# PCGUtilsDynMesh Development Conventions

This file defines module-specific conventions for work inside `PCGUtilsDynMesh`.

These conventions should be followed when adding new PCG elements, data types, settings, files, or related functionality.

## Naming: Prefer `DynMesh`

For project-owned filenames, class names, node names, and related identifiers, prefer the shorter term:

`DynMesh`

instead of:

`DynamicMesh`

Examples:

* `PCGDynMeshToPoints`
* `PCGDynMeshSelection`
* `DynMeshDeform`
* `DynMeshAttributes`

Avoid unnecessarily verbose names such as:

* `PCGDynamicMeshToPoints`
* `DynamicMeshSelectionFunctions`

This does **not** mean renaming Unreal Engine API types such as `UDynamicMesh` or `FDynamicMesh3`. Use the engine's official type names when referring to engine APIs.

The convention applies to names introduced by PCGUtilsDynMesh.

---

## PCG Settings Must Be Overrideable

PCG element settings should be declared with `PCG_Overrideable` unless there is a concrete reason they cannot be overridden.

This should be treated as the default for exposed PCG settings rather than something added selectively.

Example conceptually:

```cpp
UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Settings, meta = (PCG_Overrideable))
float Radius = 100.0f;
```

### Exception: Inline UObjects

Properties contained inside an inline-instanced `UObject` do not need to follow this requirement.

For example, configuration contained inside an inline operation/settings object may rely on that UObject's own editing and ownership model rather than exposing every property through PCG property overrides.

Do not omit `PCG_Overrideable` from normal PCG settings simply because an override is not immediately required by the current use case.

---

## Custom PCG Data Types Must Have a Distinct Pin Color

Custom PCG data types introduced by this module must not appear as colorless/default PCG pins.

A colorless data type is visually ambiguous because Unreal displays these pins as grey, and grey PCG pins normally communicate broad or "Any" data compatibility.

Custom data types must therefore have an explicit, recognizable pin color.

This applies to types such as:

* DynMesh data
* DynMesh selections
* operation/factory data
* other module-specific PCG data types

The exact color can vary by data family, but it must intentionally distinguish the type from generic/Any PCG data.

When introducing a new custom PCG data type, verify its graph pin appearance as part of the implementation.

---

# PCG Element Categories

`Generic` is not an acceptable category for PCGUtilsDynMesh elements.

All PCGUtilsDynMesh nodes should live under the root category:

`Utils DynMesh`

Nodes should then be grouped according to their functional role.

Use the most specific appropriate category.

## `Utils DynMesh`

Use the root category directly only for operations that are genuinely fundamental or do not naturally belong to a more specific group.

Do not use the root category as a fallback because classification is inconvenient.

---

## `Utils DynMesh|Selection`

For operations whose primary purpose is creating, querying, combining, filtering, or modifying a **mesh selection** rather than modifying the DynMesh itself.

Examples:

* Selection from points
* Selection by material
* Selection by normal
* Selection by spatial region
* Union/intersection/difference of selections
* Expanding or shrinking a selection
* Inverting a selection

The important distinction is that the output represents **which mesh elements are selected**, not a modified mesh.

---

## `Utils DynMesh|Topology`

For operations that change mesh topology or intentionally add/remove geometry.

Use this category when the operation may change the number or connectivity of:

* vertices
* edges
* triangles

Examples:

* Extrude
* Boolean
* Delete geometry
* Append geometry
* Weld
* Split
* Subdivide
* Remesh
* Simplify
* Fill holes
* Bridge geometry
* Cut geometry

A useful classification test is:

> Can this operation change which vertices/edges/triangles exist or how they are connected?

If yes, it generally belongs under `Topology`.

---

## `Utils DynMesh|Deform`

For operations that change the position, orientation, scale, or shape of existing geometry **without intentionally changing mesh topology**.

Examples:

* Transform selected vertices
* Translate
* Rotate
* Scale
* Bend
* Twist
* Curve deform
* Noise displacement
* Spline deformation
* Surface projection
* Relax/smooth vertex positions when topology remains unchanged

A useful classification test is:

> Does the operation modify where existing geometry is located while preserving its basic vertex/triangle connectivity?

If yes, it generally belongs under `Deform`.

Do not place topology-changing operations here simply because they also alter the shape of the mesh.

---

## `Utils DynMesh|Attributes`

For operations that modify data associated with the mesh without intentionally adding/removing geometry or changing its topology.

Examples include operations involving:

* UV layers/channels
* material IDs
* material assignments
* vertex colors
* normals/tangents
* polygroups
* weight maps
* other mesh overlays or per-element attributes

Examples:

* Add UV channel
* Remove UV channel
* Copy UV channel
* Set material IDs
* Assign vertex colors
* Recompute normals
* Create or modify polygroups
* Write mesh weight maps

A useful classification test is:

> Does the operation primarily modify metadata or attribute information attached to existing geometry?

If yes, it generally belongs under `Attributes`.

---

## `Utils DynMesh|Query`

For read-only operations that inspect or analyze DynMesh data but do not modify it.

Examples:

* Mesh bounds
* Surface area
* Volume
* Triangle count
* Vertex count
* Closest-point queries
* Sampling information from the mesh
* Reading mesh attributes
* Mesh validation or diagnostics

Query nodes should generally communicate information *about* the mesh rather than producing a modified version of it.

---

## `Utils DynMesh|Conversion`

For operations whose primary purpose is converting between DynMesh and another representation.

Examples:

* DynMesh to Points
* Points to DynMesh
* DynMesh to Static Mesh-related data
* DynMesh selection to points
* Points to DynMesh selection
* Conversion between mesh selection representations

Conversion should describe a change in **data representation**, not simply an operation that happens to output a different PCG pin type.

---

# Choosing Between Categories

## `Utils DynMesh|Creation`

For operations whose primary purpose is generating a **new DynMesh** rather than modifying an existing DynMesh.

Creation nodes may generate geometry:

* entirely from their own settings,
* procedurally from mathematical parameters,
* from other PCG data,
* from splines, points, surfaces, or other source data.

Examples:

* Create Primitive
* Create Box
* Create Sphere
* Create Cylinder
* Create Mesh from Spline
* Create Mesh from Points
* Generate procedural surface
* Construct DynMesh from another non-mesh PCG representation

A useful classification test is:

> Is this node establishing the initial geometry of a new DynMesh rather than operating on an already-existing DynMesh?

If yes, it generally belongs under `Creation`.

Creation is distinct from `Conversion`.

* **Creation** constructs new mesh geometry.
* **Conversion** changes how existing information is represented.

For example, a node that procedurally constructs a tube from spline data is a `Creation` operation even though spline data is one of its inputs. A node that exposes the vertices of an existing DynMesh as PCG points is a `Conversion` operation.

---

# Choosing Between Categories

When categorizing a node, classify it according to its **primary semantic operation**, not implementation details.

Use this order of questions:

1. Does it create an entirely new DynMesh?

   * `Creation`

2. Does it primarily create or manipulate a mesh selection without modifying the mesh?

   * `Selection`

3. Does it add/remove geometry or change vertex/edge/triangle connectivity on an existing mesh?

   * `Topology`

4. Does it move or deform existing geometry while preserving topology?

   * `Deform`

5. Does it modify UVs, materials, colors, normals, polygroups, weight maps, or other attached mesh data?

   * `Attributes`

6. Does it only inspect/read/analyze mesh data?

   * `Query`

7. Does it primarily convert existing information between data representations?

   * `Conversion`

8. Is it genuinely foundational and cross-cutting?

   * `Utils DynMesh` root


Do not create vague categories such as:

* `Generic`
* `Misc`
* `Other`
* `Helpers`
* `Operations`

If a growing family of nodes no longer fits the taxonomy cleanly, introduce a meaningful functional category rather than accumulating nodes under an ambiguous bucket.

---

# General Principle

PCGUtilsDynMesh should remain predictable to someone browsing the PCG node palette.

Naming, categorization, pin coloring, node coloring, and override behavior are part of the public UX of the module, not merely implementation details.

When adding a new feature, consider both:

1. whether the implementation is technically correct, and
2. whether the node behaves consistently with the rest of the PCGUtilsDynMesh authoring experience.
