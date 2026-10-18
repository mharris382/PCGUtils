// Copyright Max Harris

#pragma once

#include "CoreMinimal.h"
#include "Details/PCGUtilsInputShorthands.h"
#include "Elements/Painters/PCGDynMeshAxisGradientPainter.h"
#include "Elements/Painters/PCGUtilsPainterDecorator.h"

#include "PCGDynMeshBlurPainter.generated.h"

/** How much a neighbour contributes to the blurred value, by its distance as a fraction of Radius. */
UENUM(BlueprintType)
enum class EPCGUtilsPainterBlurFalloff : uint8
{
	/** Every vertex inside Radius counts equally (box blur). */
	Uniform UMETA(DisplayName="Uniform"),
	/** Weight falls linearly from 1 at the vertex to 0 at Radius. */
	Linear UMETA(DisplayName="Linear"),
	/** Gaussian weight, with Radius at three standard deviations. */
	Gaussian UMETA(DisplayName="Gaussian")
};

UCLASS(BlueprintType, ClassGroup=(Procedural), Category="PCGUtils|DynMesh|Painters")
class PCGUTILSPAINTER_API UPCGDynMeshBlurPainterFactoryData : public UPCGUtilsDynMeshPainterDecoratorFactoryData
{
	GENERATED_BODY()

public:
	UPROPERTY()
	FPCGUtilsInputShorthandDoubleAbs Radius;

	UPROPERTY()
	EPCGUtilsPainterBlurFalloff Falloff = EPCGUtilsPainterBlurFalloff::Gaussian;

	UPROPERTY()
	EPCGUtilsDynMeshPainterCoordinateSpace Space = EPCGUtilsDynMeshPainterCoordinateSpace::World;

	UPROPERTY()
	bool bKeepIslandsSeparate = false;

protected:
	virtual TSharedPtr<FPCGUtilsDynMeshPainterOperation> CreateOperationInternal() const override;
	virtual void AddToCrc(FArchiveCrc32& Ar, bool bFullDataCrc) const override;
};

/**
 * Decorator: blurs the connected Painter by averaging its value over every target vertex within Radius.
 *
 * The connected Painter is evaluated once per vertex over the whole target mesh, then each vertex takes the
 * weighted average of the vertices inside its Radius, measured as straight-line distance (not along the
 * surface). Scalars average to a scalar; colors average channel by channel, each channel over only the
 * neighbours that define it. The outer Write Selection limits which vertices are written but every vertex
 * still contributes to its neighbours.
 */
UCLASS(BlueprintType, ClassGroup=(Procedural), Category="PCGUtils|DynMesh|Painters",
	meta=(Keywords="DynMesh Painter blur radial radius smooth soften average feather decorator spread"))
class PCGUTILSPAINTER_API UPCGDynMeshBlurPainterProviderSettings : public UPCGUtilsDynMeshPainterDecoratorProviderSettings
{
	GENERATED_BODY()

public:
	UPCGDynMeshBlurPainterProviderSettings();

#if WITH_EDITOR
	virtual FName GetDefaultNodeName() const override { return TEXT("DynMeshBlurPainter"); }
	virtual FText GetDefaultNodeTitle() const override;
	virtual FText GetNodeTooltipText() const override;
	virtual FString GetAdditionalTitleInformation() const override;

	/** Gives the shorthand member its plain override labels (Radius, RadiusAttribute). */
	virtual TArray<FPCGSettingsOverridableParam> GatherOverridableParams() const override;
#endif

	/**
	 * The blur radius, in the units of Space (centimetres in World space). 0 leaves the Painter unchanged.
	 * Cost grows with the number of vertices inside one radius, so very large radii on dense meshes are slow.
	 *
	 * @Data mode reads one non-negative number per target mesh from that mesh's @Data domain (attribute default
	 * "BlurRadius"): Integer32, Integer64, Float or Double, or an attribute narrowed by an extractor. The @Data.
	 * prefix is optional on DynMesh data. The attribute is required on every target; a target without it, or with
	 * a negative value, fails with an error and is not painted. Static Mesh Component targets carry no @Data.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Painter", meta=(PCG_Overridable))
	FPCGUtilsInputShorthandDoubleAbs Radius;

	/** How a neighbour's weight falls off with distance. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Painter", meta=(PCG_Overridable))
	EPCGUtilsPainterBlurFalloff Falloff = EPCGUtilsPainterBlurFalloff::Gaussian;

	/**
	 * Where Radius is measured. World measures in world centimetres, so the blur looks the same on scaled
	 * targets; DynMesh Local measures in the mesh's own coordinates.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Painter", meta=(PCG_Overridable))
	EPCGUtilsDynMeshPainterCoordinateSpace Space = EPCGUtilsDynMeshPainterCoordinateSpace::World;

	/**
	 * Only average over vertices of the same mesh island (connected component), so separate pieces that sit
	 * close together never bleed into each other. Pairs with Random Value By Mesh Island.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Painter", meta=(PCG_Overridable))
	bool bKeepIslandsSeparate = false;

	virtual UPCGUtilsDynMeshFactoryData* CreateFactory(
		FPCGContext* InContext, UPCGUtilsDynMeshFactoryData* InFactory = nullptr) const override;
};
