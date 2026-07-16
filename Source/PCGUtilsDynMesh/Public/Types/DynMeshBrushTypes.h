// Copyright Max Harris

#pragma once

#include "CoreMinimal.h"
#include "UObject/SoftObjectPtr.h"
#include "DynMeshBrushTypes.generated.h"

class UMaterialInterface;

UENUM(BlueprintType)
enum class EPCGUtilsDynMeshBrushType : uint8
{
	Subtract UMETA(DisplayName="Subtract"),
	Union UMETA(DisplayName="Union")
};

USTRUCT(BlueprintType)
struct PCGUTILSDYNMESH_API FDynMeshBrushSettings
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Brush|Vertex Colors", meta=(InlineEditConditionToggle))
	bool bOverwriteVertexColors = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Brush|Vertex Colors", meta=(EditCondition="bOverwriteVertexColors"))
	FLinearColor VertexColor = FLinearColor::White;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Brush|Materials", meta=(InlineEditConditionToggle))
	bool bReplaceMaterial = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Brush|Materials", meta=(EditCondition="bReplaceMaterial"))
	TSoftObjectPtr<UMaterialInterface> ReplacementMaterial;
};
