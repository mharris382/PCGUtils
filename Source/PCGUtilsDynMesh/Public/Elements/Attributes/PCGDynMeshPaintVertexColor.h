// Copyright Max Harris

#pragma once

#include "CoreMinimal.h"
#include "Elements/PCGUtilsDynMeshProcessBase.h"
#include "Factories/PCGUtilsDynMeshPainterFactory.h"
#include "GeometryScript/GeometryScriptTypes.h"

#include "PCGDynMeshPaintVertexColor.generated.h"

UENUM(BlueprintType)
enum class EPCGUtilsDynMeshPainterBaseColorMode : uint8
{
	Existing UMETA(DisplayName="Existing Vertex Color"),
	Constant UMETA(DisplayName="Constant Base Color")
};

/** Resolves one scalar-or-color Painter into requested DynMesh vertex-color channels. */
UCLASS(BlueprintType, ClassGroup=(Procedural), Category="PCGUtils|DynMesh|Attributes")
class PCGUTILSDYNMESH_API UPCGDynMeshPaintVertexColorSettings
	: public UPCGUtilsDynMeshProcessBaseSettings
{
	GENERATED_BODY()

public:
#if WITH_EDITOR
	virtual FName GetDefaultNodeName() const override { return TEXT("DynMeshPaintVertexColor"); }
	virtual FText GetDefaultNodeTitle() const override;
	virtual FText GetNodeTooltipText() const override;
	virtual FLinearColor GetNodeTitleColor() const override
	{
		return FLinearColor(0.413f, 0.25f, 1.0f, 1.0f);
	}
#endif

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Color", meta=(PCG_Overridable))
	EPCGUtilsDynMeshPainterBaseColorMode BaseColorMode =
		EPCGUtilsDynMeshPainterBaseColorMode::Existing;

	/** Used for unconnected channels, and as the fallback when Existing is selected but no color exists. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Color", meta=(PCG_Overridable,
		EditCondition="BaseColorMode==EPCGUtilsDynMeshPainterBaseColorMode::Constant", EditConditionHides))
	FLinearColor ConstantBaseColor = FLinearColor(0.0f, 0.0f, 0.0f, 1.0f);

	/** Destination channels this node is allowed to modify. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Color", meta=(PCG_Overridable, ShowOnlyInnerProperties))
	FGeometryScriptColorFlags WriteChannels;

	/** Mesh positions are actor-local by convention; enable this to populate world-space Painter samples. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Coordinate Space", meta=(PCG_Overridable))
	bool bMeshIsActorLocal = true;

	virtual bool GetRequiredSelectionDomain(
		UE::Geometry::EGeometryElementType& OutElementType) const override;
	virtual bool HasDynamicPins() const override { return true; }
	virtual FPCGDataTypeIdentifier GetCurrentPinTypesID(const UPCGPin* InPin) const override;
	virtual TSharedPtr<const FPCGUtilsDynMeshProcessOperation> CreateProcessOperation(
		FPCGContext* InContext) const override;

protected:
	virtual FName GetMainInputPinLabel() const override;
	virtual TArray<FPCGPinProperties> InputPinProperties() const override;
	virtual FPCGElementPtr CreateElement() const override;
};

class PCGUTILSDYNMESH_API FPCGDynMeshPaintVertexColorElement
	: public FPCGUtilsDynMeshProcessBaseElement
{
};

class PCGUTILSDYNMESH_API FPCGUtilsDynMeshPaintVertexColorOperation final
	: public FPCGUtilsDynMeshProcessOperation
{
public:
	const UPCGUtilsDynMeshPainterFactoryData* Painter = nullptr;
	EPCGUtilsDynMeshPainterBaseColorMode BaseColorMode =
		EPCGUtilsDynMeshPainterBaseColorMode::Existing;
	FLinearColor ConstantBaseColor = FLinearColor(0.0f, 0.0f, 0.0f, 1.0f);
	FGeometryScriptColorFlags WriteChannels;
	bool bMeshIsActorLocal = true;

	virtual bool Execute(
		const FPCGUtilsDynMeshProcessInvocation& Invocation,
		FPCGUtilsDynMeshProcessOutcome& OutOutcome) const override;
};
