# PCGUtils Development Guidance

Repository-wide instructions are in `AGENTS.md`. For work in `PCGUtilsDynMesh`, also read
`Source/PCGUtilsDynMesh/claude.md` and `Docs/PCGUtilsDynMesh.md` in full. For work in `PCGUtilsFracture`, read
`Source/PCGUtilsFracture/claude.md`.

The critical DynMesh rule is that selection support is part of the process contract, not a per-node optional
feature. Use `UPCGUtilsDynMeshProcessBaseSettings` plus the shared resolver/target-handle infrastructure so a node
accepts DynMesh data, DynMesh Selection data, and an optional Selector consistently. Use `DynMesh` for all
PCGUtils-owned names and display text; retain full `DynamicMesh` only in official Unreal Engine API names.
`Factory` is implementation-only terminology and must never appear on user-facing graph surfaces. Selection
modifiers use `UPCGUtilsDynMeshSelectionOperationSettings` so the same node supports Selection and Selector modes.

`PCGUtilsFracture` uses Unreal's Geometry Collection / Fracture stack as a transient procedural modelling
backend. Its rules: `UPCGGeometryCollectionData` is immutable (mutate only a `CreateMutableCopy()`); every
topology-changing node publishes a new state via `InitializeAsRevisionOf`; fracture behaviour and bone selection
are separate factory families consumed by generic executors. Use `GC` in names, never `GeometryCollection`.
`PCGUtilsDynMesh` must never depend on `PCGUtilsFracture`.

Every palette entry in both modules is named `[CATEGORY] | [ELEMENT_NAME]`, with spaces around the pipe:
`DynMesh | Extrude Faces`, `Builder | Box`, `Select | In Bounds`, `Painter | Blend`, `GC | Fracture`,
`GC | Select | Contact`. The element name never repeats its category, and a selection name carries none of
`select`/`selection`/`selector` (GC selection names also omit `GC`). This prefix *is* the grouping mechanism:
PCG derives the palette category from `EPCGSettingsType` alone, with no per-class hook, so everything here
lands under `Dynamic Mesh` regardless. An element deriving straight from `UPCGSettings` must therefore also
override `GetType()`, or it silently disappears into `Generic`. `AGENTS.md` has the full rule plus the
context-menu search mechanics, and `PCGUtils.Palette.SearchContract` enforces both - extend that test rather
than checking the palette by hand.

