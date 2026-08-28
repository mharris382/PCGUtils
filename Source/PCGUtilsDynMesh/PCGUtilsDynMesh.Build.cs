// Copyright Max Harris

using UnrealBuildTool;

public class PCGUtilsDynMesh : ModuleRules
{
    public PCGUtilsDynMesh(ReadOnlyTargetRules Target) : base(Target)
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
                "GeometryFramework",
                "GeometryCore",
                "GeometryScriptingCore",
                "PCG",
                "PCGGeometryScriptInterop"
            }
        );

        // Write DynMesh LODs authors Static Mesh assets: it needs the asset registry to announce a newly
        // created asset. The write itself goes through GeometryScriptingCore, which is already public above
        // and guards its own editor-only implementation.
        if (Target.bBuildEditor)
        {
            PrivateDependencyModuleNames.Add("AssetRegistry");
        }
    }
}
