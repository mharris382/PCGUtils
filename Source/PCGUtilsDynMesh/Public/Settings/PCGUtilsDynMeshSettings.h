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

	/**
	 * Show a DynMesh node's main output pin as the type it will actually emit (purple Dynamic Mesh, or DynMesh
	 * Selection when Output Selection Data is on) instead of the grey union of every type the node can handle.
	 * A Builder input still turns the pin into a Builder pin. Open graphs refresh their pins when a node changes
	 * or the graph is reopened.
	 */
	UPROPERTY(Config, EditAnywhere, Category="Graph Editor", meta=(DisplayName="Show Concrete DynMesh Output Pin Types"))
	bool bShowConcreteOutputPinTypes = true;
};
