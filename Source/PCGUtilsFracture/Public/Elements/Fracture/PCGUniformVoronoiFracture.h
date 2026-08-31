// Copyright Max Harris

#pragma once

#include "CoreMinimal.h"
#include "Elements/PCGUtilsFractureElementBase.h"
#include "Factories/PCGUtilsFractureFactory.h"
#include "Factories/PCGUtilsFractureNoise.h"

#include "PCGUniformVoronoiFracture.generated.h"

/**
 * Uniform Voronoi fracture: scatters its own sites through the bounds of whatever it is fracturing.
 *
 * This is the direct equivalent of Fracture Mode's Uniform button, and the one to reach for when you just want
 * a solid broken into N pieces. Because the sites are generated from the collection's own bounds there is no
 * point input and no coordinate space to get wrong.
 *
 * Use Voronoi Fracture From Points instead when you want to *place* the cells - driving them from a PCG or
 * PCGEx scatter, a density field, or any other point source.
 */
UCLASS(BlueprintType, ClassGroup=(Procedural), Category="PCGUtils|Fracture",
	meta=(Keywords="Uniform Voronoi Fracture Shatter Break Cells Chunks GC Geometry Collection"))
class PCGUTILSFRACTURE_API UPCGUniformVoronoiFractureFactoryData : public UPCGUtilsFractureFactoryData
{
	GENERATED_BODY()

public:
	UPROPERTY()
	int32 MinVoronoiSites = 20;

	UPROPERTY()
	int32 MaxVoronoiSites = 20;

	UPROPERTY()
	int32 RandomSeed = 0;

	UPROPERTY()
	float ChanceToFracture = 1.0f;

	UPROPERTY()
	float Grout = 0.0f;

	UPROPERTY()
	bool bGroupFracture = true;

	UPROPERTY()
	bool bSplitIslands = true;

	UPROPERTY()
	float CloseVertexDistance = 0.001f;

	UPROPERTY()
	float VertexToSurfaceBridgeDistance = 0.0f;

	UPROPERTY()
	bool bAddSurfaceNoise = false;

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

/** Authoring node for Uniform Voronoi fracture. Emits a Fracture operation for Fracture GC to run. */
UCLASS(BlueprintType, ClassGroup=(Procedural), Category="PCGUtils|Fracture",
	meta=(Keywords="Uniform Voronoi Fracture Shatter Break Cells Chunks GC Geometry Collection"))
class PCGUTILSFRACTURE_API UPCGUniformVoronoiFractureSettings : public UPCGUtilsGCFactoryProviderSettings
{
	GENERATED_BODY()

public:
#if WITH_EDITOR
	virtual FName GetDefaultNodeName() const override { return TEXT("UniformVoronoiFracture"); }
	virtual FText GetDefaultNodeTitle() const override;
	virtual FText GetNodeTooltipText() const override;
	virtual FString GetAdditionalTitleInformation() const override;
#endif

	/**
	 * Fewest sites to scatter. The count for each diagram is chosen at random between Min and Max, so setting
	 * both to the same value gives an exact piece count.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Uniform Voronoi",
		meta=(PCG_Overridable, ClampMin="1", UIMax="5000"))
	int32 MinVoronoiSites = 20;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Uniform Voronoi",
		meta=(PCG_Overridable, ClampMin="1", UIMax="5000"))
	int32 MaxVoronoiSites = 20;

	/** Drives site placement, the piece count within the Min/Max range, and Chance To Fracture. */
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

	/**
	 * Generate one fracture pattern spanning every targeted bone, rather than an independent pattern per bone.
	 * With this off, each piece is fractured separately and gets its own site count and seed.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Fracture", meta=(PCG_Overridable))
	bool bGroupFracture = true;

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
	 * Off by default, and expensive for a reason that is not obvious: Unreal's fracture entry points cannot
	 * disable noise, so enabling it also subdivides every cut face down to Surface Resolution. That is where
	 * the triangle count comes from, not the displacement.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Noise", meta=(PCG_Overridable))
	bool bAddSurfaceNoise = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Noise", meta=(EditCondition="bAddSurfaceNoise"))
	FPCGFractureNoiseSettings Noise;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Fracture", AdvancedDisplay, meta=(PCG_Overridable))
	int32 Priority = 0;

	virtual FName GetMainOutputPin() const override;
	virtual UPCGUtilsGCFactoryData* CreateFactory(
		FPCGContext* InContext, UPCGUtilsGCFactoryData* InFactory = nullptr) const override;

protected:
	virtual const FPCGDataTypeBaseId& GetFactoryTypeId() const override;
};
