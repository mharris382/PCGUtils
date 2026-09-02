// Copyright Max Harris

using UnrealBuildTool;

public class PCGUtilsPainter : ModuleRules
{
    public PCGUtilsPainter(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

        // PCGUtilsPainter is part of the DynMesh / geometry-processing toolkit: it depends on
        // PCGUtilsDynMesh (never the reverse). It exists to organise the growing Painter feature
        // family and to add output targets beyond Dynamic Mesh (Static Mesh component override
        // vertex colors). The core Painter evaluation API stays geometry-agnostic where practical,
        // but the module deliberately keeps its legitimate DynMesh dependencies.
        PublicDependencyModuleNames.AddRange(
            new string[]
            {
                "Core",
                "CoreUObject",
                "Engine",
                "RenderCore",
                "DynamicMesh",
                "GeometryFramework",
                "GeometryCore",
                "GeometryScriptingCore",
                "PCG",
                "PCGGeometryScriptInterop",
                "PCGUtils",
                "PCGUtilsDynMesh"
            }
        );

        // The Static Mesh Component Painter wraps its edits in a named transaction when authored in the editor.
        // ScopedTransaction lives in UnrealEd; the module stays runtime-safe because the include and the dep are
        // both editor-only (same pattern as PCGUtilsDynMesh's AssetRegistry dependency).
        if (Target.bBuildEditor)
        {
            PrivateDependencyModuleNames.Add("UnrealEd");
        }
    }
}
