#include "Elements/Creation/PCGCreatePrimitive.h"

#include "Data/PCGBasePointData.h"
#include "Data/PCGDynamicMeshData.h"
#include "DynamicMesh/DynamicMesh3.h"
#include "DynamicMesh/DynamicMeshAttributeSet.h"
#include "Elements/Creation/CreatePrimitive/PCGCreatePrimitiveSettingsBase.h"
#include "Factories/PCGUtilsDynMeshFactories.h"
#include "Factories/PCGUtilsDynMeshBuilderFactory.h"
#include "GameFramework/Actor.h"
#include "GeometryScript/MeshBasicEditFunctions.h"
#include "PCGContext.h"
#include "PCGNode.h"
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

	/**
	 * Rebases a freshly-built Builder result's material IDs onto the combined output material array. A no-op
	 * for the usual case of one Builder, or of Builders that carry no materials at all.
	 */
	void ShiftMaterialIDs(UDynamicMesh* Mesh, int32 Offset)
	{
		if (Offset == 0 || !Mesh)
		{
			return;
		}
		Mesh->EditMesh([Offset](UE::Geometry::FDynamicMesh3& EditMesh)
		{
			if (!EditMesh.HasAttributes() || !EditMesh.Attributes()->HasMaterialID())
			{
				return;
			}
			UE::Geometry::FDynamicMeshMaterialAttribute* MaterialIDs = EditMesh.Attributes()->GetMaterialID();
			for (const int32 TriangleID : EditMesh.TriangleIndicesItr())
			{
				int32 MaterialID = 0;
				MaterialIDs->GetValue(TriangleID, &MaterialID);
				MaterialIDs->SetValue(TriangleID, MaterialID + Offset);
			}
		});
	}

	/** Resolves the target-actor transform used to convert seed points into the mesh's local space. */
	FTransform ResolveSeedActorTransform(FPCGContext* Context, bool bConvertSeedsToLocalSpace)
	{
		FTransform ActorTransform = FTransform::Identity;
		if (bConvertSeedsToLocalSpace)
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
		return ActorTransform;
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
		"Generates a Geometry Script primitive as Dynamic Mesh data. In legacy mode, one copy of the inline "
		"primitive is appended per point on the Seeds pin. In Builder mode (bUseLegacyMode disabled), the "
		"primitives and their fitting/alignment come from Builder nodes on the Builders pin instead, "
		"split into output Dynamic Mesh data according to Output Mode - one mesh per seed by default.");
}

EPCGChangeType UPCGCreatePrimitiveSettings::GetChangeTypeForProperty(FPropertyChangedEvent& PropertyChangedEvent) const
{
	EPCGChangeType ChangeType = Super::GetChangeTypeForProperty(PropertyChangedEvent);
	const FName PropertyName = PropertyChangedEvent.GetMemberPropertyName();
	if (PropertyName == GET_MEMBER_NAME_CHECKED(UPCGCreatePrimitiveSettings, bUseSeedPoints) ||
		PropertyName == GET_MEMBER_NAME_CHECKED(UPCGCreatePrimitiveSettings, bUseLegacyMode))
	{
		ChangeType |= EPCGChangeType::Structural;
	}
	return ChangeType;
}
#endif

void UPCGCreatePrimitiveSettings::ApplyDeprecationBeforeUpdatePins(
	UPCGNode* InOutNode, TArray<TObjectPtr<UPCGPin>>& InputPins,
	TArray<TObjectPtr<UPCGPin>>& OutputPins)
{
	Super::ApplyDeprecationBeforeUpdatePins(InOutNode, InputPins, OutputPins);
	if (InOutNode)
	{
		// The Builder pin became multi-connection when compound shapes landed; keep existing edges attached.
		InOutNode->RenameInputPin(
			PCGUtilsDynMeshBuilderFactoryConstants::OutputPin,
			PCGUtilsDynMeshBuilderFactoryConstants::BuildersInputPin);
	}
}

TArray<FPCGPinProperties> UPCGCreatePrimitiveSettings::InputPinProperties() const
{
	TArray<FPCGPinProperties> Pins;
	if (bUseLegacyMode)
	{
		if (bUseSeedPoints)
		{
			Pins.Emplace(SeedsPin, EPCGDataType::Point, true, true);
		}
	}
	else
	{
		Pins.Emplace_GetRef(SeedsPin, EPCGDataType::Point, true, true).SetRequiredPin();
		// Multiple connections: every Builder on this pin is evaluated for every seed and appended, which is
		// how a compound shape (column, frame, window) is expressed.
		Pins.Emplace_GetRef(
			PCGUtilsDynMeshBuilderFactoryConstants::BuildersInputPin,
			FPCGUtilsDynMeshBuilderFactoryDataTypeInfo::AsId(), true, true).SetRequiredPin();
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

	return Settings->bUseLegacyMode ? ExecuteLegacy(Context, Settings) : ExecuteBuilder(Context, Settings);
}

bool FPCGCreatePrimitiveElement::ExecuteLegacy(FPCGContext* Context, const UPCGCreatePrimitiveSettings* Settings) const
{
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

		const FTransform ActorTransform = ResolveSeedActorTransform(Context, Settings->bConvertSeedsToLocalSpace);

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

bool FPCGCreatePrimitiveElement::ExecuteBuilder(FPCGContext* Context, const UPCGCreatePrimitiveSettings* Settings) const
{
	TArray<TObjectPtr<const UPCGUtilsDynMeshBuilderFactoryData>> Factories;
	if (!PCGUtilsDynMeshFactories::GetInputFactories(
		Context, PCGUtilsDynMeshBuilderFactoryConstants::BuildersInputPin, Factories,
		PCGUtilsDynMeshFactories::GetBuilderFactoryTypes(), /*bRequired=*/true))
	{
		return true;
	}

	// This is the materialization point: every connected Builder expression is evaluated here, once per seed.
	// Nothing upstream of this node has touched geometry.
	TArray<TSharedPtr<FPCGUtilsDynMeshBuilderOperation>> Operations;
	Operations.Reserve(Factories.Num());
	for (const UPCGUtilsDynMeshBuilderFactoryData* Factory : Factories)
	{
		TSharedPtr<FPCGUtilsDynMeshBuilderOperation> Operation = Factory->CreateOperation(Context);
		if (!Operation)
		{
			PCGLog::LogErrorOnGraph(LOCTEXT("BuilderOperationFailed",
				"Create Primitive could not create an operation from one of its Builder inputs."), Context);
			return true;
		}
		Operations.Add(MoveTemp(Operation));
	}

	const EPCGUtilsDynMeshBuilderOutputMode OutputMode = Settings->OutputMode;
	const bool bSplitBySeed =
		OutputMode == EPCGUtilsDynMeshBuilderOutputMode::PerSeed ||
		OutputMode == EPCGUtilsDynMeshBuilderOutputMode::PerBuilderPerSeed;
	const bool bSplitByBuilder =
		OutputMode == EPCGUtilsDynMeshBuilderOutputMode::PerBuilder ||
		OutputMode == EPCGUtilsDynMeshBuilderOutputMode::PerBuilderPerSeed;

	const FTransform ActorTransform = ResolveSeedActorTransform(Context, Settings->bConvertSeedsToLocalSpace);

	auto EmitMesh = [Context](UDynamicMesh* Mesh, const TArray<UMaterialInterface*>& Materials,
		const FPCGTaggedData* SourceSeedInput)
	{
		UPCGDynamicMeshData* OutputData = FPCGContext::NewObject_AnyThread<UPCGDynamicMeshData>(Context);
		OutputData->Initialize(Mesh, /*bCanTakeOwnership=*/true, Materials);
		FPCGTaggedData& Output = SourceSeedInput
			? Context->OutputData.TaggedData.Emplace_GetRef(*SourceSeedInput)
			: Context->OutputData.TaggedData.Emplace_GetRef();
		Output.Data = OutputData;
		Output.Pin = PCGPinConstants::DefaultOutputLabel;
	};

	// When several Builders share one output mesh, their material IDs must be rebased onto a concatenated
	// array. When the output is split per Builder that never arises, so each output just carries its own
	// Builder's materials verbatim.
	TArray<UMaterialInterface*> ComposedMaterials;
	TArray<int32> ComposedMaterialOffsets;
	ComposedMaterialOffsets.Init(0, Operations.Num());
	TArray<TArray<UMaterialInterface*>> PerBuilderMaterials;
	PerBuilderMaterials.SetNum(Operations.Num());
	bool bMaterialLayoutResolved = false;

	// Accumulators for the modes that span seeds.
	UDynamicMesh* SingleTarget = (OutputMode == EPCGUtilsDynMeshBuilderOutputMode::Single)
		? FPCGContext::NewObject_AnyThread<UDynamicMesh>(Context) : nullptr;
	TArray<UDynamicMesh*> PerBuilderTargets;
	if (OutputMode == EPCGUtilsDynMeshBuilderOutputMode::PerBuilder)
	{
		PerBuilderTargets.Reserve(Operations.Num());
		for (int32 Index = 0; Index < Operations.Num(); ++Index)
		{
			PerBuilderTargets.Add(FPCGContext::NewObject_AnyThread<UDynamicMesh>(Context));
		}
	}

	int32 SeedCount = 0;

	for (const FPCGTaggedData& SeedInput : Context->InputData.GetInputsByPin(SeedsPin))
	{
		const UPCGBasePointData* SeedPointData = Cast<const UPCGBasePointData>(SeedInput.Data);
		if (!SeedPointData)
		{
			PCGLog::LogWarningOnGraph(LOCTEXT("InvalidSeedInput",
				"Create Primitive skipped a Seeds input that was not Point Data."), Context);
			continue;
		}

		const int32 NumPoints = SeedPointData->GetNumPoints();
		const auto TransformRange = SeedPointData->GetConstTransformValueRange();
		const auto BoundsMinRange = SeedPointData->GetConstBoundsMinValueRange();
		const auto BoundsMaxRange = SeedPointData->GetConstBoundsMaxValueRange();

		for (int32 PointIndex = 0; PointIndex < NumPoints; ++PointIndex)
		{
			FPCGUtilsDynMeshBuildContext BuildContext;
			BuildContext.Context = Context;
			BuildContext.SeedTransform = Settings->bConvertSeedsToLocalSpace
				? TransformRange[PointIndex].GetRelativeTransform(ActorTransform)
				: TransformRange[PointIndex];
			BuildContext.SeedLocalBounds = FBox(BoundsMinRange[PointIndex], BoundsMaxRange[PointIndex]);
			BuildContext.SeedData = SeedPointData;
			BuildContext.SeedIndex = PointIndex;

			++SeedCount;

			UDynamicMesh* SeedTarget = (OutputMode == EPCGUtilsDynMeshBuilderOutputMode::PerSeed)
				? FPCGContext::NewObject_AnyThread<UDynamicMesh>(Context) : nullptr;

			for (int32 OperationIndex = 0; OperationIndex < Operations.Num(); ++OperationIndex)
			{
				FPCGUtilsDynMeshBuildResult BuildResult;
				if (!Operations[OperationIndex]->Build(BuildContext, BuildResult) || !BuildResult.IsValid())
				{
					continue;
				}

				if (!bMaterialLayoutResolved)
				{
					ComposedMaterialOffsets[OperationIndex] = ComposedMaterials.Num();
					for (UMaterialInterface* Material : BuildResult.MeshData->GetMaterials())
					{
						ComposedMaterials.Add(Material);
						PerBuilderMaterials[OperationIndex].Add(Material);
					}
				}

				UDynamicMesh* SeedMesh = BuildResult.MeshData->GetMutableDynamicMesh();

				if (OutputMode == EPCGUtilsDynMeshBuilderOutputMode::PerBuilderPerSeed)
				{
					// The Builder's result already *is* this output; no append or ID rebasing needed.
					EmitMesh(SeedMesh, PerBuilderMaterials[OperationIndex], &SeedInput);
					continue;
				}

				UDynamicMesh* Target = SeedTarget ? SeedTarget
					: (SingleTarget ? SingleTarget : PerBuilderTargets[OperationIndex]);

				if (!bSplitByBuilder)
				{
					ShiftMaterialIDs(SeedMesh, ComposedMaterialOffsets[OperationIndex]);
				}
				UGeometryScriptLibrary_MeshBasicEditFunctions::AppendMeshTransformed(
					Target, SeedMesh, TArray<FTransform>{FTransform::Identity}, FTransform::Identity);
			}

			bMaterialLayoutResolved = true;

			if (SeedTarget)
			{
				EmitMesh(SeedTarget, ComposedMaterials, &SeedInput);
			}
		}
	}

	if (SeedCount == 0)
	{
		PCGLog::LogWarningOnGraph(LOCTEXT("NoSeeds",
			"Create Primitive (Builder mode) received no seed points."), Context);
	}

	if (SingleTarget)
	{
		EmitMesh(SingleTarget, ComposedMaterials, /*SourceSeedInput=*/nullptr);
	}
	for (int32 Index = 0; Index < PerBuilderTargets.Num(); ++Index)
	{
		// Spans every seed, so no single seed input's tags apply.
		EmitMesh(PerBuilderTargets[Index], PerBuilderMaterials[Index], /*SourceSeedInput=*/nullptr);
	}

	return true;
}

#undef LOCTEXT_NAMESPACE
