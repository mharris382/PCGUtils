// Copyright Max Harris

#pragma once

#include "Data/PCGVolumeData.h"
#include "Data/PCGVolumeShape.h"

#include "PCGVolumeShapeData.generated.h"

/**
 * Volume data for a PCG Volume Component's shape (box, sphere, cylinder, cone, capsule) rather than an engine Volume
 * actor. A point is inside when the exact shape holds it, rotation and non-uniform scale included; the engine's
 * volume data can only express an axis-aligned box without an actor. Works wherever volume data does: sampling,
 * intersection and difference.
 */
UCLASS(BlueprintType, ClassGroup=(Procedural))
class PCGUTILS_API UPCGVolumeShapeData : public UPCGVolumeData
{
	GENERATED_BODY()

public:
	void Initialize(const FPCGVolumeShapeDesc& InShape);

	const FPCGVolumeShapeDesc& GetShape() const { return Shape; }

	virtual void AddToCrc(FArchiveCrc32& Ar, bool bFullDataCrc) const override;
	virtual bool SamplePoint(const FTransform& Transform, const FBox& Bounds, FPCGPoint& OutPoint, UPCGMetadata* OutMetadata) const override;

protected:
	virtual UPCGSpatialData* CopyInternal(FPCGContext* Context) const override;

	UPROPERTY()
	FPCGVolumeShapeDesc Shape;
};
