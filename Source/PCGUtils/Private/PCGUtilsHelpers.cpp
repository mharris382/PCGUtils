// Copyright Max Harris

#include "PCGUtilsHelpers.h"

#include "PCGActorBase.h"
#include "Components/SplineComponent.h"
#include "GameFramework/Actor.h"
#include "Interfaces/PCGBoundsProvider.h"
#include "Interfaces/PCGComponentProvider.h"
#include "Metadata/PCGMetadata.h"
#include "Metadata/PCGMetadataAttributeTpl.h"
#include "Data/PCGPointArrayData.h"
#include "Data/PCGBasePointData.h"
#include "PCGParamData.h"
#include "Settings/PCGUtilsSettings.h"
#include "Engine/StaticMesh.h"
#include "PCGPoint.h"
#include "PCGComponent.h"
#include "PCGContext.h"

// ─────────────────────────────────────────────────────────────────────────────
// ComputeSplineBoundingBox
// ─────────────────────────────────────────────────────────────────────────────



bool UPCGUtilsHelpers::TryRefreshPCGGeneration(UActorComponent* Component, bool bForceRegenerate)
{
	if (!IsValid(Component) || !IsValid(Component->GetOwner()))
		return false;

	AActor* Owner = Component->GetOwner();
	if (!Owner) return false;
	UPCGComponent* PCGComponent = nullptr;
	bool bFoundProvider = false;
	APCGActorBase* PCGActorBaseOwner = Cast<APCGActorBase>(Owner);
	if (PCGActorBaseOwner)
	{
		bFoundProvider = true;	
		PCGActorBaseOwner->PCGComponent->GenerateLocal(true);
		return true;
	}

	auto TryProvider = [&PCGComponent, &bFoundProvider](UObject* Provider) -> bool
	{
		if (!IsValid(Provider) || !Provider->GetClass()->ImplementsInterface(UPCGComponentProvider::StaticClass()))
		{
			return false;
		}

		bFoundProvider = true;
		if (!IPCGComponentProvider::Execute_AllowsComponentTriggeredRegeneration(Provider))
		{
			return true;
		}

		PCGComponent = IPCGComponentProvider::Execute_GetPrimaryPCGComponent(Provider);
		return true;
	};

	TryProvider(Owner);
	if (!bFoundProvider)
	{
		TInlineComponentArray<UActorComponent*, 8> OwnerComponents;
		Owner->GetComponents(OwnerComponents);
		for (UActorComponent* OwnerComponent : OwnerComponents)
		{
			if (TryProvider(OwnerComponent))
			{
				break;
			}
		}
	}

	if (bFoundProvider)
	{
		if (!IsValid(PCGComponent))
		{
			return false;
		}

		PCGComponent->GenerateLocal(bForceRegenerate);
		return true;
	}

	const UPCGUtilsSettings* Settings = GetDefault<UPCGUtilsSettings>();
	if (!Settings || Settings->bRequirePCGComponentProviderForAutoRegeneration)
		return false;

	PCGComponent = Owner->FindComponentByClass<UPCGComponent>();
	if (!IsValid(PCGComponent))
	{
		return false;
	}

	PCGComponent->GenerateLocal(bForceRegenerate);
	return true;
}

FBox UPCGUtilsHelpers::ComputePathBoundingBox(UShapePathComponent* PathComponent, bool bLocalSpace)
{
	if (!PathComponent)
	{
		return FBox(EForceInit::ForceInit).ExpandBy(1.0f);
	}
	FBox Result(EForceInit::ForceInit);
	PathComponent->RebuildPoints();
	const int32 NumPoints =PathComponent->GetNumPoints();
	if (NumPoints == 0)
	{
		return Result;
	}
	
	const AActor* Owner = PathComponent->GetOwner();
	const FTransform LocalSpaceTransform = Owner ? Owner->GetActorTransform() : PathComponent->GetComponentTransform();
	auto ToOutputSpace = [&](const FVector& WorldPos) -> FVector
	{
		return bLocalSpace ? LocalSpaceTransform.InverseTransformPosition(WorldPos) : WorldPos;
	};
	
	for (int32 PointIndex = 0; PointIndex < NumPoints; ++PointIndex)
	{
		const FVector WorldPos =  PathComponent->GetComponentTransform().TransformPosition(PathComponent->GetPathPoint(PointIndex));
		Result += ToOutputSpace(WorldPos);
	}
	return Result.ExpandBy(1.0f);
}

FBox UPCGUtilsHelpers::ComputeSplineBoundingBox(
	const USplineComponent* SplineComponent,
	bool bLocalSpace,
	int32 SampleSubdivisionCount)
{
	if (!SplineComponent)
	{
		return FBox(EForceInit::ForceInit).ExpandBy(1.0f);
	}

	// Clamp to the documented minimum of 0.
	const int32 Subdivisions = FMath::Max(0, SampleSubdivisionCount);

	FBox Result(EForceInit::ForceInit);
	Result = Result.ExpandBy(1.0);
	const int32 NumPoints = SplineComponent->GetNumberOfSplinePoints();
	if (NumPoints == 0)
	{
		return Result;
	}

	// Helper: convert a world-space position to the desired output space.
	// When bLocalSpace is true we want actor-local space, not spline-component-local space,
	// so that the bounding box accounts for the spline component's RelativeTransform offset.
	const AActor* Owner = SplineComponent->GetOwner();
	const FTransform LocalSpaceTransform = Owner ? Owner->GetActorTransform() : SplineComponent->GetComponentTransform();
	auto ToOutputSpace = [&](const FVector& WorldPos) -> FVector
	{
		return bLocalSpace ? LocalSpaceTransform.InverseTransformPosition(WorldPos) : WorldPos;
	};

	// ── Control points ────────────────────────────────────────────────────────
	for (int32 PointIndex = 0; PointIndex < NumPoints; ++PointIndex)
	{
		const FVector WorldPos = SplineComponent->GetLocationAtSplinePoint(PointIndex, ESplineCoordinateSpace::World);
		Result += ToOutputSpace(WorldPos);
	}

	// ── Intra-segment samples ─────────────────────────────────────────────────
	const float SplineLength = SplineComponent->GetSplineLength();
	if (Subdivisions > 0 && SplineLength > KINDA_SMALL_NUMBER)
	{
		const bool bIsClosed   = SplineComponent->IsClosedLoop();
		const int32 NumSegments = bIsClosed ? NumPoints : NumPoints - 1;

		// Step size in [0,1] normalised per-segment space.
		const float StepT = 1.0f / static_cast<float>(Subdivisions + 1);

		for (int32 SegIndex = 0; SegIndex < NumSegments; ++SegIndex)
		{
			// Distance range for this segment.
			const float SegStartDist = SplineComponent->GetDistanceAlongSplineAtSplinePoint(SegIndex);
			const float SegEndDist   = SplineComponent->GetDistanceAlongSplineAtSplinePoint(
				(SegIndex + 1) % NumPoints);

			// Handle wrap-around on closed loops where end < start.
			const float EffectiveEnd  = (SegEndDist > SegStartDist)
				? SegEndDist
				: SegEndDist + SplineLength;
			const float SegLength     = EffectiveEnd - SegStartDist;

			for (int32 SubIndex = 1; SubIndex <= Subdivisions; ++SubIndex)
			{
				const float T        = SubIndex * StepT;
				const float Distance = FMath::Fmod(SegStartDist + T * SegLength, SplineLength);
				const FVector WorldPos = SplineComponent->GetLocationAtDistanceAlongSpline(
					Distance, ESplineCoordinateSpace::World);

				Result += ToOutputSpace(WorldPos);
			}
		}
	}

	return Result.ExpandBy(1.0f);
}

// ─────────────────────────────────────────────────────────────────────────────
// ComputeActorSplineBoundingBox
// ─────────────────────────────────────────────────────────────────────────────

FBox UPCGUtilsHelpers::ComputeActorPCGBoundingBox(
	const AActor* Actor,
	bool bLocalSpace,
	int32 SampleSubdivisionCount)
{
	if (!Actor)
	{
		return FBox(EForceInit::ForceInit).ExpandBy(1.0f);
	}
	FBox Result(EForceInit::ForceInit);
	Result = Result.ExpandBy(1.0);
#if WITH_EDITOR

	TArray<USplineComponent*> SplineComponents;
	Actor->GetComponents<USplineComponent>(SplineComponents);

	for (const USplineComponent* Spline : SplineComponents)
	{
		const FBox SplineBox = ComputeSplineBoundingBox(Spline, bLocalSpace, SampleSubdivisionCount);
		if (SplineBox.IsValid)
		{
			Result += SplineBox;
		}
	}
	
	TInlineComponentArray<UActorComponent*, 8> Components;
	Actor->GetComponents(Components);
	for (UActorComponent* Component : Components)
	{
		if (!IsValid(Component) || !Component->GetClass()->ImplementsInterface(UPCGBoundsProvider::StaticClass()))
		{
			continue;
		}

		FBox ActorRelativeBounds(EForceInit::ForceInit);
		if (IPCGBoundsProvider::Execute_GetPCGActorBoundingBox(Component, const_cast<AActor*>(Actor), ActorRelativeBounds)
			&& ActorRelativeBounds.IsValid)
		{
			Result += bLocalSpace
				? ActorRelativeBounds
				: ActorRelativeBounds.TransformBy(Actor->GetActorTransform());
		}
	}
#endif
	return Result;
}

UStaticMesh* UPCGUtilsHelpers::ResolveStaticMeshFromData(
	const FPCGDataCollection& Collection,
	FName PinName,
	FName AttributeName,
	FString Tag)
{
	for (const FPCGTaggedData& TaggedData : Collection.GetAllInputs())
	{
		if (TaggedData.Pin != PinName)
		{
			continue;
		}

		if (!Tag.IsEmpty() && !TaggedData.Tags.Contains(Tag))
		{
			continue;
		}

		const UPCGParamData* ParamData = Cast<UPCGParamData>(TaggedData.Data);
		if (!ParamData)
		{
			continue;
		}

		const UPCGMetadata* Metadata = ParamData->ConstMetadata();
		if (!Metadata)
		{
			continue;
		}

		const FPCGMetadataAttribute<FSoftObjectPath>* Attribute =
			Metadata->GetConstTypedAttribute<FSoftObjectPath>(AttributeName);
		if (!Attribute)
		{
			continue;
		}

		const FSoftObjectPath AssetPath = Attribute->GetValueFromItemKey(PCGFirstEntryKey);
		if (AssetPath.IsNull())
		{
			continue;
		}

		const FSoftObjectPtr SoftObjectReference(AssetPath);
		UObject* Object = SoftObjectReference.Get();

		if (UStaticMesh* Mesh = Cast<UStaticMesh>(Object))
		{
			return Mesh;
		}
	}

	return nullptr;
}


UPCGPointArrayData* UPCGUtilsHelpers::CreatePointArrayDataFromPoints(
	FPCGContext* Context,
	const TArray<FPCGPoint>& Points)
{
	if (!Context)
	{
		return nullptr;
	}
	UPCGPointArrayData* OutputData = FPCGContext::NewObject_AnyThread<UPCGPointArrayData>(Context);
	
	if (!OutputData)
	{
		return nullptr;
	}
	const int32 NumPoints = Points.Num();

	OutputData->SetNumPoints(NumPoints, false);
	auto Transforms = OutputData->GetTransformValueRange();
	auto Densities = OutputData->GetDensityValueRange();
	auto BoundsMin = OutputData->GetBoundsMinValueRange();
	auto BoundsMax = OutputData->GetBoundsMaxValueRange();
	auto Colors = OutputData->GetColorValueRange();
	auto Steepness = OutputData->GetSteepnessValueRange();
	auto Seeds = OutputData->GetSeedValueRange();
	auto MetadataEntries = OutputData->GetMetadataEntryValueRange();

	for (int32 Index = 0; Index < NumPoints; ++Index)
	{
		const FPCGPoint& Point = Points[Index];
		Transforms[Index] = Point.Transform;
		Densities[Index] = Point.Density;
		BoundsMin[Index] = Point.BoundsMin;
		BoundsMax[Index] = Point.BoundsMax;
		Colors[Index] = Point.Color;
		Steepness[Index] = Point.Steepness;
		Seeds[Index] = Point.Seed;
		MetadataEntries[Index] = Point.MetadataEntry;
	}

	return OutputData;
}


