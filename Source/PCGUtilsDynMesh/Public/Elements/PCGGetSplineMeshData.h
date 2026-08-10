#pragma once

#include "CoreMinimal.h"
#include "Elements/PCGDataFromActor.h"
#include "GeometryScript/MeshBooleanFunctions.h"

#include "PCGGetSplineMeshData.generated.h"

class USplineMeshComponent;

/**
 * Finds USplineMeshComponents on the actors resolved by the inherited Get Actor Data actor/component selection
 * and extracts their already-authored, deformed render geometry as UPCGDynamicMeshData.
 *
 * This bridges hand-authored editor USplineMeshComponents (optionally Editor Only / Hidden In Game) into the
 * normal PCGUtilsDynMesh Dynamic Mesh pipeline, without requiring the artist to recreate the layout inside a
 * PCG graph. This is a read-only extraction: source components, their static meshes, and their materials are
 * never created, modified, hidden, attached/detached, or destroyed.
 */
UCLASS(BlueprintType, ClassGroup = (Procedural), Category = "PCGUtils|Dynamic Mesh")
class PCGUTILSDYNMESH_API UPCGGetSplineMeshDataSettings : public UPCGDataFromActorSettings
{
	GENERATED_BODY()

public:
	UPCGGetSplineMeshDataSettings();

#if WITH_EDITOR
	virtual FName GetDefaultNodeName() const override { return TEXT("GetSplineMeshData"); }
	virtual FText GetDefaultNodeTitle() const override;
	virtual FText GetNodeTooltipText() const override;
	virtual FLinearColor GetNodeTitleColor() const override { return FLinearColor(0.413f, 0.25f, 1.0f, 1.0f); }
#endif

	//~Begin UPCGDataFromActorSettings interface
	virtual EPCGDataType GetDataFilter() const override { return EPCGDataType::DynamicMesh; }

protected:
#if WITH_EDITOR
	virtual bool DisplayModeSettings() const override { return false; }
#endif
	//~End UPCGDataFromActorSettings interface

	virtual TArray<FPCGPinProperties> OutputPinProperties() const override;
	virtual FPCGElementPtr CreateElement() const override;

public:
	/** Render LOD of each spline mesh component's Static Mesh to bake. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Geometry", meta = (PCG_Overridable, ClampMin = "0", UIMin = "0"))
	int32 RenderLODIndex = 0;

	/**
	 * Converts each spline mesh component's world-space geometry into the PCG target actor's local space before
	 * output. Enable this when the resulting Dynamic Mesh will be attached to that actor.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Output", meta = (PCG_Overridable))
	bool bConvertWorldToActorLocal = true;

	/**
	 * If false (default), each found USplineMeshComponent produces its own independent UPCGDynamicMeshData.
	 * If true, every spline mesh component found across the whole actor query is boolean-unioned into a single
	 * UPCGDynamicMeshData, with materials remapped into one coherent shared material array.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Output", meta = (PCG_Overridable))
	bool bUnionMeshes = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Output", meta = (PCG_Overridable, EditCondition = "bUnionMeshes", EditConditionHides, ShowOnlyInnerProperties))
	FGeometryScriptMeshBooleanOptions UnionOptions;
};

class PCGUTILSDYNMESH_API FPCGGetSplineMeshDataElement : public FPCGDataFromActorElement
{
protected:
	virtual void ProcessActors(FPCGContext* Context, const UPCGDataFromActorSettings* Settings, const TArray<AActor*>& FoundActors, TArray<FPCGTaskId>& OutDynamicDependencies) const override;
	virtual void ProcessActors(FPCGContext* Context, const UPCGDataFromActorSettings* Settings, const TArray<AActor*>& FoundActors) const override;
	virtual void ProcessActor(FPCGContext* Context, const UPCGDataFromActorSettings* Settings, AActor* FoundActor) const override;
};
