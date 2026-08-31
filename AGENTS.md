# PCGUtils Repository Guidance

These rules apply to the whole PCGUtils plugin repository. Module-specific guidance may add constraints; for
`PCGUtilsDynMesh`, read `Source/PCGUtilsDynMesh/claude.md` and `Docs/PCGUtilsDynMesh.md` before editing.

## PCGUtilsDynMesh principles

- DynMesh processing must feel PCG-native: unified pins, overrideable settings, useful typing/colors, predictable
  categories, and consistent PCG-world/DynMesh-local coordinate handling.
- Expose Geometry Script comprehensively. When Geometry Script lacks a useful operation, a faithful,
  license-compatible Blender-derived implementation is preferred to reinventing the algorithm.
- Every operation should support DynMesh Selection data and the optional Selector input unless partial
  application is semantically invalid. Document exceptions.
- A node that consumes DynMesh/Selection data must derive from `UPCGUtilsDynMeshProcessBaseSettings` unless it is
  a selection-authoring/filter node with a more specific selection base. Specialized executors must resolve input
  through `FPCGUtilsDynMeshProcessFunctions` or `FPCGUtilsMeshTargetFunctions::CreateTarget(..., Settings)`.
- Selection-only operations override `RequiresSelection()`. Domain-specific operations override
  `GetRequiredSelectionDomain()` and rely on shared conversion.
- Whole-mesh-only Geometry Script operations use `FPCGUtilsMeshTargetHandle` and the restoration method matching
  their result: region reinsertion for topology, vertex-position restoration for deformation, or a future
  attribute-domain compositor.
- Selection modifiers such as Expand, Contract, Select Connected, and Select Boundary derive from
  `UPCGUtilsDynMeshSelectionOperationSettings`. One element must support both materialized `Selection` mode and
  reusable `Selector` decorator mode; do not create parallel standalone/provider nodes.
- Do not derive a DynMesh process directly from `UPCGSettings`/`IPCGElement`, hand-roll mesh/selection copying, or
  add a Selector pin that the executor does not actually consume.

## Naming

Use `DynMesh`, not `DynamicMesh`, in all PCGUtils-owned filenames, identifiers, node names/titles, categories,
pin/data display names, and documentation. Keep official engine names such as `UDynamicMesh`, `FDynamicMesh3`,
`UPCGDynamicMeshData`, and Geometry Script APIs unchanged. For existing reflected classes, prefer user-facing
display-name changes first; perform C++ renames only with appropriate Core Redirects.

Use `GC`, not `GeometryCollection`, in PCGUtils-owned names and display text. Spelled out, it makes node titles
unusably long (`Geometry Collection Bones To Points` versus `GC Bones To Points`), and those titles are what
users actually type in the palette. This covers filenames, node names/titles, categories, pin labels, and
attribute prefixes (`GC_BoneIndex`). Keep official engine names such as `FGeometryCollection` and
`FManagedArrayCollection` unchanged, and add `Geometry Collection` as a node title alias so search still finds
it. The one deliberate exception is the root data class `UPCGGeometryCollectionData`, spelled out because it is
the name that appears in `Cast<>` calls and error text; its display name is still `GC`.

`Factory` is an internal C++ implementation term only. Never expose it in node titles, pin labels, tooltips,
data-type display names, errors, or end-user documentation. The user-facing term is always `Selector`. Existing
reflected C++ types containing `Factory` remain unchanged until a redirect-backed compatibility migration exists.

In `PCGUtilsFracture` the equivalent user-facing pin labels are `GC`, `Fracture` and `Selection`.

## Validation

Build the affected Unreal target after C++ changes. Inspect graph pins for process nodes: the primary input must
accept DynMesh and DynMesh Selection data, and `Selector` must be present and functional unless the node
is a documented exception.
