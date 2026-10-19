// Copyright Max Harris

using UnrealBuildTool;

public class PCGUtilsTexturesShaders : ModuleRules
{
    public PCGUtilsTexturesShaders(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

        // Global shaders must be registered before the shader maps are built, so this module loads at
        // PostConfigInit and depends only on the rendering core. Everything that needs Engine, PCG or UObjects
        // lives in PCGUtilsTextures, which loads at Default and calls in here with a raw RHI texture.
        PublicDependencyModuleNames.AddRange(
            new string[]
            {
                "Core",
                "RenderCore",
                "RHI"
            }
        );

        PrivateDependencyModuleNames.AddRange(
            new string[]
            {
                "Projects"
            }
        );
    }
}
