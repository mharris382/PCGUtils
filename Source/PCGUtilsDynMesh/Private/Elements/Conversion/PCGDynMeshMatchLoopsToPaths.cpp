// Copyright Max Harris

#include "Elements/Conversion/PCGDynMeshMatchLoopsToPaths.h"

#include "Data/PCGBasePointData.h"
#include "PCGContext.h"
#include "PCGPin.h"
#include "Utils/PCGLogErrors.h"

#define LOCTEXT_NAMESPACE "PCGDynMeshMatchLoopsToPaths"

namespace
{
	TArray<FVector> ReadMatchLoopPositions(const UPCGBasePointData* PointData, const FVector& IgnoreAxis)
	{
		TArray<FVector> Positions;
		const TConstPCGValueRange<FTransform> Transforms = PointData->GetConstTransformValueRange();
		Positions.Reserve(Transforms.Num());
		for (int32 Index = 0; Index < Transforms.Num(); ++Index)
		{
			const FVector Position = Transforms[Index].GetLocation();
			Positions.Add(Position - IgnoreAxis * FVector::DotProduct(Position, IgnoreAxis));
		}
		return Positions;
	}

	/** Average distance from each point of From to the closed polyline To. */
	double AverageDistanceToClosedPolyline(const TArray<FVector>& From, const TArray<FVector>& To)
	{
		if (From.IsEmpty() || To.IsEmpty())
		{
			return TNumericLimits<double>::Max();
		}

		double Total = 0.0;
		for (const FVector& Point : From)
		{
			double BestDistSq = TNumericLimits<double>::Max();
			for (int32 Index = 0; Index < To.Num(); ++Index)
			{
				const FVector Closest = FMath::ClosestPointOnSegment(Point, To[Index], To[(Index + 1) % To.Num()]);
				BestDistSq = FMath::Min(BestDistSq, FVector::DistSquared(Point, Closest));
			}
			Total += FMath::Sqrt(BestDistSq);
		}
		return Total / From.Num();
	}
}

#if WITH_EDITOR
FText UPCGDynMeshMatchLoopsToPathsSettings::GetDefaultNodeTitle() const
{
	return LOCTEXT("NodeTitle", "DynMesh | Match Loops To Paths");
}

FText UPCGDynMeshMatchLoopsToPathsSettings::GetNodeTooltipText() const
{
	return LOCTEXT("NodeTooltip",
		"For each Target path, emits the Loop that lies closest to it. Use it to find which mesh boundary loop "
		"(from DynMesh | Selection To Paths) an authored path produced. Both outputs are ordered by Target and "
		"have the same count, so a Copy Attributes node downstream can carry each Target's data attributes onto "
		"its Loop. Loops and Targets pass through unchanged.");
}
#endif

TArray<FPCGPinProperties> UPCGDynMeshMatchLoopsToPathsSettings::InputPinProperties() const
{
	TArray<FPCGPinProperties> Pins;
	Pins.Emplace_GetRef(PCGDynMeshMatchLoopsToPathsConstants::LoopsPin, EPCGDataType::Point, true, true,
		LOCTEXT("LoopsTooltip", "Candidate closed loops, typically every boundary loop of a mesh.")).SetRequiredPin();
	Pins.Emplace_GetRef(PCGDynMeshMatchLoopsToPathsConstants::TargetsPin, EPCGDataType::Point, true, true,
		LOCTEXT("TargetsTooltip", "Closed paths to find a loop for. One loop is emitted per matched target.")).SetRequiredPin();
	return Pins;
}

TArray<FPCGPinProperties> UPCGDynMeshMatchLoopsToPathsSettings::OutputPinProperties() const
{
	TArray<FPCGPinProperties> Pins;
	Pins.Emplace(PCGDynMeshMatchLoopsToPathsConstants::LoopsPin, EPCGDataType::Point, true, true,
		LOCTEXT("OutLoopsTooltip", "The closest loop for each matched target, in target order."));
	Pins.Emplace(PCGDynMeshMatchLoopsToPathsConstants::TargetsPin, EPCGDataType::Point, true, true,
		LOCTEXT("OutTargetsTooltip", "The matched targets, in the same order and count as Loops."));
	Pins.Emplace(PCGDynMeshMatchLoopsToPathsConstants::UnmatchedTargetsPin, EPCGDataType::Point, true, true,
		LOCTEXT("OutUnmatchedTooltip", "Targets with no loop within Max Average Distance."));
	return Pins;
}

FPCGElementPtr UPCGDynMeshMatchLoopsToPathsSettings::CreateElement() const
{
	return MakeShared<FPCGDynMeshMatchLoopsToPathsElement>();
}

bool FPCGDynMeshMatchLoopsToPathsElement::ExecuteInternal(FPCGContext* Context) const
{
	TRACE_CPUPROFILER_EVENT_SCOPE(FPCGDynMeshMatchLoopsToPathsElement::ExecuteInternal);
	check(Context);

	const UPCGDynMeshMatchLoopsToPathsSettings* Settings = Context->GetInputSettings<UPCGDynMeshMatchLoopsToPathsSettings>();
	check(Settings);

	const FVector IgnoreAxis = Settings->IgnoreAxis.GetSafeNormal();

	const TArray<FPCGTaggedData> LoopInputs = Context->InputData.GetInputsByPin(PCGDynMeshMatchLoopsToPathsConstants::LoopsPin);
	const TArray<FPCGTaggedData> TargetInputs = Context->InputData.GetInputsByPin(PCGDynMeshMatchLoopsToPathsConstants::TargetsPin);

	TArray<TArray<FVector>> LoopPositions;
	LoopPositions.SetNum(LoopInputs.Num());
	for (int32 LoopIndex = 0; LoopIndex < LoopInputs.Num(); ++LoopIndex)
	{
		if (const UPCGBasePointData* LoopData = Cast<const UPCGBasePointData>(LoopInputs[LoopIndex].Data))
		{
			LoopPositions[LoopIndex] = ReadMatchLoopPositions(LoopData, IgnoreAxis);
		}
	}

	for (int32 TargetIndex = 0; TargetIndex < TargetInputs.Num(); ++TargetIndex)
	{
		const FPCGTaggedData& TargetInput = TargetInputs[TargetIndex];
		const UPCGBasePointData* TargetData = Cast<const UPCGBasePointData>(TargetInput.Data);
		if (!TargetData)
		{
			PCGLog::LogWarningOnGraph(FText::Format(
				LOCTEXT("TargetNotPoints", "Match Loops To Paths: Targets input {0} is not point data; skipped."),
				FText::AsNumber(TargetIndex)), Context);
			continue;
		}

		const TArray<FVector> TargetPositions = ReadMatchLoopPositions(TargetData, IgnoreAxis);

		int32 BestLoop = INDEX_NONE;
		double BestDistance = TNumericLimits<double>::Max();
		for (int32 LoopIndex = 0; LoopIndex < LoopPositions.Num(); ++LoopIndex)
		{
			if (LoopPositions[LoopIndex].Num() < 3 || TargetPositions.Num() < 3)
			{
				continue;
			}
			// Both directions: one direction alone lets a short loop lying along part of a long path score well.
			const double Distance = 0.5 * (
				AverageDistanceToClosedPolyline(LoopPositions[LoopIndex], TargetPositions) +
				AverageDistanceToClosedPolyline(TargetPositions, LoopPositions[LoopIndex]));
			if (Distance < BestDistance)
			{
				BestDistance = Distance;
				BestLoop = LoopIndex;
			}
		}

		const bool bWithinRange = Settings->MaxAverageDistance <= 0.0 || BestDistance <= Settings->MaxAverageDistance;
		if (BestLoop == INDEX_NONE || !bWithinRange)
		{
			if (BestLoop == INDEX_NONE)
			{
				PCGLog::LogWarningOnGraph(FText::Format(
					LOCTEXT("NoCandidate", "Match Loops To Paths: target {0} has no loop to match: {1} loops were supplied and a loop and a target each need at least 3 points."),
					FText::AsNumber(TargetIndex), FText::AsNumber(LoopInputs.Num())), Context);
			}
			else
			{
				PCGLog::LogWarningOnGraph(FText::Format(
					LOCTEXT("TooFar", "Match Loops To Paths: target {0}'s closest loop is {1} away on average, beyond Max Average Distance ({2})."),
					FText::AsNumber(TargetIndex), FText::AsNumber(BestDistance), FText::AsNumber(Settings->MaxAverageDistance)), Context);
			}
			FPCGTaggedData& Unmatched = Context->OutputData.TaggedData.Emplace_GetRef(TargetInput);
			Unmatched.Pin = PCGDynMeshMatchLoopsToPathsConstants::UnmatchedTargetsPin;
			continue;
		}

		FPCGTaggedData& LoopOutput = Context->OutputData.TaggedData.Emplace_GetRef(LoopInputs[BestLoop]);
		LoopOutput.Pin = PCGDynMeshMatchLoopsToPathsConstants::LoopsPin;
		FPCGTaggedData& TargetOutput = Context->OutputData.TaggedData.Emplace_GetRef(TargetInput);
		TargetOutput.Pin = PCGDynMeshMatchLoopsToPathsConstants::TargetsPin;
	}

	return true;
}

#undef LOCTEXT_NAMESPACE
