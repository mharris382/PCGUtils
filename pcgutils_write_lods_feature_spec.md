# Task: PCG Element — Write Dynamic Mesh LODs to Static Mesh

## Context (read this first)

You're working in `PCGUtilsDynMesh`, a module of the open-source `PCGUtils` library (github.com/mharris382/PCGUtils), which extends Epic's `PCGGeometryScriptInterop` plugin by exposing Dynamic Mesh operations as native C++ PCG nodes. The target project is **non-Nanite**, mobile-first (Quest 2), stylized art style.

**Before writing any code**, inspect the actual repo — don't invent conventions from scratch:
- Find the existing base settings/element classes in this module (there's a selection-processing base, e.g. something like `UPCGDynamicMeshSelectionProcessBaseSettings`, and other already-shipped elements) and follow their naming, pin-declaration, and logging conventions rather than introducing new patterns.
- Find how existing elements resolve soft object paths from param/attribute-set pins, if that pattern already exists in the module — reuse it.
- Find how existing elements do `PCGE_LOG`-style logging, and match that exact usage.
- Check whether there's already a global budget/settings singleton (something like `UPCGUtilsDynMeshSettings`) that other elements consult for safety limits (max vertex/triangle counts, Warn/Clamp/Reject modes) — this new element should respect the same pattern if it exists, since it's another potentially-expensive operation.

If anything below conflicts with an established convention you find in the codebase, **follow the codebase, not this doc**, and flag the discrepancy.

## Goal

A native C++ PCG element that takes a Static Mesh asset (by soft object path) and a set of `UPCGDynamicMeshData` inputs, and writes each input as a specific LOD on that Static Mesh asset — an editor-time operation intended to run inside standalone/batch-run PCG graphs (batch graph execution already exists elsewhere in this module) as part of an automated LOD-generation pipeline: upstream Simplify nodes produce progressively decimated dynamic meshes, this element commits them to the asset.

## Pins

- **`SourceMeshes`** (input): `EPCGDataType::DynamicMesh`. Must accept **multiple incoming edges on a single pin** — this is standard PCG pin behavior, don't restrict it to a single connection.
- **Target asset pin** (input): carries the target Static Mesh's soft object path. Use whatever param/attribute-set convention the module already uses for asset references; if none exists, an `FSoftObjectPath`-valued attribute on a param data is the natural choice.
- **Output** (optional but recommended): pass through success/failure and/or a reference to the written Static Mesh, matching however other elements in this module report status, so a batch orchestrator graph can log/aggregate results across many assets.

## Settings

| Field | Type | Default | Notes |
|---|---|---|---|
| `LODAssignmentMode` | `EPCGUtilsLODAssignmentMode { ByInputOrder, ByTag }` | `ByInputOrder` | See below |
| `BaseLODIndex` | `int32` | `0` | Only relevant in `ByInputOrder` mode. Nth connected edge → LOD `BaseLODIndex + N`. |
| `bCreateAssetIfMissing` | `bool` | `false` | If the soft path doesn't resolve, create a new `UStaticMesh` asset there instead of failing. |
| `bUseSectionMaterials` | `bool` | `true` | Passed to the GeometryScript write call; assumes the DynamicMesh's material IDs already correspond to the target's material slot indices. |
| `bRecomputeNormals` / `bRecomputeTangents` | `bool` | `false` | Default off — the pipeline expects normals to be handled upstream (seam-aware simplification or a baked normal map), not regenerated generically, since generic recompute degrades stylized hard-surface shading. |
| `LODScreenSizes` | `TArray<float>` (or equivalent) | — | Explicit per-LOD screen size, aligned to LOD index. Don't rely on `bAutoComputeLODScreenSize` — see Execution below. Propose a concrete shape for this if the module has no existing convention (array indexed by LOD vs. a falloff curve); flag the choice rather than silently picking one. |
| `bWarnOnNonMonotonicTriangleCount` | `bool` | `true` | Sanity check, `ByInputOrder` mode only — see Execution. |

`EPCGUtilsLODAssignmentMode`:
- **`ByInputOrder`**: LOD index is derived from the position of each edge in the `SourceMeshes` pin's connection order (`BaseLODIndex + i`).
- **`ByTag`**: each input's `FPCGTaggedData.Tags` is expected to contain a tag matching `LOD<N>`; parse `N` directly. Robust to reconnection/reordering; use this as the safer alternative when order can't be guaranteed (e.g. if this element is ever driven from inside a Loop/Subgraph rather than static wiring).

## Execution behavior

1. Guard the operative body in `#if WITH_EDITOR` — this mutates asset source data and cannot run at runtime. Outside `WITH_EDITOR`, log an error and no-op cleanly.
2. Mark the element **main-thread-only** (asset mutation + `PostEditChange`/`Build` aren't safe off the game thread) — verify the exact `IPCGElement` override name/signature against this engine version's `IPCGElement.h` before assuming it matches prior art.
3. Mark the element **non-cacheable**. It writes to external state (the target asset) not represented in the PCG data flow — a cache hit could silently skip a write that should have happened.
4. Resolve the target `UStaticMesh` from the soft path:
   - Missing + `bCreateAssetIfMissing == false` → log a graph error, skip this execution cleanly.
   - Missing + `bCreateAssetIfMissing == true` → create the asset at that path via the engine's asset tools.
5. Gather `SourceMeshes` inputs for this pin and resolve each to a `(LODIndex, UDynamicMesh*)` pair per the assignment mode.
6. Validate: LOD indices in range (check the engine's max static mesh LOD count constant — verify the exact name/value, don't assume), no duplicates. Log and skip invalid entries rather than crashing.
7. In `ByInputOrder` mode with the warning flag on: check triangle counts are non-increasing across ordered inputs; log a warning (not a failure) if violated — this is a cheap tripwire for a miswired graph, not a hard requirement.
8. Grow the target's source model array to fit the highest LOD index being written before committing.
9. For each `(LODIndex, DynamicMesh)`, write it into that LOD slot using the engine's DynamicMesh→StaticMesh LOD write path (Epic's `UGeometryScriptLibrary_StaticMeshFunctions::CopyMeshToStaticMesh` is the existing, tested implementation of this — calling it directly from C++ is fine and preferable to reimplementing the `FMeshDescription` conversion yourself). Pass `bUseSectionMaterials` and the recompute-normals/tangents settings through. **Verify the exact struct/enum field names for the target-LOD and options parameters against this engine version** rather than assuming a specific spelling.
10. After each successful write, explicitly set that LOD's reduction settings to "no further reduction" (something like `PercentTriangles = 1.0f`) and turn off auto screen-size computation in favor of the configured `LODScreenSizes` value — otherwise UE's internal reducer or auto screen-size logic can silently re-touch geometry you already hand-decimated.
11. After all LODs are written: trigger the asset rebuild (`PostEditChange()`), mark the package dirty. **Do not save the package from inside this element** — leave saving to whatever orchestrates the batch run, so a multi-asset batch can save once at the end rather than per-asset.
12. Report success/failure per LOD write (from the GeometryScript call's outcome) via whatever logging macro/pattern this module already uses, and on the output pin if one exists.

## Explicitly out of scope here

- The Simplify/decimation chain producing the input meshes (assumed to already exist).
- Collision generation.
- Package saving / batch iteration across multiple target assets (a separate orchestrator concern).
- Nanite handling (project is non-Nanite; leave `NaniteSettings` untouched).
- GPU pipeline integration.

## Things to verify against the actual engine headers before/while implementing — do not assume these from general knowledge

- Exact enum values for the GeometryScript "target LOD" struct's LOD-type field (something like `MaxAvailable` / `SourceModel` / `RenderData` — unverified against this engine version).
- Exact `IPCGElement` main-thread-only override name and signature.
- Exact field names on the GeometryScript copy-options struct (recompute normals/tangents, remove degenerates, material replacement flags).
- The engine's max static-mesh-LOD-count constant (name and value).
- `UStaticMesh` API for growing the source-model array to a given count, and its behavior when the target count is smaller than the current count.
- Whether calling the GeometryScript static UFUNCTION directly from C++ has any caveats vs. calling it from Blueprint.
- This module's existing conventions for: pin naming, soft-object-path param-pin pattern, settings base class, logging macro usage, output pin conventions, and any global operation-budget singleton other elements consult.

## Acceptance criteria

- A target `UStaticMesh` (pre-existing) with 3 `SourceMeshes` inputs (full-res, ~50%, ~15%, via upstream Simplify nodes) wired in `ByInputOrder` mode produces a mesh with 3 correctly populated, correctly ordered LODs, visible and correct in the Static Mesh Editor, with material slot assignments preserved.
- The same inputs individually tagged `LOD0`/`LOD1`/`LOD2` and connected in **any** order, with `ByTag` mode selected, produce an identical result.
- A missing target path with `bCreateAssetIfMissing = false` logs a clear graph error and does not crash the graph run.
- A missing target path with `bCreateAssetIfMissing = true` creates a new asset with the given LODs.
- A deliberately out-of-order connection (higher-poly mesh wired after a lower-poly one) in `ByInputOrder` mode produces a graph warning rather than a silent incorrect write.
- No package is saved to disk by this element under any of the above.
