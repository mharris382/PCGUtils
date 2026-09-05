# Library standardization

## Agreed architectural contract

- DynMesh is both an analysis representation and a mutable geometry representation. A static mesh painting
  workflow may use an intermediate DynMesh solely to author selections and painting fields.
- Painter retains its dependency on DynMesh. Shared immutable factory data, dependency retention, priority,
  input collection, and per-execution operation infrastructure belong in PCGUtilsCore, not PCGUtils.
- Priority remains fundamental factory state. Ordering semantics belong to each consuming family:
  selection short-circuit precedence is descending; existing Builder composition order is unchanged.
- One public selection element implements each algorithm. Selector is the default representation; Selection
  materializes the same logic. Representation presets use centrally formatted suffixes and searchable keywords.
- Public terminology is family-specific: Selector, Builder, Painter. Factory and Provider remain internal terms.
- Processor capability changes in this pass are editor/UX only. Existing selection requirements, domain
  conversion, output behavior, and the shared runtime resolver remain authoritative.
- Painter Builder integration is deferred until Builder conventions are standardized. In particular, deferred
  operations must retain Painter UObject dependencies explicitly rather than capturing unrooted pointers.

## Progress and verification

The shared factory foundation has moved to PCGUtilsCore. Its existing C++ names and include paths remain
unchanged for source compatibility; Core Redirects cover the new reflected module ownership. No mesh module
is a dependency of Core. Painter still depends on DynMesh. Generic infrastructure no longer migrates selection
pin names: selection bases own that compatibility behavior.

2026-09-05: The full UtilsDevProjectEditor target builds successfully. The native operation base now has an
out-of-line constructor, copy constructor, and destructor so Core exports its cross-module symbols. All 32
PCGUtils.DynMesh automation tests passed, including Core.ModuleContract, Selection.PaletteContract, and the
existing Painter, Builder, Boolean, topology, and UV tests. This verifies the foundation, not the remaining refactor.

Nine processor titles now explicitly identify DynMesh: Set DynMesh Material, Set DynMesh Vertex Colors,
Bevel DynMesh Edges, Delete DynMesh Selection, Separate DynMesh Selection, Remesh DynMesh, Warp DynMesh,
Deform DynMesh Along Spline, and Project DynMesh UVs. Search keywords were added to these processors plus
Smooth, Extrude, and Inset. Reflected class names and runtime geometry behavior are unchanged.

## Remaining implementation phases

Painter correspondence update (2026-09-05): Bounds Brush Painter and Painter by Vertex ID names/keywords are
implemented. New ID nodes use an explicit int32 VertexIndex attribute (configurable), support reordered/sparse
point datasets, and reject duplicate/invalid IDs. Legacy serialized graphs retain point-order behavior via a
custom version. The full editor target now builds and all 34 PCGUtils.DynMesh tests pass, including the new
VertexIDMapping and BlendValues tests. The legacy point-order test also passes. No manual graph migration or
interactive editor UX validation is claimed by this automation result.

Painter Blend is implemented with a stable title, operation subtitle, existing serialized enum values preserved,
Mix/Screen additions, scalar/color handling, constant Factor and optional scalar Mask. Base A defines the output
channels; missing blend B channels preserve A. Alpha is an ordinary channel, not implicit opacity. The numerical
tests cover all seven modes, factor interpolation, scalar broadcast, and undefined channel preservation.

1. Foundation extraction and regression verification: complete.
2. Centralize processor editor capabilities without changing runtime policy. Hide irrelevant selection/output
   controls on queries, required-selection operations, and mesh-only outputs. Standardize processor titles,
   keywords, pin labels, and rename migrations; audit source-node category overrides.
3. Converge DynMesh To Points and DynMesh Selection To Points into one public, selection-aware converter using
   the process settings base and shared read-only resolver. Preserve old graphs through a hidden compatibility
   class, including legacy coordinate defaults. Expose generated attribute names and independent output flags.
4. Painter Blend implementation and numerical regression tests: complete. Interactive pin/migration and
   dedicated Mask integration coverage remain part of final integration verification.
5. Standardize explicit correspondence as Painter by Vertex ID and spatial influence as Bounds Brush Painter.
   Explicit ID lookup must not depend on point order, positions, or bounds. Retain an explicit legacy point-order
   mode. Surface projection/interpolation is excluded from this refactor; it is a separate feature, not ID mapping.
6. Update Painter terminology/docs and test generic fields on both DynMesh and static-mesh-style evaluation
   contexts. Preserve Set Vertex Color's distinct overlay/seam behavior rather than merging it blindly.
7. Add palette/pin contract and integration coverage; compile all affected modules and run automation. Record
   Builder follow-up and remaining-module audit findings without claiming whole-library completion.
