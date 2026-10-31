// Copyright Max Harris

#pragma once

#include "CoreMinimal.h"
#include "Elements/UV/PCGDynamicMeshUVProcessBase.h"

#include "PCGUtilsDynMeshNodeColor.h"
#include "PCGDynMeshUVChannels.generated.h"

UENUM(BlueprintType)
enum class EPCGUtilsDynMeshUVChannelOperation : uint8
{
	Copy UMETA(DisplayName="Copy From Layer", ToolTip="Copy Source Layer's UVs into UV Layer, seams included. A selection copies only its triangles."),
	Clear UMETA(ToolTip="Remove UV Layer's UVs, leaving the triangles without UVs. A selection clears only its triangles."),
	SetCount UMETA(DisplayName="Set Layer Count", ToolTip="Give the mesh exactly Layer Count UV layers: new ones start empty, extra ones are removed. Whole mesh; a selection does not limit it.")
};

/**
 * Manages UV layers: copy one into another (UV0 into UV1 before packing a lightmap channel, say), clear one, or set
 * how many the mesh has. UV Layer is the layer written.
 */
UCLASS(BlueprintType, ClassGroup=(Procedural), Category="PCGUtils|DynMesh|UV",
	meta=(Keywords="DynMesh mesh UV channel layer set copy clear count lightmap selection selector"))
class PCGUTILSDYNMESH_API UPCGDynMeshUVChannelsSettings : public UPCGDynamicMeshUVProcessBaseSettings
{
	GENERATED_BODY()

public:
#if WITH_EDITOR
	virtual FName GetDefaultNodeName() const override { return TEXT("DynMeshUVChannels"); }
	virtual FText GetDefaultNodeTitle() const override;
	virtual FText GetNodeTooltipText() const override;
	virtual FString GetAdditionalTitleInformation() const override;
	virtual FLinearColor GetNodeTitleColor() const override { return PCGUtilsDynMeshNodeColor::Title; }
#endif

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="UV", meta=(PCG_Overridable, DisplayPriority=0))
	EPCGUtilsDynMeshUVChannelOperation Operation = EPCGUtilsDynMeshUVChannelOperation::Copy;

	/** The layer copied from. Must exist on the mesh and differ from UV Layer. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="UV", meta=(PCG_Overridable, ClampMin=0, UIMin=0, UIMax=7,
		EditCondition="Operation==EPCGUtilsDynMeshUVChannelOperation::Copy", EditConditionHides))
	int32 SourceLayer = 0;

	/** How many UV layers the mesh ends with, 1 to 8. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="UV", meta=(PCG_Overridable, ClampMin=1, UIMin=1, ClampMax=8, UIMax=8,
		EditCondition="Operation==EPCGUtilsDynMeshUVChannelOperation::SetCount", EditConditionHides))
	int32 LayerCount = 2;

	virtual TSharedPtr<const FPCGUtilsDynMeshProcessOperation> CreateProcessOperation(FPCGContext* InContext) const override;
	virtual bool SupportsDeferredBuilderProcessing() const override { return true; }

protected:
	virtual FPCGElementPtr CreateElement() const override;
};

class PCGUTILSDYNMESH_API FPCGDynMeshUVChannelsElement final : public FPCGDynamicMeshUVProcessBaseElement
{
};

class PCGUTILSDYNMESH_API FPCGUtilsDynMeshUVChannelsOperation final : public FPCGUtilsDynMeshUVProcessOperation
{
public:
	EPCGUtilsDynMeshUVChannelOperation Operation = EPCGUtilsDynMeshUVChannelOperation::Copy;
	int32 SourceLayer = 0;
	int32 LayerCount = 2;

protected:
	virtual bool ShouldProcessUVs(const FPCGUtilsDynMeshProcessInvocation& Invocation, TArrayView<const int32> TriangleIDs) const override;
	virtual bool ProcessUVs(const FPCGUtilsDynMeshProcessInvocation& Invocation, UE::Geometry::FDynamicMesh3& Mesh,
		UE::Geometry::FDynamicMeshUVOverlay& UVOverlay, TArrayView<const int32> TriangleIDs) const override;
};
