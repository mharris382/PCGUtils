#pragma once

#include "CoreMinimal.h"
#include "PCGContext.h"
#include "PCGSettings.h"
#include "Elements/PCGDynamicMeshBaseElement.h"

#include "PCGDynMeshToPoints.generated.h"

namespace PCGDynMeshToPointsConstants
{
	const FName InDynamicMeshLabel = TEXT("In Dynamic Mesh");
}

UCLASS(BlueprintType, ClassGroup = (Procedural), Category="PCGUtils|Dynamic Mesh")
class PCGUTILSDYNMESH_API UPCGDynMeshToPointsSettings : public UPCGDynamicMeshBaseSettings
{
	GENERATED_BODY()

public:
#if WITH_EDITOR
	virtual bool ShouldDrawNodeCompact() const override { return true; }
	virtual bool GetCompactNodeIcon(FName& OutCompactNodeIcon) const override
	{
		OutCompactNodeIcon = TEXT("PCGUtils.DynamicMeshToPoints");
		return true;
	}
	virtual FName GetDefaultNodeName() const override { return FName(TEXT("DynMsh|Point")); }
	virtual FText GetDefaultNodeTitle() const override
	{
		return NSLOCTEXT("PCGUtils", "DynMeshToPoints_Title", "Dynamic Mesh To Points");
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
