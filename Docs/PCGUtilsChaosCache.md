# PCGUtilsChaosCache — `GC | Load Chaos Cache`

Status: implemented. This document is the investigation, the implementation strategy, and the self-review
that revised it before any code was written.

## Goal

Record a Geometry Collection simulation **outside PCG** with Unreal's Chaos Cache (a Chaos Cache Manager in
Record mode, run in PIE/Simulate), then pull that recording into a PCG graph and sample it at a chosen time to
get the collection's bone state at that moment — as ordinary GC data that every `PCGUtilsFracture` node
accepts.

The node has no input pins and one `GC` output pin.

---

## 1. How ChaosCaching stores a Geometry Collection recording

Engine sources: `Engine/Plugins/Experimental/ChaosCaching/Source/ChaosCaching` (UE 5.8).

### Assets

- **`UChaosCacheCollection`** is the asset a user creates and assigns to a Cache Manager. It holds
  `TArray<UChaosCache*> Caches` as instanced subobjects, **one per observed component**, keyed by
  `FObservedComponent::CacheName` (auto-named `Cache_N` when the user leaves it blank).
- **`UChaosCache`** is one component's recording. Relevant public (UPROPERTY) state:
  - `ParticleTracks : TArray<FPerParticleCacheData>` — one transform track per recorded particle.
  - `TrackToParticle : TArray<int32>` — track index → the adapter's particle index. For Geometry
    Collections the particle index **is the transform (bone) index** of the rest collection
    (`FGeometryCollectionCacheAdapter::Record_PostSolve`: `Pending.ParticleIndex = TransformIndex`).
  - `RecordedDuration`, `NumRecordedFrames`, `InterpolationMode`.
  - A private `FCacheSpawnableTemplate Spawnable`, readable through the exported `GetSpawnableTemplate()`:
    `DuplicatedTemplate` is a `StaticDuplicateObject` copy of the recorded component (so for a GC cache, a
    `UGeometryCollectionComponent` whose `RestCollection` references the GC asset), `ComponentTransform` is
    the component's transform **relative to the Cache Manager**, `InitialTransform` its world transform.
    `BeginRecord` builds this, so a never-recorded cache has no template.
  - The cache stores **no geometry**. Geometry must come from the rest collection.

### What a track contains

`FParticleTransformTrack`: raw position/rotation keys (scale keys are always written as 1), `KeyTimestamps`,
`BeginOffset` (time of the first key) and `bDeactivateOnEnd`.

The GC adapter records, every physics step, every transform whose particle is **active** — or disabled but
held by an active *internal* cluster — as

```
Recorded = MassToLocal[b]^-1 * (R, X)particle * WorldToCacheManager
```

i.e. the **bone's** world transform (centre-of-mass offset removed; caches older than Version 1 are fixed up
in `UChaosCache::PostLoad`), expressed in **Cache Manager actor space**.

Children of an intact cluster are disabled and are *not* recorded; they ride along rigidly with their
parent. When a cluster releases, the adapter writes one last key for it with `bPendingDeactivate`, which sets
the track's `bDeactivateOnEnd`; the released children start their own tracks at that time. A particle that
stops being active for any other reason (sleep removal, kill field) simply stops getting keys.

### Time

Key times are seconds since the Cache Manager triggered (plus `RestartTimeStart` when re-simulating from a
restart time). `RecordedDuration` is `lastKey - firstKey` across all tracks.

### How the engine reads it back at an arbitrary time

`FGeometryCollectionCacheAdapter::SetRestState` (what scrubbing the Cache Manager's *Start Time* uses):

1. `BeginPlayback()` token (evaluation returns empty with only a log warning without one).
2. `UChaosCache::Evaluate` with a fresh `FPlaybackTickRecord` at `T`, `MassToLocal = nullptr`. Evaluate skips
   tracks that have not begun (`BeginOffset > T`) and deactivated tracks past their end, so result arrays are
   **compacted** and must be read through `ParticleIndices`, never by position. Keys are interpolated with
   the collection's interpolation mode and clamped to the track's own key range.
3. `Transform[b] = Evaluated * CacheManagerToComponent` — written straight into the bone's **local** slot.
4. Every cluster released before `T` (from `GC_Enable` events) is set to **identity**, so that the
   component-space transforms written into the children's local slots in step 3 come out right.

Step 3–4 is a rendering shortcut: it produces a hierarchy whose `ComputeGlobalTransforms` is only correct
because broken ancestors were zeroed. It is not a valid collection state for anything that reads the
hierarchy.

### Linking constraints (important)

ChaosCaching is built as its own module, and much of its public header surface is **not exported**:
`FParticleTransformTrack`'s methods, `FCacheEventTrack` / `FCacheEventHandle::GetConst` (`IsAlive` is
out-of-line), and the GC event structs `FEnableStateEvent` / `FBreakingEvent` (`StaticStruct()`,
`EventName`). Anything in another module that calls them fails to link. The exported surface is
`UChaosCache` (`Evaluate`, `EvaluateTransform`, `BeginPlayback`/`EndPlayback`, `GetDuration`,
`GetSpawnableTemplate`, `BeginRecord`/`EndRecord`/`AddFrame_Concurrent`) and `UChaosCacheCollection`.
Reading public UPROPERTY *fields* (`ParticleTracks[i].TransformData.KeyTimestamps`, `bDeactivateOnEnd`,
`TrackToParticle`) is fine — that is plain member access, no symbol import.

---

## 2. Strategy (as first drafted)

1. New module `PCGUtilsChaosCache` (Runtime) depending on `PCGUtilsFracture`, `ChaosCaching`,
   `GeometryCollectionEngine`, `PCG`. Nothing else in the plugin depends on it.
2. `UPCGLoadChaosCacheAssetSettings : UPCGUtilsFractureElementBaseSettings`, in
   `Elements/Conversion/` so the shared category mapper files it under `PCGUtils|GC|Conversion`. Palette
   title `GC | Load Chaos Cache`.
3. Settings: `CacheCollection` (soft ptr), `SampleTime` (float), `bNormalizedTime` (bool).
4. Execute: load asset, take each cache's rest collection from the spawnable template, call
   `UChaosCache::Evaluate`, reproduce `SetRestState` into a copy of the rest collection, publish it as a new
   lineage like `GC | From Asset`.
5. Diagnostics: null asset → error; unrecorded cache → warning; time outside the range → warning (clamped).

---

## 3. Self-evaluation, and what changed

**(a) Reproducing `SetRestState` literally would publish a broken hierarchy.** Writing component-space
transforms into local slots and zeroing broken clusters only works for the renderer. Every consumer here
resolves bones through `ComputeGlobalTransforms`, and `PCGUtilsFracture/claude.md` forbids writing
`Collection.Transform[]` directly. **Revised:** evaluate the *global* (collection-space) transform of every
recorded bone and apply them through `PCGUtilsGeometryCollectionTransforms::SetBoneGlobalTransforms` with
`EPCGGeometryCollectionNestedBoneHandling::Independent` — nearest-the-root first, each bone reaching exactly
its recorded transform, unrecorded descendants riding along with their parent's stored local transform
(which is exactly what Chaos does for an intact cluster).

**(b) Released clusters need a transform, and identity is wrong for us.** The engine zeroes them because they
stop rendering. In GC data a cluster bone still exists (`Bones To Points` emits it). **Revised:** a released
cluster holds its **release pose** — its track's final key, evaluated through the exported
`UChaosCache::EvaluateTransform`. Its released children are set independently in (a), so the choice cannot
leak into them.

**(c) Release detection cannot use the `GC_Enable` events.** Those types are not exported (§1). **Revised:**
a track with `bDeactivateOnEnd` whose last key time is before `T` is a released cluster — the same test
`UChaosCache::Evaluate` uses to skip it, read from public fields. The adapter writes that deactivation key for
exactly the clusters it emits `GC_Enable` events for, so the two are equivalent.

**(d) The time range must match what "normalized 1.0" should mean.** `RecordedDuration` is
`lastKey - firstKey`; the first key sits one physics step after 0, so treating `Duration` as the absolute
end would leave `1.0` just short of the final recorded state. **Revised:** the valid absolute range is
`[0, EndTime]` where `EndTime` is the latest key across all tracks; normalized time maps `[0,1]` onto it.
Before a track's first key the engine leaves the bone at rest, and so do we.

**(e) Index safety.** Transform indices must still match the rest collection when the cache transforms are
applied. Publishing compacts hidden geometry (Geometry group only, bone indices unaffected), but the
transforms are applied **before** publishing anyway. Every `TrackToParticle` entry is validated against the
bone count first — the engine's own `ValidForPlayback` check — and a violation is an error naming the track,
particle index and bone count.

**(f) A cache collection can hold several caches, and not all are GC.** A Cache Manager can observe several
components, including static meshes. **Added:** `CacheName` (empty = every GC cache in the collection, one
output each, each tagged with its cache name). A cache whose template is not a `UGeometryCollectionComponent`
is skipped with a warning, or an error when it was named explicitly.

**(g) Geometry source.** The template's rest collection is right in the normal case, but a template can be
missing (never recorded) or reference a transient collection (e.g. a component spawned at runtime).
**Added:** optional `RestCollectionOverride`. A never-recorded cache with an override still emits the rest
state (plus the "not recorded" warning); without one it emits nothing.

**(g2) Placement.** The recording knows three frames. **Added:** `Space` — `Component` (default: the GC
asset's own local space, the same frame `GC | From Asset` produces, so a sampled state overlays an
unsimulated import), `Cache Manager` (relative to the actor that recorded it), `Recorded World` (world
transform at record time). Non-component spaces are applied with `PlaceCollection`, i.e. onto the root bones.

**(h) Scaled components.** Chaos simulates a scaled GC with scaled particle geometry but records unscaled
rigid transforms, and the engine needs a MassToLocal conjugation to replay it. Collection geometry is
bone-local and unscaled, so there is no exact representation. **Decision:** the rigid part of the
component transform is used and a warning names the scale; recording with unit-scale components is the
documented workflow.

**(i) Recording in progress.** `BeginPlayback` refuses while a cache is open for record. That is an error
for that cache, not an empty result.

**(j) Isolation claim.** The module can be deleted without touching anything else, but the plugin's
`ChaosCaching` dependency is **also** used by `PCGUtilsSimulation` (cache adapter + spike). Dropping ChaosCaching
from the plugin means removing both modules and the `ChaosCaching` entry in `PCGUtils.uplugin`. The palette
contract test reaches classes by package name through `TObjectIterator`, so it needs no change when the
module is removed.

**(k) Parity with `GC | From Asset`.** Same async-load pattern, dynamic tracking of the loaded assets (so
re-recording the cache refreshes the graph), `bExtractMaterials`, `bKeepHiddenGeometry`,
`ValidateFractureRequirements` on the rest collection, publish as a new lineage.

---

## 4. Final design

### Module

`Source/PCGUtilsChaosCache`, `Runtime`, `PreDefault`. Depends on `PCGUtilsFracture` (and through it
`PCGUtilsCore`), `PCG`, `ChaosCaching`, `GeometryCollectionEngine`, Chaos via `SetupModulePhysicsSupport`.

Removing it: delete the folder and its entry in `PCGUtils.uplugin`'s `Modules`.

### Settings — `GC | Load Chaos Cache`

| Setting | Type | Default | Meaning |
|---|---|---|---|
| Cache Collection | `TSoftObjectPtr<UChaosCacheCollection>` | — | Required. Null is an error. |
| Sample Time | float | 0 | Seconds, or a fraction of the recording when normalized. |
| Normalized Time | bool | false | Interpret Sample Time as `[0,1]` of `[0, EndTime]`. |
| Cache Name | FName | None | One cache by name; None = every GC cache. |
| Space | enum | Component | Component / Cache Manager / Recorded World. |
| Rest Collection Override | `TSoftObjectPtr<UGeometryCollection>` | — | Advanced. Geometry source instead of the template's. |
| Extract Materials | bool | true | As `GC | From Asset`. |
| Keep Hidden Geometry | bool | false | Advanced. As `GC | From Asset`. |
| Synchronous Load | bool | false | Debug. |

Output: `GC`, one data per sampled cache, tagged with the cache name.

### Execution per cache

1. Resolve the rest collection (override, else template's `RestCollection`). Missing → skip (warning if the
   cache was never recorded, error otherwise).
2. Copy it (`CopyTo`), `ValidateFractureRequirements`.
3. If the cache has no tracks → warn "not recorded", publish the rest state.
4. Validate `TrackToParticle` against the bone count.
5. Resolve `T` (normalize, range check → warning + clamp).
6. `BeginPlayback`; `Evaluate` (transforms only, fresh tick record at `T`, no MassToLocal); `EndPlayback`.
7. Released clusters (b, c) → release pose via `EvaluateTransform`.
8. `CollectionGlobal = CacheManagerSpace * ComponentTransform(rigid)^-1`; `SetBoneGlobalTransforms(Independent)`.
9. Optional `PlaceCollection` for the chosen space; `PublishNewLineage`; tag with cache name.

### Tests (`PCGUtils.ChaosCache.*`)

A real `UChaosCache` recorded through the exported `BeginRecord` / `AddFrame_Concurrent` / `EndRecord` API
against a transient GC component, under a non-identity Cache Manager transform, with a root cluster that
releases mid-recording:

- sampling before the release moves the pieces rigidly with the root;
- sampling after it puts each piece at its own recorded transform and holds the root at its release pose;
- normalized and absolute time agree; out-of-range time warns and clamps;
- an unrecorded cache warns; a null asset errors; contract (title, no inputs, one `GC` output).
