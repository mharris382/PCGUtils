# PCGUtilsChaosCache Development Conventions

Module-specific conventions for `PCGUtilsChaosCache`. Repository-wide rules are in `AGENTS.md`; GC data rules
are in `Source/PCGUtilsFracture/claude.md`. The investigation, strategy and engine citations are
`Docs/PCGUtilsChaosCache.md` - read it before changing the sampling.

## What this module is for

Reading Chaos Cache recordings into PCG. Currently one node, `GC | Load Chaos Cache`: sample a recorded
Geometry Collection simulation at one time and emit the posed rest collection as GC data.

## Why it is a separate module

It is the only PCGUtils code outside `PCGUtilsSimulation` that depends on the Experimental `ChaosCaching`
plugin, and nothing depends on it. Deleting the folder and its `PCGUtils.uplugin` entry removes it cleanly.
Keep it that way: `PCGUtilsFracture` must never depend on this module, and new Chaos Cache readers belong here.

```
PCGUtilsChaosCache -> PCGUtilsFracture -> PCGUtilsDynMesh -> PCGUtils
```

## Rules

- **Only UChaosCache's exported API and public UPROPERTY fields.** `FParticleTransformTrack`'s methods,
  `FCacheEventTrack`/`FCacheEventHandle`, and the GC event structs (`FEnableStateEvent`, ...) are not exported
  and do not link from here. Read track fields directly; evaluate through `UChaosCache::Evaluate` /
  `EvaluateTransform`. Do not construct `FPendingFrameWrite` either - its event-track map's destructor is
  not exported (which is why the tests write tracks into the public fields).
- **Bone index = particle index.** The GC adapter records `ParticleIndex = TransformIndex`. Apply the cache to
  the rest collection *before* publishing, while indices still match, and validate every `TrackToParticle`
  entry against the bone count first.
- **Read results through `ParticleIndices`.** `Evaluate` skips tracks that have not begun or have deactivated.
- **Always hold a `BeginPlayback` token around `Evaluate`**, with a fresh `FPlaybackTickRecord` per sample and
  `bEvaluateChannels` set explicitly (the context constructor leaves it uninitialised).
- **Do not copy `FGeometryCollectionCacheAdapter::SetRestState`.** It writes component-space transforms into
  parent-relative slots and zeroes released clusters for the renderer. Write collection-space transforms
  through `PCGUtilsGeometryCollectionTransforms::SetBoneGlobalTransforms` (`Independent`); released clusters
  hold their release pose.
- The element derives from `UPCGUtilsFractureElementBaseSettings` and lives under `Elements/Conversion/`, so it
  lands in `PCGUtils|GC|Conversion`. `PCGUtils.Palette.SearchContract` covers `/Script/PCGUtilsChaosCache`.

## Tests

`PCGUtils.ChaosCache.*`. The fixture reuses `PCGUtilsFracture`'s header-only test helpers by relative include.
