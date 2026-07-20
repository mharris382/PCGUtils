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
                "GeometryFramework",
                "GeometryScriptingCore",
                "PCG"
            }
        );
    }
}
