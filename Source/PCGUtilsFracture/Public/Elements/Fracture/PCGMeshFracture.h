// Copyright Max Harris

#pragma once

#include "CoreMinimal.h"
#include "Elements/PCGUtilsFractureElementBase.h"
#include "Factories/PCGUtilsFractureFactory.h"
#include "Factories/PCGUtilsFractureProvider.h"
#include "FractureEngineFracturing.h"
#include "Metadata/PCGAttributePropertySelector.h"

#include "PCGMeshFracture.generated.h"

namespace UE::Geometry { class FDynamicMesh3; }

namespace PCGMeshFractureConstants
{
	/** Cutter geometry as DynMesh data, already in the collection's (DynMesh-local) space. */
	inline const FName DynMeshInputPin = TEXT("DynMesh");

	/** Cutter geometry as points carrying a Static Mesh attribute - one mesh instance per point. */
	inline const FName PointsInputPin = TEXT("Points");
}

/**
 * Mesh fracture: cuts the target bones with an arbitrary closed mesh, Fracture Mode's Mesh tool.
 *
 * The cutter is resolved once, when the operation is authored: every DynMesh input and every point's Static Mesh
 * is converted into collection space and appended into one mesh, which is then self-unioned so that overlapping
 * inputs describe a single clean volume rather than nested shells the cut would treat as extra surfaces.
 */
UCLASS(BlueprintType, ClassGroup=(Procedural), Category="PCGUtils|Fracture")
class PCGUTILSFRACTURE_API UPCGMeshFractureFactoryData : public UPCGUtilsFractureFactoryData
{
	GENERATED_BODY()

public:
	/** The combined cutter, in collection space. Shared because the factory is immutable once emitted. */
	TSharedPtr<const UE::Geometry::FDynamicMesh3> CuttingMesh;

	/** Bounds of CuttingMesh, kept for diagnostics when a single cut misses the geometry. */
	UPROPERTY()
	FBox CutterBounds = FBox(ForceInit);

	UPROPERTY()
	EMeshCutterCutDistribution CutDistribution = EMeshCutterCutDistribution::SingleCut;

	UPROPERTY()
	int32 NumberToScatter = 10;

	UPROPERTY()
	int32 GridX = 2;

	UPROPERTY()
	int32 GridY = 2;

	UPROPERTY()
	int32 GridZ = 2;

	UPROPERTY()
	float Variability = 0.0f;

	UPROPERTY()
	float MinScaleFactor = 0.5f;

	UPROPERTY()
	float MaxScaleFactor = 1.5f;

	UPROPERTY()
	bool bRandomOrientation = true;

	UPROPERTY()
	float RollRange = 180.0f;

	UPROPERTY()
	float PitchRange = 180.0f;

	UPROPERTY()
	float YawRange = 180.0f;

	UPROPERTY()
	int32 RandomSeed = 0;

	UPROPERTY()
	float ChanceToFracture = 1.0f;

	UPROPERTY()
	bool bSplitIslands = true;

	UPROPERTY()
	float CloseVertexDistance = 0.001f;

	UPROPERTY()
	float VertexToSurfaceBridgeDistance = 0.0f;

	UPROPERTY()
	bool bSelfUnionInput = true;

	virtual bool Fracture(
		FGeometryCollection& InOutCollection,
		const FDataflowTransformSelection& InTargetBones,
		FPCGContext* InContext,
		FPCGUtilsGeometryCollectionMutationResult& OutMutation) const override;

	virtual FString GetOperationDescription() const override;

protected:
	virtual void AddToCrc(FArchiveCrc32& Ar, bool bFullDataCrc) const override;
};

/**
 * Authoring node for mesh fracture. Emits a Fracture operation for GC | Fracture to run.
 *
 * Connect the cutter as DynMesh data, as points carrying a Static Mesh attribute (the output of a Static Mesh
 * Spawner, for example), or both. Mirrors Fracture Mode's Mesh tool and the MeshCutter Dataflow node.
 */
UCLASS(BlueprintType, ClassGroup=(Procedural), Category="PCGUtils|Fracture",
	meta=(Keywords="Mesh Cut Cutter Cutting Boolean Fracture Shatter Break Static Mesh DynMesh Points GC Geometry Collection"))
class PCGUTILSFRACTURE_API UPCGMeshFractureSettings : public UPCGUtilsFractureProviderSettings
{
	GENERATED_BODY()

public:
	UPCGMeshFractureSettings();

#if WITH_EDITOR
	virtual FName GetDefaultNodeName() const override { return TEXT("MeshFracture"); }
	virtual FText GetDefaultNodeTitle() const override;
	virtual FText GetNodeTooltipText() const override;
	virtual FString GetAdditionalTitleInformation() const override;
#endif

	/** Loading a Static Mesh and reading its mesh description must happen on the game thread. */
	virtual bool RequiresMainThread(FPCGContext* InContext) const override;

	/**
	 * Self-union the combined cutter before cutting.
	 *
	 * Leave this on. Every input is appended into one mesh, and wherever two of them overlap - or a single input
	 * intersects itself - the cutter has surfaces *inside* its own volume. The cut would treat each of those as a
	 * real boundary and leave slivers along them. Self-union resolves the overlaps into one closed volume.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Cutter", meta=(PCG_Overridable))
	bool bSelfUnionInput = true;

	/** Static Mesh attribute (a soft object path) on the Points input. Each point places one instance of it. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Cutter", meta=(PCG_Overridable))
	FPCGAttributePropertyInputSelector MeshAttribute;

	/**
	 * Convert the Points input from world space into the collection's space before building the cutter.
	 *
	 * Leave this on. PCG authors points in world space by convention while the collection lives in its source
	 * DynMesh's local space. DynMesh cutters are never converted - they already share the collection's space.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Cutter", meta=(PCG_Overridable))
	bool bConvertPointsToLocalSpace = true;

	/** Use a Static Mesh's Nanite hi-res source when it has one. Editor only; ignored at runtime. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Cutter", AdvancedDisplay,
		meta=(PCG_Overridable, DisplayName="Use HiRes"))
	bool bUseHiRes = false;

	/** Static Mesh LOD to cut with when not using the hi-res source. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Cutter", AdvancedDisplay,
		meta=(PCG_Overridable, ClampMin="0", DisplayName="LOD Level"))
	int32 LODLevel = 0;

	/**
	 * How to arrange the cutter. Single Cut uses it exactly where it is. Uniform Random and Grid place copies of it
	 * through the target's bounds, positioning the cutter's own origin - the DynMesh pivot, or the collection
	 * origin for a cutter built from points - just as Fracture Mode positions a static mesh's pivot.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Distribution", meta=(PCG_Overridable))
	EMeshCutterCutDistribution CutDistribution = EMeshCutterCutDistribution::SingleCut;

	/** Number of cutter copies to scatter. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Distribution", meta=(PCG_Overridable, ClampMin="1", UIMax="5000",
		EditCondition="CutDistribution == EMeshCutterCutDistribution::UniformRandom", EditConditionHides))
	int32 NumberToScatter = 10;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Distribution", meta=(PCG_Overridable, DisplayName="Grid Width",
		ClampMin="1", UIMax="100", ClampMax="5000", EditCondition="CutDistribution == EMeshCutterCutDistribution::Grid",
		EditConditionHides))
	int32 GridX = 2;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Distribution", meta=(PCG_Overridable, DisplayName="Grid Depth",
		ClampMin="1", UIMax="100", ClampMax="5000", EditCondition="CutDistribution == EMeshCutterCutDistribution::Grid",
		EditConditionHides))
	int32 GridY = 2;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Distribution", meta=(PCG_Overridable, DisplayName="Grid Height",
		ClampMin="1", UIMax="100", ClampMax="5000", EditCondition="CutDistribution == EMeshCutterCutDistribution::Grid",
		EditConditionHides))
	int32 GridZ = 2;

	/** Magnitude of random displacement applied to each grid position. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Distribution", meta=(PCG_Overridable, ClampMin="0.0", Units="cm",
		EditCondition="CutDistribution == EMeshCutterCutDistribution::Grid", EditConditionHides))
	float Variability = 0.0f;

	/** A random uniform scale is chosen between Min and Max for each copy. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Distribution", meta=(PCG_Overridable, ClampMin="0.001",
		EditCondition="CutDistribution != EMeshCutterCutDistribution::SingleCut", EditConditionHides))
	float MinScaleFactor = 0.5f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Distribution", meta=(PCG_Overridable, ClampMin="0.001",
		EditCondition="CutDistribution != EMeshCutterCutDistribution::SingleCut", EditConditionHides))
	float MaxScaleFactor = 1.5f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Distribution", meta=(PCG_Overridable,
		EditCondition="CutDistribution != EMeshCutterCutDistribution::SingleCut", EditConditionHides))
	bool bRandomOrientation = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Distribution", meta=(PCG_Overridable, DisplayName="+/- Roll Range",
		ClampMin="0", ClampMax="180", Units="deg",
		EditCondition="CutDistribution != EMeshCutterCutDistribution::SingleCut && bRandomOrientation", EditConditionHides))
	float RollRange = 180.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Distribution", meta=(PCG_Overridable, DisplayName="+/- Pitch Range",
		ClampMin="0", ClampMax="180", Units="deg",
		EditCondition="CutDistribution != EMeshCutterCutDistribution::SingleCut && bRandomOrientation", EditConditionHides))
	float PitchRange = 180.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Distribution", meta=(PCG_Overridable, DisplayName="+/- Yaw Range",
		ClampMin="0", ClampMax="180", Units="deg",
		EditCondition="CutDistribution != EMeshCutterCutDistribution::SingleCut && bRandomOrientation", EditConditionHides))
	float YawRange = 180.0f;

	/** Drives scatter placement, scale/orientation variation, and Chance To Fracture. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Fracture", meta=(PCG_Overridable))
	int32 RandomSeed = 0;

	/** Probability that each targeted bone is fractured. 1 fractures every target. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Fracture",
		meta=(PCG_Overridable, ClampMin="0.0", ClampMax="1.0"))
	float ChanceToFracture = 1.0f;

	/** Split a fractured piece into separate bones when the cut leaves it in disconnected parts. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Split Islands", meta=(PCG_Overridable))
	bool bSplitIslands = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Split Islands",
		meta=(PCG_Overridable, EditCondition="bSplitIslands", Units="cm", ClampMin="0.0"))
	float CloseVertexDistance = 0.001f;

	/** If > 0, bridge separate islands whose surfaces are within this vertex-to-triangle distance. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Split Islands",
		meta=(PCG_Overridable, EditCondition="bSplitIslands", Units="cm", ClampMin="0.0"))
	float VertexToSurfaceBridgeDistance = 0.0f;

protected:
	virtual UPCGUtilsFractureFactoryData* CreateFractureFactory(
		FPCGContext* InContext, UPCGUtilsGeometryCollectionFactoryData* InFactory) const override;
	virtual TArray<FPCGPinProperties> InputPinProperties() const override;
};
