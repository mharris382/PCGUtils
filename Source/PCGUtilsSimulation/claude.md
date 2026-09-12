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

**The Phase 0 spike (`Chaos/`, `Components/`, `Spike/`) has no PCG dependency**, deliberately: it
answers engine questions and must not need a graph to run. The module itself now depends on `PCG`,
`PCGUtilsFracture`, `GeometryCollectionEngine` and `FieldSystemEngine`, but only for
`GC | Spawn Component` (below), which is independent of the recording pipeline. Keep the spike free of
those includes.

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

## `GC | Spawn Component`

The module's first PCG element, and the first thing in PCGUtils that spawns components. Design and
engine citations: `PCGSpawnGeometryCollectionComponent_Investigation.md` at the repo root. It turns
Geometry Collection asset references (typically `GC | Save Asset`'s `AssetPath`) into PCG-managed
`UGeometryCollectionComponent`s and wires Initialization Fields and a Chaos solver into them from pins.

It is a *live* start state for PIE/Simulate, not a recording participant, so none of the cache rules
above apply to it. Keep it that way; if it is ever meant to be observed by a cache manager, its
components first need deterministic names (see the investigation, §6).

Rules that are load-bearing:

- **Everything the physics proxy reads is set before `RegisterComponent()`** - rest collection,
  `InitializationFields`, `ChaosSolverActor`, overrides, attachment and world transform. In a game world
  registration creates the proxy and reads the fields exactly once. `ConfigureComponent` owns this and
  `check`s the component is unregistered.
- **Lifetime is PCG's.** One `UPCGManagedComponentList` per execution, components tagged like Add
  Component's. No reuse: a GC component carries simulation state.
- **The template is copied, never used as an archetype.** An archetype inside a graph asset would make
  every generated level component depend on that package.
- **Fields and solver are pins because a graph asset cannot reference level actors.** Do not add
  actor-reference settings for them.
- It derives from `UPCGUtilsFractureElementBaseSettings` for the palette bucket and GC domain colour,
  and is in the `GC | ` family, so `PCGUtils.Palette.SearchContract` covers this package.

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
