# PCGUtilsSimulation — Phase 0 spike runbook

Phase 0 of the plan in `PCGUtilsSimulation_Investigation.md` (repo root). It answers four engine
questions **before** any PCG code is written, and it is a genuine go/no-go: if question (a) or (c)
fails, the manual-recording premise needs rethinking before Phase 1 starts.

Nothing in this spike touches PCG. `PCGUtilsSimulation.Build.cs` deliberately has no PCG dependency.

---

## The four questions

| | Question | Probe |
|---|---|---|
| **(a)** | Does recording work in **Simulate-In-Editor**, not just Play-In-Editor? | Step 5, then Probe 1 |
| **(b)** | Is the `UChaosCacheCollection` asset dirty and savable after exiting? | `EndPlay` log + Probe 1 |
| **(c)** | Can the cache be evaluated at **arbitrary times from a cold editor session** — no PIE, no component, no solver? | **Probe 2** |
| **(d)** | Risk R6: does `AChaosCacheManager` mishandle a component it cannot treat as single-bodied? | `BeginPlay` log |

(c) is the load-bearing one. Everything downstream — sample-at-time, path extraction, all readback —
is built on it.

---

## What ships in this phase

| File | Keeper? |
|---|---|
| `Components/PCGSimulationBodiesComponent.h/.cpp` | **Yes** — V1 item |
| `Chaos/PCGSimulationCacheAdapter.h/.cpp` | **Yes** — V1 item |
| `PCGUtilsSimulationModule.h/.cpp` | **Yes** |
| `Spike/PCGSimulationSpikeActor.h/.cpp` | **No** — throwaway harness, delete after Phase 1 |

The component and adapter are not throwaway on purpose. The investigation (§8) rejected the
"one `UStaticMeshComponent` + one cache per point" shortcut precisely because
`FStaticMeshCacheAdapter` hard-codes `ParticleIndex = 0`, which makes the whole identity design
impossible. Controlling `ParticleIndex` is the thing being proven here, so the code that does it is
built once, now.

---

## Setup

1. **Regenerate project files and build.** `ChaosCaching` is `EnabledByDefault: false` in the
   engine, so it has been added to both `PCGUtils.uplugin` and `UtilsDevProject.uproject`. It pulls
   in the `Takes` plugin as a dependency.

2. **Create a cache collection.** Content Browser → right-click → Physics →
   **Chaos Cache Collection**. Name it e.g. `CC_Phase0Spike`.

3. **Place the spike actor.** Place Actors → search `PCG Simulation Spike Actor`. Put it somewhere
   with clear space; it builds its own floor.

4. **Configure it:**

   | Property | Value |
   |---|---|
   | `Cache Collection` | the asset from step 2 |
   | `Cache Mode` | `Record` |
   | `Body Mesh` | any static mesh **with simple collision** — `/Engine/BasicShapes/Cube` works |
   | `Grid Counts` | `5, 5, 4` → 100 bodies |
   | `Participant Cache Name` | leave as `PCGSim_Spike_Participant0` |

   A mesh with no simple collision makes every body skip with a warning naming the mesh — that is
   deliberate, because the alternative is silently falling through the floor.

---

## Running it

### Step 5 — record in **Simulate**, not Play

Toolbar play-mode dropdown → **Simulate**. Let it run ~5 seconds, then **Stop**.

Use Simulate rather than Play deliberately: it is question (a), and nothing in `AChaosCacheManager`
or the adapters gates on `IsGameWorld()` / `EWorldType`, so it *should* work. If it does, the whole
feature avoids ever needing a game mode or player controller.

Watch `LogPCGUtilsSimulation` in the Output Log. On `BeginPlay` you should see the body count and
the **(d) R6 probe** line — both values are expected `false`. On `EndPlay`:

```
EndPlay - collection 'CC_Phase0Spike' dirty=YES. Cache 'PCGSim_Spike_Participant0' found duration=5.0 frames=... tracks=100
```

`dirty=NO` or `MISSING` is a failure and the log says which question failed.

### Step 6 — save

The collection asset should be marked dirty. **Save it** (Ctrl+S / Save All). This is (b): the
collection is a content asset in its own package, so it is *not* PIE-duplicated — the recording made
inside PIE lands in the real asset. That is the entire mechanism the offline workflow depends on.

### Step 7 — **restart the editor**

Do not skip this. Question (c) is specifically about a *cold* session. Reopening the level without
ever entering PIE is what proves the cache is readable as pure data.

### Step 8 — run the probes

Select the spike actor. In the details panel, `Spike | Probes`:

**1. Report Cache Contents** — inventory. Expect `ParticleTracks: 100`, `TrackToParticle: 100
entries`, and `per-particle curves: 6` (the velocity channels the adapter records, which no engine
adapter does). Transform key counts show whether `EndRecord`'s compression collapsed settled bodies —
`min` well below `max` means it did.

**2. Evaluate Over Time** — the critical probe. Prints `world type` (expect `2` = Editor, which is
how you know (c) is being tested properly), then samples the cache at N times and prints transform
and speed for the ordinals in `Ordinals To Print`.

**Pass condition: Z decreases over successive sample times and then settles.** That means
`BeginPlayback` + `SetLastTime(T)` + `Evaluate` returned real interpolated transforms with no PIE, no
component and no solver anywhere in the picture.

**3. Verify Ordinal Identity** — checks `TrackToParticle` is a clean permutation of the authored body
ordinals: no duplicates, nothing out of range. This is the half of the identity chain Chaos gives us
(investigation §4.4); the other half — ordinal back to a PCG point — is Phase 1's job.

Some `never recorded` entries are expected only if bodies were skipped for missing collision.
Anything else wants investigating.

---

## Things the probes are built to not get wrong

Three traps found during the investigation, each of which would produce plausible-looking wrong
answers:

- **`Evaluate` needs a playback token.** `UChaosCache::Evaluate` early-returns an *empty* result with
  only a `Warning` if `CurrentPlaybackCount == 0`. Probe 2 takes a `BeginPlayback()` token and fails
  loudly if it cannot. Without it the probe would report "nothing" and look like a recording failure.

- **Result arrays are compacted.** `FCacheEvaluationResult::Transform[i]` is *not* body `i` — tracks
  that have not begun or have deactivated are skipped. `ParticleIndices` is the authoritative
  mapping, so Probe 2 does `ParticleIndices.IndexOfByKey(ordinal)` rather than indexing directly.
  Getting this wrong is the single easiest way to build a readback that works on the spike and breaks
  on real data.

- **A fresh `FPlaybackTickRecord` per sample.** Reusing one carries `LastTime` forward and makes
  event evaluation incremental — correct for playback, wrong for random access.

And one found while writing the harness:

- **`FindOrAddObservedComponent` silently early-returns for `RF_Transient` components** — *"we do
  not want to record transient component as they may not exists when we start recording"*. A plain
  `NewObject` does not set that flag, but several component-creation helpers do, and the failure mode
  is an empty cache with nothing logged anywhere. `BuildRuntimeRepresentation` checks the observed
  list afterwards and logs an error naming the flag.

---

## What Phase 0 deliberately does not test

- **`AChaosSolverActor`.** The floor here is a plain `UBoxComponent`, not the free
  `bHasFloor`/`FloorHeight` particle from investigation §4.6. That floor belongs to a solver actor's
  own solver and these bodies go into the world solver; routing them to a private solver is Phase 1
  work. None of the four questions depend on which floor is used.
- **Geometry Collections.** Phase 2.
- **Determinism (R1).** Two recordings of an unchanged state are *not* expected to match unless the
  project enables `bTickPhysicsAsync` + `AsyncFixedTimeStepSize`. Do not read a difference between
  two records as a spike failure.
- **Anything PCG.** By construction.

---

## Recording the outcome

When the probes have run, write the results into
`PCGUtilsSimulation_Investigation.md` §11 Phase 0 as a short pass/fail table. If (a) or (c) fails,
stop and re-plan rather than starting Phase 1.
