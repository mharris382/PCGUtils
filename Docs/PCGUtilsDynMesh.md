# PCGUtilsDynMesh

`PCGUtilsDynMesh` is the PCG-native DynMesh processing layer in PCGUtils. It is intended to be more than a set of
Geometry Script wrappers: it provides a consistent graph contract for mesh ownership, selections, reusable
selectors, domains, coordinate spaces, and operations that Geometry Script only exposes for complete meshes.

## Goals

1. Make DynMesh workflows feel native to PCG, including flexible typed pins, overrides, graph composition, and
   consistent transitions between PCG world space and DynMesh local space.
2. Expose Geometry Script's mesh-processing surface comprehensively through PCG.
3. Port useful procedural operations from Blender when no Geometry Script equivalent exists, following the
   source algorithm and applicable open-source license requirements.
4. Make practically every operation selection-aware unless applying it to only part of a mesh is semantically
   invalid.

## Unified process contract

`UPCGUtilsDynMeshProcessBaseSettings` is the settings contract for nodes that process or query an existing mesh.
Its primary input accepts either:

- `UPCGDynamicMeshData`, representing the whole DynMesh; or
- `UPCGDynamicMeshSelectionData`, representing a materialized selection tied to its source DynMesh.

It also exposes one optional `Selector` input. For a whole-mesh input, the selector materializes the effective
selection. For an incoming materialized selection, the selector is evaluated in the effective domain
and intersected with that selection.

`FPCGUtilsDynMeshProcessFunctions::ResolveInput()` is the common resolver for specialized/query executors.
`FPCGUtilsDynMeshProcessBaseElement` provides the default deep-copy-and-`ProcessMesh()` mutation executor.

### Required selections and domains

An operation that cannot run without a selection overrides `RequiresSelection()`. It then accepts either a
materialized selection input or a whole DynMesh plus a Selector. A bare DynMesh without a selector produces
a graph error from the shared resolver.

An operation with a native vertex, edge, or face domain overrides `GetRequiredSelectionDomain()`. Incoming
selections and selectors are converted centrally using Geometry Script-compatible inclusive conversion. Override
`AllowPartialSelectionDomainInclusion()` only when full incident-element inclusion is required.

## Geometry Script API shapes

| Geometry Script shape | PCGUtilsDynMesh execution |
|---|---|
| Whole-mesh and selection overloads | Dispatch to the appropriate overload using the resolved selection. |
| Optional selection/no-selection behavior | Pass the resolved selection and explicit no-selection policy. |
| Selection-only operation | Override `RequiresSelection()` and the required domain. |
| Whole-mesh-only operation | Operate through a Mesh Target handle and restore only the selected result domain. |

## Mesh Target handles

`FPCGUtilsMeshTargetHandle` separates selection resolution from working-mesh preparation and restoration.
Handle-based elements still derive from the process settings base and pass their settings to
`FPCGUtilsMeshTargetFunctions::CreateTarget(..., Settings)`, ensuring that Selector inputs are honored.

## Selection modifiers

Selection-transforming nodes derive from `UPCGUtilsDynMeshSelectionOperationSettings`. Their `Operation Mode`
switch provides two graph forms from the same element:

- `Selection` consumes and emits materialized DynMesh Selection data.
- `Selector` decorates an upstream Selector and emits another reusable Selector.

Expand Selection, Contract Selection, Select Connected, and Select Boundary follow this contract. Older
standalone/provider duplicates remain loadable only for graph compatibility and are deprecated.

### Preparation and restoration

- `Region` extracts the selected triangle region. Use it when the operation may change topology and can safely be
  welded back. Finalize with `RestoreRegion()`.
- `FullMeshCopy` preserves source vertex IDs. Use it for whole-mesh-only deformations or selection-aware Geometry
  Script calls. Finalize deformations with `RestoreVertexPositions()`.
- A selection-aware operation that directly and completely edits the full copy may need no compositor, but this
  is only valid when its own Geometry Script selection scopes every changed result domain.

Attribute-only whole-mesh operations may eventually need dedicated compositors for colors, UVs, normals,
materials, polygroups, or weight maps. Do not misuse vertex-position restoration for non-position results.

## Coordinate spaces

PCG points, splines, and bounds are normally authored in world space. DynMesh geometry in this module is normally
target-actor-local. Nodes that combine these representations use `PCGUtilsDynMeshSpaceHelpers` or
`PCGUtilsSplineHelpers`; conversion should not be reimplemented per node.

Unreal's `UPCGDynamicMeshData` currently carries geometry and materials but no coordinate-space provenance.
Therefore automatic conversion is reliable only where the node knows the provenance of the external PCG data and
the module's local-space convention. The explicit To World/To Local nodes remain necessary for arbitrary inputs.

## Naming

PCGUtils-owned terminology uses `DynMesh` to keep node names compact and distinguish them from vanilla Unreal
Dynamic Mesh nodes. Official Unreal API types retain their engine names, including `UDynamicMesh`,
`FDynamicMesh3`, and `UPCGDynamicMeshData`.

Existing reflected element classes are not renamed merely for terminology, because that can invalidate serialized
references. Their node names, titles, data display names, and other user-facing text should use `DynMesh` now;
future C++ renames require Core Redirects.

## Extension checklist

When adding an operation:

1. Classify its Geometry Script selection shape and result domains.
2. Derive from `UPCGUtilsDynMeshProcessBaseSettings` or a more specific selection-authoring/filter base.
3. Override `RequiresSelection()` and `GetRequiredSelectionDomain()` where applicable.
4. Use the default process executor when direct mutation is sufficient; otherwise use the shared resolver or Mesh
   Target handle and pass the settings object.
5. Choose a restoration strategy for every result domain the operation modifies.
6. Use shared coordinate-space helpers for world/local interaction.
7. Confirm the graph exposes a DynMesh-or-Selection input and a functional optional Selector pin, unless
   the exception is documented.
8. Use `DynMesh` in PCGUtils-owned display names and make normal settings PCG-overrideable.
9. Build the module and visually inspect the node's pins and overrides.

## Recommended base-class evolution

The current base can be improved further:

- Replace the Boolean `RequiresSelection()` with an explicit policy enum: `Optional`, `Required`, or `Unsupported`.
  `Unsupported` would document rare whole-object semantic operations such as coordinate-space conversion.
- Separate selection input/selector policy from mutation-output policy so query/conversion nodes do not inherit an
  irrelevant `bOutputSelectionData` setting.
- Add a result-domain declaration (topology, positions, normals, UVs, colors, materials, and so on) so the target
  layer can validate or select the correct compositor automatically.
- Add a space policy once PCGUtils can represent coordinate-space provenance on DynMesh data. The process base
  could then normalize inputs and outputs automatically instead of relying on per-node switches.
- Cache one resolved selector per execution and reuse it for every mesh input rather than parsing the selector pin
  for each input.
- Add automation tests covering whole mesh, materialized selection, selector selection, intersection, empty
  selections, required-selection failures, domain conversion, and each restoration strategy.
