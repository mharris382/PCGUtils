// Copyright Max Harris

#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "PCGUtilsDynMeshSettings.generated.h"

class UMaterialInterface;

UCLASS(Config=Editor, DefaultConfig, meta=(DisplayName="PCG Utils DynMesh"))
class PCGUTILSDYNMESH_API UPCGUtilsDynMeshSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	UPCGUtilsDynMeshSettings();

	virtual FName GetCategoryName() const override;
	virtual FText GetSectionText() const override;

	UPROPERTY(Config, EditAnywhere, Category="Brushes", meta=(AllowedClasses="/Script/Engine.MaterialInterface"))
	TSoftObjectPtr<UMaterialInterface> StaticMeshBrushMaterial;
};
