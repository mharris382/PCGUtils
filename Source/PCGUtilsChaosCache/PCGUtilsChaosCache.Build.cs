// Copyright Max Harris

using UnrealBuildTool;

/**
 * Everything in PCGUtils that reads a Chaos Cache, isolated so the ChaosCaching dependency can be dropped by
 * deleting this module and its entry in PCGUtils.uplugin. Nothing else in the plugin depends on it.
 * (PCGUtilsSimulation also uses ChaosCaching independently; see Docs/PCGUtilsChaosCache.md.)
 */
public class PCGUtilsChaosCache : ModuleRules
{
    public PCGUtilsChaosCache(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

        PublicDependencyModuleNames.AddRange(
            new string[]
            {
                "Core",
                "CoreUObject",
                "Engine",

                // UChaosCacheCollection / UChaosCache. An Experimental engine plugin, declared in
                // PCGUtils.uplugin. Only its exported UChaosCache API and public UPROPERTY fields are used: the
                // track and event types in its headers are not exported and do not link from here.
                "ChaosCaching",

                // UGeometryCollection / UGeometryCollectionComponent - the cache's recorded component template
                // and the rest collection that supplies the geometry.
                "GeometryCollectionEngine",

                "PCG",
                "PCGUtilsCore",

                // GC data, the publisher and the bone-transform library. The direction is the allowed one:
                // PCGUtilsFracture must never depend on this module.
                "PCGUtilsFracture",
            }
        );

        // FGeometryCollection and the managed-array collection live in Chaos.
        SetupModulePhysicsSupport(Target);

        // The automation tests (editor-only) build their fixture solid with PCGUtilsFracture's test helpers, which
        // call FDynamicMesh3 directly - a symbol only a direct dependency links.
        if (Target.bBuildEditor)
        {
            PrivateDependencyModuleNames.Add("GeometryCore");
        }
    }
}
