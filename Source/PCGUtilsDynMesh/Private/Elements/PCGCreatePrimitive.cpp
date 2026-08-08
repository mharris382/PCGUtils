#include "Elements/PCGCreatePrimitive.h"

#include "Data/PCGBasePointData.h"
#include "Data/PCGDynamicMeshData.h"
#include "DynamicMesh/DynamicMesh3.h"
#include "Elements/CreatePrimitive/PCGCreatePrimitiveSettingsBase.h"
#include "GameFramework/Actor.h"
#include "GeometryScript/MeshBasicEditFunctions.h"
#include "PCGContext.h"
#include "PCGPin.h"
#include "UDynamicMesh.h"
#include "Utils/PCGLogErrors.h"

#define LOCTEXT_NAMESPACE "PCGCreatePrimitiveElement"

namespace
{
	const FName SeedsPin = TEXT("Seeds");

	/**
	 * Resolves the "Copy to Seed Bounds" placement transform: scales/positions PrimitiveBounds (the primitive's
	 * native local-space bounds, generated at the identity transform) so it fits snugly inside the seed's own
	 * bounds, oriented by the seed's rotation.
	 *
	 * Derivation: we want a transform T with T.Rotation = SeedTransform.GetRotation() and a per-axis local
	 * Scale such that T maps PrimitiveBounds onto the seed's world-space bounds. Scale on axis i is
	 * TargetSize[i] / PrimitiveSize[i] (guarded against a near-zero PrimitiveSize[i], which happens for
	 * flat/2D primitives such as Rectangle or Disc). Translation is then solved so that PrimitiveBounds' center
	 * - after that scale and rotation - lands exactly on the seed's world-space bounds center, which correctly
	 * accounts for primitives whose native bounds are not centered at their local origin (eg Origin=Base).
	 */
	FTransform ResolveBoundsFitTransform(
		const UE::Geometry::FAxisAlignedBox3d& PrimitiveBounds,
		const FTransform& SeedTransform,
		const FVector& SeedBoundsMin,
		const FVector& SeedBoundsMax)
	{
		const FVector PrimitiveCenter(PrimitiveBounds.Center());
		const FVector PrimitiveSize(PrimitiveBounds.Max - PrimitiveBounds.Min);

		// Seed bounds are defined in the point's own local space (unscaled); the target world-space size along
		// the seed's local axes therefore needs the seed's own scale applied, while the target center does not
		// (it is resolved into world space below via SeedTransform.TransformPosition, which already includes scale).
		const FVector TargetSize = (SeedBoundsMax - SeedBoundsMin) * SeedTransform.GetScale3D();
		const FVector TargetCenterLocal = (SeedBoundsMax + SeedBoundsMin) * 0.5;

		FVector FitScale(1.0, 1.0, 1.0);
		for (int32 Axis = 0; Axis < 3; ++Axis)
		{
			if (FMath::Abs(PrimitiveSize[Axis]) > UE_KINDA_SMALL_NUMBER)
			{
				FitScale[Axis] = TargetSize[Axis] / PrimitiveSize[Axis];
			}
		}

		const FQuat Rotation = SeedTransform.GetRotation();
		const FVector WorldCenter = SeedTransform.TransformPosition(TargetCenterLocal);
		const FVector Translation = WorldCenter - Rotation.RotateVector(PrimitiveCenter * FitScale);

		return FTransform(Rotation, Translation, FitScale);
	}

	FTransform ResolveSeedPlacementTransform(
		EPCGCreatePrimitiveSeedPlacement PlacementMode,
		bool bHavePrimitiveBounds,
		const UE::Geometry::FAxisAlignedBox3d& PrimitiveBounds,
		const FTransform& SeedTransform,
		const FVector& SeedBoundsMin,
		const FVector& SeedBoundsMax)
	{
		if (PlacementMode == EPCGCreatePrimitiveSeedPlacement::Bounds && bHavePrimitiveBounds)
		{
			return ResolveBoundsFitTransform(PrimitiveBounds, SeedTransform, SeedBoundsMin, SeedBoundsMax);
		}
		return SeedTransform;
	}
}

UPCGCreatePrimitiveSettings::UPCGCreatePrimitiveSettings(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	Primitive = CreateDefaultSubobject<UPCGCreatePrimitiveBoxSettings>(TEXT("DefaultPrimitive"));
}

#if WITH_EDITOR
FText UPCGCreatePrimitiveSettings::GetDefaultNodeTitle() const
{
	return LOCTEXT("NodeTitle", "Create Primitive");
}

FText UPCGCreatePrimitiveSettings::GetNodeTooltipText() const
{
	return LOCTEXT("NodeTooltip",
		"Generates a Geometry Script primitive as Dynamic Mesh data. With Use Seed Points enabled, one copy of "
		"the primitive is appended per point on the Seeds pin, placed either by the seed's transform or fitted "
		"to the seed's bounds.");
}

EPCGChangeType UPCGCreatePrimitiveSettings::GetChangeTypeForProperty(FPropertyChangedEvent& PropertyChangedEvent) const
{
	EPCGChangeType ChangeType = Super::GetChangeTypeForProperty(PropertyChangedEvent);
	if (PropertyChangedEvent.GetMemberPropertyName() == GET_MEMBER_NAME_CHECKED(UPCGCreatePrimitiveSettings, bUseSeedPoints))
	{
		ChangeType |= EPCGChangeType::Structural;
	}
	return ChangeType;
}
#endif

TArray<FPCGPinProperties> UPCGCreatePrimitiveSettings::InputPinProperties() const
{
	TArray<FPCGPinProperties> Pins;
	if (bUseSeedPoints)
	{
		Pins.Emplace(SeedsPin, EPCGDataType::Point, true, true);
	}
	return Pins;
}

TArray<FPCGPinProperties> UPCGCreatePrimitiveSettings::OutputPinProperties() const
{
	return {FPCGPinProperties(PCGPinConstants::DefaultOutputLabel, EPCGDataType::DynamicMesh, true, true)};
}

FPCGElementPtr UPCGCreatePrimitiveSettings::CreateElement() const
{
	return MakeShared<FPCGCreatePrimitiveElement>();
}

bool FPCGCreatePrimitiveElement::ExecuteInternal(FPCGContext* Context) const
{
	TRACE_CPUPROFILER_EVENT_SCOPE(FPCGCreatePrimitiveElement::ExecuteInternal);
	check(Context);

	const UPCGCreatePrimitiveSettings* Settings = Context->GetInputSettings<UPCGCreatePrimitiveSettings>();
	check(Settings);

	const UPCGCreatePrimitiveSettingsBase* PrimitiveSettings = Settings->Primitive;

	// Generate the primitive once at the identity transform; this both serves as the entire output when seed
	// points are disabled, and as the reusable "stamp" appended per-seed (with a resolved transform) otherwise.
	UDynamicMesh* ReferenceMesh = FPCGContext::NewObject_AnyThread<UDynamicMesh>(Context);
	if (PrimitiveSettings)
	{
		PrimitiveSettings->AppendPrimitive(ReferenceMesh, FTransform::Identity);
	}
	else
	{
		PCGLog::LogErrorOnGraph(LOCTEXT("NoPrimitiveConfigured", "Create Primitive has no primitive type configured; producing an empty Dynamic Mesh."), Context);
	}

	UDynamicMesh* OutputMesh = ReferenceMesh;

	if (Settings->bUseSeedPoints && PrimitiveSettings)
	{
		OutputMesh = FPCGContext::NewObject_AnyThread<UDynamicMesh>(Context);

		FTransform ActorTransform = FTransform::Identity;
		if (Settings->bConvertSeedsToLocalSpace)
		{
			if (const AActor* TargetActor = Context->GetTargetActor(nullptr))
			{
				ActorTransform = TargetActor->GetActorTransform();
			}
			else
			{
				PCGLog::LogWarningOnGraph(LOCTEXT("MissingTargetActor",
					"Create Primitive could not resolve a target actor; seed positions remain in their original space."), Context);
			}
		}

		bool bHavePrimitiveBounds = false;
		UE::Geometry::FAxisAlignedBox3d PrimitiveBounds;
		if (Settings->Placement == EPCGCreatePrimitiveSeedPlacement::Bounds)
		{
			const UE::Geometry::FDynamicMesh3* ReferenceMeshPtr = ReferenceMesh->GetMeshPtr();
			if (ReferenceMeshPtr && ReferenceMeshPtr->TriangleCount() > 0)
			{
				PrimitiveBounds = ReferenceMeshPtr->GetBounds();
				bHavePrimitiveBounds = true;
			}
			else
			{
				PCGLog::LogWarningOnGraph(LOCTEXT("EmptyPrimitiveBounds",
					"Create Primitive: the configured primitive produced no geometry, so Copy to Seed Bounds cannot compute a fitting transform; seeds will use their transform directly instead."), Context);
			}
		}

		TArray<FTransform> PlacementTransforms;
		for (const FPCGTaggedData& SeedInput : Context->InputData.GetInputsByPin(SeedsPin))
		{
			const UPCGBasePointData* SeedPointData = Cast<const UPCGBasePointData>(SeedInput.Data);
			if (!SeedPointData)
			{
				PCGLog::LogWarningOnGraph(LOCTEXT("InvalidSeedInput", "Create Primitive skipped a Seeds input that was not Point Data."), Context);
				continue;
			}

			const int32 NumPoints = SeedPointData->GetNumPoints();
			const auto TransformRange = SeedPointData->GetConstTransformValueRange();
			const auto BoundsMinRange = SeedPointData->GetConstBoundsMinValueRange();
			const auto BoundsMaxRange = SeedPointData->GetConstBoundsMaxValueRange();

			PlacementTransforms.Reserve(PlacementTransforms.Num() + NumPoints);
			for (int32 PointIndex = 0; PointIndex < NumPoints; ++PointIndex)
			{
				const FTransform SeedTransform = Settings->bConvertSeedsToLocalSpace
					? TransformRange[PointIndex].GetRelativeTransform(ActorTransform)
					: TransformRange[PointIndex];

				PlacementTransforms.Add(ResolveSeedPlacementTransform(
					Settings->Placement, bHavePrimitiveBounds, PrimitiveBounds,
					SeedTransform, BoundsMinRange[PointIndex], BoundsMaxRange[PointIndex]));
			}
		}

		if (!PlacementTransforms.IsEmpty())
		{
			UGeometryScriptLibrary_MeshBasicEditFunctions::AppendMeshTransformed(
				OutputMesh, ReferenceMesh, PlacementTransforms, FTransform::Identity);
		}
	}

	UPCGDynamicMeshData* OutputData = FPCGContext::NewObject_AnyThread<UPCGDynamicMeshData>(Context);
	OutputData->Initialize(OutputMesh, /*bCanTakeOwnership=*/true, TArray<UMaterialInterface*>{});
	FPCGTaggedData& Output = Context->OutputData.TaggedData.Emplace_GetRef();
	Output.Data = OutputData;
	Output.Pin = PCGPinConstants::DefaultOutputLabel;

	return true;
}

#undef LOCTEXT_NAMESPACE
