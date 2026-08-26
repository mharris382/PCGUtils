# PCGUtilsDynMesh Development Conventions

This file defines module-specific conventions for work inside `PCGUtilsDynMesh`.

These conventions should be followed when adding new PCG elements, data types, settings, files, or related functionality.

---

## Core Product Principles

`PCGUtilsDynMesh` exists to make DynMesh processing feel native to PCG rather than like a thin collection of
unrelated Geometry Script wrappers. Every design and review must preserve these priorities:

1. Make DynMesh data PCG-native and graph-friendly, including consistent handling of PCG world space versus
   DynMesh local space.
2. Expose Geometry Script's capabilities comprehensively through PCG.
3. Port useful procedural operations from Blender when Geometry Script has no equivalent. Follow the original
   open-source algorithm and license obligations rather than guessing at equivalent behavior.
4. Make practically every operation selection-aware unless partial application is semantically invalid.

The fourth point is a default requirement, not an optional enhancement. A process node should consume either
bare DynMesh data or DynMesh Selection data through one input. It should also expose the optional `Selector`
input so a selection can be generated inline. An exception must be documented in the node when applying
the operation to only part of a mesh does not make sense (for example, changing the coordinate system of only
some vertices).

---

## The Four Geometry Script Selection Shapes

Before implementing an operation, identify its Geometry Script API shape:

1. **Whole-mesh and selection overloads:** dispatch to the matching overload.
2. **One function with an optional selection/no-selection policy:** pass the effective selection and select the
   appropriate no-selection behavior.
3. **Selection required:** override `RequiresSelection()` on `UPCGUtilsDynMeshProcessBaseSettings`. The process
   accepts materialized DynMesh Selection data or bare DynMesh data plus a Selector. Bare DynMesh data
   without either must fail with a graph error in the shared resolver.
4. **Whole-mesh only:** use `FPCGUtilsMeshTargetHandle` to operate on a temporary region or full copy, then restore
   the applicable result domain to the untouched source mesh.

Do not implement separate ad hoc input paths for these cases. The process base resolves materialized selections,
selector selections, domain conversion, and intersection; the Mesh Target layer handles temporary working meshes
and restoration.

---

## Elements Must Build On the Existing DynMesh Base Classes

Do not derive a new PCG element's Settings/Element pair directly from `UPCGSettings`/`IPCGElement` when an existing PCGUtilsDynMesh (or engine) base class already models the shape of its input. Building a new node "from scratch" in parallel to the module's existing infrastructure - instead of extending it - is not acceptable, even if the from-scratch version works. Before writing a new Settings/Element pair, search the module for something that already fits; do not assume it must be written fresh.

* `UPCGDynamicMeshBaseSettings` / `IPCGDynamicMeshBaseElement` - **an engine base**, not module code. It lives in `Elements/PCGDynamicMeshBaseElement.h` inside the `PCGGeometryScriptInterop` plugin (already a `PCGUtilsDynMesh.Build.cs` dependency) and resolves correctly as long as no PCGUtilsDynMesh file reuses that same relative path. It fixes "Add Node" categorization and provides `CopyOrSteal()`. Use it directly only when no more specific base fits.
* `UPCGDynamicMeshSelectionBaseSettings` / `FPCGDynamicMeshSelectionBaseElement` (`Selections/PCGDynamicMeshSelectionBase.h`) - authors a brand-new selection from a bare DynMesh input only.
* `UPCGDynamicMeshSelectionFilterBaseSettings` / `FPCGDynamicMeshSelectionFilterBaseElement` (`Selections/PCGDynamicMeshSelectionFilterBase.h`) - computes a selection while accepting either a bare Mesh or an existing Selection to intersect with (eg Sharp Edge Filter, Edge Direction, Select in Point Bounds). Read-only - never copies the mesh.
* `UPCGUtilsDynMeshProcessBaseSettings` / `FPCGUtilsDynMeshProcessBaseElement` (`PCGUtilsDynMeshProcessBase.h`) - the required base contract for a process/query/conversion that consumes DynMesh or DynMesh Selection data. It owns the unified input and optional Selector pin, selection requirement/domain policy, and shared resolution. Its default element executor deep-copies and calls `ProcessMesh(...)`; specialized executors may override `ExecuteInternal()` but must still use `FPCGUtilsDynMeshProcessFunctions::ResolveInput()` or pass their settings to `CreateTarget()`.
* `UPCGUtilsDynMeshSelectionOperationSettings` / `FPCGUtilsDynMeshSelectionOperationElement` - the shared base for operations that modify an existing selection. The same node must support materialized `Selection` mode and reusable `Selector` decorator mode. Do not create parallel standalone and provider nodes for the same operation.
* **`FPCGUtilsMeshTargetFunctions` / `FPCGUtilsMeshTargetHandle`** (`MeshTarget/PCGUtilsMeshTargetHandle.h`, `PCGUtilsMeshTargetFunctions.h`) - the working-mesh/restoration layer for cases that cannot use the default process executor. It complements the process base; it is not an alternative to the process settings contract. Handle-based nodes derive from `UPCGUtilsDynMeshProcessBaseSettings`, expose the inherited Selector pin, and call `CreateTarget(..., Settings)` so materialized and selector selections are resolved consistently.

**Exception:** an element that sources its own data rather than operating on an existing DynMesh/Selection input - eg a "Get Actor Data"-style acquisition element - has nothing to model against these bases and may derive directly from `UPCGSettings`/`IPCGElement`. This is the only routine exception; it is not a general escape hatch for "the base class didn't fit my exact use case."

---

## Resolving Mesh-or-Selection Input: Use `FPCGUtilsMeshTargetHandle`

For a **mutating** element that accepts either a whole DynMesh or a DynMesh Selection, resolve the input through
`FPCGUtilsMeshTargetFunctions::CreateTarget(Input.Data, Preparation, Context, Settings)`. Passing the process
settings is mandatory: it applies a connected Selector and domain conversion before target preparation.
Do not hand-roll `Cast<UPCGDynamicMeshSelectionData>` / `GetSourceMeshData()` / manual copying.

**An element should not care whether its input was a bare DynMesh or an existing Selection** - the handle decides what that requires:

* `Handle.IsSelection()` - true if the input was already a Selection; use this to decide whether `Handle.GetSelection()` holds real data.
* `Handle.GetSelection() -> const FGeometryScriptMeshSelection&` - the canonical selection for a Selection source. For a bare-Mesh source it is empty, so an element whose GeometryScript call always requires *some* selection (eg `ApplyMeshBevelEdgeSelection`) must synthesize one itself in that case - `UGeometryScriptLibrary_MeshSelectionFunctions::CreateSelectAllMeshSelection(Handle.GetTargetMesh(), Selection, SelectionType)` does this directly against the handle's already-created target mesh, at no extra copy cost (see Bevel Edges).
* `Handle.GetTargetMesh() -> UDynamicMesh*` - the mesh to operate on. Pick the right `EPCGUtilsMeshTargetPreparation` when calling `CreateTarget()`: `Region` extracts just the selected triangles into a standalone submesh for operations that have no selection concept of their own (Remesh, Warp); `FullMeshCopy` hands over the *whole* mesh, appropriate when the GeometryScript call is itself selection-aware (it already knows how to scope its own effect - Bevel Edges) or when the element blends vertex positions back in itself. Do not reach for `Region` merely because the element has a Selection input - that machinery (submesh extraction + weld-back) is real work an already-selection-aware operation doesn't need.
* After mutating, finalize with the `Restore*` call matching what the operation actually did (`RestoreRegion()` for `Region`, `RestoreVertexPositions()` for a `FullMeshCopy` vertex-position blend) - or with neither if the operation directly and completely mutates the `FullMeshCopy` target itself (eg Bevel Edges, which needs no compositing step). Then call `FPCGUtilsMeshTargetFunctions::EmitOutput()` to produce the output data.

For a **read-only** element that only ever computes a Selection (never mutates a mesh) - eg the filter chain built on `UPCGDynamicMeshSelectionFilterBaseSettings` - `FPCGUtilsMeshTargetHandle` does not apply, since `CreateTarget()` always makes a working copy. Those elements resolve the source mesh directly (`Cast<UPCGDynamicMeshSelectionData>` / `GetSourceMeshData()`), matching `PCGDynamicMeshSelectionFilterBase.cpp`.

## Selection Pins Must Be Domain-Agnostic

A `UPCGDynamicMeshSelectionData` input pin must accept vertex, edge, and triangle selections regardless of the element domain required by the implementation. Do not reject, silently skip, or require the user to recreate an upstream selection merely because its `FGeometrySelection::ElementType` differs from the operation's native domain.

When an operation requires a specific domain, convert the incoming selection internally with GeometryScript's `ConvertMeshSelection` semantics. Use `PCGUtilsDynMeshSelectionDomains::ConvertSelection()` rather than hand-rolling incident-element conversion. Inclusive conversion (`bAllowPartialInclusion = true`) is the default; expose the restrictive full-inclusion behavior only as an advanced setting when it is useful.

The same rule applies to selectors. A selector whose predicate is natively meaningful in only one domain uses the internal `UPCGUtilsDynMeshDomainSelectionFactoryData` and `UPCGUtilsDynMeshDomainSelectionFactoryProviderSettings` types. Implement only the native predicate; the base materializes and converts it automatically when `Build DynMesh Selection` or another selector requests a different domain.

Processes derived from `UPCGUtilsDynMeshProcessBaseSettings` opt into the same behavior by overriding `GetRequiredSelectionDomain()`. Return `true` and the native vertex, edge, or face element type required by the operation. Override `AllowPartialSelectionDomainInclusion()` only when the process requires restrictive full-inclusion conversion.

`UPCGUtilsDynMeshProcessBaseSettings` exposes one optional, single-connection `Selector` pin. When connected, the selector result becomes the process selection for a complete mesh input, or intersects an incoming selection-data input. A domain required by `GetRequiredSelectionDomain()` takes precedence; otherwise the base uses `SelectionFactoryEvaluationDomain`. For selection-only operations, override `RequiresSelection()`; never enforce this independently inside each element.

This is a user-facing graph contract: selection wires communicate mesh-element membership, not a hidden compatibility requirement. Domain conversion is an implementation detail and must not produce mysterious empty results or runtime-only domain errors.

## Coordinate-Space Contract

PCG-authored points, splines, and bounds are world-space by default; DynMesh geometry is target-actor-local by
default. Nodes that combine them must use `PCGUtilsDynMeshSpaceHelpers`/`PCGUtilsSplineHelpers` and expose a clear
conversion setting where inference is not possible. Never scatter bespoke target-actor transform code through a
new element.

The engine `UPCGDynamicMeshData` type does not record coordinate-space provenance. Until PCGUtils introduces a
space-aware data wrapper or metadata contract, do not claim that an arbitrary mesh's space can be inferred
perfectly. Treat local space as the module default, preserve explicit To World/To Local nodes, and document any
node that intentionally operates in world space. A future process-base space policy should centralize input and
output conversion once provenance is representable.

## Naming: Prefer `DynMesh`

`Factory` is implementation-only vocabulary. The graph, palette, pin labels, data display names, tooltips,
errors, and user documentation always call reusable selection predicates `Selectors`. Existing reflected C++
types containing `Factory` remain until a redirect-backed rename is safe; never copy that term into display text.

Selection operations that can either materialize or decorate a selection use the shared selection-operation base.
When a Selector implementation supersedes a standalone materialized node, preserve the old class for serialized
graphs but mark it deprecated and direct users to the unified node.

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

The convention applies to filenames, C++ identifiers, pin display names, data-type display names, node names,
node titles, categories, tooltips, and documentation introduced by PCGUtilsDynMesh. Existing Unreal Engine API
names remain unchanged. When compatibility prevents renaming an existing reflected class immediately, change its
user-facing node/data display names now and schedule the C++ rename with Core Redirects separately.

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
* operation/provider data
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
