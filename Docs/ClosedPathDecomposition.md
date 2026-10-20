# Closed path decomposition

**Path | Decompose Closed**, under **PCGUtils > Paths**, accepts ordered point paths and emits one `UPCGPointArrayData` per inferred surface patch. Each dataset is independent. Consecutive points form straight edges; the last connects to the first. Do not duplicate the first point at the end.

Input requires a **Data-domain bool `IsClosed=true`**, compatible with PCGExtendedToolkit path data. `Closed Attribute Name` exposes this required contract and defaults to `IsClosed` (enter the bare name). Output retains it even if metadata inheritance is disabled. Legacy point datasets are also accepted; output is always PointArrayData. PCGEx is not a plugin dependency.

## Interior cut flag

Enable **Output Is Cut** to write an optional **point-domain bool**; **Is Cut Attribute Name** defaults to `IsCut` and is editable. Disabled by default. For output point i, true means the edge **i -> (i+1) modulo point count** is an interior cut, rather than an original boundary edge. This includes the final-to-first edge. Each side of a shared seam is flagged in its own path direction. A point at a cut's end is false if its next edge follows the exterior boundary. Consecutive cut segments each flag their starting point, so a point within a multi-edge cut is true.

Flags are per occurrence, not global per source vertex: a shared vertex can be true in one output and false in another. No new vertices are introduced. Enabling the flag replaces any existing point attribute with that name; disabling it leaves inherited attributes untouched. `None` warns and skips the optional write.

## Bias Flat Cuts

**Bias Flat Cuts** defaults to **off**, preserving the original algorithm. Enable it to favor larger horizontal regions bounded by points at the same **world Z**, instead of spreading height changes across those regions. It changes the seed triangulation preference and preserves the resulting flat-to-slope seams during merging. It never changes point positions.

This is a preference, not a global maximum-area guarantee. Level triangles receive half the usual area-plus-perimeter cost; other triangles retain their original cost. Level means a height span no greater than `1e-6` times the median input edge length (numerical tolerance). Existing concavity, warp and intersection constraints still apply. If the biased seed intersects itself, the solver retries the original algorithm. Rotating the path around world Z preserves the intent; tilting it can change which regions qualify as horizontal. Sloped planar ramps are not classified as level.

The current 58-point Ex capture reproduces the reported choice: off retains the diagonal into the raised section; on creates the level seam between captured vertices 37 and 45, with `IsCut` set on both sides. The remaining flat region may still contain cuts required by concavity limits.

## Settings

| Setting | Default | Meaning |
|---|---:|---|
| Bias Flat Cuts | false | Prefer horizontal regions and preserve their seams with slopes |
| Area Weight | 1 | Normalized triangle area cost |
| Length Weight | 0.1 | Normalized triangle perimeter cost |
| Max Concavity | 0.1 | Maximum 1 - projected area / convex hull area for merges |
| Max Quad Warp | 0.35 | Normalized plane deviation for patches of up to four points |
| Max Polygon Warp | 0.02 | Normalized plane deviation for larger patches |
| Max Input Points | 128 | Per-path search guard, 3..128 |
| Inherit Metadata | true | Retain all input metadata domains |

Numeric settings support constants or strict per-input `@Data` attribute reads. For point paths, include the selector prefix, e.g. `@Data.Concavity`. Missing/mistyped values report an error for that dataset. Invalid inputs emit no patches; other input datasets continue.

Every output copies the source native point properties by exact vertex index: transform, density, bounds, color, steepness, seed and metadata correspondence. Tags and target actor are retained. Metadata inheritance defaults on, including `@Data`; disabling it drops source attributes but retains native point properties and the required closure contract.

## Algorithm and limits

The solver normalizes 3D positions by median boundary edge length, finds a minimum-cost triangulated disk by dynamic programming, screens seed triangle intersections, then greedily merges adjacent patches subject to concavity and fitted-plane warp limits. It supports overhangs without a single global projection. Complexity is cubic; the hard cap is 128 points.

This is an experimental surface inference heuristic. A boundary does not uniquely specify its interior. Inputs are independent disk boundaries, not holes; there are no inserted interior vertices. Intersection checks use floating-point tolerances. A downstream mesher can choose different diagonals for a warped patch. Inspect the actual mesh, normals, seams and collision before production use.

## Migration and validation

This replaces the initial CourseGen `Decompose Closed Spline` experiment. A class redirect is supplied, but the data contract changed: reconnect point-path data, not spline data. To use a spline-only consumer, convert each output path to a closed linear spline afterward.

Native suite: `PCGUtils.Paths.Decomposition`. Covers the captured 32-point Ex fixture / 12 handmade patches, cyclic starts, reversed winding, rigid transforms and uniform scale, folded overhangs, invalid geometry, multiple inputs, metadata/native property preservation and outgoing cut flags. The fixture is a saved capture; these tests do not validate a subsequently edited Island actor or authored graph.

Verified 2026-10-20 on UE 5.8: Development Editor build with `-DisableAdaptiveUnity` succeeded; five decomposition tests plus `PCGUtils.Palette.SearchContract` passed (6 successes, 0 warnings, 0 failures). Project report: `Saved/Automation/PathDecomposition/index.json`.

The `FlatCutBias` native regression also checks the current Ex capture, node-setting wiring, both seam flags, reversed winding, cyclic starts, translations, uniform scaling, world-Z rotations and an entirely level square.
