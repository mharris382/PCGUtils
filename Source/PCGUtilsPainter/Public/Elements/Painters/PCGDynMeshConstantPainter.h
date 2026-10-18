// Copyright Max Harris

#pragma once

#include "CoreMinimal.h"
#include "Details/PCGUtilsInputShorthands.h"
#include "Factories/PCGUtilsDynMeshFactoryProvider.h"
#include "Factories/PCGUtilsDynMeshPainterFactory.h"
#include "GeometryScript/GeometryScriptTypes.h"

#include "PCGDynMeshConstantPainter.generated.h"

/** Immutable configuration for a Painter that returns one value for every vertex of a target. */
UCLASS(BlueprintType, ClassGroup=(Procedural), Category="PCGUtils|DynMesh|Painters")
class PCGUTILSPAINTER_API UPCGDynMeshConstantPainterFactoryData
	: public UPCGUtilsDynMeshPainterFactoryData
{
	GENERATED_BODY()

public:
	UPROPERTY()
	EPCGUtilsDynMeshPainterValueType ValueType = EPCGUtilsDynMeshPainterValueType::Scalar;

	UPROPERTY()
	FPCGUtilsInputShorthandFloat Value;

	UPROPERTY()
	FPCGUtilsInputShorthandVector4 Color;

	UPROPERTY()
	FGeometryScriptColorFlags ColorChannels;

protected:
	virtual TSharedPtr<FPCGUtilsDynMeshPainterOperation> CreateOperationInternal() const override;
	virtual void AddToCrc(FArchiveCrc32& Ar, bool bFullDataCrc) const override;
};

/**
 * Paints one value over the whole target: a scalar, or a color that defines a chosen set of channels.
 *
 * The value is a constant, or is read from each target mesh's own @Data domain when the Painter is evaluated.
 * One Painter wired into Paint DynMesh Vertex Color therefore gives every mesh its own value: with
 * Value = @Data.PathColor.R, each mesh is painted with the red channel of the path color it was built from.
 */
UCLASS(BlueprintType, ClassGroup=(Procedural), Category="PCGUtils|DynMesh|Painters",
	meta=(Keywords="DynMesh Painter constant value color colour flat fill uniform attribute data per mesh"))
class PCGUTILSPAINTER_API UPCGDynMeshConstantPainterProviderSettings
	: public UPCGUtilsDynMeshFactoryProviderSettings
{
	GENERATED_BODY()

public:
	UPCGDynMeshConstantPainterProviderSettings();

#if WITH_EDITOR
	virtual FName GetDefaultNodeName() const override { return TEXT("DynMeshConstantPainter"); }
	virtual FText GetDefaultNodeTitle() const override;
	virtual FText GetNodeTooltipText() const override;
	virtual FString GetAdditionalTitleInformation() const override;

	/** Gives the shorthand members their plain override labels (Value, ValueAttribute, Color, ColorAttribute). */
	virtual TArray<FPCGSettingsOverridableParam> GatherOverridableParams() const override;
#endif

	/** Scalar paints one number that the consumer broadcasts to its Write Channels. Color paints the Color Channels below. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Painter", meta=(PCG_Overridable))
	EPCGUtilsDynMeshPainterValueType ValueType = EPCGUtilsDynMeshPainterValueType::Scalar;

	/**
	 * The scalar painted on every vertex. Scalar only.
	 *
	 * @Data mode reads one number per target mesh from that mesh's @Data domain (attribute default "Value"):
	 * Integer32, Integer64, Float or Double, or any attribute narrowed by an extractor, e.g. PathColor.R. The
	 * @Data. prefix is optional on DynMesh data. The attribute is required on every target; a target without it
	 * fails with an error naming the attribute and is not painted. Static Mesh Component targets carry no @Data,
	 * so @Data mode cannot be used with Paint Static Mesh Vertex Colors.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Painter",
		meta=(PCG_Overridable, EditCondition="ValueType == EPCGUtilsDynMeshPainterValueType::Scalar", EditConditionHides))
	FPCGUtilsInputShorthandFloat Value;

	/**
	 * The color painted on every vertex, as linear RGBA. Color only.
	 *
	 * @Data mode reads one Vector4 per target mesh from that mesh's @Data domain (attribute default "PathColor",
	 * the attribute shape-path data writes its path color to). The attribute is required on every target; a
	 * target without it, or with a non-Vector4 value, fails with an error and is not painted.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Painter",
		meta=(PCG_Overridable, EditCondition="ValueType == EPCGUtilsDynMeshPainterValueType::Color", EditConditionHides))
	FPCGUtilsInputShorthandVector4 Color;

	/**
	 * The channels the color defines. The consumer writes only channels that are both defined here and in its
	 * own Write Channels; every other channel keeps its existing value. Color only.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Painter",
		meta=(PCG_Overridable, EditCondition="ValueType == EPCGUtilsDynMeshPainterValueType::Color", EditConditionHides))
	FGeometryScriptColorFlags ColorChannels;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Painter", AdvancedDisplay, meta=(PCG_Overridable))
	int32 Priority = 0;

	virtual FName GetMainOutputPin() const override;
	virtual UPCGUtilsDynMeshFactoryData* CreateFactory(
		FPCGContext* InContext, UPCGUtilsDynMeshFactoryData* InFactory = nullptr) const override;

protected:
	virtual const FPCGDataTypeBaseId& GetFactoryTypeId() const override;
};
