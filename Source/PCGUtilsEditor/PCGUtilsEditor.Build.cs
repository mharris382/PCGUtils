// Copyright Max Harris

using UnrealBuildTool;

public class PCGUtilsEditor : ModuleRules
{
    public PCGUtilsEditor(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

        PublicDependencyModuleNames.AddRange(
            new string[]
            {
                "Core",
                "CoreUObject",
                "Engine",
                "GameplayTags",
                "PCGUtils",
                "PCG",
                "SlateCore",
                "Slate",
                
                "GeometryScriptingCore",
                "Projects",
                "RenderCore",
                "RHI",
                "ModelingOperators",
                "UnrealEd"
            }
        );

        PrivateDependencyModuleNames.AddRange(
            new string[]
            {
                "GeometryCore",
                "GeometryFramework",
                "ModelingComponents",
                
                "PCGEditor",
                "PropertyPath",

                "UnrealEd",
                "PropertyEditor",
                "EditorStyle",
                "Projects",   

                "AppFramework",
                "ApplicationCore",
                "AssetDefinition",
                "AssetTools",
                "AssetRegistry",

                "ContentBrowser",
                "DesktopWidgets",
                "DetailCustomizations",
                "DeveloperSettings",
                
                "EditorFramework",
                "EditorScriptingUtilities",
                "EditorStyle",
                "EditorSubsystem",
                "EditorWidgets",
                
                "GraphEditor",
                "InputCore",
                "Kismet",
                "KismetWidgets",
                
                "RenderCore",
                "SourceControl",
                "StructUtilsEditor",
                "ToolMenus",
                "ToolWidgets",
                "TypedElementFramework",
                "TypedElementRuntime",
				
                "LevelEditor",
                "SceneOutliner",
                "AdvancedPreviewScene",

                "ComponentVisualizers"
            }
        );
    }
}
