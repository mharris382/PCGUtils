# PCGUtils Development Guidance

Repository-wide instructions are in `AGENTS.md`. For work in `PCGUtilsDynMesh`, also read
`Source/PCGUtilsDynMesh/claude.md` and `Docs/PCGUtilsDynMesh.md` in full.

The critical DynMesh rule is that selection support is part of the process contract, not a per-node optional
feature. Use `UPCGUtilsDynMeshProcessBaseSettings` plus the shared resolver/target-handle infrastructure so a node
accepts DynMesh data, DynMesh Selection data, and an optional Selector consistently. Use `DynMesh` for all
PCGUtils-owned names and display text; retain full `DynamicMesh` only in official Unreal Engine API names.
`Factory` is implementation-only terminology and must never appear on user-facing graph surfaces. Selection
modifiers use `UPCGUtilsDynMeshSelectionOperationSettings` so the same node supports Selection and Selector modes.
