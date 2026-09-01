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
            }
        );

        PrivateDependencyModuleNames.AddRange(
            new string[]
            {
                // FBodyInstance, UBodySetup.
                "PhysicsCore",

                // AChaosSolverActor - the per-capture solver, and the free floor (bHasFloor).
                "ChaosSolverEngine",
            }
        );

        // Chaos itself: FPBDRigidParticleHandle, FSingleParticlePhysicsProxy, the solver types the
        // cache adapter touches from the physics thread. Matches how PCGUtilsFracture pulls Chaos in.
        SetupModulePhysicsSupport(Target);

        // PHASE 0 NOTE: this module deliberately has NO dependency on PCG or any other PCGUtils
        // module. The Phase 0 spike answers engine questions only; nothing here may need a PCG
        // graph to run. The PCG-facing data types and elements arrive in Phase 1.
    }
}
