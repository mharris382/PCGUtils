# Processing several meshes at once

Status: investigation and plan, 2026-10-30. Nothing here is built yet.

## Answer

No DynMesh node runs its inputs in parallel. Every node handles its inputs one after another, and almost all of
them do it on the game thread. When 20 meshes arrive at Decimate, they are decimated one at a time while the
editor waits.

`ParallelFor` appears in five places, all of them *inside* one mesh: Smooth, Project Points (per point), the
surface correspondence used by the Painter LOD transfer, and the Blur and Distance painters.

| Group | Count | What to do |
|---|---|---|
| Nodes on the shared process base (Decimate, Simplify, Remesh, Boolean-free topology, deforms, attributes) | 24 | One change in the base, then a per-node opt-in |
| Nodes with their own input loop and real per-input work | 12 executors (the Selection source base covers 6 Selection nodes) | Same pattern, node by node |
| Cheap per input; parallel would gain little | about 10 | Leave as they are |
| Real game-thread work (assets, components, actors) | 6 | Must stay serial |

So about 36 executors have no good reason to be serial. 24 of them share one loop:
`FPCGUtilsDynMeshProcessBaseElement::ExecuteInternal` in `Source/PCGUtilsDynMesh/Private/Elements/PCGUtilsDynMeshProcessBase.cpp`.

## Why they are serial today

Each reason below was checked against the code and the 5.8 engine source.

1. **The process base declares `CanExecuteOnlyOnMainThread` = true, and most own-loop nodes copy it.** The stated
   reason is resolving the target actor and PCG data. That is true for setup only. The geometry does not need
   the game thread, and `PCGUtilsDynMeshSurfacePathing.h` ("Threading") already says so. The engine's own
   Boolean and Transform DynMesh nodes run Geometry Script on worker threads (they do not override
   `CanExecuteOnlyOnMainThread`), so Geometry Script on a fresh `UDynamicMesh` off the game thread is proven.
   **Not a blocker.**

2. **Operations create UObjects inside `Execute`.** Examples: the Mesh Target region wrappers that Decimate
   (Collapse) and other whole-mesh Geometry Script calls go through (`PCGUtilsMeshTargetFunctions.cpp`, 6 calls);
   Boolean's operand copy; Array's template mesh; the topology base's result selection; Paint Vertex Color.
   They all call `FPCGContext::NewObject_AnyThread`, which is not safe from several worker threads at once:
   - it adds to `FPCGContext::AsyncObjects`, a `TSet` with no lock;
   - it has `ensure(!AsyncState.bIsRunningOnMainThread)`, which fires when a worker calls it for an element that
     is itself running on the game thread.

   **Real blocker. Needs a helper (below).**

3. **Five operations read the target actor's transform per input, inside `Execute`.** These are Transform, Array
   and Mirror in World space, UV Project, and Paint Vertex Color. Each goes through
   `PCGUtilsDynMeshSpaceHelpers::ResolveMeshActorTransform`. Reading an actor from a worker is a game-thread
   access the codebase avoids on purpose. **Small blocker. Needs a prefetch (below).**

4. **Paint Vertex Color's LOD mode shares state across inputs.** It keeps a `mutable` cache of painted primaries,
   and a lower LOD waits on its LOD 0. **Blocker for that mode only.**

5. **Logging is not a blocker.** `FPCGNodeVisualLogs::Log` takes a lock and defers the node-changed broadcast
   when it is called off the game thread (`PCG/Private/Utils/PCGNodeVisualLogs.cpp`). The one rule is to always
   pass the context: `FPCGContext::GetContextForThread()` is null on a worker, and the message would land in
   the output log as `[NOCONTEXT]` instead of on the node. Every operation already passes `Invocation.Context`.

6. **Determinism is not a blocker.** No element code uses a shared `FRandomStream`. Seeds come from settings
   and `Invocation.InputIndex`, which do not depend on the order inputs finish in. Outputs must still be emitted
   in input order, so results are collected per input and emitted afterwards.

7. **Operations are already meant to be thread-safe.** The operation contract
   (`PCGUtilsDynMeshProcessOperation.h`) says operations are immutable, hold no per-evaluation state, and may be
   evaluated for many seeds. Painter operations are documented as safe for concurrent `Evaluate`. Builder
   factories keep no caches. Items 2 to 4 are where the code drifted from that contract.

## Inventory

### Process-base nodes (24): one loop decides all of them

Ready once the base can run inputs in parallel and the allocation helper exists (19):
Decimate, Simplify, Remesh, Self Union, Planar Cut, Merge by Distance, Smooth, Warp, Spline Deform,
Delete Selection, Bevel Edges, Extrude, Extrude to Paths, Inset, Set PolyGroup, Clear PolyGroups,
Set Vertex Color, Material, Properties.

Need the actor-transform prefetch first (5): Transform, Array, Mirror, UV Project, Paint Vertex Color.
Paint Vertex Color stays serial in LOD mode until stage 2b.

Notes:
- Warp and Extrude to Paths read the target actor in `CreateProcessOperation`, which runs once on the game
  thread before any input. That is fine.
- Material loads its materials in `CreateProcessOperation`. That is fine.
- Spline Deform copies the spline into the operation as a value. That is fine.

### Own-loop executors worth converting (12)

| Executor | What runs per input | Notes |
|---|---|---|
| Boolean (operand base) | One A×B pair, or one sequential fold | The fold over operands stays serial inside one output. Outputs are independent. |
| Realize Builders | Every Builder for every seed | Probably the biggest win: one seed runs a whole deferred chain. `Build` per (seed, Builder) is independent. Appending, material-offset assignment and emitting are order-dependent and stay serial. Parallel only if every operation in the chain opts in. |
| Separate Islands | Island search plus one submesh per island | Heavier since the Intersecting mode (self-intersection tree). |
| Separate Selection | Splitting per input | |
| Selection source base (6 nodes: Mesh Occlusion, Self Occlusion, Edge Direction, Sharp Edge Filter, Select Triangles, Selection From Spline) | Evaluating the factory per input | Occlusion is raycast-heavy. The Distance, Bounds and Inside Mesh factories read the actor transform, so they need the prefetch too. |
| Trace Surface Path, Route Path on DynMesh | Per mesh, every path | Resolve the actor transform in the serial pass; it already happens at the top of the loop. |
| Sample | Sampling per input | |
| Path to Mesh, Loft Paths | Triangulation per path | |
| From Static Mesh (execute phase) | Conversion per (mesh, LOD) | Loading stays in `PrepareData`. All LODs multiplies the work. |
| From Static Mesh Points | One append per point data | |

### Leave serial

Real game-thread work: Save Static Mesh, Write DynMesh LODs (asset creation and `PostEditChange`), Paint Static
Mesh Vertex Color (components), Actor Space Transform, Get Spline Mesh Data and Get Static Mesh Brush Data
(actor gathering).

Cheap per input: Selection To Paths, Selection To Points, To Points, Spline To DynMesh, Apply Points by
Vertex ID, Create Primitive, the selection-operation base (expand, contract, boundary), the selection filter
bases, Match Loops to Paths. Project Points is already parallel per point.

## Plan

### Stage 1: parallel inputs in the process base (stays on the game thread)

Split `FPCGUtilsDynMeshProcessBaseElement::ExecuteInternal` into three passes over the concrete inputs:

1. **Prepare, serial.** For each input: copy the mesh, create the output data, rebase the selection, run
   `ResolveInput` (Selector evaluation), and resolve the actor transform into the invocation. This is the same
   code as today in the same order, so warnings and skips come out unchanged.
2. **Execute, `ParallelFor` with `EParallelForFlags::Unbalanced`.** Run `Operation->Execute` for each prepared
   slot and write the output properties. Each slot owns its mesh, so nothing is shared.
3. **Emit, serial, in input order.** Build selection outputs (these allocate), the auxiliary outputs, and the
   tagged data, exactly as now.

The parallel path is gated three ways:
- **A new opt-in on the operation**, `virtual bool SupportsParallelInputs() const { return false; }`, so each node
  is turned on deliberately after it is checked.
- **At least two concrete inputs.**
- **A console variable**, `pcgutils.DynMesh.ParallelInputs` (default 1), to switch it off for debugging and to
  time serial against parallel. A per-node setting would only add clutter.

**Allocation helper** (`PCGUtilsCore`, for example `PCGUtilsAsyncObjects::NewObject<T>(Context)`):
- **On the game thread:** calls `FPCGContext::NewObject_AnyThread`, so nothing changes.
- **On a worker inside a parallel scope:** takes `FGCScopeGuard`, calls `::NewObject`, and records the object
  under a lock. When the `ParallelFor` returns, the scope runs on the game thread and clears
  `EInternalObjectFlags::Async` on each recorded object and its subobjects. The engine does the same in
  `PCGExecuteBlueprint.cpp` (around line 587). Without this, the objects would never be garbage collected.
- **On a worker outside any scope** (an element that already runs off the game thread, serially): calls
  `FPCGContext::NewObject_AnyThread`, as now.

The scope is a thread-local pointer set by the runner around each body call. Then replace
`FPCGContext::NewObject_AnyThread` in every path an operation can reach:
- the Mesh Target functions;
- Boolean, Array, the topology base and Paint Vertex Color;
- the Builder materialization paths.

Add one line to the extension checklist in `PCGUtilsDynMesh.md`: "Inside an operation, allocate through
`PCGUtilsAsyncObjects::NewObject`."

**Actor-transform prefetch:**
- Add a resolved actor transform (optional) to `FPCGUtilsDynMeshProcessInvocation`, filled in the prepare pass.
- Add an overload, `ResolveMeshActorTransform(const FPCGUtilsDynMeshProcessInvocation&, bool bConvertToLocalSpace)`,
  that reads the prefetched value. It falls back to today's lookup when the value is empty, as in deferred
  Builder evaluation.
- Move the 5 operations onto the overload.

**Opt in the 19 ready nodes, then the 5 prefetch nodes.** Paint Vertex Color returns
`!bPaintLODsFromHighest` for now.

### Stage 2: own-loop executors

Factor the three passes into a small shared runner, for example
`PCGUtilsParallelInputs::Run(Context, Num, Prepare, Body, Emit)`. It owns the scope, the console variable and the
ordering. Then convert the 12 executors in the table, most valuable first: Realize Builders, Boolean, Separate
Islands, the Selection source base, From Static Mesh, then the rest.

**Realize Builders.**
- Add `SupportsParallelBuild()` to Builder factories. It is true only when every process operation in the chain
  supports parallel inputs, so one unconverted node in a chain keeps that chain serial.
- The body produces one build result per (seed, Builder).
- The emit pass appends results and assigns material offsets in today's order, so outputs stay byte-for-byte
  the same.

**Selection factories.** `FPCGUtilsDynMeshSelectionEvaluationContext` gets the prefetched actor transform, so
the Distance, Bounds and Inside Mesh factories stop reading the actor.

**Stage 2b: Paint Vertex Color LOD mode in two waves.**
1. Paint the primaries and the ungrouped inputs in parallel.
2. Transfer onto the other LODs in parallel.

The painted-primary cache becomes a per-execution array filled by wave 1, not a `mutable` map.

### Stage 3: off the game thread

Stages 1 and 2 cut the wall time, but the game thread still waits for it, so the editor still freezes for the
duration. Stage 3 removes the freeze using the phase pattern From Static Mesh already uses:
`CanExecuteOnlyOnMainThread` returns true only for `PrepareData`.

Changes to the process base:
- **`PrepareData` (game thread):** calls `CreateProcessOperation` (Material loads assets, Warp reads the actor),
  resolves the Selector and prefetches actor transforms. It stores them in a custom context.
- **Execute:** runs on a worker. Inside the parallel body, `FPCGContext::NewObject_AnyThread` becomes legal from
  workers, though its `AsyncObjects` set still needs the helper's lock.
- **Derived executors** that override `ExecuteInternal` keep their own `CanExecuteOnlyOnMainThread` until each is
  checked.

Do this only after stages 1 and 2 have shown the parallel bodies are clean. It touches every process node's
threading at once.

### Out of scope, noted

The process base deep-copies every input mesh, even when nothing else uses that input. The engine nodes use
`CopyOrSteal` instead. Copying could also move into the parallel body once Selector resolution can run there.
Both are worth doing later.

## Tests

All tests run in the scratch host (`C:\PCGUtilsShadow`, DebugGame, non-unity).

- **`PCGUtils.DynMesh.ParallelInputs.MatchesSerial`**, table-driven over every opted-in node:
  - 8 inputs of different sizes, run with the console variable at 0 and at 1;
  - compares output order, vertex and triangle counts, positions, @Data, tags, selection outputs and auxiliary
    outputs.
- **`PCGUtils.DynMesh.ParallelInputs.AsyncFlagsCleared`**: objects made on workers carry no `Async` flag after
  the run.
- **`PCGUtils.DynMesh.ParallelInputs.ErrorsFromWorkers`**: Decimate with a bad Ratio on 8 inputs logs 8 errors
  on the node, none as `[NOCONTEXT]`.
- **The existing `PCGUtils.Painter.LODGroup.*` tests** pass with the console variable on.
- **A timing test that logs numbers only and never fails:**
  - Decimate Collapse 0.5 on 16 spheres of 20k triangles;
  - Separate Islands Intersecting on 8 banana-tree meshes;
  - Realize Builders with 64 seeds.

  Record the serial and parallel times here.

## Risks

- **Hidden game-thread work in Geometry Script calls we have not read.** The engine running these functions on
  workers covers the common ones, and the serial/parallel comparison test covers each opted-in node. A stray
  `check(IsInGameThread())` would fail loudly in the DebugGame build.
- **Peak memory rises.** Temporary meshes (Mesh Target region copies, for example) now exist for several inputs
  at once, at most one set per worker. Outputs were already all held until the node finished.
- **Load imbalance.** One huge mesh among small ones makes wall time that mesh's time. That is never worse than
  serial, and `Unbalanced` lets the small ones fill the other workers.
- **Nested parallelism.** Smooth and several Geometry Script functions already use `ParallelFor` internally.
  UE's task system handles nesting from the same pool.
- **Log order.** Messages on a node may come out in a different order from run to run. The messages themselves
  do not change.

## Expected gain

Not measured yet. In theory: with N similar inputs on C cores, the execute pass takes about 1/min(N, C) of its
serial time, and the prepare and emit passes stay serial. One input gains nothing, because the parallel path is
skipped. For the banana-tree pipeline (5 trees × 4 LODs = 20 meshes through Decimate, Separate Islands and
Paint), every node in the chain would see the full core count.
