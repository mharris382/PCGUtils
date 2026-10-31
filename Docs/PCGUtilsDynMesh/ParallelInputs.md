# Processing several meshes at once

Status: investigation and plan, 2026-10-30. Stage 1 is written (see Stage 1, "As written") but not yet
compiled or tested; stages 2 and 3 are not started.

## Answer

No DynMesh node runs its inputs in parallel. Every node handles its inputs one after another, and almost all of
them do it on the game thread. When 20 meshes arrive at Decimate, they are decimated one at a time while the
editor waits.

`ParallelFor` appears in five places, all of them *inside* one mesh: Smooth, Project Points (per point), the
surface correspondence used by the Painter LOD transfer, and the Blur and Distance painters.

| Group | Count | What to do |
|---|---|---|
| Nodes on the shared process base (Decimate, Simplify, Remesh, Boolean-free topology, deforms, attributes) | 24 | One change in the base; on by default, with a per-node opt-out and a Disable Parallel advanced setting |
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
| Realize Builders | Every Builder for every seed | Probably the biggest win: one seed runs a whole deferred chain. `Build` per (seed, Builder) is independent. Appending, material-offset assignment and emitting are order-dependent and stay serial. Parallel only if no node in the chain opts out or has Disable Parallel set. |
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

The parallel path runs only when every one of these allows it:

| Gate | Where | Who sets it | Purpose |
|---|---|---|---|
| **Disable Parallel** (`bDisableParallelInputs`, default false) | `UPCGUtilsDynMeshProcessBaseSettings`, `AdvancedDisplay`, category `Advanced` | the user, per node instance | Check whether a problem comes from the parallel path: tick it on the suspect node, regenerate, compare. Not `PCG_Overridable`: it is a debugging switch, not data. |
| **Node opt-out**, `virtual bool SupportsParallelInputs() const { return true; }` | the settings class | the node's author | A node that must never run its inputs in parallel returns false. Its Disable Parallel row is hidden (`CanEditChange` / a details customization), since ticking it would do nothing. |
| **Operation opt-out**, `virtual bool SupportsParallelExecute() const { return true; }` | `IPCGUtilsDynMeshProcessOperation` | the operation, from its own settings | For cases that depend on the node's settings: Paint Vertex Color returns `!bPaintLODsFromHighest` until Stage 2b. |
| **At least two concrete inputs** | the base | — | One input gains nothing. |
| **Console variable** `pcgutils.ParallelInputs` (default 1) | global | the user | Turns it off for every node at once, and times serial against parallel. |

**Default on, opt out.** The base turns parallel on for every node, and a node opts out with a reason. That
makes the parallel path the normal path, so problems show up instead of hiding behind flags nobody turned on.
Each `return false` carries a one-line comment saying why (for example "writes to the target actor's
components"), so the list of opt-outs explains itself.

**Logging.** When parallel is skipped for a reason other than input count, the node logs one Verbose line on
`LogPCGUtilsDynMesh` naming the gate ("Parallel inputs off: Disable Parallel is set"), so a user checking
whether it ran in parallel can tell.

**As written (2026-10-30, not yet compiled or run):**

- **Runner and allocation helper**, `PCGUtilsCore/Public/Helpers/PCGUtilsParallelInputs.h`:
  - `PCGUtilsParallelInputs::ForEach(Context, Num, bParallel, Body)` runs `ParallelFor` (Unbalanced) only when
    `bParallel`, `Num >= 2` and the caller is on the game thread; otherwise a plain loop. It returns whether it
    went parallel, and counts parallel runs for tests (`GetParallelRunCount`).
  - `PCGUtilsAsyncObjects::NewObject<T>(Context, ...)`: on a worker inside a run, `FGCScopeGuard` + `::NewObject`,
    recorded under a lock; everywhere else exactly `FPCGContext::NewObject_AnyThread`. When the run ends,
    `FAllocationScope` clears `EInternalObjectFlags::Async` on each recorded object and its subobjects on the
    game thread, as `PCGGraphExecutor::ClearAsyncFlags` and `PCGExecuteBlueprint.cpp` do.
  - Console variable `pcgutils.ParallelInputs` (default 1), shared by every PCGUtils module, not DynMesh only.
- **Every `FPCGContext::NewObject_AnyThread` in PCGUtilsDynMesh and PCGUtilsPainter** (91 calls, 65 files) now
  goes through the helper. Outside a parallel run it is the same call, so this changes nothing on its own, and it
  covers paths an operation can reach that nobody has listed.
- **Process base:** the three passes; the gates as in the table above; the Verbose line naming the gate.
  Unmigrated nodes (no process operation, `ProcessMesh` instead) always run serially.
- **Opt-outs:**
  - Node-level, `SupportsParallelInputs() == false`: the nodes with their own executor (Selection to Paths,
    Sample, Route Path, Trace Surface Path, Apply Points By Vertex ID, the operand base and so Boolean, Project
    Points, Separate Islands, Separate Selection) and Properties (runs through `ProcessMesh`). For the own-executor
    nodes this only hides the row, since their loop never reaches the base; Stage 2 removes those opt-outs.
  - Operation-level: Paint Vertex Color returns `LODPlan.PrimaryOfInput.IsEmpty()`, so Paint LODs From Highest
    Detail stays serial.
- **Behaviour change in the emit pass:** when building the full-mesh selection output fails, the half-made output
  entry is now removed. Before, it stayed in the output as a copy of the input's tagged data.

**The actor-transform prefetch moved to Stage 3.** Reading the target actor from a worker is safe in Stage 1:
`FPCGContext::GetTargetActor` is plain reads, and the game thread is blocked inside the `ParallelFor` for the
whole run, so nothing can move the actor or collect it. It only becomes necessary once the element itself runs
off the game thread, where the game thread keeps ticking.

**Code reachable from an operation's `Execute`** was checked for other context use: operations read pins,
settings and load assets only in `CreateProcessOperation`, which runs serially; Paint's selector resolution
happens only in LOD mode, which is serial.

**First own-loop user: DynMesh | Generate LODs.** It is not on the process base. It allocates every output
serially, then calls `PCGUtilsParallelInputs::ForEach` once over every (input, LOD) pair, so one input's LODs
also decimate at once. Its own `Disable Parallel` and the CVar gate it. This is the shape Stage 2 converts the
other own-loop executors to.

### Stage 2: own-loop executors

Factor the three passes into a small shared runner, for example
`PCGUtilsParallelInputs::Run(Context, Num, bAllowParallel, Prepare, Body, Emit)`. It owns the scope, the console
variable, the input-count gate and the ordering. The caller passes `bAllowParallel` from its own gates, so
the process base and the own-loop executors share one implementation. Executors with their own settings class
get the same **Disable Parallel** advanced setting and `SupportsParallelInputs()` opt-out, in a small shared
settings mixin or by copying the two members. The rule is the same everywhere: a user can always force serial
on one node. Then convert the 12 executors in the table, most valuable first: Realize Builders, Boolean, Separate
Islands, the Selection source base, From Static Mesh, then the rest.

**Realize Builders.**
- Add `SupportsParallelBuild()` to Builder factories. It is true only when every node in the chain allows parallel (no opt-out, Disable Parallel off,
  operation opt-out allows it), so one serial node in a chain keeps that chain serial.
- The body produces one build result per (seed, Builder).
- The emit pass appends results and assigns material offsets in today's order, so outputs stay byte-for-byte
  the same.

**Selection factories.** `FPCGUtilsDynMeshSelectionEvaluationContext` gets the prefetched actor transform, so
the Distance, Bounds and Inside Mesh factories stop reading the actor (needed for Stage 3 only).

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
- **Actor-transform prefetch** (moved here from Stage 1): an optional resolved actor transform on
  `FPCGUtilsDynMeshProcessInvocation`, filled in `PrepareData`, and a
  `ResolveMeshActorTransform(const FPCGUtilsDynMeshProcessInvocation&, bool)` overload that falls back to today's
  lookup when it is empty (deferred Builder evaluation). Move Transform, Array, Mirror, UV Project and Paint onto
  it. Off the game thread the game thread keeps ticking, so reading the actor from a worker is no longer safe.
- **`PCGUtilsParallelInputs::ForEach` refuses to go parallel off the game thread** today, because only the game
  thread may clear Async flags. Stage 3 needs another way to hand worker-created objects back: for example,
  allocate the per-input objects in `PrepareData`, or clear the flags in a short game-thread phase after Execute.

Do this only after stages 1 and 2 have shown the parallel bodies are clean. It touches every process node's
threading at once.

### Out of scope, noted

The process base deep-copies every input mesh, even when nothing else uses that input. The engine nodes use
`CopyOrSteal` instead. Copying could also move into the parallel body once Selector resolution can run there.
Both are worth doing later.

## Tests

All tests run in the scratch host (`C:\PCGUtilsShadow`, DebugGame, non-unity).

- **`PCGUtils.DynMesh.ParallelInputs.Gates`**: each gate on its own forces the serial path (Disable Parallel,
  a node opt-out, an operation opt-out, one input, the console variable at 0), and with none set the parallel
  path runs. Observed via a test hook that counts parallel bodies.
- **`PCGUtils.DynMesh.ParallelInputs.MatchesSerial`**, table-driven over every node that doesn't opt out:
  - 8 inputs of different sizes, run with Disable Parallel on and off;
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
  workers covers the common ones, and the serial/parallel comparison test covers each node that does not opt out. A stray
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
