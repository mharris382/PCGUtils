// Copyright Max Harris

using UnrealBuildTool;

public class PCGUtilsVoxel : ModuleRules
{
	public PCGUtilsVoxel(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(
			new string[]
			{
				"Core",
				"CoreUObject",
				"Engine",
				"DynamicMesh",
				"GeometryCore",
				"GeometryFramework",
				"PCG",
				"PCGGeometryScriptInterop",
				"PCGUtilsCore",
				// Space helpers: DynMesh output follows the DynMesh family's target-actor-local convention.
				// The dependency only runs Voxel -> DynMesh, never the other way.
				"PCGUtilsDynMesh"
			}
		);
	}
}
