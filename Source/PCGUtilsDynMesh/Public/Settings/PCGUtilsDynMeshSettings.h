// Copyright Max Harris

#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "UObject/SoftObjectPtr.h"
#include "PCGUtilsDynMeshSettings.generated.h"

class UMaterialInterface;

UCLASS(Config=Editor, DefaultConfig, meta=(DisplayName="PCG Utils DynMesh"))
class PCGUTILSDYNMESH_API UPCGUtilsDynMeshSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	UPCGUtilsDynMeshSettings();

	virtual FName GetCategoryName() const override;
#if WITH_EDITOR
	virtual FText GetSectionText() const override;
#endif
	UPROPERTY(Config, EditAnywhere, Category="Brushes", meta=(DisplayName = "Default Mesh Brush Override Material", AllowedClasses="/Script/Engine.MaterialInterface"))
	TSoftObjectPtr<UMaterialInterface> StaticMeshBrushMaterial;
};
