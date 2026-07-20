#pragma once

#include "CoreMinimal.h"
#include "Data/PCGUtilsComponentData.h"
#include "Elements/PCGDataFromActor.h"

#include "PCGGetStaticMeshData.generated.h"

/** Collects static mesh components from actors as individual point data. */
UCLASS(BlueprintType, ClassGroup=(Procedural), Category="PCGUtils|Actor Data")
class PCGUTILS_API UPCGGetStaticMeshDataSettings : public UPCGDataFromActorSettings
{
	GENERATED_BODY()

public:
	UPCGGetStaticMeshDataSettings();

#if WITH_EDITOR
	virtual FName GetDefaultNodeName() const override { return FName(TEXT("PCGUtils|GetStaticMeshData")); }
	virtual FText GetDefaultNodeTitle() const override;
	virtual FText GetNodeTooltipText() const override;
#endif

	virtual EPCGDataType GetDataFilter() const override { return EPCGDataType::Point; }

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Settings")
	FName MeshOutputAttributeName = FName(TEXT("Mesh"));

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Settings", meta=(InlineEditConditionToggle))
	bool bExtractMeshMaterials = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Settings", meta=(EditCondition="bExtractMeshMaterials", ClampMin="1", UIMin="1", ToolTip="Maximum number of mesh materials to extract. When greater than one, a zero-based number is appended to the material attribute name. When set to one, the attribute name is used as-is."))
	int32 MaxMaterialCount = 1;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Settings", meta=(EditCondition="bExtractMeshMaterials", ToolTip="Base attribute name for extracted materials. When Max Material Count is greater than one, a zero-based number is appended to this name. When set to one, this name is used as-is."))
	FName MaterialOutputAttributeName = FName(TEXT("Material"));

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Settings", meta = (ShowOnlyInnerProperties))
	FGetComponentDataSettings ComponentSettings;

protected:
	virtual TArray<FPCGPinProperties> OutputPinProperties() const override;
	virtual FPCGElementPtr CreateElement() const override;

#if WITH_EDITOR
	virtual bool DisplayModeSettings() const override { return false; }
#endif
};

class PCGUTILS_API FPCGGetStaticMeshDataElement : public FPCGDataFromActorElement
{
protected:
	virtual void ProcessActor(
		FPCGContext* Context,
		const UPCGDataFromActorSettings* Settings,
		AActor* FoundActor) const override;
};
