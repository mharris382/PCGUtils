# Coordinate spaces: contract and November 21 investigation

PCG points and splines use world space. PCGUtils DynMesh geometry normally uses the full PCG target actor's
local frame, including translation, rotation and scale. Automatic conversion defaults to enabled.

## The museum failure

From Points already removed the actor's full transform. Unreal 5.8's native DynMesh visualization and Spawn
Dynamic Mesh attach components with relative location, world rotation and world scale. Reused native managed
DynMesh components explicitly reset to an identity rotation, unit scale and root location. This restores only
translation and is incompatible with the PCGUtils full-local convention on transformed actors.

Engine evidence: PCGGeometryScriptInterop/Private/Resources/PCGDynamicMeshManagedComponent.cpp,
GetOrCreateDynamicMeshManagedComponent and MarkAsReused; its visualization and Spawn Dynamic Mesh both use
that resource helper. The photographed level has not been visually revalidated by this investigation.

Use **DynMesh | Debug** for editor previews and **DynMesh | Spawn** for managed scene output. Both default
to actor-local input and apply the input data's complete target-actor transform. An optional Target Actor changes
component ownership without changing the input's coordinate frame. Explicitly world-space meshes can disable
Input Is Actor Local. Debug creates preview resources only in editor builds. Spawn passes input data through.
Resources are scoped to the output settings and input index; Debug, Spawn and separate output nodes do not
accidentally share one component. Reuse restores the full current frame.

The new nodes do not alter Unreal's standard Debug node, per-node debug checkbox, or Spawn Dynamic Mesh.
Those native endpoints still use their engine convention. The retained DebugDynMesh subgraph and existing
museum graphs must be migrated deliberately; existing saved graph assets were not overwritten here.

## Shared conversions

PCGUtilsDynMeshSpaceHelpers already existed. It resolved an actor transform, but several nodes performed the
actual conversion independently. It now also provides:

- TransformMeshBetweenSpaces: source-local -> world -> destination-local in one vertex pass, with exact
  forward/inverse evaluation. It does not collapse the map into an SRT relative/inverse transform, which cannot
  represent shear between arbitrarily rotated nonuniform frames. Normals use inverse transpose, tangents use
  the vector transform, and a change of handedness corrects triangle winding.
- TransformNormalToWorld: scale-correct surface normals.
- TransformSurfaceFrameToWorld: full position conversion and an orthonormal normal/tangent point frame.

Updated callers: From Points; To Points; To World/To Local; Sample Mesh; legacy Selection To Points; surface
path frame output; Route Path On DynMesh and Trace Surface Path inverse position/vector conversions; legacy
Get Static Mesh Brush Data; spline-mesh component conversion; and From SplineMesh generation. Spline meshes
are deformed in world space first and complete vertices are then converted before local-space union.

From Points now records the execution target actor, matching the frame it actually used, rather than carrying
an unrelated source point actor. To Points now defaults to world output. Explicit settings remain available.
Graphs relying on the former default-local To Points behavior should explicitly turn world output off.
Sample Mesh retains its existing sample/bounds scale while correcting surface orientation.

The broader suite also exposed a Realize Builders @Data value-copy failure. It now uses the existing shared
value-copy helper rather than metadata-domain parenting, preserving the existing test value and inherited data.

## Remaining release audit (D52)

These are actual source findings requiring focused validation; this work does not certify every DynMesh node.

| Area | Finding / required work |
| --- | --- |
| Realize Builders and deprecated Create Primitive | Seed placement still uses GetRelativeTransform. Rotated nonuniform actor scales can require shear. Preserve Actor Local / Builder Local decorator semantics when introducing an exact affine seed placement; do not simply build every decorator in world space. |
| Refit Builder | SourceFrame.Inverse is applied as a single SRT. Validate nonuniform frame extraction and retargeting. |
| Path To Mesh | Geometry can be local to a separate frame recorded under the configurable @Data.MeshTransform name. It is not necessarily actor-local. Decide and test the automatic frame contract before treating generic output nodes as sufficient. |
| Other selectors, painters, voxel and fracture crossings | Inspect the captured source inventory, identify intentional orientation-only transforms versus real coordinate conversions, and test representative mixed-input operations with independently transformed source and destination actors. Search hits alone are not proof of a defect. |
| Saved examples and retained debug graph | Replace native endpoints with PCGUtils endpoints where full actor-local data is used. Verify a moved, rotated, nonuniformly scaled and mirrored actor in the actual museum, then capture screenshots. |
| Provenance | Native UPCGDynamicMeshData records TargetActor but has no local/world/custom-frame flag. This patch keeps explicit settings; it does not infer the frame of arbitrary native mesh data. |

Validation records are in B:/_PCGUtilsRelease/20261121/CoordinateSpaces*.log and the task's
PCGUtilsCoordinateSpaces folder. Editor automation, packaged runtime compilation and live visual checks are
separate evidence; the final verification record identifies which completed.

## Verified result

33 targeted editor tests passed against the final binaries, including independent nonuniform output ownership. UAT and final incremental UBT builds passed Win64 editor, Development and Shipping. The binary-only update is installed in PCGUtilsExampleProject; its native file hashes match the tested package, 187 retained Content files are unchanged, and startup verified the new classes/defaults with no project C++ modules or implementation source. The photographed museum graph remains unmodified and visually unverified; D52 remains open.
