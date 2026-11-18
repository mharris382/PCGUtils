# Component reader filtering fixes

November 17, 2026. Active plugin: `B:\UnrealEngine5_Projects\_repos7\MonkeyGolf\MonkeyGolf\VRMonkeyGolf\Plugins\PCGUtils`.

## Behavior changed

Get Marker Data, Get PCG Point Data, Get Path Data, Get Shape Path Data and Get PCG Spline Data now use the execution context's component selector. The engine populates this selector with component references supplied through From Input; consulting only the saved settings allowed eligible sibling components to leak into the output. Shape Path and PCG Spline readers also now apply the inherited selector instead of ignoring it.

No reflected properties, node classes, pins, actor-discovery behavior or asset paths were changed. Existing specialized reader hooks remain in place.

## Files

- `Source/PCGUtils/Private/Elements/PCGGetMarkerData.cpp`
- `Source/PCGUtils/Private/Elements/PCGGetPointData.cpp`
- `Source/PCGUtils/Private/Elements/PCGGetPathData.cpp`
- `Source/PCGUtils/Private/Elements/PCGGetShapePathData.cpp`
- `Source/PCGUtils/Private/Elements/PCGGetPCGSplineData.cpp`
- `Source/PCGUtils/Private/Tests/PCGUtilsActorDataFilterTests.cpp` (new)
- `Source/PCGUtils/Private/Tests/PCGUtilsComponentModifierTests.cpp` (existing reader fixtures now use the correct actor-data execution context)
- Dependent project fixture: `Plugins/CourseGen/Source/CourseGenAmbiance/Private/Tests/CourseGenAmbianceTests.cpp` now uses the actor-data context when directly invoking its inherited spline reader. No bird gameplay behavior changed.

## Validation completed

The isolated UE 5.8 DebugGame editor target built successfully with `-DisableAdaptiveUnity`. The test host is `C:\PCGUtilsShadow\Shadow.uproject`, refreshed from the active plugin's source, config, resources, shaders and descriptor. All seven changed source files were verified byte-identical between the active plugin and the tested host.

All 12 selected automation tests passed:

- Five new tests under `PCGUtils.ActorData.ComponentFiltering`: Markers, PointProviders, PathProviders, ShapePaths, PCGSplines.
- Seven existing tests under `PCGUtils.Modifiers`, including modifier-stack behavior through the marker, point and path readers.

The final active-project focused suite also passed **all 13 tests**, adding `CourseGen.Ambiance.SplineReaderComponentFiltering`. That regression checks the inherited reader's component tags, execution-time component references, specialized component rejection and configured species metadata name/value. The active project's final build succeeded with `-DisableAdaptiveUnity`.

A separate earlier run included `CourseGen.Ambiance.SpeciesFoundation`; it failed only its existing assertion that the legacy DataTable `DisplayName` field was removed. The current `FCourseGenAmbientSpeciesRow` source still contains that field. Its schema assertion was left unchanged, and that failure remains recorded in `ProjectTests.log`. All 12 PCGUtils tests passed in that run as well. The new focused inheritance test does not replace or suppress the existing test.

Each new reader test exercises unrestricted selection, matching/missing/wildcard tags, matching/unrelated component classes, selecting either sibling by execution-time reference, rejecting an unrelated actor's component reference, intersecting list/tag selection, and an invalid actor. These are 55 reader scenarios across five tests. Output existence, selected component tags and preserved actor tags are also checked.

The tests call the actual reader processing callbacks with an actor-data execution context. They do not constitute a complete end-to-end audit of inherited actor discovery, partitioned generation, cache invalidation or production graph generation.

All 184 plugin content assets still match the original content hashes. Existing voxel edits were preserved. No production graph or Blueprint was resaved, and no padding migration was performed.

## Project build

The first active-project build stopped before C++ compilation on an unrelated duplicate `ECourseEmbeddedShapeMode` declaration in CourseGen. That old header was subsequently removed by concurrent project work. The full main-project retry succeeded, followed by a successful incremental rebuild for the dependent bird-spline test fixture. The active project's Development editor binaries now include the changes. See `ProjectBuildRetry.log` and `ProjectBuildFinal.log`.

## Evidence

`Saved/Audit/PCGUtilsFab/2026-11-17_ComponentFiltering/` in the active project contains:

- `Before/` and `source_before.json`: backups and scoped source hashes.
- `Build.log`: initial active-project header-tool error.
- `ShadowBuild.log`: successful isolated build.
- `Tests.log`: all 12 automation test results and exit code 0.
- `verification.json`: test names, content comparison and active/tested source hashes.
- `ProjectBuildRetry.log`: main-project build retry after the concurrent header cleanup.
- `ProjectBuildFinal.log`: successful rebuild after the dependent bird-spline fixture update.
- `ProjectBuildFocused.log`: successful build including the focused inherited-reader regression.
- `ProjectTests.log`: first project test run, including the unrelated `DisplayName` schema assertion failure.
- `FocusedProjectTests.log`: final focused project suite, 13 passed and exit code 0.
- `final_verification.json`: final source/content preservation checks and recorded results.

The working changes remain uncommitted. Fab graph conversion, vector padding migration and the wider actor-reader audit remain separate work.
