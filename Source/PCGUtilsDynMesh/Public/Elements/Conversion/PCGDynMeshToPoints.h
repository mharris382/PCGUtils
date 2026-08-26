#pragma once

#include "CoreMinimal.h"
#include "PCGCommon.h"
#include "PCGContext.h"
#include "PCGSettings.h"
#include "Elements/PCGDynamicMeshBaseElement.h"

#include "PCGDynMeshToPoints.generated.h"

namespace PCGDynMeshToPointsConstants
{
	const FName InDynamicMeshLabel = TEXT("In Dynamic Mesh");
}

UCLASS(BlueprintType, ClassGroup = (Procedural), Category="PCGUtils|DynMesh")
class PCGUTILSDYNMESH_API UPCGDynMeshToPointsSettings : public UPCGDynamicMeshBaseSettings
{
	GENERATED_BODY()

public:
#if WITH_EDITOR
	virtual FLinearColor GetNodeTitleColor() const override { return FLinearColor(0.413f, 0.25f ,1.0f, 1.0f);	}
	virtual bool ShouldDrawNodeCompact() const override { return true; }
	virtual bool GetCompactNodeIcon(FName& OutCompactNodeIcon) const override
	{
		OutCompactNodeIcon = PCGNodeConstants::Icons::CompactNodeConvert;
		return true;
	}
	virtual FName GetDefaultNodeName() const override { return FName(TEXT("DynMesh|Point")); }
	virtual FText GetDefaultNodeTitle() const override
	{
		return NSLOCTEXT("PCGUtils", "DynMeshToPoints_Title", "DynMesh To Points");
	}
	virtual FText GetNodeTooltipText() const override
	{
		return NSLOCTEXT("PCGUtils", "DynMeshToPoints_Tooltip",
			"Converts Dynamic Mesh vertices to PCG points in vertex-index order, preserving position, vertex color, and normal-derived rotation.");
	}
#endif

	virtual TArray<FPCGPinProperties> InputPinProperties() const override;
	virtual TArray<FPCGPinProperties> OutputPinProperties() const override;

	/** Transform local mesh vertices and normals into the PCG target actor's world space. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings", meta = (PCG_Overridable))
	bool bOutputToWorldSpace = false;

	/** Add the source dynamic-mesh vertex ID to every generated point. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings", meta = (PCG_Overridable))
	bool bOutputVertexIndex = false;

	/** Attribute that receives the source dynamic-mesh vertex ID. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings", meta = (PCG_Overridable, EditCondition = "bOutputVertexIndex"))
	FName VertexIndexAttribute = TEXT("VertexIndex");

protected:
	virtual FPCGElementPtr CreateElement() const override;
};

struct FPCGDynMeshToPointsContext : public FPCGContext {};

class FPCGDynMeshToPointsElement : public IPCGDynamicMeshBaseElement
{
public:
	virtual bool CanExecuteOnlyOnMainThread(FPCGContext* Context) const override { return true; }

protected:
	virtual FPCGContext* CreateContext() override;
	virtual bool PrepareDataInternal(FPCGContext* InContext) const override { return true; }
	virtual bool ExecuteInternal(FPCGContext* InContext) const override;
};
