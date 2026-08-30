// Copyright Max Harris

#pragma once

#include "Factories/PCGUtilsDynMeshDomainSelectionFactory.h"
#include "GeometryScript/GeometryScriptTypes.h"

#include "PCGDynMeshPolygroupSelectionFactory.generated.h"

UENUM(BlueprintType)
enum class EPCGUtilsDynMeshPolygroupSelectionMode : uint8
{
	GroupIDs UMETA(DisplayName="Group IDs"),
	HighestGroupID UMETA(DisplayName="Highest Group ID")
};

/** Triangle-native PolyGroup predicate; the shared domain adapter handles vertex and edge consumers. */
UCLASS(BlueprintType, ClassGroup=(Procedural), Category="PCGUtils|DynMesh|Selections")
class PCGUTILSDYNMESH_API UPCGDynMeshPolygroupSelectionFactoryData
	: public UPCGUtilsDynMeshDomainSelectionFactoryData
{
	GENERATED_BODY()

public:
	UPROPERTY()
	FGeometryScriptGroupLayer GroupLayer;

	UPROPERTY()
	EPCGUtilsDynMeshPolygroupSelectionMode SelectionMode = EPCGUtilsDynMeshPolygroupSelectionMode::GroupIDs;

	UPROPERTY()
	TArray<int32> GroupIDs = { 0 };

	UPROPERTY()
	bool bInvertSelection = false;

protected:
	virtual UE::Geometry::EGeometryElementType GetNativeElementTypeInternal() const override
	{
		return UE::Geometry::EGeometryElementType::Face;
	}
	virtual TSharedPtr<FPCGUtilsDynMeshSelectionOperation> CreateNativeOperationInternal() const override;
	virtual void AddToCrc(FArchiveCrc32& Ar, bool bFullDataCrc) const override;
};

/** Authors a reusable Selector, without requiring a mesh until the consuming node evaluates it. */
UCLASS(BlueprintType, ClassGroup=(Procedural), Category="PCGUtils|DynMesh|Selections")
class PCGUTILSDYNMESH_API UPCGDynMeshPolygroupSelectionFactoryProviderSettings
	: public UPCGUtilsDynMeshDomainSelectionFactoryProviderSettings
{
	GENERATED_BODY()

public:
#if WITH_EDITOR
	virtual FName GetDefaultNodeName() const override { return TEXT("DynMeshPolygroupSelector"); }
	virtual FText GetDefaultNodeTitle() const override;
	virtual TArray<FText> GetNodeTitleAliases() const override;
	virtual FText GetNodeTooltipText() const override;
#endif

	/** Default triangle groups, or an extended PolyGroup layer by index. The layer must exist on the mesh. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Selection", meta=(PCG_Overridable))
	FGeometryScriptGroupLayer GroupLayer;

	/** Highest Group ID selects the largest ID actually used by triangles, not the allocation counter or creation order. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Selection", meta=(PCG_Overridable))
	EPCGUtilsDynMeshPolygroupSelectionMode SelectionMode = EPCGUtilsDynMeshPolygroupSelectionMode::GroupIDs;

	/** Select the union of these IDs. Unknown IDs are ignored; an empty list selects nothing before inversion. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Selection", meta=(PCG_Overridable, ClampMin="0",
		EditCondition="SelectionMode == EPCGUtilsDynMeshPolygroupSelectionMode::GroupIDs", EditConditionHides))
	TArray<int32> GroupIDs = { 0 };

	/** Invert the triangle region before converting it to the consumer's requested domain. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Selection", meta=(PCG_Overridable))
	bool bInvertSelection = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Selection", AdvancedDisplay, meta=(PCG_Overridable))
	int32 Priority = 0;

	virtual FName GetMainOutputPin() const override;
	virtual UPCGUtilsDynMeshFactoryData* CreateFactory(
		FPCGContext* InContext, UPCGUtilsDynMeshFactoryData* InFactory = nullptr) const override;

protected:
	virtual const FPCGDataTypeBaseId& GetFactoryTypeId() const override;
};
