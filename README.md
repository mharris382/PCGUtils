# PCGUtils for Unreal Engine

PCGUtils is a collection of native components, interfaces, PCG nodes, editor tools, and dynamic-mesh utilities built on Unreal Engine's Procedural Content Generation framework. The plugin descriptor currently targets Unreal Engine 5.8.

The repository also contains packaged PCG graphs and subgraphs under `Content/`. Those are binary Unreal assets, so this README focuses on the features implemented in the readable source modules. Some bundled graph assets may use [PCG Extended Toolkit](https://github.com/PCGEx/PCGExtendedToolkit); the native modules do not declare it as a compile-time dependency.

## Modules

- `PCGUtils` — core actors, components, provider interfaces, metadata types, helper libraries, and general PCG nodes.
- `PCGUtilsDynMesh` — native dynamic-mesh nodes, triangle-selection data, brush actors, and mesh helpers.
- `PCGUtilsEditor` — component visualizers, details customizations, and editor styling.

## PCG actor and baking workflow

`PCGActorBase` is a reusable actor base for PCG-driven tools. It owns a PCG component and an editor bounds box, computes bounds from supported components and splines, and can regenerate when its utility components are edited. It also provides:

- A stable seed and standardized bake settings.
- Save name, save path, asset group, and baked-asset metadata.
- Pre-bake and post-bake override graphs.
- A Call in Editor action to recenter the actor pivot without moving attached spline geometry.
- Blueprint hooks for custom bounds, editor color, and remapping other local-space data after recentering.

The packaged content includes a `BakeMesh` workflow and related templates. When baking multiple meshes from one graph, give each dynamic-mesh data set a distinct `@Data.Label` to avoid save-name collisions.

## Components and paths

### PCG Marker Component

A designer-positioned box that implements the bounds and point-provider interfaces. PCG can retrieve markers as points with standardized group, density, color, and override-graph metadata. The editor visualizer supports configurable selected/unselected colors, fill opacity, and wireframe display.

![](https://i.imgur.com/usURBXv.png)

### PCG Spline Component

Extends Unreal's spline component with the `PCGPathProvider` interface, shared `PathData`, optional curved-segment subdivision, editor colors, and edit-triggered PCG regeneration. It is the common spline base for the generated spline components below.

### PCG Child Spline Component

Copies a normalized section of an attached spline or a tagged spline component on another actor. It supports open and closed source splines, forward wrapping across a closed-loop seam, configurable endpoint tangent modes, preserved editable points before and after the copied section, a Z offset, automatic source-change detection, and manual refresh. Its visualizer marks the copy range on the source spline.

### Shape Path Component

Produces an ordered path from an instanced shape generator instead of hand-edited spline control points. Native generators cover:

- Circle
- Arc
- Rectangle
- Polygon
- Star

The component implements both path and bounds providers, carries standard path metadata, rebuilds in the editor, and has a dedicated visualizer.

![](https://i.imgur.com/utLnXaE.png)

### PCG Shape Spline Component

Generates a real `PCGSplineComponent` from the same shape-generator system. It can optionally preserve manual point offsets while generator settings change.

## Provider interfaces and shared data

The core module defines Blueprint-compatible interfaces that decouple PCG nodes from concrete component classes:

- `PCGPathProvider` — ordered path points, local/world-space indication, path metadata, and closed-loop state.
- `PCGPointProvider` — point collections with shared point metadata.
- `PCGBoundsProvider` — actor-relative bounds contribution.
- `PCGBakeSettingsProvider` — standardized asset-baking settings.
- `PCGComponentProvider` — access to a primary PCG component and regeneration policy.

`FPathComponentData`, `FPointComponentData`, bake settings, and override-graph types provide consistent metadata and extension hooks across these interfaces.

## Native general-purpose PCG nodes

- **Get PCG Path Data** — collects any `PCGPathProvider` as `UPCGPointArrayData` or PCG spline data, preserving path metadata and tags.
- **Get PCG Point Data** — collects points from `PCGPointProvider` components.
- **Get Marker Data** — retrieves marker components and their point metadata.
- **Get PCG Spline Data** — retrieves PCG spline components with their standardized settings.
- **Get Shape Path Data** and **Get Spline Data With Overrides** — compatibility nodes for the specialized path component workflows.
- **Get Static Mesh Data** — emits one point-data collection per matching static-mesh component.
- **Get Actor Bake Settings** — reads standardized bake settings through the provider interface using preconfigured node variants.
- **Get Override Graph Sets** — routes data according to serialized override-graph metadata.

The component-query workflow is shown below:

![](https://i.imgur.com/f2wDGJI.png)

## Native dynamic-mesh operations

All native dynamic-mesh PCG operations currently implemented in the source are listed here:

- **Spline To Dynamic Mesh** (`SplineMesh To DynMesh`) — deforms a static mesh along every segment of an input PCG spline. It supports mesh selection from a data attribute, spline-mesh axis/up/roll settings, render LOD, tangent scaling, optional segment union, and world-to-target-actor conversion.
- **Dynamic Mesh To Points** — creates points in vertex-index order while preserving vertex positions, colors, and normal-derived rotations, with optional world-space output.
- **Apply Points To Dynamic Mesh** — writes point positions back to mesh vertices and can also write normals, full or component-wise vertex colors, and mapped UV channels. It supports local-space conversion and configurable point/vertex count mismatch handling.
- **Assign Material** — assigns material slots to triangles using face-normal classification.
- **Merge By Distance** — merges nearby dynamic-mesh vertices using a configurable threshold.
- **Select Mesh Triangles** — creates reusable triangle-selection data by edge length or face normal, with minimum matching-edge and inversion controls.
- **Dynamic Mesh Selection To Points** — converts the unique vertices used by selected faces into PCG points, optionally in target-actor world space.
- **Delete Mesh Selection** — deep-copies the source mesh and removes the selected triangle faces.

The dynamic-mesh module also provides selection data types and a brush framework: `DynMeshBrushActor`, `StaticMeshBrushActor`, and `StaticMeshBrushComponent` expose additive/subtractive brush configuration and convert static-mesh geometry for dynamic-mesh workflows.

## Helper libraries and editor support

Native helper functions cover spline and shape-path bounds, actor bounds aggregation, PCG regeneration, static-mesh references stored in PCG metadata, conversion from legacy `FPCGPoint` arrays to `UPCGPointArrayData`, and render-bake utilities.

The editor module registers visualizers for markers, shape paths, and child splines, plus details customization for override-graph values. These tools keep component-authored PCG data visible and editable directly in the level viewport.

## Dependencies

The plugin enables Unreal's `PCG`, `GeometryScripting`, and `PCGGeometryScriptInterop` plugins. Dynamic-mesh functionality also uses Unreal's Geometry Framework and Modeling components through the module dependencies declared in `Source/`.
