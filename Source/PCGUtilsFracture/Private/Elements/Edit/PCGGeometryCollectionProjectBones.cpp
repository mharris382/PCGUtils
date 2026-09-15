// Copyright Max Harris

#include "Elements/Edit/PCGGeometryCollectionProjectBones.h"

#include "Data/PCGDynamicMeshData.h"
#include "Data/PCGGeometryCollectionData.h"
#include "Data/PCGUtilsGeometryCollectionRevisionPublisher.h"
#include "DynamicMesh/DynamicMesh3.h"
#include "Elements/PCGUtilsDynMeshSpaceHelpers.h"
#include "Factories/PCGUtilsGeometryCollectionSelectionFactory.h"
#include "FunctionLibraries/PCGUtilsGeometryCollectionHelpers.h"
#include "FunctionLibraries/PCGUtilsGeometryCollectionHierarchy.h"
#include "FunctionLibraries/PCGUtilsGeometryCollectionSupportSampling.h"
#include "FunctionLibraries/PCGUtilsGeometryCollectionTransforms.h"
#include "GameFramework/Actor.h"
#include "Geometry/PCGUtilsProjectionEnvironment.h"
#include "Geometry/PCGUtilsProjectionSolver.h"
#include "GeometryCollection/GeometryCollection.h"
#include "PCGContext.h"
#include "PCGGraphExecutionStateInterface.h"
#include "PCGPin.h"
#include "PCGUtilsFracture.h"
#include "UDynamicMesh.h"
#include "Utils/PCGLogErrors.h"

#define LOCTEXT_NAMESPACE "PCGGCProjectBones"

namespace
{
	/** Per-collection tallies, reported once rather than per bone. */
	struct FProjectionStats
	{
		int32 NumUnits = 0;
		int32 NumProjected = 0;
		int32 NumMissed = 0;
		int32 NumWithoutGeometry = 0;
		int32 NumTraces = 0;
	};

	/**
	 * The bones a projection treats as rigid units.
	 *
	 * Reduced to a non-nested antichain, because a cluster and a piece inside it cannot both be settled
	 * independently - moving the cluster already moves the piece. Dropping the descendant is the same rule
	 * GC | Transform Bones applies by default, and for the same reason.
	 */
	void ResolveFrontier(
		const FGeometryCollection& Collection,
		const UPCGGeometryCollectionProjectBonesSettings* Settings,
		const FDataflowTransformSelection* Selection,
		TArray<int32>& OutBones)
	{
		OutBones.Reset();

		TArray<int32> Seeds;
		if (Selection)
		{
			const int32 NumTransforms = Collection.NumElements(FGeometryCollection::TransformGroup);
			for (int32 Bone = 0; Bone < NumTransforms; ++Bone)
			{
				if (Selection->IsValidIndex(Bone) && Selection->IsSelected(Bone))
				{
					Seeds.Add(Bone);
				}
			}
		}
		else
		{
			// No selection means every piece. Deliberately not every *bone*: projecting the root as one unit
			// would settle the whole collection rigidly, which is a thing a user can ask for by selecting the
			// root but is a surprising default.
			PCGUtilsGeometryCollectionHierarchy::GatherPieces(Collection, Seeds);
		}

		if (Settings->Resolution == EPCGGeometryCollectionProjectionResolution::Pieces)
		{
			TSet<int32> Pieces;
			TArray<int32> Under;
			for (const int32 Seed : Seeds)
			{
				PCGUtilsGeometryCollectionHierarchy::GatherPiecesUnder(Collection, Seed, Under);
				Pieces.Append(Under);
			}
			OutBones = Pieces.Array();
			OutBones.Sort();
		}
		else
		{
			OutBones = MoveTemp(Seeds);
		}

		// Pieces are already an antichain, so this only bites in Selection mode - which is exactly where a user
		// can hand us a cluster and one of its children at the same time.
		PCGUtilsGeometryCollectionTransforms::ReduceToAntichain(Collection, OutBones, /*OutDropped=*/nullptr);
	}

	/** Traces one bone's samples and solves the transform that settles it. */
	PCGUtilsProjectionSolver::FSolveResult ProjectOneBone(
		const FGeometryCollection& Collection,
		const UPCGGeometryCollectionProjectBonesSettings* Settings,
		const IPCGUtilsProjectionEnvironment& Environment,
		const FVector& Direction,
		int32 BoneIndex,
		TConstArrayView<FTransform> GlobalTransforms,
		const FTransform& CollectionToOutput,
		TArray<FVector>& SampleScratch,
		TArray<PCGUtilsProjectionSolver::FSupportSample>& TracedScratch,
		FProjectionStats& Stats)
	{
		SampleScratch.Reset();

		PCGUtilsGeometryCollectionSupportSampling::FSamplingSettings SamplingSettings;
		SamplingSettings.Accuracy = Settings->Accuracy;
		SamplingSettings.Direction = Direction;

		const int32 NumSamples = PCGUtilsGeometryCollectionSupportSampling::GatherSupportSamples(
			Collection, BoneIndex, GlobalTransforms, CollectionToOutput, SamplingSettings, SampleScratch);
		if (NumSamples == 0)
		{
			++Stats.NumWithoutGeometry;
			return PCGUtilsProjectionSolver::FSolveResult();
		}

		TracedScratch.Reset(NumSamples);
		for (const FVector& Sample : SampleScratch)
		{
			PCGUtilsProjectionSolver::FSupportSample Traced;
			Traced.Position = Sample;

			// Start behind the sample so a piece already intersecting the surface reports a negative distance
			// and gets pushed back out, rather than missing everything between it and the ground.
			const FVector Start = Sample - Direction * Settings->StartOffset;
			const FVector End = Sample + Direction * Settings->MaximumDistance;

			++Stats.NumTraces;
			if (Environment.Trace(Start, End, Traced.HitLocation, Traced.HitNormal))
			{
				Traced.Travel = FVector::DotProduct(Traced.HitLocation - Sample, Direction);
				Traced.bHit = true;
			}
			TracedScratch.Add(Traced);
		}

		PCGUtilsProjectionSolver::FSolveSettings SolveSettings;
		SolveSettings.Direction = Direction;
		return PCGUtilsProjectionSolver::Solve(TracedScratch, SolveSettings);
	}
}

#if WITH_EDITOR
FText UPCGGeometryCollectionProjectBonesSettings::GetDefaultNodeTitle() const
{
	return LOCTEXT("Title", "GC | Project Bones");
}

FText UPCGGeometryCollectionProjectBonesSettings::GetNodeTooltipText() const
{
	return LOCTEXT("Tooltip",
		"Settles fracture pieces onto the geometry beneath them by tracing, with no simulation. Each unit "
		"travels until its first contact, so a piece rests on its leading corner rather than sinking. Connect a "
		"Target mesh to project against it directly, or leave it empty to trace level collision. Resolution "
		"decides what settles as one rigid unit: a selected cluster keeps its pieces together, while Pieces "
		"lets every fragment fall on its own.");
}

FString UPCGGeometryCollectionProjectBonesSettings::GetAdditionalTitleInformation() const
{
	// Bounds accuracy at Selection resolution is the default and says nothing worth the graph width. Show
	// whichever of the two the user changed, since both visibly change what the node does.
	TArray<FString> Parts;
	if (Resolution == EPCGGeometryCollectionProjectionResolution::Pieces)
	{
		Parts.Add(TEXT("Pieces"));
	}
	if (Accuracy == EPCGGeometryCollectionProjectionAccuracy::Pivot)
	{
		Parts.Add(TEXT("Pivot"));
	}
	return FString::Join(Parts, TEXT(", "));
}
#endif

TArray<FPCGPinProperties> UPCGGeometryCollectionProjectBonesSettings::InputPinProperties() const
{
	TArray<FPCGPinProperties> Pins;
	Pins.Emplace_GetRef(
		PCGGeometryCollectionProjectBonesConstants::CollectionInputPin,
		FPCGGeometryCollectionDataTypeInfo::AsId(), /*bAllowMultipleConnections=*/true, /*bAllowMultipleData=*/true)
		.SetRequiredPin();
	// Optional: unconnected means trace the level instead.
	Pins.Emplace(
		PCGGeometryCollectionProjectBonesConstants::TargetInputPin,
		EPCGDataType::DynamicMesh, /*bAllowMultipleConnections=*/false, /*bAllowMultipleData=*/false);
	Pins.Emplace(
		PCGUtilsGeometryCollectionSelectionFactoryConstants::SelectionInputPin,
		FPCGUtilsGeometryCollectionSelectionFactoryDataTypeInfo::AsId(),
		/*bAllowMultipleConnections=*/false, /*bAllowMultipleData=*/false);
	return Pins;
}

TArray<FPCGPinProperties> UPCGGeometryCollectionProjectBonesSettings::OutputPinProperties() const
{
	TArray<FPCGPinProperties> Pins;
	Pins.Add(FPCGPinProperties(
		PCGGeometryCollectionProjectBonesConstants::CollectionOutputPin,
		FPCGGeometryCollectionDataTypeInfo::AsId(), true, true));
	return Pins;
}

FPCGElementPtr UPCGGeometryCollectionProjectBonesSettings::CreateElement() const
{
	return MakeShared<FPCGGeometryCollectionProjectBonesElement>();
}

bool FPCGGeometryCollectionProjectBonesElement::ExecuteInternal(FPCGContext* Context) const
{
	check(Context);
	const UPCGGeometryCollectionProjectBonesSettings* Settings =
		Context->GetInputSettings<UPCGGeometryCollectionProjectBonesSettings>();
	check(Settings);

	const FVector Direction = Settings->Direction.GetSafeNormal();
	if (Direction.IsNearlyZero())
	{
		PCGLog::LogErrorOnGraph(
			LOCTEXT("ZeroDirection", "GC Project Bones needs a non-zero Direction."), Context);
		return true;
	}

	// --- Pick an environment ---------------------------------------------------------------------------
	//
	// A connected Target is traced in the collection's own space: GC data and DynMesh data are both authored in
	// target-actor-local space, so they already share one, and involving the actor transform would only be a
	// way to get it wrong. World collision is the opposite - it only exists in world space - so that path
	// resolves the actor transform and works there.
	const UPCGDynamicMeshData* TargetData = nullptr;
	for (const FPCGTaggedData& Input :
		Context->InputData.GetInputsByPin(PCGGeometryCollectionProjectBonesConstants::TargetInputPin))
	{
		if (const UPCGDynamicMeshData* Candidate = Cast<const UPCGDynamicMeshData>(Input.Data))
		{
			TargetData = Candidate;
			break;
		}
	}

	TUniquePtr<IPCGUtilsProjectionEnvironment> Environment;
	FTransform CollectionToOutput = FTransform::Identity;

	if (TargetData)
	{
		const UDynamicMesh* TargetMesh = TargetData->GetDynamicMesh();
		const UE::Geometry::FDynamicMesh3* Mesh = TargetMesh ? TargetMesh->GetMeshPtr() : nullptr;
		if (!Mesh || Mesh->TriangleCount() == 0)
		{
			PCGLog::LogErrorOnGraph(
				LOCTEXT("EmptyTarget", "GC Project Bones was given a Target mesh with no triangles."), Context);
			return true;
		}

		TUniquePtr<FPCGUtilsDynMeshProjectionEnvironment> MeshEnvironment =
			MakeUnique<FPCGUtilsDynMeshProjectionEnvironment>(*Mesh, FTransform::Identity);
		if (!MeshEnvironment->IsUsable())
		{
			PCGLog::LogErrorOnGraph(
				LOCTEXT("UnusableTarget", "GC Project Bones could not build a spatial index for the Target mesh."),
				Context);
			return true;
		}
		Environment = MoveTemp(MeshEnvironment);
	}
	else
	{
		const UWorld* World = Context->ExecutionSource.IsValid()
			? Context->ExecutionSource->GetExecutionState().GetWorld() : nullptr;
		if (!World)
		{
			PCGLog::LogErrorOnGraph(
				LOCTEXT("NoWorld",
					"GC Project Bones has no world to trace against. Connect a Target mesh, or run this in a "
					"graph with a PCG component."), Context);
			return true;
		}

		CollectionToOutput = PCGUtilsDynMeshSpaceHelpers::ResolveMeshActorTransform(
			Context, /*MeshData=*/nullptr, /*bConvertToLocalSpace=*/true);

		FPCGUtilsWorldProjectionQueryParams QueryParams;
		QueryParams.CollisionChannel = Settings->CollisionChannel;
		QueryParams.bTraceComplex = Settings->bTraceComplex;
		if (Settings->bIgnoreTargetActor)
		{
			if (const AActor* TargetActor = Context->GetTargetActor(nullptr))
			{
				QueryParams.IgnoredActors.Add(TargetActor);
			}
		}
		Environment = MakeUnique<FPCGUtilsWorldProjectionEnvironment>(World, MoveTemp(QueryParams));
	}

	// Collection space -> output space, so the delta the solver returns in output space can be conjugated back.
	const FTransform OutputToCollection = CollectionToOutput.Inverse();

	for (const FPCGTaggedData& Input :
		Context->InputData.GetInputsByPin(PCGGeometryCollectionProjectBonesConstants::CollectionInputPin))
	{
		const UPCGGeometryCollectionData* InputData = Cast<const UPCGGeometryCollectionData>(Input.Data);
		if (!InputData || !InputData->HasCollection())
		{
			PCGLog::LogWarningOnGraph(
				LOCTEXT("InvalidInput", "GC Project Bones skipped an input with no valid Geometry Collection."),
				Context);
			continue;
		}

		if (InputData->NumTransforms() == 0)
		{
			PCGLog::LogErrorOnGraph(
				LOCTEXT("EmptyCollection", "GC Project Bones received an empty Geometry Collection."), Context);
			continue;
		}

		auto PassThrough = [Context, &Input]()
		{
			// Preserves object identity and StateId, so a selection authored against this collection downstream
			// still resolves. Publishing an identical revision would break that for no benefit.
			FPCGTaggedData& Output = Context->OutputData.TaggedData.Emplace_GetRef(Input);
			Output.Pin = PCGGeometryCollectionProjectBonesConstants::CollectionOutputPin;
		};

		TSharedRef<FGeometryCollection> Collection = InputData->CreateMutableCopy();

		FDataflowTransformSelection Selection;
		bool bHasSelection = false;
		{
			const FPCGUtilsGeometryCollectionSelectionEvaluationContext EvaluationContext(*InputData, *Collection);
			if (!PCGUtilsGeometryCollectionSelectionFactories::ResolveSelectionFromPin(
				Context, PCGUtilsGeometryCollectionSelectionFactoryConstants::SelectionInputPin,
				EvaluationContext, /*bRequired=*/false, Selection, bHasSelection))
			{
				continue;
			}
		}

		TArray<int32> Frontier;
		ResolveFrontier(*Collection, Settings, bHasSelection ? &Selection : nullptr, Frontier);
		if (Frontier.IsEmpty())
		{
			PCGLog::LogWarningOnGraph(
				LOCTEXT("NoTargets", "GC Project Bones resolved no bones to project."), Context);
			PassThrough();
			continue;
		}

		// Resolved once, before anything moves: every unit is measured against the collection as it arrived, so
		// two fragments cannot settle onto each other's new positions depending on processing order.
		TArray<FTransform> GlobalTransforms;
		PCGUtilsGeometryCollectionHelpers::ComputeGlobalTransforms(*Collection, GlobalTransforms);

		FProjectionStats Stats;
		Stats.NumUnits = Frontier.Num();

		TArray<int32> MovedBones;
		TArray<FTransform> MovedDeltas;
		MovedBones.Reserve(Frontier.Num());
		MovedDeltas.Reserve(Frontier.Num());

		TArray<FVector> SampleScratch;
		TArray<PCGUtilsProjectionSolver::FSupportSample> TracedScratch;

		for (const int32 Bone : Frontier)
		{
			// Snapshot rather than read the running total: "this bone had nothing to sample" and "some earlier
			// bone had nothing to sample" are different questions, and only the first one is being asked here.
			const int32 WithoutGeometryBefore = Stats.NumWithoutGeometry;

			const PCGUtilsProjectionSolver::FSolveResult Solved = ProjectOneBone(
				*Collection, Settings, *Environment, Direction, Bone, GlobalTransforms, CollectionToOutput,
				SampleScratch, TracedScratch, Stats);

			if (!Solved.bSolved)
			{
				if (Stats.NumWithoutGeometry == WithoutGeometryBefore)
				{
					// It had samples; they simply found nothing within range.
					++Stats.NumMissed;
				}
				continue;
			}

			// The solve is in output space; bone transforms are in collection space. Conjugating rather than
			// just rotating the vector keeps this correct when rotation support lands.
			const FTransform DeltaInCollection = CollectionToOutput * Solved.Delta * OutputToCollection;

			MovedBones.Add(Bone);
			MovedDeltas.Add(DeltaInCollection);
			++Stats.NumProjected;
		}

		if (MovedBones.IsEmpty())
		{
			PCGLog::LogWarningOnGraph(FText::Format(
				LOCTEXT("NothingHit",
					"GC Project Bones found nothing beneath any of its {0} unit(s) within {1} units, so nothing "
					"moved. Check the direction, the maximum distance, and that the target has collision."),
				FText::AsNumber(Stats.NumUnits), FText::AsNumber(Settings->MaximumDistance)), Context);
			PassThrough();
			continue;
		}

		FPCGUtilsGeometryCollectionBoneTransformResult ApplyResult;
		if (!PCGUtilsGeometryCollectionTransforms::ApplyBoneDeltas(
			*Collection, MovedBones, MovedDeltas,
			EPCGGeometryCollectionNestedBoneHandling::Topmost, ApplyResult))
		{
			PCGLog::LogErrorOnGraph(
				LOCTEXT("ApplyFailed", "GC Project Bones could not apply its solved transforms."), Context);
			continue;
		}

		if (Stats.NumMissed > 0 || Stats.NumWithoutGeometry > 0)
		{
			PCGLog::LogWarningOnGraph(FText::Format(
				LOCTEXT("PartialProjection",
					"GC Project Bones left {0} unit(s) untouched: {1} found nothing within range and {2} had no "
					"geometry to sample."),
				FText::AsNumber(Stats.NumMissed + Stats.NumWithoutGeometry),
				FText::AsNumber(Stats.NumMissed), FText::AsNumber(Stats.NumWithoutGeometry)), Context);
		}

		// Transforms only: no bone was added, removed or reparented, and geometry is bone-local so not a vertex
		// changed. The publisher carries the piece mesh cache over and drops Proximity.
		FPCGUtilsGeometryCollectionMutationResult Mutation;
		Mutation.bTransformsChanged = true;

		UPCGGeometryCollectionData* OutputData = PCGUtilsGeometryCollectionRevisionPublisher::PublishRevision(
			Context, InputData, Collection, Mutation);
		if (!OutputData)
		{
			continue;
		}

		FPCGTaggedData& Output = Context->OutputData.TaggedData.Emplace_GetRef(Input);
		Output.Data = OutputData;
		Output.Pin = PCGGeometryCollectionProjectBonesConstants::CollectionOutputPin;

		UE_LOG(LogPCGUtilsFracture, Log,
			TEXT("GC Project Bones: %d of %d unit(s) settled against %s using %d trace(s). Result %s (revision %d)"),
			ApplyResult.NumApplied, Stats.NumUnits, *Environment->Describe(), Stats.NumTraces,
			*PCGUtilsGeometryCollectionHelpers::DescribeCollection(*Collection), OutputData->GetRevision());
	}

	return true;
}

#undef LOCTEXT_NAMESPACE
