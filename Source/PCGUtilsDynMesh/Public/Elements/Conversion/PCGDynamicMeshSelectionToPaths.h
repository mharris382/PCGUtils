#pragma once

#include "CoreMinimal.h"
#include "PCGSettings.h"

#include "PCGDynamicMeshSelectionToPaths.generated.h"

/**
 * Converts the boundary loops of a Dynamic Mesh Selection's implied triangle region into ordered PCG point data.
 * Each output data item is a closed PCGEx-compatible path (its first point is not repeated at the end).
 */
UCLASS(BlueprintType, ClassGroup=(Procedural), Category="PCGUtils|Dynamic Mesh")
class PCGUTILSDYNMESH_API UPCGDynamicMeshSelectionToPathsSettings : public UPCGSettings
{
	GENERATED_BODY()

public:
#if WITH_EDITOR
	virtual FName GetDefaultNodeName() const override { return TEXT("DynamicMeshSelectionToPaths"); }
	virtual FText GetDefaultNodeTitle() const override;
	virtual FText GetNodeTooltipText() const override;
	virtual FLinearColor GetNodeTitleColor() const override { return FLinearColor(0.413f, 0.25f, 1.0f, 1.0f); }
#endif

	/** Transform mesh-local boundary positions into the PCG target actor's world space. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Settings", meta=(PCG_Overridable))
	bool bOutputToWorldSpace = true;

	/** Tag projected negative-winding loops as holes, matching the PCGEx/Clipper2 path convention. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Settings|Tagging", meta=(PCG_Overridable, InlineEditConditionToggle))
	bool bTagHoles = false;

	/** Data tag added to paths classified as holes. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Settings|Tagging", meta=(PCG_Overridable, EditCondition="bTagHoles"))
	FString HoleTag = TEXT("Hole");

	/**
	 * Normal of the plane used to determine path winding. The axis is interpreted in output space:
	 * world space when Output To World Space is enabled, otherwise mesh-local space.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Settings|Tagging", meta=(PCG_Overridable, EditCondition="bTagHoles"))
	FVector ProjectionAxis = FVector::UpVector;

protected:
	virtual TArray<FPCGPinProperties> InputPinProperties() const override;
	virtual TArray<FPCGPinProperties> OutputPinProperties() const override;
	virtual FPCGElementPtr CreateElement() const override;
};

class PCGUTILSDYNMESH_API FPCGDynamicMeshSelectionToPathsElement : public IPCGElement
{
public:
	virtual bool CanExecuteOnlyOnMainThread(FPCGContext* Context) const override { return true; }

protected:
	virtual bool ExecuteInternal(FPCGContext* Context) const override;
};
