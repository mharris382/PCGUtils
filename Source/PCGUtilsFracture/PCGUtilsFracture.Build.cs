// Copyright Max Harris

using UnrealBuildTool;

public class PCGUtilsFracture : ModuleRules
{
    public PCGUtilsFracture(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

        PublicDependencyModuleNames.AddRange(
            new string[]
            {
                "Core",
                "CoreUObject",
                "Engine",
                "DeveloperSettings",

                "DynamicMesh",
                "GeometryCore",
                "GeometryFramework",

                // FGeometryCollectionToDynamicMeshes - the DynMesh <-> GC converter. An engine Runtime module,
                // not a plugin.
                "MeshConversionEngineTypes",

                // FDataflowTransformSelection, the native bone-selection currency every FractureEngine entry
                // point takes. Also an engine Runtime module, not the Dataflow editor plugin.
                "DataflowCore",

                // Epic's fracture backend. FractureEngine lives in the "Fracture" plugin, PlanarCut in
                // "PlanarCutPlugin"; both are declared in PCGUtils.uplugin. We deliberately do NOT depend on
                // FractureEditor (editor tool mode) or GeometryCollectionNodes (Dataflow node wrappers).
                "FractureEngine",
                "PlanarCut",

                "PCG",
                "PCGGeometryScriptInterop",
                "PCGUtils",
                "PCGUtilsDynMesh"
            }
        );

        // FGeometryCollection, FManagedArrayCollection, GeometryCollectionAlgo and the collection facades all
        // live in the Chaos module. Matches how FractureEngine itself pulls Chaos in.
        SetupModulePhysicsSupport(Target);
    }
}
