# PCGUtilsDynMesh Development Conventions

This file defines module-specific conventions for work inside `PCGUtilsDynMesh`.

These conventions should be followed when adding new PCG elements, data types, settings, files, or related functionality.

---

## Elements Must Build On the Existing DynMesh Base Classes

Do not derive a new PCG element's Settings/Element pair directly from `UPCGSettings`/`IPCGElement` when an existing PCGUtilsDynMesh (or engine) base class already models the shape of its input. Building a new node "from scratch" in parallel to the module's existing infrastructure - instead of extending it - is not acceptable, even if the from-scratch version works. Before writing a new Settings/Element pair, search the module for something that already fits; do not assume it must be written fresh.

* `UPCGDynamicMeshBaseSettings` / `IPCGDynamicMeshBaseElement` - **an engine base**, not module code. It lives in `Elements/PCGDynamicMeshBaseElement.h` inside the `PCGGeometryScriptInterop` plugin (already a `PCGUtilsDynMesh.Build.cs` dependency) and resolves correctly as long as no PCGUtilsDynMesh file reuses that same relative path - do not create a file at `Elements/PCGDynamicMeshBaseElement.h` inside this module, it will collide with the engine header of the same name and fail to build. It fixes "Add Node" categorization (see below) and provides a `CopyOrSteal()` static helper. Every other base below derives from it (directly or transitively); use it directly only when none of the more specific bases fit but the element still operates on Dynamic Mesh / Dynamic Mesh Selection data (eg Bevel Edges, whose actual mesh resolution goes through `PCGUtilsMeshTargetFunctions` below, but whose Settings/Element still derive from this for the categorization fix).
* `UPCGDynamicMeshSelectionBaseSettings` / `FPCGDynamicMeshSelectionBaseElement` (`Selections/PCGDynamicMeshSelectionBase.h`) - authors a brand-new selection from a bare Dynamic Mesh input only (eg Selection From Points, Selection From Spline).
* `UPCGDynamicMeshSelectionFilterBaseSettings` / `FPCGDynamicMeshSelectionFilterBaseElement` (`Selections/PCGDynamicMeshSelectionFilterBase.h`) - computes a selection while accepting either a bare Mesh or an existing Selection to intersect with (eg Sharp Edge Filter, Edge Direction, Select in Point Bounds). Read-only - never copies the mesh.
* `UPCGDynamicMeshSelectionProcessBaseSettings` / `FPCGDynamicMeshSelectionProcessBaseElement` (`PCGDynamicMeshSelectionProcessBase.h`) - a lighter-weight mutation base: unconditionally deep-copies the source mesh once, then hands the derived element `ProcessMesh(UPCGDynamicMeshData* MeshData, const UPCGDynamicMeshSelectionData* SelectionData, FPCGContext*)` to mutate directly (eg Material). Fine for a mutation with no topology-preservation/region concerns.
* **`FPCGUtilsMeshTargetFunctions` / `FPCGUtilsMeshTargetHandle`** (`MeshTarget/PCGUtilsMeshTargetHandle.h`, `PCGUtilsMeshTargetFunctions.h`) - the module's primary, most mature infrastructure for mutation operations that accept either a whole Dynamic Mesh or a Mesh Selection (Remesh, Warp, Spline Deform, Bevel Edges). Prefer this over `UPCGDynamicMeshSelectionProcessBaseSettings` for any new mutating element - see "Resolving Mesh-or-Selection Input" below. It is a set of static helpers, not a Settings/Element base class, so pair it with `UPCGDynamicMeshBaseSettings` / `IPCGDynamicMeshBaseElement` (or a plain `UPCGSettings`/`IPCGElement`, matching the existing Remesh/Warp/Spline Deform precedent) for the actual class hierarchy.

**Exception:** an element that sources its own data rather than operating on an existing DynMesh/Selection input - eg a "Get Actor Data"-style acquisition element - has nothing to model against these bases and may derive directly from `UPCGSettings`/`IPCGElement`. This is the only routine exception; it is not a general escape hatch for "the base class didn't fit my exact use case."

---

## Resolving Mesh-or-Selection Input: Use `FPCGUtilsMeshTargetHandle`

For a **mutating** element that accepts either a whole Dynamic Mesh or a Mesh Selection, resolve the input through `FPCGUtilsMeshTargetFunctions::CreateTarget()` (`MeshTarget/PCGUtilsMeshTargetFunctions.h`) and the `FPCGUtilsMeshTargetHandle` it returns. Do not hand-roll `Cast<UPCGDynamicMeshSelectionData>(Input.Data)` / `GetSourceMeshData()` / manual `FDynamicMesh3` copying again for this shape of element - that logic already exists, and is already used by Remesh, Warp, and Spline Deform.

**An element should not care whether its input was a bare Dynamic Mesh or an existing Selection** - the handle decides what that requires:

* `Handle.IsSelection()` - true if the input was already a Selection; use this to decide whether `Handle.GetSelection()` holds real data.
* `Handle.GetSelection() -> const FGeometryScriptMeshSelection&` - the canonical selection for a Selection source. For a bare-Mesh source it is empty, so an element whose GeometryScript call always requires *some* selection (eg `ApplyMeshBevelEdgeSelection`) must synthesize one itself in that case - `UGeometryScriptLibrary_MeshSelectionFunctions::CreateSelectAllMeshSelection(Handle.GetTargetMesh(), Selection, SelectionType)` does this directly against the handle's already-created target mesh, at no extra copy cost (see Bevel Edges).
* `Handle.GetTargetMesh() -> UDynamicMesh*` - the mesh to operate on. Pick the right `EPCGUtilsMeshTargetPreparation` when calling `CreateTarget()`: `Region` extracts just the selected triangles into a standalone submesh for operations that have no selection concept of their own (Remesh, Warp); `FullMeshCopy` hands over the *whole* mesh, appropriate when the GeometryScript call is itself selection-aware (it already knows how to scope its own effect - Bevel Edges) or when the element blends vertex positions back in itself. Do not reach for `Region` merely because the element has a Selection input - that machinery (submesh extraction + weld-back) is real work an already-selection-aware operation doesn't need.
* After mutating, finalize with the `Restore*` call matching what the operation actually did (`RestoreRegion()` for `Region`, `RestoreVertexPositions()` for a `FullMeshCopy` vertex-position blend) - or with neither if the operation directly and completely mutates the `FullMeshCopy` target itself (eg Bevel Edges, which needs no compositing step). Then call `FPCGUtilsMeshTargetFunctions::EmitOutput()` to produce the output data.

For a **read-only** element that only ever computes a Selection (never mutates a mesh) - eg the filter chain built on `UPCGDynamicMeshSelectionFilterBaseSettings` - `FPCGUtilsMeshTargetHandle` does not apply, since `CreateTarget()` always makes a working copy. Those elements resolve the source mesh directly (`Cast<UPCGDynamicMeshSelectionData>` / `GetSourceMeshData()`), matching `PCGDynamicMeshSelectionFilterBase.cpp`.

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

### How this is actually enforced in C++

A native C++ PCG element has no free-form category string to set - the "Add Node" menu groups elements by the fixed `EPCGSettingsType` enum, via `UPCGSettings::GetType()`. When a Settings class doesn't override `GetType()`, it defaults to `EPCGSettingsType::Generic` - this is how a node silently ends up in `Generic`, and it is easy to miss because nothing fails to compile.

`EPCGSettingsType::DynamicMesh` is the closest available bucket for this module, and `UPCGDynamicMeshBaseSettings` (see "Elements Must Build On the Existing DynMesh Base Classes" above) already overrides `GetType()` to return it. **Every element built on that base, or on anything deriving from it, gets this for free and must not override `GetType()` again.** This is the other reason (besides reuse) that new elements must build on the existing base classes rather than deriving from `UPCGSettings` directly - deriving directly means remembering to override `GetType()` by hand every time, and forgetting it is exactly how a node ends up in `Generic`.

The finer-grained groupings described below (`Utils DynMesh|Selection`, `Utils DynMesh|Topology`, etc.) are this module's own documentation/naming convention for organizing nodes conceptually - they are not literally selectable as a native `EPCGSettingsType` value. Reflect them through the node's display name/tooltip/file organization, not through a `GetType()` override.

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
