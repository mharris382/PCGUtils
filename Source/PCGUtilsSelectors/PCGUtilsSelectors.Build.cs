// Copyright Max Harris

using UnrealBuildTool;

public class PCGUtilsSelectors : ModuleRules
{
	public PCGUtilsSelectors(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

		// Point selectors (selectors that only know points) and the wrappers that turn one into each domain's own
		// selector. This module depends on every domain; no domain depends on it, or knows point selectors exist.
		PublicDependencyModuleNames.AddRange(
			new string[]
			{
				"Core",
				"CoreUObject",
				"Engine",
				"DataflowCore",
				"DynamicMesh",
				"GeometryCore",
				"GeometryFramework",
				"GeometryScriptingCore",
				"GeometryCollectionEngine",
				"PCG",
				"PCGGeometryScriptInterop",
				"PCGUtilsCore",
				"PCGUtilsDynMesh",
				"PCGUtilsFracture",
				"PCGUtilsVoxel"
			}
		);

		// FGeometryCollection lives in Chaos.
		SetupModulePhysicsSupport(Target);
	}
}
