#pragma once

#include "CoreMinimal.h"
#include "Elements/PCGDynamicMeshBaseElement.h"

#include "PCGDynamicMeshSelectionProcessBase.generated.h"

class UPCGDynamicMeshData;
class UPCGDynamicMeshSelectionData;

namespace PCGDynamicMeshSelectionProcessConstants
{
	inline const FName InputPin = TEXT("In");
	inline const FName OutputPin = TEXT("Out");
}

/** Base settings for mesh processors that accept either a Dynamic Mesh or a selection tied to one. */
UCLASS(Abstract, BlueprintType, ClassGroup=(Procedural), Category="PCGUtils|Dynamic Mesh")
class PCGUTILSDYNMESH_API UPCGDynamicMeshSelectionProcessBaseSettings : public UPCGDynamicMeshBaseSettings
{
	GENERATED_BODY()

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
