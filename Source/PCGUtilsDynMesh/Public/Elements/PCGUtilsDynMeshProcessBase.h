#pragma once

#include "CoreMinimal.h"
#include "Elements/PCGDynamicMeshBaseElement.h"
#include "Selections/GeometrySelection.h"

#include "PCGUtilsDynMeshProcessBase.generated.h"

class UPCGDynamicMeshData;
class UPCGDynamicMeshSelectionData;
class UPCGUtilsDynMeshSelectionFactoryData;
class UPCGData;

namespace PCGUtilsDynMeshProcessConstants
{
	inline const FName InputPin = TEXT("In");
	inline const FName SelectionFactoryInputPin = TEXT("Selector");
	inline const FName OutputPin = TEXT("Out");
}

/** Domain in which an optional Selector is evaluated when the process does not require one itself. */
UENUM(BlueprintType)
enum class EPCGUtilsDynMeshProcessSelectionEvaluationDomain : uint8
{
	Triangle,
	Vertex,
	Edge
};

/**
 * Common settings contract for operations that consume either DynMesh data or DynMesh Selection data.
 * The optional Selector is resolved against each input and intersects an incoming materialized selection.
 */
UCLASS(Abstract, BlueprintType, ClassGroup=(Procedural), Category="PCGUtils|DynMesh")
class PCGUTILSDYNMESH_API UPCGUtilsDynMeshProcessBaseSettings : public UPCGDynamicMeshBaseSettings
{
	GENERATED_BODY()

public:
	/**
	 * Override when ProcessMesh requires selection inputs in one element domain.
	 * Return true and set OutElementType to Vertex, Edge, or Face. The base converts selection-data inputs before
	 * ProcessMesh. A complete Dynamic Mesh input has no selection domain and is never affected by this requirement.
	 * The default returns false, preserving the incoming domain and all existing process behavior.
	 */
	virtual bool GetRequiredSelectionDomain(
		UE::Geometry::EGeometryElementType& OutElementType) const
	{
		return false;
	}

	/**
	 * Override for operations that cannot run without a selection. A materialized Selection input or a connected
	 * Selector satisfies the requirement; a bare DynMesh without a factory produces a graph error.
	 */
	virtual bool RequiresSelection() const { return false; }

	/** Conversion policy used whenever the base implicitly converts an incoming selection domain. */
	virtual bool AllowPartialSelectionDomainInclusion() const { return true; }

	/** Evaluation domain for the optional Selector when the derived process does not require a domain. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Selector", meta=(PCG_Overridable))
	EPCGUtilsDynMeshProcessSelectionEvaluationDomain SelectionFactoryEvaluationDomain =
		EPCGUtilsDynMeshProcessSelectionEvaluationDomain::Triangle;

	/** Output the effective selection tied to the processed mesh so it can be reused downstream. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Selector", meta=(PCG_Overridable))
	bool bOutputSelectionData = false;

protected:
	virtual void ApplyDeprecationBeforeUpdatePins(
		UPCGNode* InOutNode, TArray<TObjectPtr<UPCGPin>>& InputPins,
		TArray<TObjectPtr<UPCGPin>>& OutputPins) override;
	virtual TArray<FPCGPinProperties> InputPinProperties() const override;
	virtual TArray<FPCGPinProperties> OutputPinProperties() const override;
};

/** Resolved, domain-compatible process input. Any synthesized selection is owned by the PCG context. */
struct PCGUTILSDYNMESH_API FPCGUtilsDynMeshResolvedInput
{
	const UPCGDynamicMeshData* MeshData = nullptr;
	const UPCGDynamicMeshSelectionData* SelectionData = nullptr;

	bool IsValid() const { return MeshData != nullptr; }
	const UPCGData* GetData() const;
};

/** Shared selection/factory resolution used by both the simple process executor and Mesh Target operations. */
class PCGUTILSDYNMESH_API FPCGUtilsDynMeshProcessFunctions
{
public:
	static FPCGUtilsDynMeshResolvedInput ResolveInput(
		const UPCGData* InputData, const UPCGUtilsDynMeshProcessBaseSettings* Settings, FPCGContext* Context);
};

/** Resolves either input kind, creates an owned mesh, and delegates the mutation to derived processors. */
class PCGUTILSDYNMESH_API FPCGUtilsDynMeshProcessBaseElement : public IPCGDynamicMeshBaseElement
{
public:
	virtual bool CanExecuteOnlyOnMainThread(FPCGContext*) const override { return true; }

protected:
	virtual bool ExecuteInternal(FPCGContext* Context) const override;
	virtual bool ProcessMesh(UPCGDynamicMeshData*, const UPCGDynamicMeshSelectionData*, FPCGContext*) const
	{
		return false;
	}
};
