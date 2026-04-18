// Copyright Max Harris

using UnrealBuildTool;

public class PCGGrammarUtils : ModuleRules
{
    public PCGGrammarUtils(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

        PublicDependencyModuleNames.AddRange(
            new string[]
            {
                "Core",
                "CoreUObject",
                "Engine",
                "PCG",
            }
        );
    }
}
