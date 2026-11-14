# Component Modifiers (2026-11-13)

Status: **slice 1 (runtime) built and tested headless; slices 2-4 not built.** Nothing has been seen in a real
editor yet. Tests: `PCGUtils.Modifiers.*` (`Source/PCGUtils/Private/Tests/PCGUtilsComponentModifierTests.cpp`).
Code: `Source/PCGUtils/{Public,Private}/Modifiers/`. Until slice 2, modifiers are added through the component's
**Modifiers** array in the Details panel, with no custom header row and no viewport preview.

A Blender-style modifier stack that lives on a PCGUtils component and changes what
PCG reads from that component. A Get node sees the result of the stack instead of the raw component. One marker
with an Array modifier comes out of `Get Marker Data` as a row of markers. One half-drawn path with a Mirror
modifier comes out of `Get PCG Path Data` as a symmetrical closed path.

Modifiers never change the component. The stack takes the component's data in and gives modified data out. It
does not write back to the component's own settings, such as its closed-loop flag or transform.

## Where things live

| Piece | Module | What it is |
| --- | --- | --- |
| `UPCGUtilsComponentModifier` | PCGUtils | Abstract `EditInlineNew` base. Declares targets and has one virtual per target. |
| `FPCGUtilsModifierStack` | PCGUtils | `TArray<TObjectPtr<UPCGUtilsComponentModifier>>` (Instanced) plus `Apply*` helpers. |
| `IPCGUtilsModifierStackOwner` | PCGUtils | Interface returning the stack, so any component (CourseGen ones too) can opt in. |
| `UPCGUtilsArrayModifier`, `UPCGUtilsMirrorModifier` | PCGUtils | The two first modifiers. |
| `FPCGUtilsModifierVisualizerRegistry` | PCGUtilsEditor | `UClass*` → visualizer factory. Lookup walks up the class chain. |
| `FPCGUtilsModifierCustomization` | PCGUtilsEditor | Shared header row: enabled, show visualization, **Edit in Scene**. |
| `UPCGUtilsModifierEdMode` | PCGUtilsEditor | Lightweight editor mode that owns the gizmo while a modifier is being edited. |
| `FPCGUtilsArrayModifierVisualizer`, `FPCGUtilsMirrorModifierVisualizer` | PCGUtilsEditor | Drawing and handles. |

`PCGUtilsEditor` startup registers each visualizer against its modifier class. That is the only link between a
modifier and its widget. The customization asks the registry whether the modifier's class has a visualizer
registered. If one is, it shows the **Edit in Scene** button. If not, there is no button. A modifier does nothing
special to opt in.

## Targets

```cpp
UENUM(meta=(Bitflags, UseEnumValuesAsMaskValuesInEditor="true"))
enum class EPCGUtilsModifierTarget : uint8 { None = 0, Marker = 1 << 0, Path = 1 << 1, Volume = 1 << 2 };
```

A modifier returns its supported targets from `GetSupportedTargets()` and overrides the matching virtuals:

```cpp
virtual void ModifyPoints (const FPCGUtilsModifierContext&, const UPCGBasePointData* In,  TArray<UPCGBasePointData*>& Out) const;
virtual void ModifyPath   (const FPCGUtilsModifierContext&, const UPCGSplineData* In,     TArray<UPCGSplineData*>& Out) const;
virtual void ModifyVolume (const FPCGUtilsModifierContext&, const UPCGSpatialData* In,    TArray<UPCGSpatialData*>& Out) const;
```

Each virtual takes one dataset in and gives a list out, because Array and unfused Mirror turn one path into
several. The stack feeds every output of one modifier into the next. The context carries the component, its
transform (the modifier space, as an object's own axes are in Blender), a seed, and a warning sink.

The rule for data a node modifies applies here too. Output is initialised from the input, so `@Data`, tags and
per-point metadata survive. Copies inherit their source point's metadata entry. A modifier that adds an
attribute, such as Array's copy index, exposes the attribute's name as a setting with its own toggle.

A modifier on a component whose kind it does not support shows a warning on the modifier row. The modifier still
saves, and the stack skips it.

## How Get nodes apply the stack

The Get node builds the dataset exactly as it does today, with the component's metadata already written. It then
calls `FPCGUtilsModifierStack::Apply*` and emits each result, with the component's tags on every one.

- **Markers** (`Get Marker Data`, `Get PCG Point Data`): the stack runs on the point data.
- **Paths** (`Get PCG Path Data`, `Get PCG Spline Data`, `Get Shape Path Data`): the stack runs on spline data,
  which keeps exact tangents. In Points mode, each resulting spline's control points are turned back into path
  points. Its tangents go into the same `ArriveTangent`/`LeaveTangent` attributes the node writes now.
  `@Data.IsClosed` is rewritten from each result.
- **Volumes**: the hook is defined, but nothing implements it yet.

Components that get a stack in v1: `UPCGMarkerComponent`, `UPCGSplineComponent`, `UShapePathComponent`
(`UCourseShapePathComponent` gets it by inheritance).

Known gap: CourseGen code that calls `IPCGPathProvider::Execute_GetPCGPathPoints` directly bypasses the stack.
This includes Course Design Export, route ramp snapshots and `CourseGenHelperFunctionLibrary`. Those callers
should move to a shared `UPCGUtilsModifierLibrary::GetModifiedPaths(Component)` when it matters to them.

## Array

This follows Blender's Array modifier (`source/blender/modifiers/intern/MOD_array.cc`). All math is in modifier
space, the component's own frame, as Blender works in the object's local space.

**Step translation.** `D = Relative Offset * BoundsSize + Constant Offset`, where BoundsSize is the input's
local bounds before any copies are made (`max - min` per axis). For a marker, those are its `BoundsMin/Max`.
For a path, they are its points' bounds in component space. Relative Offset defaults to `(1, 0, 0)`. Constant
Offset is off by default. Each offset has its own enable toggle, as in Blender.

**Count.** The fit types work as in Blender:
- **Fixed Count**: the count is the Count setting. Count is clamped to at least 1. The original is copy 0.
- **Fit Length**: `count = floor((Length + eps) / |step translation|) + 1`. This measures from the start of the
  first copy to the start of the last.
- **Fit Curve**: same formula, with Length replaced by the length of the referenced curve.

A zero step leaves one copy, with a warning.

| Mode | How copy *c* is placed | Edit in Scene |
| --- | --- | --- |
| **Line** | Position is linear: `c * D`. Rotation and scale are iterative: copy *c* has `RotationStep^c` and `ScaleStep^c` applied about its own pivot. The copies' orientation is not preserved when a rotation step is set. The line direction is always `D` and never rotates. | Gizmo on copy 1. Move edits the step, by changing Constant Offset. Rotate edits Rotation Step. Scale edits Scale Step. |
| **Transform** | Blender's Object Offset, applied as a full matrix: `Offset = T(D) * Step`, and copy *c* = `Offset^c`. Rotation compounds into position, which gives circles, helices, fans and spirals. | Full move/rotate/scale gizmo on copy 1, editing Step. Blender edits this through an empty object. |
| **Circle** | Convenience form of Transform. Copies are rotated about Center around Axis by `Total Angle / Count` per copy. At 360 the copies divide the ring evenly, with no duplicate at the seam. | Move: the center. |
| **Curve** | Count comes from Fit Curve. Copies are spaced `|D|` apart along the referenced curve and aligned to its tangent. This is what Blender gets by stacking Array (Fit Curve) with a Curve modifier. | None. The curve's own spline editor is the handle. |

Optional per-copy attribute: `ArrayIndex` (int32, the copy number *c*, original = 0). Its name is exposed.

Blender's other options:
- **Merge** (Distance, First and Last Copies): for markers, points within Distance of a point from the previous
  copy are dropped. First and Last compares the last copy against the first, which removes a closed ring's seam
  duplicate. For paths, Merge joins consecutive copies whose ends are within Distance into one path. This comes
  in slice 4. Until then, each path copy is its own path.
- **Start / End Cap**: Blender adds a different mesh one step before the first copy (`Offset^-1`) and one step
  after the last (`Offset^count`). Not in v1. If wanted, a cap would be an extra point with its own group ID.
- **UV offset**: no equivalent.

## Mirror

Mirror plane is the modifier space's plane. **Axis** chooses X, Y or Z. **Mirror Plane Offset** is a
transform, editable in the scene with the gizmo. **Fuse** applies to paths only. **Merge Distance**
works like Blender's.

- **Marker**: the original plus its mirror. Rotation and bounds are reflected, so handed things stay handed.
  An optional side attribute is available (`MirrorSide`, name exposed).
- **Path, Fuse off**: two paths, the original and its mirror, with point order reversed so the winding stays
  consistent.
- **Path, Fuse on, open source**: the designer draws half a shape with its ends on the plane. Each end within
  Merge Distance of the plane is joined to its mirrored twin. When both ends join, the result is a **closed**
  path, even though the component itself is an open path. An end that is too far from the plane is not
  joined. It stays apart, and the visualizer draws a warning at that end. Nothing is moved, and no bridging
  segment is made up.
- **Path, Fuse on, closed source**: the result is the union of the two closed shapes, in the path's plane. If
  they don't overlap, the result is two closed paths, with a warning.

## Editing in the scene

**Edit in Scene** on a modifier row activates `UPCGUtilsModifierEdMode` for that component and modifier.
While the mode is active:

- the component's own transform gizmo is hidden. The mode supplies the widget location and takes the input
  deltas, so the gizmo edits the modifier: Array's step, circle center or transform, or Mirror's plane;
- the visualizer draws its handles and a full preview of the stack's result;
- every edit is one undo transaction on the component, and PCG regenerates when the drag ends.

The mode ends when you press the button again, select something else, or the component is deleted.
Undoing the component's creation also ends it. Escape ends it too. An editor mode is used instead of
the component visualizer manager because a component class gets only one visualizer. Splines already
use the engine's spline visualizer, and a mode works the same on every component type.

The modifier's warnings, such as a mirror end off the plane or a Curve mode with no curve, are drawn on the
spot in the viewport. They also go to the PCG graph as node warnings. They never block an edit.

Outside edit mode, the component visualizers draw a faint preview of the stack while the component is selected.
**Show Visualization** on each modifier turns that modifier's part of the preview off.

## How slice 1 was built

These are details not covered above:
- **Paths in the Get nodes.**
  - In Spline mode, a spline component goes into the stack exactly as authored (`UPCGSplineData::Initialize(Component)`).
  - In Points mode, the stack gets the provider's own path points. That way a stacked path and an unstacked path
    are sampled the same way, including the PCG Spline's subdivision.
  - Each result's control points are turned back into path points, and its @Data is copied across with
    `FPCGUtilsModifierHelpers::CopyDataDomain`. That carries ArrayIndex and MirrorSide, and source @Data too.
- **@Data.IsClosed** comes from each result, not from the component. A fused half path reports closed.
- **Mirror defaults**: X on, Fuse on, Merge on, Merge Distance 10 cm. Blender's default is 0.1 cm, which is too small
  to hit by hand in a level viewport.
- **Closed-path union** traces both shapes at Union Tolerance and unions them with `UE::Geometry::PolygonsUnion`
  in the path's own plane. Each vertex's height comes from the nearest traced point. The result is a linear path.
  When the mirror plane lies along the path's plane, there is nothing to union: the modifier warns and keeps the
  two shapes as separate paths.
- **Warnings** reach the PCG graph as node warnings prefixed `Actor.Component:`. Each one that is about a place carries
  its world location, ready for slice 2's viewport drawing.
- **Fixed on the way**: `Get PCG Point Data` had its component filter inverted
  (`FilterComponent(...)` instead of `!FilterComponent(...)`). It skipped every component that matched, which by
  default is all of them.

## Build order

1. Runtime: base, stack, interface, Array (Line, Circle, Transform), Mirror (markers, open/closed paths,
   fuse), stack on three components, five Get nodes. Headless tests on the stack output.
2. Editor: customization, registry, preview drawing in the marker / shape path / spline visualizers.
3. Editor: modifier edit mode plus the Array and Mirror visualizers.
4. Array Curve mode, path merge, volume target.
