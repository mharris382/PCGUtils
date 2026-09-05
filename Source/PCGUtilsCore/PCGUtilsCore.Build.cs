// Copyright Max Harris
using UnrealBuildTool;

public class PCGUtilsCore : ModuleRules
{
    public PCGUtilsCore(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
        PublicDependencyModuleNames.AddRange(new string[] { "Core", "CoreUObject", "Engine", "PCG" });
    }
}
