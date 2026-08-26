#pragma once

#include "CoreMinimal.h"
#include "PCGCommon.h"
#include "Elements/PCGUtilsDynMeshProcessBase.h"

#include "PCGDynamicMeshSelectionToPoints.generated.h"

/** Converts the incoming selection to unique mesh vertices and emits them as PCG points. */
UCLASS(BlueprintType, ClassGroup=(Procedural), Category="PCGUtils|DynMesh")
class PCGUTILSDYNMESH_API UPCGDynamicMeshSelectionToPointsSettings : public UPCGUtilsDynMeshProcessBaseSettings
{
	GENERATED_BODY()

public:
	virtual bool RequiresSelection() const override { return true; }
	virtual bool GetRequiredSelectionDomain(UE::Geometry::EGeometryElementType& OutElementType) const override
	{
		OutElementType = UE::Geometry::EGeometryElementType::Vertex;
		return true;
	}
	virtual bool AllowPartialSelectionDomainInclusion() const override { return bAllowPartialInclusion; }

#if WITH_EDITOR
	virtual FName GetDefaultNodeName() const override { return TEXT("DynMeshSelectionToPoints"); }
	virtual FText GetDefaultNodeTitle() const override;
	virtual FText GetNodeTooltipText() const override;
	virtual FLinearColor GetNodeTitleColor() const override { return FLinearColor(0.413f, 0.25f ,1.0f, 1.0f);	}
	virtual bool ShouldDrawNodeCompact() const override { return true; }
	virtual bool GetCompactNodeIcon(FName& OutCompactNodeIcon) const override
	{
		OutCompactNodeIcon = PCGNodeConstants::Icons::CompactNodeConvert;
		return true;
	}
#endif

	/** Transform mesh-local vertex positions and normals into the PCG target actor's world space. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Settings", meta=(PCG_Overridable))
	bool bOutputToWorldSpace = true;

	/** Include a vertex when any incident source element is selected. Disable to require full inclusion. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Selection", AdvancedDisplay, meta=(PCG_Overridable))
	bool bAllowPartialInclusion = true;

protected:
	virtual TArray<FPCGPinProperties> InputPinProperties() const override;
	virtual TArray<FPCGPinProperties> OutputPinProperties() const override;
	virtual FPCGElementPtr CreateElement() const override;
};

class PCGUTILSDYNMESH_API FPCGDynamicMeshSelectionToPointsElement : public FPCGUtilsDynMeshProcessBaseElement
{
public:
	virtual bool CanExecuteOnlyOnMainThread(FPCGContext* Context) const override { return true; }

protected:
	virtual bool ExecuteInternal(FPCGContext* Context) const override;
};
