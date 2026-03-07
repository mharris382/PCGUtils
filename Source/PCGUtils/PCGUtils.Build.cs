// Copyright Max Harris

using UnrealBuildTool;

public class PCGUtils : ModuleRules
{
    public PCGUtils(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

        PublicDependencyModuleNames.AddRange(
            new string[]
            {
                "Core",
                "CoreUObject",
                "Engine",
                "GeometryScriptingCore",
                "PCG",
                "PCGGeometryScriptInterop",
                "RHI",
                "RenderCore",
                "ModelingOperators",
                
            }
        );

        PrivateDependencyModuleNames.AddRange(
            new string[]
            {
                "GeometryCore",
                "GeometryFramework",
                "ModelingComponents",
            }
        );

        if (Target.bBuildEditor == true)
        {
            PrivateDependencyModuleNames.AddRange(
                new string[]
                {
                    "AdvancedPreviewScene",
                    "UnrealEd",
                    "PCGEditor"
                }
            );
        }
    }
}
