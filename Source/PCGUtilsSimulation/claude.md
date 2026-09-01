# PCGUtilsSimulation Development Conventions

Module-specific conventions for `PCGUtilsSimulation`. Repository-wide rules are in `AGENTS.md`.

The design rationale, with engine source citations, is `PCGUtilsSimulation_Investigation.md` at the
repo root. Read it before changing anything in this module — most of what looks arbitrary here is
load-bearing, and the investigation says why.

**Current state: Phase 0 (de-risking spike).** See `Docs/PCGUtilsSimulation_Phase0.md` for the
runbook and the four questions it answers.

---

## What this module is for

Offline Chaos simulation as composable PCG data: PCG authors a simulation's starting state, the user
records it once through Unreal's normal editor simulation, and PCG reads the cached result back at
arbitrary times without ever re-simulating.

Three stages that must stay separate:

```
authoring (PCG graph)  ->  execution (editor/PIE recording)  ->  readback (PCG graph)
```

The PCG graph never waits for physics and never enters or exits PIE.

---

## Dependency direction

```
PCGUtilsEditor -> PCGUtilsSimulation -> PCGUtilsFracture -> PCGUtilsDynMesh -> PCGUtils
```

`PCGUtilsFracture` must never depend on this module, mirroring the existing DynMesh/Fracture rule.

Engine dependencies: `ChaosCaching` (an Experimental plugin, `EnabledByDefault: false` — it is
declared in `PCGUtils.uplugin` and enabled in the `.uproject`), `ChaosSolverEngine`, `PhysicsCore`,
and Chaos itself via `SetupModulePhysicsSupport`.

**Phase 0 has no PCG dependency at all**, deliberately: the spike answers engine questions and must
not need a graph to run. That changes in Phase 1.

---

## The one thing this module exists to control: `ParticleIndex`

`UChaosCache::FlushPendingFrames` grows `TrackToParticle` from whatever integer the adapter puts in
`FPendingParticleWrite::ParticleIndex`:

```cpp
if (!TrackToParticle.Find(ParticleIndex, TrackIndex)) { TrackToParticle.Add(ParticleIndex); ... }
```

An adapter that writes `ParticleIndex = ordinal within the participant` gets that ordinal back out of
`FCacheEvaluationResult::ParticleIndices` at readback, unchanged. That is the only link in the
identity chain the engine supplies; everything from the ordinal back to a PCG point is ours to
persist.

This is why `FStaticMeshCacheAdapter` cannot be used: it hard-codes `ParticleIndex = 0`, one particle
per component. `UPCGSimulationBodiesComponent` + `FPCGSimulationCacheAdapter` exist to get that
integer under our control, and they are not throwaway spike code.

---

## Rules

**Never change `FPCGSimulationCacheAdapter::GetGuid()`.** It is written into every `UChaosCache` this
adapter records and is how a cache is matched back to its adapter. Changing it silently invalidates
every recording ever made.

**Readback goes through `FCacheEvaluationResult::ParticleIndices`, never array position.** The result
arrays are compacted — `Evaluate` skips tracks that have not begun (`BeginOffset`) or have
deactivated (`bDeactivateOnEnd`), so `Transform[i]` is not body `i`. Code that assumes otherwise works
on a dense test case and breaks on real data.

**Always take a `BeginPlayback()` token before `Evaluate()`.** `UChaosCache::Evaluate` early-returns
an *empty* result with only a `Warning` when `CurrentPlaybackCount == 0`. The failure looks like a
missing recording.

**Always pass an explicit deterministic cache name to `FindOrAddObservedComponent`.**
`AChaosCacheManager::AddNewObservedComponent` otherwise assigns
`MakeUniqueObjectName(...)`, so names drift on every rebuild and orphan every prior recording.

**A fresh `FPlaybackTickRecord` per random-access evaluation.** Reusing one carries `LastTime` forward
and makes event evaluation incremental — correct for playback, wrong for random access. Cumulative
"everything up to T" event queries use `SetLastTime(0)` + `SetDt(T)`, the way
`FGeometryCollectionCacheAdapter::GatherAllBreaksUpToTime` does.

**Components observed by the cache manager need deterministic names.** `FObservedComponent` resolves
through `FSoftComponentReference` with `PathToComponent = Comp->GetPathName(Owner)`, which for a
runtime component owned by the manager is just the component's name.

**Build the runtime representation before `Super::BeginPlay()`.** `AChaosCacheManager::BeginPlay`
calls `Start()` -> `BeginEvaluate()`, which resolves observed components and opens their caches.
Components created after that are invisible to the recording.

---

## Naming

`Simulation`, spelled out, in both C++ and user-facing text. Unlike the `GC` case there is no
length problem — `Build Simulation State` and `Sample Simulation` read fine — and `Sim` is ambiguous.

Engine names stay unchanged: `UChaosCache`, `UChaosCacheCollection`, `AChaosCacheManager`,
`FComponentCacheAdapter`.

---

## Not yet decided

Phase 1 onward. Do not implement ahead of the investigation without re-reading it:

- `UPCGSimulationPointArrayData : UPCGPointArrayData` **is rejected** — `CopyInternal` hard-codes
  `NewObject_AnyThread<UPCGPointArrayData>`, so a subclass is sliced by the first copy. Composition
  instead; see investigation §4.1–4.2.
- GC participants need a baked `UGeometryCollection` asset — `UPCGGeometryCollectionData` is
  non-serializable by design. This module is the first in the plugin allowed to create assets,
  actors and components; `PCGUtilsFracture`'s "nothing here creates an asset" rule stays intact
  precisely because that work lives here instead.
