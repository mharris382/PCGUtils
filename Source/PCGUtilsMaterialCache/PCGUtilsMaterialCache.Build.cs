using UnrealBuildTool;

public class PCGUtilsMaterialCache : ModuleRules
{
	public PCGUtilsMaterialCache(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new[]
		{
			"Core",
			"CoreUObject",
			"Engine",
			"PCG"
		});

		// Kept independent of the PCGUtils module: the material cache is a general-purpose
		// runtime service, and PCG integration is a client of it rather than the other way
		// around.
	}
}
