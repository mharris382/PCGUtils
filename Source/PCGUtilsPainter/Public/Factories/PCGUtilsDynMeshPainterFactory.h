// Copyright Max Harris

#pragma once

#include "CoreMinimal.h"
#include "Factories/PCGUtilsDynMeshFactoryData.h"
#include "Factories/PCGUtilsDynMeshOperation.h"

#include "PCGUtilsDynMeshPainterFactory.generated.h"

class UPCGDynamicMeshData;

namespace UE::Geometry
{
	class FDynamicMesh3;
}

namespace PCGUtilsDynMeshPainterConstants
{
	inline const FName OutputPin = TEXT("Painter");
}

UENUM(BlueprintType)
enum class EPCGUtilsDynMeshPainterValueType : uint8
{
	Scalar,
	Color
};

enum class EPCGUtilsDynMeshPainterColorChannel : uint8
{
	None = 0,
	Red = 1 << 0,
	Green = 1 << 1,
	Blue = 1 << 2,
	Alpha = 1 << 3,
	All = 0x0F
};
ENUM_CLASS_FLAGS(EPCGUtilsDynMeshPainterColorChannel)

/** A Painter result is either an untargeted scalar or a color with explicit valid channels. */
struct PCGUTILSPAINTER_API FPCGUtilsDynMeshPainterValue
{
	EPCGUtilsDynMeshPainterValueType Type = EPCGUtilsDynMeshPainterValueType::Scalar;
	float Scalar = 0.0f;
	FVector4f Color = FVector4f::Zero();
	EPCGUtilsDynMeshPainterColorChannel ColorChannels =
		EPCGUtilsDynMeshPainterColorChannel::None;

	static FPCGUtilsDynMeshPainterValue MakeScalar(float InScalar)
	{
		FPCGUtilsDynMeshPainterValue Value;
		Value.Scalar = InScalar;
		return Value;
	}

	static FPCGUtilsDynMeshPainterValue MakeColor(
		const FVector4f& InColor,
		EPCGUtilsDynMeshPainterColorChannel InChannels =
			EPCGUtilsDynMeshPainterColorChannel::All)
	{
		FPCGUtilsDynMeshPainterValue Value;
		Value.Type = EPCGUtilsDynMeshPainterValueType::Color;
		Value.Color = InColor;
		Value.ColorChannels = InChannels;
		return Value;
	}
};

namespace PCGUtilsDynMeshPainters
{
	/**
	 * Applies a Painter value to requested destination channels. Scalars broadcast to every requested channel;
	 * colors write only the intersection of requested and explicitly valid color channels.
	 * Returns the channels actually written.
	 */
	PCGUTILSPAINTER_API EPCGUtilsDynMeshPainterColorChannel ResolveValueToColor(
		const FPCGUtilsDynMeshPainterValue& Value,
		EPCGUtilsDynMeshPainterColorChannel RequestedChannels,
		FVector4f& InOutColor);
}

/** One destination-agnostic field sample on a DynMesh. */
struct PCGUTILSPAINTER_API FPCGUtilsDynMeshPainterSample
{
	FVector LocalPosition = FVector::ZeroVector;
	FVector WorldPosition = FVector::ZeroVector;
	FVector LocalNormal = FVector::UpVector;
	FVector WorldNormal = FVector::UpVector;
	int32 VertexID = INDEX_NONE;
};

/**
 * Read-only state shared by a complete Painter expression tree, for the whole Initialize -> Prepare -> Evaluate
 * lifecycle.
 *
 * `Mesh` is the **canonical Dynamic Mesh** the Painter graph runs against and is now ALWAYS present, whatever
 * the original target domain: for a native Dynamic Mesh target it is that mesh; for a Static Mesh Component
 * target it is the transient connectivity-preserving LOD0 representation. Topology-dependent Painters
 * (`Random Value by Mesh Island`, `Selection Painter Switch`) read it directly during `Prepare()` and work on
 * every target automatically.
 *
 * `MeshData` is a `UPCGDynamicMeshData` view of `Mesh` — the real graph data for a native Dynamic Mesh target,
 * a transient wrapper for other targets — provided so operations can reuse Geometry Script utilities (e.g.
 * DynMesh selection domain conversion). It is non-null whenever `Mesh` is.
 *
 * `bIsNativeDynMeshTarget` is the narrower question "did this target originate as a graph `UPCGDynamicMeshData`
 * that participates in DynMesh<->Points dataset pairing?". Only `Painter by Vertex ID` needs that; it rejects a
 * target where this is false.
 */
struct PCGUTILSPAINTER_API FPCGUtilsDynMeshPainterEvaluationContext
{
	FPCGUtilsDynMeshPainterEvaluationContext(
		const UPCGDynamicMeshData* InMeshData,
		const UE::Geometry::FDynamicMesh3& InMesh,
		const FTransform& InLocalToWorld,
		int32 InDataSetIndex = 0,
		int32 InDataSetCount = 1,
		bool bInIsNativeDynMeshTarget = true)
		: MeshData(InMeshData), Mesh(&InMesh), LocalToWorld(InLocalToWorld),
		  bIsNativeDynMeshTarget(bInIsNativeDynMeshTarget),
		  DataSetIndex(InDataSetIndex), DataSetCount(InDataSetCount)
	{
	}

	const UPCGDynamicMeshData* MeshData = nullptr;
	const UE::Geometry::FDynamicMesh3* Mesh = nullptr;
	FTransform LocalToWorld = FTransform::Identity;
	bool bIsNativeDynMeshTarget = true;
	/** Pairing coordinates for Painters backed by ordered per-DynMesh external datasets. */
	int32 DataSetIndex = 0;
	int32 DataSetCount = 1;
};

USTRUCT(meta=(PCG_DataTypeDisplayName="DynMesh Painter"))
struct FPCGUtilsDynMeshPainterFactoryDataTypeInfo : public FPCGUtilsDynMeshFactoryDataTypeInfo
{
	GENERATED_BODY()

	PCG_DECLARE_TYPE_INFO(PCGUTILSPAINTER_API);
};

class FPCGUtilsDynMeshPainterOperation;

/** Immutable scalar/color field configuration transported through PCG pins. */
UCLASS(Abstract, BlueprintType, ClassGroup=(Procedural), Category="PCGUtils|DynMesh|Painters")
class PCGUTILSPAINTER_API UPCGUtilsDynMeshPainterFactoryData : public UPCGUtilsDynMeshFactoryData
{
	GENERATED_BODY()

public:
	PCG_ASSIGN_TYPE_INFO(FPCGUtilsDynMeshPainterFactoryDataTypeInfo)

	/** Creates and context-binds a runtime operation. Common initialization cannot be bypassed. */
	TSharedPtr<FPCGUtilsDynMeshPainterOperation> CreateOperation(FPCGContext* InContext) const;

protected:
	virtual TSharedPtr<FPCGUtilsDynMeshPainterOperation> CreateOperationInternal() const;
};

/**
 * Runtime scalar-or-color field evaluated for mesh samples by a Painter consumer.
 *
 * Lifecycle (driven once per operation instance by the Painter execution layer, per root operation and target):
 *
 *   1. `Initialize(Context)` — validate configuration, resolve pin inputs, and create + `Initialize` any child
 *      operations. Cheap. A composite must build its children here.
 *   2. `Prepare(Context)` — the optional one-off, mesh-wide topology pass a factory needs before per-vertex
 *      evaluation (connected components, one selector evaluation, a cached vertex lookup, ...). The default is a
 *      no-op, so existing stateless Painters are unaffected. A composite MUST call `Prepare` on its children.
 *      `Context.Mesh` (the canonical Dynamic Mesh) is always valid here.
 *   3. `Evaluate(Sample)` — called for every (possibly parallel) vertex sample. After `Prepare` returns true the
 *      operation MUST be immutable: `Evaluate` is `const` and must be safe for concurrent calls.
 *
 * A failed `Initialize` or `Prepare` returns false (after logging on the graph) and the whole Painter is
 * discarded, exactly as an invalid factory input already is.
 */
class PCGUTILSPAINTER_API FPCGUtilsDynMeshPainterOperation : public FPCGUtilsDynMeshOperation
{
public:
	virtual bool Initialize(const FPCGUtilsDynMeshPainterEvaluationContext& InPainterContext);

	/** One-off mesh-wide preparation before any Evaluate(). Default: no-op. Composites must prepare children. */
	virtual bool Prepare(const FPCGUtilsDynMeshPainterEvaluationContext& InPainterContext) { return true; }

	virtual EPCGUtilsDynMeshPainterValueType GetOutputType() const = 0;
	virtual FPCGUtilsDynMeshPainterValue Evaluate(
		const FPCGUtilsDynMeshPainterSample& Sample) const = 0;

protected:
	const FPCGUtilsDynMeshPainterEvaluationContext* PainterContext = nullptr;
};

namespace PCGUtilsDynMeshFactories
{
	PCGUTILSPAINTER_API const TSet<FPCGDataTypeBaseId>& GetPainterFactoryTypes();
}

namespace PCGUtilsDynMeshPainterFactories
{
	/** Resolves zero or one Painter from a pin; required pins report a graph error when empty. */
	PCGUTILSPAINTER_API bool GetSinglePainter(
		FPCGContext* Context,
		FName PinLabel,
		const UPCGUtilsDynMeshPainterFactoryData*& OutPainter,
		bool bRequired);
}
