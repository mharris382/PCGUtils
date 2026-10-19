// Copyright Max Harris

using UnrealBuildTool;

public class PCGUtilsTextures : ModuleRules
{
    public PCGUtilsTextures(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

        // Textures as PCG inputs: greyscale image sampling (CPU texture source or GPU compute), image
        // vectorization, and the Image Path component. The path component implements PCGUtils' path
        // provider interface, so PCGUtils is public.
        PublicDependencyModuleNames.AddRange(
            new string[]
            {
                "Core",
                "CoreUObject",
                "Engine",
                "PCG",
                "PCGUtils"
            }
        );

        PrivateDependencyModuleNames.AddRange(
            new string[]
            {
                "ImageCore",
                "RenderCore",
                "RHI",
                "PCGUtilsTexturesShaders"
            }
        );
    }
}
