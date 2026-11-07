// Copyright Max Harris

#include "Data/PCGVolumeShapeData.h"

#include "Helpers/PCGHelpers.h"
#include "PCGContext.h"
#include "PCGPoint.h"
#include "Serialization/ArchiveCrc32.h"

void UPCGVolumeShapeData::Initialize(const FPCGVolumeShapeDesc& InShape)
{
	Shape = InShape;
	// Bounds hold the whole shape. Strict bounds stay empty: a rotated or round shape fills no axis-aligned box
	// worth computing, so every point inside Bounds is tested against the shape itself.
	Super::Initialize(Shape.GetWorldBounds());
	StrictBounds = FBox(ForceInit);
}

void UPCGVolumeShapeData::AddToCrc(FArchiveCrc32& Ar, bool bFullDataCrc) const
{
	Super::AddToCrc(Ar, bFullDataCrc);
	FPCGVolumeShapeDesc Copy = Shape;
	uint8 ShapeType = static_cast<uint8>(Copy.Shape);
	Ar << ShapeType;
	Ar << Copy.BoxExtent;
	Ar << Copy.Radius;
	Ar << Copy.HalfHeight;
	Ar << Copy.Sides;
	Ar << Copy.ShapeToWorld;
}

bool UPCGVolumeShapeData::SamplePoint(const FTransform& InTransform, const FBox& InBounds, FPCGPoint& OutPoint, UPCGMetadata* OutMetadata) const
{
	const FVector Position = InTransform.GetLocation();
	if (!PCGHelpers::IsInsideBounds(GetBounds(), Position))
	{
		return false;
	}
	OutPoint.Transform = InTransform;
	OutPoint.SetLocalBounds(InBounds);
	OutPoint.Density = Shape.Contains(Position) ? 1.0f : 0.0f;
	return OutPoint.Density > 0.0f;
}

UPCGSpatialData* UPCGVolumeShapeData::CopyInternal(FPCGContext* Context) const
{
	UPCGVolumeShapeData* Copy = FPCGContext::NewObject_AnyThread<UPCGVolumeShapeData>(Context);
	CopyBaseVolumeData(Copy);
	Copy->Shape = Shape;
	return Copy;
}
