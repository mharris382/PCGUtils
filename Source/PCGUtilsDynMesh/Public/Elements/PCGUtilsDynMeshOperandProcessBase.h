// Copyright Max Harris

#pragma once

#include "Elements/PCGUtilsDynMeshProcessBase.h"
#include "Elements/PCGBooleanOperation.h"

#include "PCGUtilsDynMeshOperandProcessBase.generated.h"

namespace PCGUtilsDynMeshOperandProcessConstants
{
	inline const FName InputPin = TEXT("InA");
	inline const FName OperandPin = TEXT("InB");
}

/**
 * Whole-mesh process with an optional operand. All three geometry pins share one type: DynMesh or Builder.
 * Selection exception: binary solid operations have no partial-mesh semantics. No Selection or Selector is
 * accepted; active Builder selections are ignored and cleared after processing. Missing operands pass through.
 * Derived nodes only implement CreateProcessOperation(), using Invocation.OperandMeshData as read-only input.
 */
UCLASS(Abstract, BlueprintType, ClassGroup=(Procedural), Category="PCGUtils|DynMesh", HideCategories=(Selector))
class PCGUTILSDYNMESH_API UPCGUtilsDynMeshOperandProcessBaseSettings : public UPCGUtilsDynMeshProcessBaseSettings
{
	GENERATED_BODY()

public:
	virtual bool SupportsDeferredBuilderProcessing() const override { return true; }
	virtual FPCGDataTypeIdentifier GetCurrentPinTypesID(const UPCGPin* InPin) const override;

	/** Same pairing/broadcasting modes as the engine Boolean Operation. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category=Settings, meta=(PCG_Overridable))
	EPCGBooleanOperationMode Mode = EPCGBooleanOperationMode::EachAWithEachB;

	/** Missing operands always preserve the primary tags, regardless of this setting. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category=Settings, meta=(PCG_Overridable))
	EPCGBooleanOperationTagInheritanceMode TagInheritanceMode = EPCGBooleanOperationTagInheritanceMode::Both;

protected:
	virtual FName GetMainInputPinLabel() const override { return PCGUtilsDynMeshOperandProcessConstants::InputPin; }
	virtual FName GetOperandInputPinLabel() const { return PCGUtilsDynMeshOperandProcessConstants::OperandPin; }
	virtual TArray<FPCGPinProperties> InputPinProperties() const override;
	virtual TArray<FPCGPinProperties> OutputPinProperties() const override;
	virtual FPCGElementPtr CreateElement() const override;
#if WITH_EDITOR
	virtual TArray<FPCGSettingsOverridableParam> GatherOverridableParams() const override;
#endif

private:
	FPCGDataTypeIdentifier GetOperandProcessDataTypes() const;
	friend class FPCGUtilsDynMeshOperandProcessBaseElement;
};

/** Shared pairing, input validation, passthrough, ownership, and deferred composition for operand processes. */
class PCGUTILSDYNMESH_API FPCGUtilsDynMeshOperandProcessBaseElement : public FPCGUtilsDynMeshProcessBaseElement
{
protected:
	virtual bool ExecuteInternal(FPCGContext* Context) const override;
};
