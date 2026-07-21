#pragma once

#include "CoreMinimal.h"
#include "PCGData.h"
#include "Data/Registry/PCGDataType.h"
#include "Selections/GeometrySelection.h"

#include "PCGDynamicMeshSelectionData.generated.h"

class UPCGDynamicMeshData;
struct FPCGContext;

USTRUCT(meta=(PCG_DataTypeDisplayName="Dynamic Mesh Selection"))
struct FPCGDataTypeInfoDynamicMeshSelection : public FPCGDataTypeInfo
{
	GENERATED_BODY()

	PCG_DECLARE_TYPE_INFO(PCGUTILSDYNMESH_API);
};

/** A triangle-face selection tied to the exact PCG Dynamic Mesh on which it was evaluated. */
UCLASS(BlueprintType)
class PCGUTILSDYNMESH_API UPCGDynamicMeshSelectionData : public UPCGData
{
	GENERATED_BODY()

public:
	PCG_ASSIGN_TYPE_INFO(FPCGDataTypeInfoDynamicMeshSelection)

	void Initialize(const UPCGDynamicMeshData* InSourceMesh, UE::Geometry::FGeometrySelection InSelection);

	const UPCGDynamicMeshData* GetSourceMeshData() const { return SourceMeshData; }
	const UE::Geometry::FGeometrySelection& GetSelection() const { return Selection; }

	virtual void VisitDataNetwork(TFunctionRef<void(const UPCGData*)> Action) const override;
	virtual UPCGData* DuplicateData(FPCGContext* Context, bool bInitializeMetadata = true) const override;
	virtual bool CanBeSerialized() const override { return false; }

protected:
	virtual void AddToCrc(FArchiveCrc32& Ar, bool bFullDataCrc) const override;
	virtual bool SupportsFullDataCrc() const override { return true; }

private:
	UPROPERTY()
	TObjectPtr<UPCGDynamicMeshData> SourceMeshData;

	UE::Geometry::FGeometrySelection Selection;
};
