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

`GC` and `GeometryCollection` split by audience, in opposite directions to the `DynMesh` rule.

**User-facing text uses `GC`**: node titles, categories, pin labels, data-type display names, and attribute
prefixes (`GC_BoneIndex`). Spelled out, titles become unusably long - `Geometry Collection Bones To Points`
versus `GC Bones To Points` - and those titles are what users type in the palette.

**C++ uses the full `GeometryCollection`**: class names, filenames, namespaces. `UPCGGeometryCollectionBonesToPointsSettings`,
not `UPCGGCBonesToPointsSettings`. The abbreviation is ambiguous in source - `GC` reads as garbage collection
to anyone who has not seen the node names - and C++ identifiers are read far more often than they are typed.
Spelling it out in code is also what makes the short display name unambiguous when someone goes looking for the
class behind a node.

Keep official engine names such as `FGeometryCollection` and `FManagedArrayCollection` unchanged. Put synonyms
in `UCLASS(meta=(Keywords="..."))` rather than `GetNodeTitleAliases()`, which adds a duplicate palette entry
per alias.

`Factory` is an internal C++ implementation term only. Never expose it in node titles, pin labels, tooltips,
data-type display names, errors, or end-user documentation. The user-facing term is always `Selector`. Existing
reflected C++ types containing `Factory` remain unchanged until a redirect-backed compatibility migration exists.

In `PCGUtilsFracture` the equivalent user-facing pin labels are `GC`, `Fracture` and `Selection`.

## Attributes written to PCG data

Two rules, both about the user being able to see and control what a node produces.

**1. Every attribute a node writes must expose its name as a setting.** No exceptions. An attribute whose name
lives only in C++ is undiscoverable: the user has to run the graph, open the attribute table and read the names
back before they can filter on anything. That is a bad enough experience on its own, and it also silently
prevents the name from being changed to avoid a collision. Exposing the name fixes both, and doubles as
documentation - the details panel becomes the list of what this node can produce.

**2. One flag per attribute, with the name gated behind it.** Never a single flag that bulk-writes several
attributes: the user cannot tell what they are getting, cannot see what any of them are called, and pays for
ones they did not want. Each optional attribute gets its own `bOutput<Thing>` boolean plus its own
`<Thing>AttributeName`, with the name hidden until the flag is on:

```cpp
UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Attributes|Surface", meta=(PCG_Overridable))
bool bOutputExposureRatio = false;

UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Attributes|Surface",
    meta=(PCG_Overridable, EditCondition="bOutputExposureRatio", EditConditionHides))
FName ExposureRatioAttributeName = TEXT("GC_ExposureRatio");
```

Default optional attributes to **off**. Skip the work that computes them when nothing that needs it is enabled,
and warn on the graph if a flag is on but its name is `None`.

The one permitted exception is an attribute that forms a hard contract with another node - one that, if absent,
makes the data useless to its intended consumer. Such an attribute may be written unconditionally, but its name
must still be exposed so it can be matched against the consuming node. Say so in the property comment.

## Validation

Build the affected Unreal target after C++ changes. Inspect graph pins for process nodes: the primary input must
accept DynMesh and DynMesh Selection data, and `Selector` must be present and functional unless the node
is a documented exception.
