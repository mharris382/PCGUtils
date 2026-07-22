using UnrealBuildTool;

public class PCGUtilsDataCache : ModuleRules
{
	public PCGUtilsDataCache(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new[]
		{
			"Core",
			"CoreUObject",
			"Engine",
			"PCG",
			"DeveloperSettings"
		});

		// Asset creation is routed through PCG's exported utility, whose editor-only
		// implementation owns its UnrealEd/ContentBrowser dependencies. This module
		// therefore remains runtime-safe and needs no unconditional editor modules.
	}
}
