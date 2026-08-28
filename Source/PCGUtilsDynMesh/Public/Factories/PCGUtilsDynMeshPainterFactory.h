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
struct PCGUTILSDYNMESH_API FPCGUtilsDynMeshPainterValue
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
	PCGUTILSDYNMESH_API EPCGUtilsDynMeshPainterColorChannel ResolveValueToColor(
		const FPCGUtilsDynMeshPainterValue& Value,
		EPCGUtilsDynMeshPainterColorChannel RequestedChannels,
		FVector4f& InOutColor);
}

/** One destination-agnostic field sample on a DynMesh. */
struct PCGUTILSDYNMESH_API FPCGUtilsDynMeshPainterSample
{
	FVector LocalPosition = FVector::ZeroVector;
	FVector WorldPosition = FVector::ZeroVector;
	FVector LocalNormal = FVector::UpVector;
	FVector WorldNormal = FVector::UpVector;
	int32 VertexID = INDEX_NONE;
};

/** Read-only mesh state shared by a complete Painter expression tree. */
struct PCGUTILSDYNMESH_API FPCGUtilsDynMeshPainterEvaluationContext
{
	FPCGUtilsDynMeshPainterEvaluationContext(
		const UPCGDynamicMeshData* InMeshData,
		const UE::Geometry::FDynamicMesh3& InMesh,
		const FTransform& InLocalToWorld,
		int32 InDataSetIndex = 0,
		int32 InDataSetCount = 1)
		: MeshData(InMeshData), Mesh(InMesh), LocalToWorld(InLocalToWorld),
		  DataSetIndex(InDataSetIndex), DataSetCount(InDataSetCount)
	{
	}

	const UPCGDynamicMeshData* MeshData = nullptr;
	const UE::Geometry::FDynamicMesh3& Mesh;
	FTransform LocalToWorld = FTransform::Identity;
	/** Pairing coordinates for Painters backed by ordered per-DynMesh external datasets. */
	int32 DataSetIndex = 0;
	int32 DataSetCount = 1;
};

USTRUCT(meta=(PCG_DataTypeDisplayName="DynMesh Painter"))
struct FPCGUtilsDynMeshPainterFactoryDataTypeInfo : public FPCGUtilsDynMeshFactoryDataTypeInfo
{
	GENERATED_BODY()

	PCG_DECLARE_TYPE_INFO(PCGUTILSDYNMESH_API);
};

class FPCGUtilsDynMeshPainterOperation;

/** Immutable scalar/color field configuration transported through PCG pins. */
UCLASS(Abstract, BlueprintType, ClassGroup=(Procedural), Category="PCGUtils|DynMesh|Painters")
class PCGUTILSDYNMESH_API UPCGUtilsDynMeshPainterFactoryData : public UPCGUtilsDynMeshFactoryData
{
	GENERATED_BODY()

public:
	PCG_ASSIGN_TYPE_INFO(FPCGUtilsDynMeshPainterFactoryDataTypeInfo)

	/** Creates and context-binds a runtime operation. Common initialization cannot be bypassed. */
	TSharedPtr<FPCGUtilsDynMeshPainterOperation> CreateOperation(FPCGContext* InContext) const;

protected:
	virtual TSharedPtr<FPCGUtilsDynMeshPainterOperation> CreateOperationInternal() const;
};

/** Runtime scalar-or-color field evaluated for mesh samples by a Painter consumer. */
class PCGUTILSDYNMESH_API FPCGUtilsDynMeshPainterOperation : public FPCGUtilsDynMeshOperation
{
public:
	virtual bool Initialize(const FPCGUtilsDynMeshPainterEvaluationContext& InPainterContext);
	virtual EPCGUtilsDynMeshPainterValueType GetOutputType() const = 0;
	virtual FPCGUtilsDynMeshPainterValue Evaluate(
		const FPCGUtilsDynMeshPainterSample& Sample) const = 0;

protected:
	const FPCGUtilsDynMeshPainterEvaluationContext* PainterContext = nullptr;
};

namespace PCGUtilsDynMeshFactories
{
	PCGUTILSDYNMESH_API const TSet<FPCGDataTypeBaseId>& GetPainterFactoryTypes();
}

namespace PCGUtilsDynMeshPainterFactories
{
	/** Resolves zero or one Painter from a pin; required pins report a graph error when empty. */
	PCGUTILSDYNMESH_API bool GetSinglePainter(
		FPCGContext* Context,
		FName PinLabel,
		const UPCGUtilsDynMeshPainterFactoryData*& OutPainter,
		bool bRequired);
}
