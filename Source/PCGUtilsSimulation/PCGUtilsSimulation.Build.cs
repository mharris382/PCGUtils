// Copyright Max Harris

using UnrealBuildTool;

public class PCGUtilsSimulation : ModuleRules
{
    public PCGUtilsSimulation(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

        PublicDependencyModuleNames.AddRange(
            new string[]
            {
                "Core",
                "CoreUObject",
                "Engine",

                // UChaosCache / UChaosCacheCollection / AChaosCacheManager / FComponentCacheAdapter.
                // An Experimental engine plugin, declared in PCGUtils.uplugin and enabled in the
                // .uproject - it is NOT EnabledByDefault.
                "ChaosCaching",

                // GC | Spawn Component. The settings derive from UPCGUtilsFractureElementBaseSettings (palette
                // bucket and GC domain colour) and read the AssetPath attribute GC | Save Asset writes. The
                // direction is the allowed one: PCGUtilsFracture must never depend on this module.
                "PCG",
                "PCGUtilsFracture",

                // UGeometryCollectionComponent / UGeometryCollection, referenced by the spawn settings' template.
                "GeometryCollectionEngine",
            }
        );

        PrivateDependencyModuleNames.AddRange(
            new string[]
            {
                // FBodyInstance, UBodySetup.
                "PhysicsCore",

                // AChaosSolverActor - the per-capture solver, the free floor (bHasFloor), and the Solver pin
                // of GC | Spawn Component.
                "ChaosSolverEngine",

                // AFieldSystemActor / UFieldSystemComponent - the Initialization Fields of GC | Spawn Component.
                "FieldSystemEngine",
            }
        );

        // FScopedTransaction, so GC | Spawn Component's spawns undo as one step in the editor. Editor-only, the
        // same way PCG's own Add Component pulls it in.
        if (Target.bBuildEditor)
        {
            PrivateDependencyModuleNames.Add("UnrealEd");
        }

        // Chaos itself: FPBDRigidParticleHandle, FSingleParticlePhysicsProxy, the solver types the
        // cache adapter touches from the physics thread. Matches how PCGUtilsFracture pulls Chaos in.
        SetupModulePhysicsSupport(Target);

        // The Phase 0 cache spike (Chaos/, Components/, Spike/) still has no PCG dependency and must stay
        // runnable without a graph. The PCG dependency above exists for GC | Spawn Component, which is
        // independent of the recording pipeline; see claude.md.
    }
}
