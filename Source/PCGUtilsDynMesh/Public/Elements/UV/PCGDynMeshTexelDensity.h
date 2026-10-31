// Copyright Max Harris

#pragma once

#include "CoreMinimal.h"
#include "Details/PCGUtilsInputShorthands.h"
#include "Elements/UV/PCGDynamicMeshUVProcessBase.h"

#include "PCGUtilsDynMeshNodeColor.h"
#include "PCGDynMeshTexelDensity.generated.h"

UENUM(BlueprintType)
enum class EPCGUtilsDynMeshTexelDensityMode : uint8
{
	EachIsland UMETA(DisplayName="Each Island", ToolTip="Scale every island on its own to the target density, about its own centre. Islands may then overlap; pack afterwards if they must not."),
	WholeRegion UMETA(DisplayName="Whole Region", ToolTip="Scale all the islands by one factor, about the centre of their UV bounds, so their average density is the target. Relative sizes and the layout are kept.")
};

/**
 * Scales UV islands so a texture of Texture Resolution pixels gets Texel Density pixels per metre of surface: the
 * usual way to keep tiling textures and lightmap resolution even across meshes. Surface area is measured in the
 * mesh's own units (cm), ignoring the target actor's scale.
 *
 * Texel Density and Texture Resolution are input shorthands: a constant, or one value per input from its @Data.
 */
UCLASS(BlueprintType, ClassGroup=(Procedural), Category="PCGUtils|DynMesh|UV",
	meta=(Keywords="DynMesh mesh UV texel density pixels per meter resolution scale tiling normalize selection selector"))
class PCGUTILSDYNMESH_API UPCGDynMeshTexelDensitySettings : public UPCGDynamicMeshUVProcessBaseSettings
{
	GENERATED_BODY()

public:
	UPCGDynMeshTexelDensitySettings();

#if WITH_EDITOR
	virtual FName GetDefaultNodeName() const override { return TEXT("DynMeshTexelDensity"); }
	virtual FText GetDefaultNodeTitle() const override;
	virtual FText GetNodeTooltipText() const override;
	virtual FString GetAdditionalTitleInformation() const override;
	virtual FLinearColor GetNodeTitleColor() const override { return PCGUtilsDynMeshNodeColor::Title; }
#endif

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="UV", meta=(PCG_Overridable, DisplayPriority=0))
	EPCGUtilsDynMeshTexelDensityMode Mode = EPCGUtilsDynMeshTexelDensityMode::EachIsland;

	/**
	 * Target pixels per metre of surface.
	 *
	 * @Data mode reads one number (Integer32, Integer64, Float or Double, above 0) per input from its @Data domain
	 * (default TexelDensity). Required on every input; the constant is never used as a fallback.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Texel Density", meta=(PCG_Overridable))
	FPCGUtilsInputShorthandDoubleAbs TexelDensity;

	/**
	 * Width in pixels of the texture the UVs are for.
	 *
	 * @Data mode reads one integer (Integer32 or Integer64, above 0) per input from its @Data domain (default
	 * TextureResolution). Required on every input; the constant is never used as a fallback.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Texel Density", meta=(PCG_Overridable))
	FPCGUtilsInputShorthandInteger32Abs TextureResolution;

	virtual TSharedPtr<const FPCGUtilsDynMeshProcessOperation> CreateProcessOperation(FPCGContext* InContext) const override;
	virtual bool SupportsDeferredBuilderProcessing() const override { return TexelDensity.IsConstant() && TextureResolution.IsConstant(); }

protected:
	virtual FPCGElementPtr CreateElement() const override;
};

class PCGUTILSDYNMESH_API FPCGDynMeshTexelDensityElement final : public FPCGDynamicMeshUVProcessBaseElement
{
};

class PCGUTILSDYNMESH_API FPCGUtilsDynMeshTexelDensityOperation final : public FPCGUtilsDynMeshUVProcessOperation
{
public:
	EPCGUtilsDynMeshTexelDensityMode Mode = EPCGUtilsDynMeshTexelDensityMode::EachIsland;
	FPCGUtilsInputShorthandDoubleAbs TexelDensity;
	FPCGUtilsInputShorthandInteger32Abs TextureResolution;

protected:
	virtual bool ProcessUVs(const FPCGUtilsDynMeshProcessInvocation& Invocation, UE::Geometry::FDynamicMesh3& Mesh,
		UE::Geometry::FDynamicMeshUVOverlay& UVOverlay, TArrayView<const int32> TriangleIDs) const override;
};
