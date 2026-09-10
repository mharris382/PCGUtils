// Copyright Max Harris

#pragma once

#include "CoreMinimal.h"
#include "Elements/Selections/PCGUtilsDynMeshSelectionSource.h"
#include "Factories/PCGUtilsDynMeshDomainSelectionFactory.h"

#include "PCGDynMeshSelectionFromPointsFactory.generated.h"

class UPCGBasePointData;

/** Whether the integer values read from PCG points are Dynamic Mesh vertex IDs or triangle IDs. */
UENUM(BlueprintType)
enum class EPCGDynMeshSelectionFromPointsIDMode : uint8
{
	VertexIDs UMETA(DisplayName="Vertex IDs"),
	TriangleIDs UMETA(DisplayName="Triangle IDs")
};

namespace PCGDynMeshSelectionFromPointsFactoryConstants
{
	inline const FName PointsInputPin = TEXT("Points");

	/** Vertex ID presets reuse the shared Selector/Selection representation indices from the source base. */
	inline constexpr int32 VertexIDSelectorPreconfiguredIndex =
		PCGUtilsDynMeshSelectionSourceConstants::SelectorPreconfiguredIndex;
	inline constexpr int32 VertexIDSelectionPreconfiguredIndex =
		PCGUtilsDynMeshSelectionSourceConstants::SelectionPreconfiguredIndex;

	/** "Selection From Triangle IDs" alias presets. */
	inline constexpr int32 TriangleIDSelectorPreconfiguredIndex = 1100;
	inline constexpr int32 TriangleIDSelectionPreconfiguredIndex = 1101;
}

/** Selection factory that matches vertex or triangle IDs read from an integer attribute on PCG points. */
UCLASS(BlueprintType, ClassGroup=(Procedural), Category="PCGUtils|DynMesh|Selections")
class PCGUTILSDYNMESH_API UPCGDynMeshSelectionFromPointsFactoryData
	: public UPCGUtilsDynMeshDomainSelectionFactoryData
{
	GENERATED_BODY()

public:
	UPROPERTY()
	TArray<TObjectPtr<const UPCGBasePointData>> PointData;

	UPROPERTY()
	EPCGDynMeshSelectionFromPointsIDMode IDMode = EPCGDynMeshSelectionFromPointsIDMode::VertexIDs;

	UPROPERTY()
	FName VertexIndexAttribute = TEXT("VertexIndex");

	UPROPERTY()
	FName TriangleIdAttribute = TEXT("TriangleId");

protected:
	virtual UE::Geometry::EGeometryElementType GetNativeElementTypeInternal() const override
	{
		return IDMode == EPCGDynMeshSelectionFromPointsIDMode::TriangleIDs
			? UE::Geometry::EGeometryElementType::Face
			: UE::Geometry::EGeometryElementType::Vertex;
	}
	virtual TSharedPtr<FPCGUtilsDynMeshSelectionOperation> CreateNativeOperationInternal() const override;
	virtual void AddToCrc(FArchiveCrc32& Ar, bool bFullDataCrc) const override;
};

UCLASS(BlueprintType, ClassGroup=(Procedural), Category="PCGUtils|DynMesh|Selections",
	meta=(Keywords="Vertex IDs Triangle IDs Face IDs Point Indices Selection Selector Selection From Triangle IDs DynMesh Select"))
class PCGUTILSDYNMESH_API UPCGDynMeshSelectionFromPointsFactoryProviderSettings
	: public UPCGUtilsDynMeshDomainSelectionSourceSettings
{
	GENERATED_BODY()

public:
#if WITH_EDITOR
	virtual FName GetDefaultNodeName() const override { return TEXT("DynMeshSelectionFromPointsFactory"); }
	virtual FText GetDefaultNodeTitle() const override;
	virtual FText GetNodeTooltipText() const override;
	virtual FString GetAdditionalTitleInformation() const override;
	virtual TArray<FPCGPreConfiguredSettingsInfo> GetPreconfiguredInfo() const override;
	virtual void ApplyPreconfiguredSettings(const FPCGPreConfiguredSettingsInfo& PreconfiguredInfo) override;
#endif

	/** Whether the point values are Dynamic Mesh vertex IDs or triangle IDs. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Selection", meta=(PCG_Overridable))
	EPCGDynMeshSelectionFromPointsIDMode IDMode = EPCGDynMeshSelectionFromPointsIDMode::VertexIDs;

	/** Integer point attribute containing Dynamic Mesh vertex IDs. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Selection", meta=(PCG_Overridable,
		EditCondition="IDMode == EPCGDynMeshSelectionFromPointsIDMode::VertexIDs", EditConditionHides))
	FName VertexIndexAttribute = TEXT("VertexIndex");

	/** Integer point attribute containing Dynamic Mesh triangle IDs. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Selection", meta=(PCG_Overridable,
		EditCondition="IDMode == EPCGDynMeshSelectionFromPointsIDMode::TriangleIDs", EditConditionHides))
	FName TriangleIdAttribute = TEXT("TriangleId");

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Selection", AdvancedDisplay, meta=(PCG_Overridable))
	int32 Priority = 0;

	virtual UPCGUtilsDynMeshFactoryData* CreateFactory(
		FPCGContext* InContext, UPCGUtilsDynMeshFactoryData* InFactory = nullptr) const override;

protected:
	virtual const FPCGDataTypeBaseId& GetFactoryTypeId() const override;
	virtual TArray<FPCGPinProperties> SourceInputPinProperties() const override;
};
