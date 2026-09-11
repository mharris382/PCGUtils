// Copyright Max Harris

#pragma once

#include "CoreMinimal.h"
#include "Elements/Creation/PrimitiveBuilder/PCGUtilsPrimitiveFittingDetails.h"
#include "Elements/PCGUtilsFractureElementBase.h"
#include "Factories/PCGUtilsFractureFactory.h"
#include "Factories/PCGUtilsFractureNoise.h"
#include "Factories/PCGUtilsFractureProvider.h"
#include "FractureEngineFracturing.h"

#include "PCGPlanarFracture.generated.h"

class UPCGBasePointData;

namespace PCGPlanarFractureConstants
{
	/** Optional: one cutting plane per point, replacing the randomly generated ones. */
	inline const FName PlanesInputPin = TEXT("Planes");
}

/**
 * Shared parameters of the three planar cutters, which differ only in how they place their cutting planes.
 *
 * Every FFractureEngineFracturing plane-based entry point takes the same trailing block of fracture, island
 * split and noise arguments; keeping it in one struct is what stops three near-identical factories from
 * drifting apart, and what makes the next one (Mesh Cutter) a parameter set rather than a port.
 */
USTRUCT(BlueprintType)
struct PCGUTILSFRACTURE_API FPCGPlanarFractureCommonSettings
{
	GENERATED_BODY()

	/** Drives plane placement, angle/offset variation, and Chance To Fracture. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Fracture", meta=(PCG_Overridable))
	int32 RandomSeed = 0;

	/** Probability that each targeted bone is fractured. 1 fractures every target. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Fracture",
		meta=(PCG_Overridable, ClampMin="0.0", ClampMax="1.0"))
	float ChanceToFracture = 1.0f;

	/** Gap left between neighbouring pieces. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Fracture",
		meta=(PCG_Overridable, ClampMin="0.0", Units="cm"))
	float Grout = 0.0f;

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

	/**
	 * Displace the fracture surfaces with noise instead of leaving them planar.
	 *
	 * Off by default, and expensive for a reason that is not obvious: enabling it also subdivides every cut
	 * face down to Surface Resolution, and that is where the triangle count comes from rather than the
	 * displacement itself. Leaving it off costs nothing here - the plane, slice and brick cutters skip the
	 * subdivision entirely when the amplitude is zero, unlike the Voronoi entry point.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Noise", meta=(PCG_Overridable))
	bool bAddSurfaceNoise = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Noise",
		meta=(PCG_Overridable, EditCondition="bAddSurfaceNoise"))
	FPCGFractureNoiseSettings Noise;

	void AddToCrc(FArchiveCrc32& Ar) const;
};

/**
 * Planar fracture: cuts the target bones with flat planes.
 *
 * Without a Planes input the planes are scattered randomly through the target's bounds, which is Fracture
 * Mode's Planar button. Connect points to Planes to place them instead - each point's transform becomes one
 * cutting plane, with the plane's normal taken from the point's Z axis - which makes "cut along this spline" or
 * "cut where these hits landed" ordinary PCG work.
 */
UCLASS(BlueprintType, ClassGroup=(Procedural), Category="PCGUtils|Fracture",
	meta=(Keywords="Planar Plane Cut Slice Fracture Shatter Break GC Geometry Collection Flat Split"))
class PCGUTILSFRACTURE_API UPCGPlanarFractureFactoryData : public UPCGUtilsFractureFactoryData
{
	GENERATED_BODY()

public:
	UPROPERTY()
	int32 NumPlanes = 1;

	/** Explicit cutting planes, already converted into collection space. Empty means generate them. */
	UPROPERTY()
	TArray<FTransform> CutPlaneTransforms;

	UPROPERTY()
	EPCGUtilsPlaneTransformMode TransformMode = EPCGUtilsPlaneTransformMode::Explicit;

	UPROPERTY()
	FPCGUtilsBoundsRelativeTransformDetails BoundsPlacement;

	UPROPERTY()
	FPCGPlanarFractureCommonSettings Common;

	virtual bool Fracture(
		FGeometryCollection& InOutCollection,
		const FDataflowTransformSelection& InTargetBones,
		FPCGContext* InContext,
		FPCGUtilsGeometryCollectionMutationResult& OutMutation) const override;

	virtual FString GetOperationDescription() const override;

protected:
	virtual void AddToCrc(FArchiveCrc32& Ar, bool bFullDataCrc) const override;
};

/** Authoring node for planar fracture. Emits a Fracture operation for GC | Fracture to run. */
UCLASS(BlueprintType, ClassGroup=(Procedural), Category="PCGUtils|Fracture",
	meta=(Keywords="Planar Plane Cut Fracture Shatter Break GC Geometry Collection Flat Split From Points"))
class PCGUTILSFRACTURE_API UPCGPlanarFractureSettings : public UPCGUtilsFractureProviderSettings
{
	GENERATED_BODY()

public:
#if WITH_EDITOR
	virtual FName GetDefaultNodeName() const override { return TEXT("PlanarFracture"); }
	virtual FText GetDefaultNodeTitle() const override;
	virtual FText GetNodeTooltipText() const override;
	virtual FString GetAdditionalTitleInformation() const override;
#endif

	/**
	 * How many planes to scatter through the target's bounds.
	 *
	 * Ignored when points are connected to Planes: those points become the cut pattern, and no random planes
	 * are added to it.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Planar",
		meta=(PCG_Overridable, ClampMin="1", UIMax="100",
			EditCondition="TransformMode == EPCGUtilsPlaneTransformMode::Explicit", EditConditionHides))
	int32 NumPlanes = 1;

	/** Preserve point/random plane placement, or resolve one plane against the target collection bounds. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Planar", meta=(PCG_Overridable))
	EPCGUtilsPlaneTransformMode TransformMode = EPCGUtilsPlaneTransformMode::Explicit;

	/** Builder-style placement of one plane against the target collection's local bounds. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Planar",
		meta=(PCG_Overridable, EditCondition="TransformMode == EPCGUtilsPlaneTransformMode::BoundsRelative", EditConditionHides, ShowOnlyInnerProperties))
	FPCGUtilsBoundsRelativeTransformDetails BoundsPlacement;

	/**
	 * Convert the Planes points from world space into the collection's space before cutting.
	 *
	 * Leave this on. PCG authors points in world space by convention while the collection lives in its source
	 * DynMesh's local space, so without the conversion every plane lands somewhere else entirely. Disable only
	 * when the points were already authored in the collection's space.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Planar", meta=(PCG_Overridable,
		EditCondition="TransformMode == EPCGUtilsPlaneTransformMode::Explicit", EditConditionHides))
	bool bConvertPlanesToLocalSpace = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Fracture", meta=(PCG_Overridable, ShowOnlyInnerProperties))
	FPCGPlanarFractureCommonSettings Common;

	/**
	 * Bounds Relative mode needs no external input at all - the bounds come from the target collection and the
	 * transform from Bounds Placement's own fitting settings - so the Planes pin is only exposed in Explicit mode.
	 */
	virtual bool HasDynamicPins() const override { return true; }

protected:
	virtual UPCGUtilsFractureFactoryData* CreateFractureFactory(
		FPCGContext* InContext, UPCGUtilsGeometryCollectionFactoryData* InFactory) const override;
	virtual TArray<FPCGPinProperties> InputPinProperties() const override;
};

/**
 * Slice fracture: divides the target bones on a regular X/Y/Z grid, with optional angle and offset jitter.
 *
 * The one to reach for when you want *even* pieces - panels, floor tiles, a wall split into courses - rather
 * than the irregular cells Voronoi gives. Setting an axis to 0 leaves it uncut.
 */
UCLASS(BlueprintType, ClassGroup=(Procedural), Category="PCGUtils|Fracture",
	meta=(Keywords="Slice Grid Fracture Shatter Break GC Geometry Collection Even Regular Panels Tiles Split"))
class PCGUTILSFRACTURE_API UPCGSliceFractureFactoryData : public UPCGUtilsFractureFactoryData
{
	GENERATED_BODY()

public:
	/** Cutting planes per axis, not divisions. See the authoring node for the distinction. */
	UPROPERTY()
	int32 SlicesX = 1;

	UPROPERTY()
	int32 SlicesY = 1;

	UPROPERTY()
	int32 SlicesZ = 1;

	UPROPERTY()
	float SliceAngleVariation = 0.0f;

	UPROPERTY()
	float SliceOffsetVariation = 0.0f;

	UPROPERTY()
	FPCGPlanarFractureCommonSettings Common;

	virtual bool Fracture(
		FGeometryCollection& InOutCollection,
		const FDataflowTransformSelection& InTargetBones,
		FPCGContext* InContext,
		FPCGUtilsGeometryCollectionMutationResult& OutMutation) const override;

	virtual FString GetOperationDescription() const override;

protected:
	virtual void AddToCrc(FArchiveCrc32& Ar, bool bFullDataCrc) const override;
};

/** Authoring node for slice fracture. */
UCLASS(BlueprintType, ClassGroup=(Procedural), Category="PCGUtils|Fracture",
	meta=(Keywords="Slice Grid Fracture Shatter Break GC Geometry Collection Even Regular Panels Tiles Split"))
class PCGUTILSFRACTURE_API UPCGSliceFractureSettings : public UPCGUtilsFractureProviderSettings
{
	GENERATED_BODY()

public:
#if WITH_EDITOR
	virtual FName GetDefaultNodeName() const override { return TEXT("SliceFracture"); }
	virtual FText GetDefaultNodeTitle() const override;
	virtual FText GetNodeTooltipText() const override;
	virtual FString GetAdditionalTitleInformation() const override;
#endif

	/**
	 * Cutting planes placed along each axis of the target's bounds - *not* the number of resulting pieces.
	 *
	 * The engine steps the extent by (Slices + 1), so N planes give N+1 divisions on that axis and the piece
	 * count for an axis-aligned grid is (X+1)(Y+1)(Z+1). Set an axis to 0 to leave it uncut; the default of 1
	 * on each axis halves all three, giving 8 pieces.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Slice",
		meta=(PCG_Overridable, ClampMin="0", UIMax="50"))
	int32 SlicesX = 1;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Slice",
		meta=(PCG_Overridable, ClampMin="0", UIMax="50"))
	int32 SlicesY = 1;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Slice",
		meta=(PCG_Overridable, ClampMin="0", UIMax="50"))
	int32 SlicesZ = 1;

	/** Random tilt applied to each slicing plane, in degrees. 0 keeps the grid perfectly axis-aligned. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Slice",
		meta=(PCG_Overridable, ClampMin="0.0", UIMax="90.0", Units="deg"))
	float SliceAngleVariation = 0.0f;

	/** Random displacement applied to each slicing plane along its normal. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Slice",
		meta=(PCG_Overridable, ClampMin="0.0", Units="cm"))
	float SliceOffsetVariation = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Fracture", meta=(PCG_Overridable, ShowOnlyInnerProperties))
	FPCGPlanarFractureCommonSettings Common;

protected:
	virtual UPCGUtilsFractureFactoryData* CreateFractureFactory(
		FPCGContext* InContext, UPCGUtilsGeometryCollectionFactoryData* InFactory) const override;
};

/**
 * Brick fracture: divides the target bones into a masonry bond pattern.
 *
 * Requires a Grout above zero - the bond is defined by the mortar between the bricks, and with no gap the
 * cutter has nothing to cut along.
 */
UCLASS(BlueprintType, ClassGroup=(Procedural), Category="PCGUtils|Fracture",
	meta=(Keywords="Brick Bond Masonry Wall Stretcher Stack English Header Flemish Fracture GC Geometry Collection"))
class PCGUTILSFRACTURE_API UPCGBrickFractureFactoryData : public UPCGUtilsFractureFactoryData
{
	GENERATED_BODY()

public:
	UPROPERTY()
	EFractureBrickBondEnum Bond = EFractureBrickBondEnum::Dataflow_FractureBrickBond_Stretcher;

	UPROPERTY()
	float BrickLength = 194.0f;

	UPROPERTY()
	float BrickHeight = 57.0f;

	UPROPERTY()
	float BrickDepth = 92.0f;

	UPROPERTY()
	FPCGPlanarFractureCommonSettings Common;

	virtual bool Fracture(
		FGeometryCollection& InOutCollection,
		const FDataflowTransformSelection& InTargetBones,
		FPCGContext* InContext,
		FPCGUtilsGeometryCollectionMutationResult& OutMutation) const override;

	virtual FString GetOperationDescription() const override;

protected:
	virtual void AddToCrc(FArchiveCrc32& Ar, bool bFullDataCrc) const override;
};

/** Authoring node for brick fracture. */
UCLASS(BlueprintType, ClassGroup=(Procedural), Category="PCGUtils|Fracture",
	meta=(Keywords="Brick Bond Masonry Wall Stretcher Stack English Header Flemish Fracture GC Geometry Collection"))
class PCGUTILSFRACTURE_API UPCGBrickFractureSettings : public UPCGUtilsFractureProviderSettings
{
	GENERATED_BODY()

public:
	UPCGBrickFractureSettings();

#if WITH_EDITOR
	virtual FName GetDefaultNodeName() const override { return TEXT("BrickFracture"); }
	virtual FText GetDefaultNodeTitle() const override;
	virtual FText GetNodeTooltipText() const override;
	virtual FString GetAdditionalTitleInformation() const override;
#endif

	/** Masonry bond pattern, matching Fracture Mode's Brick tool. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Brick", meta=(PCG_Overridable))
	EFractureBrickBondEnum Bond = EFractureBrickBondEnum::Dataflow_FractureBrickBond_Stretcher;

	/** Defaults are a UK metric brick in centimetres. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Brick",
		meta=(PCG_Overridable, ClampMin="0.01", Units="cm"))
	float BrickLength = 194.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Brick",
		meta=(PCG_Overridable, ClampMin="0.01", Units="cm"))
	float BrickHeight = 57.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Brick",
		meta=(PCG_Overridable, ClampMin="0.01", Units="cm"))
	float BrickDepth = 92.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Fracture", meta=(PCG_Overridable, ShowOnlyInnerProperties))
	FPCGPlanarFractureCommonSettings Common;

protected:
	virtual UPCGUtilsFractureFactoryData* CreateFractureFactory(
		FPCGContext* InContext, UPCGUtilsGeometryCollectionFactoryData* InFactory) const override;
};
