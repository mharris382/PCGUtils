// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "PCGComponent.h"
#include "Engine/DeveloperSettings.h"
#include "PCGUtilsSettings.generated.h"




/**
 * 
 */
UCLASS(Config=Game, DefaultConfig, meta=(DisplayName="PCGUtils"))
class PCGUTILS_API UPCGUtilsSettings : public UDeveloperSettings
{
	GENERATED_BODY()
public:
	
	UPROPERTY(Config, EditAnywhere, Category="PCG Actor Defaults", meta = (ToolTip = "Default Save Path where PCGActors will save assets"))
	FDirectoryPath DefaultAssetSavePath = FDirectoryPath(TEXT("/Game/_Generated"));
	
	UPROPERTY(Config, EditAnywhere, Category="PCG Actor Defaults", meta = (ToolTip = "Default GenerationTrigger for the PCGActor's PCG Component"))
	EPCGComponentGenerationTrigger DefaultGenerationTrigger = EPCGComponentGenerationTrigger::GenerateOnDemand;

	UPROPERTY(Config, EditAnywhere, Category="PCG Actor Defaults", meta = (ToolTip = "Default Behavior for the PCGActor's PCG Component"))
	bool bRegenerateInEditorByDefault = true;

	UPROPERTY(Config, EditAnywhere, Category="PCG Actor Defaults", meta = (ToolTip = "Default Behavior for the PCGActor's PCG Component"))
	bool bGenerateOnDropByDefault = true;
	
	UPROPERTY(Config, EditAnywhere, Category = "PCG Actor Defaults|RuntimeGeneration", meta = (ToolTip = "override default generation radii for the PCGActor's PCG Component"))
	bool bOverrideGenerationRadiiByDefault = false;

	UPROPERTY(Config, EditAnywhere, Category = "PCG Actor Defaults|RuntimeGeneration", meta = (EditCondition = "bOverrideGenerationRadiiByDefault", ToolTip = "PCGActor's PCG Component default generation radii"))
	FPCGRuntimeGenerationRadii DefaultGenerationRadii;
	
	UPROPERTY(Config,	EditAnywhere,Category = "PCG Editor",	meta = (DisplayName = "Disable PCG Editor Cache on Startup",ToolTip = "Automatically disables pcg.cache.editor.enabled when PCGUtils loads."))
	bool bAutoDisablePCGEditorCache = false;
	
	UPROPERTY(Config,	EditAnywhere,Category = "PCG Editor",	meta = (ToolTip = "If true, pcg util components that trigger automatic regeneration in the editor will only be able to regenerate if a PCGComponentProvider is implemented by the actor or a component on the actor.  Otherwise the first PCGComponent found will be used if no provider is located "))
	bool bRequirePCGComponentProviderForAutoRegeneration = false;	
	
	virtual FName GetCategoryName() const override { return FName("Plugins"); }
};
