// Copyright Max Harris

#pragma once

#include "CoreMinimal.h"
#include "Elements/PCGUtilsFractureElementBase.h"
#include "Factories/PCGUtilsFractureFactory.h"
#include "Factories/PCGUtilsFractureNoise.h"

#include "PCGVoronoiFracture.generated.h"

namespace PCGVoronoiFractureConstants
{
	inline const FName SitesInputPin = TEXT("Sites");
}

/**
 * Voronoi fracture whose cells are placed by PCG points - the Houdini-style primitive, where an arbitrary
 * scatter drives the fracture pattern. Use Uniform Voronoi Fracture when you just want N evenly-sized pieces
 * and do not care where the cells land.
 *
 * Sites are resolved into the collection's space when the factory is authored.
 */
UCLASS(BlueprintType, ClassGroup=(Procedural), Category="PCGUtils|Fracture")
class PCGUTILSFRACTURE_API UPCGVoronoiFractureFactoryData : public UPCGUtilsFractureFactoryData
{
	GENERATED_BODY()

public:
	/** Voronoi cell centres, already converted into the Geometry Collection's own space. */
	TArray<FVector> Sites;

	UPROPERTY()
	int32 RandomSeed = 0;

	UPROPERTY()
	float ChanceToFracture = 1.0f;

	UPROPERTY()
	float Grout = 0.0f;

	UPROPERTY()
	bool bSplitIslands = false;

	UPROPERTY()
	bool bAddSurfaceNoise = false;

	/**
	 * The world/local transform that was resolved when the sites were captured, and which interpretation was
	 * applied. Kept so that if the sites turn out to miss the geometry entirely, the node can check whether
	 * the *other* interpretation would have hit it and say so outright, rather than printing two boxes and
	 * leaving the user to work it out.
	 */
	UPROPERTY()
	FTransform SiteSpaceTransform = FTransform::Identity;

	UPROPERTY()
	bool bSitesWereTreatedAsWorldSpace = true;

	UPROPERTY()
	FPCGFractureNoiseSettings Noise;

	virtual bool Fracture(
		FGeometryCollection& InOutCollection,
		const FDataflowTransformSelection& InTargetBones,
		FPCGContext* InContext) const override;

	virtual FString GetOperationDescription() const override;

protected:
	virtual void AddToCrc(FArchiveCrc32& Ar, bool bFullDataCrc) const override;
};

/**
 * Authoring node for point-driven Voronoi fracture. Emits a Fracture operation - it does not fracture anything
 * itself, and carries no target selection; Fracture GC decides which bones to apply it to.
 */
UCLASS(BlueprintType, ClassGroup=(Procedural), Category="PCGUtils|Fracture",
	meta=(Keywords="Voronoi Sites Points Fracture Shatter Cells Scatter GC Geometry Collection"))
class PCGUTILSFRACTURE_API UPCGVoronoiFractureSettings : public UPCGUtilsGCFactoryProviderSettings
{
	GENERATED_BODY()

public:
#if WITH_EDITOR
	virtual FName GetDefaultNodeName() const override { return TEXT("VoronoiFractureFromPoints"); }
	virtual FText GetDefaultNodeTitle() const override;
	virtual FText GetNodeTooltipText() const override;
	virtual FString GetAdditionalTitleInformation() const override;
#endif

	/**
	 * Treat the site points as world-space and convert them into the collection's local space using the PCG
	 * target actor's transform. Disable only if the points are already authored in the mesh's own space.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Space", meta=(PCG_Overridable))
	bool bSitesAreWorldSpace = true;

	/** Drives which bones survive Chance To Fracture, and jitters the cut geometry. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Fracture", meta=(PCG_Overridable))
	int32 RandomSeed = 0;

	/** Probability that each targeted bone is actually fractured. 1 fractures every target. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Fracture",
		meta=(PCG_Overridable, ClampMin="0.0", ClampMax="1.0"))
	float ChanceToFracture = 1.0f;

	/** Gap left between neighbouring pieces. Larger values shrink each piece away from its neighbours. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Fracture",
		meta=(PCG_Overridable, ClampMin="0.0", Units="cm"))
	float Grout = 0.0f;

	/** Split a fractured piece into separate bones when the cut leaves it in disconnected parts. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Fracture", meta=(PCG_Overridable))
	bool bSplitIslands = true;

	/**
	 * Displace the fracture surfaces with noise instead of leaving them planar.
	 *
	 * Off by default, and worth leaving off unless you want the look: Unreal's fracture entry point has no way
	 * to disable noise, so enabling it also subdivides every cut face down to Surface Resolution. That is
	 * where the triangle count comes from, not the displacement.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Fracture", meta=(PCG_Overridable))
	bool bAddSurfaceNoise = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Fracture", AdvancedDisplay,
		meta=(EditCondition="bAddSurfaceNoise"))
	FPCGFractureNoiseSettings Noise;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Fracture", AdvancedDisplay, meta=(PCG_Overridable))
	int32 Priority = 0;

	virtual FName GetMainOutputPin() const override;
	virtual UPCGUtilsGCFactoryData* CreateFactory(
		FPCGContext* InContext, UPCGUtilsGCFactoryData* InFactory = nullptr) const override;

protected:
	virtual const FPCGDataTypeBaseId& GetFactoryTypeId() const override;
	virtual TArray<FPCGPinProperties> InputPinProperties() const override;
};
