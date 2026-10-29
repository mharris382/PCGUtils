// Copyright Max Harris

#pragma once

#include "CoreMinimal.h"
#include "Elements/PCGUtilsDynMeshProcessBase.h"
#include "Factories/PCGUtilsDynMeshPainterFactory.h"
#include "GeometryScript/GeometryScriptTypes.h"

namespace UE::Geometry { class FDynamicMesh3; }
class UPCGUtilsDynMeshSelectionFactoryData;

#include "PCGUtilsDynMeshNodeColor.h"
#include "PCGDynMeshPaintVertexColor.generated.h"

UENUM(BlueprintType)
enum class EPCGUtilsDynMeshPainterBaseColorMode : uint8
{
	Existing UMETA(DisplayName="Existing Vertex Color"),
	Constant UMETA(DisplayName="Constant Base Color")
};

/**
 * Resolves one scalar-or-color Painter into requested DynMesh vertex-color channels. With Paint LODs From Highest
 * Detail, inputs that are LODs of one mesh are painted once, on the most detailed, and the rest receive a copy.
 */
UCLASS(BlueprintType, ClassGroup=(Procedural), Category="PCGUtils|DynMesh|Attributes", meta=(Keywords="DynMesh mesh paint painter vertex color colour selection selector"))
class PCGUTILSPAINTER_API UPCGDynMeshPaintVertexColorSettings
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
		return PCGUtilsDynMeshNodeColor::Title;
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

	/**
	 * Treats inputs that are LODs of one mesh as a group: only the most detailed LOD is painted, and its colors
	 * are copied onto the group's other LODs by closest-surface projection. Every LOD then shows the same paint,
	 * so a random-per-island tint does not jump when the LOD switches. Inputs are grouped by LOD Source
	 * Attribute and ranked by LOD Attribute; an input missing either is painted on its own, with a warning.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="LODs", meta=(PCG_Overridable, DisplayName="Paint LODs From Highest Detail"))
	bool bPaintLODsFromHighest = false;

	/**
	 * Required with Paint LODs From Highest Detail. @Data attribute (int32 or int64) holding each input's LOD
	 * number. The lowest number in a group (LOD 0) is the one painted. DynMesh | From Static Mesh writes it.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="LODs", meta=(PCG_Overridable, EditCondition="bPaintLODsFromHighest"))
	FName LODAttribute = TEXT("LOD");

	/**
	 * Required with Paint LODs From Highest Detail. @Data attribute naming the mesh each input is a LOD of: a
	 * soft object path, string, name or integer. Inputs with equal values are one group. DynMesh | From Static
	 * Mesh writes it.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="LODs", meta=(PCG_Overridable, EditCondition="bPaintLODsFromHighest"))
	FName LODSourceAttribute = TEXT("SourceMesh");

	/**
	 * Each island of a lower LOD takes its colors from the one island of the painted LOD most of its vertices
	 * land on, so a leaf never picks up its neighbour's color where the two pass close together. Turn off to
	 * project every vertex to the nearest painted surface regardless of island.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="LODs", meta=(PCG_Overridable, EditCondition="bPaintLODsFromHighest"))
	bool bKeepIslandsTogether = true;

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

class PCGUTILSPAINTER_API FPCGDynMeshPaintVertexColorElement
	: public FPCGUtilsDynMeshProcessBaseElement
{
};

class PCGUTILSPAINTER_API FPCGUtilsDynMeshPaintVertexColorOperation final
	: public FPCGUtilsDynMeshProcessOperation
{
public:
	const UPCGUtilsDynMeshPainterFactoryData* Painter = nullptr;
	EPCGUtilsDynMeshPainterBaseColorMode BaseColorMode =
		EPCGUtilsDynMeshPainterBaseColorMode::Existing;
	FLinearColor ConstantBaseColor = FLinearColor(0.0f, 0.0f, 0.0f, 1.0f);
	FGeometryScriptColorFlags WriteChannels;
	bool bMeshIsActorLocal = true;

	/** Paint LODs From Highest Detail. Filled by the settings while the node runs; empty otherwise. */
	struct FLODPlan
	{
		/** Per concrete input index: the input index whose paint it receives, or INDEX_NONE to paint itself. */
		TArray<int32> PrimaryOfInput;
		/** Per concrete input index: the input data, to paint a primary that has not been reached yet. */
		TArray<const UPCGData*> Inputs;
		/** Per concrete input index: its LOD number and group, for messages. */
		TArray<int32> LODOfInput;
		TArray<FString> GroupOfInput;
	};
	FLODPlan LODPlan;
	bool bKeepIslandsTogether = true;
	const UPCGUtilsDynMeshSelectionFactoryData* Selector = nullptr;
	FPCGUtilsDynMeshProcessSelectionPolicy SelectionPolicy;

	virtual bool Execute(
		const FPCGUtilsDynMeshProcessInvocation& Invocation,
		FPCGUtilsDynMeshProcessOutcome& OutOutcome) const override;

private:
	/** Paints MeshData in place. */
	bool PaintInPlace(UPCGDynamicMeshData* MeshData, const UPCGDynamicMeshSelectionData* SelectionData,
		const UPCGDynamicMeshData* SourceMeshData, FPCGContext* Context, int32 InputIndex, int32 InputCount) const;

	/** The painted mesh of a group's primary input, painting it now if no invocation has yet. Null on failure. */
	const UE::Geometry::FDynamicMesh3* GetPaintedPrimary(int32 PrimaryIndex, FPCGContext* Context, int32 InputCount) const;

	/** Painted primaries, by input index. The process element runs on the game thread only. */
	mutable TMap<int32, TSharedPtr<UE::Geometry::FDynamicMesh3>> PaintedPrimaries;
	mutable TSet<int32> FailedPrimaries;
};
