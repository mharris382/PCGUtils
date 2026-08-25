#pragma once

#include "CoreMinimal.h"
#include "Elements/PCGDynamicMeshBaseElement.h"
#include "Selections/GeometrySelection.h"

#include "PCGDynamicMeshSelectionProcessBase.generated.h"

class UPCGDynamicMeshData;
class UPCGDynamicMeshSelectionData;

namespace PCGDynamicMeshSelectionProcessConstants
{
	inline const FName InputPin = TEXT("In");
	inline const FName SelectionFactoryInputPin = TEXT("Selection Factory");
	inline const FName OutputPin = TEXT("Out");
}

/** Domain in which an optional Selection Factory is evaluated when the process does not require one itself. */
UENUM(BlueprintType)
enum class EPCGUtilsDynMeshProcessSelectionEvaluationDomain : uint8
{
	Triangle,
	Vertex,
	Edge
};

/** Base settings for mesh processors that accept either a Dynamic Mesh or a selection tied to one. */
UCLASS(Abstract, BlueprintType, ClassGroup=(Procedural), Category="PCGUtils|Dynamic Mesh")
class PCGUTILSDYNMESH_API UPCGDynamicMeshSelectionProcessBaseSettings : public UPCGDynamicMeshBaseSettings
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

	/** Conversion policy used whenever the base implicitly converts an incoming selection domain. */
	virtual bool AllowPartialSelectionDomainInclusion() const { return true; }

	/** Evaluation domain for the optional Selection Factory when the derived process does not require a domain. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Selection Factory", meta=(PCG_Overridable))
	EPCGUtilsDynMeshProcessSelectionEvaluationDomain SelectionFactoryEvaluationDomain =
		EPCGUtilsDynMeshProcessSelectionEvaluationDomain::Triangle;

	/** Output the effective selection tied to the processed mesh so it can be reused downstream. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Selection Factory", meta=(PCG_Overridable))
	bool bOutputSelectionData = false;

protected:
	virtual TArray<FPCGPinProperties> InputPinProperties() const override;
	virtual TArray<FPCGPinProperties> OutputPinProperties() const override;
};

/** Resolves either input kind, creates an owned mesh, and delegates the mutation to derived processors. */
class PCGUTILSDYNMESH_API FPCGDynamicMeshSelectionProcessBaseElement : public IPCGDynamicMeshBaseElement
{
public:
	virtual bool CanExecuteOnlyOnMainThread(FPCGContext*) const override { return true; }

protected:
	virtual bool ExecuteInternal(FPCGContext* Context) const override;
	virtual bool ProcessMesh(UPCGDynamicMeshData* MeshData,
		const UPCGDynamicMeshSelectionData* SelectionData, FPCGContext* Context) const = 0;
};
