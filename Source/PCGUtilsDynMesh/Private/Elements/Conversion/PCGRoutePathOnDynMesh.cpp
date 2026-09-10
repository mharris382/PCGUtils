// Copyright Max Harris

#include "Elements/Conversion/PCGRoutePathOnDynMesh.h"

#include "Data/PCGBasePointData.h"
#include "Data/PCGPointArrayData.h"
#include "Elements/Conversion/PCGUtilsDynMeshSurfacePathCommon.h"
#include "Elements/PCGUtilsDynMeshSpaceHelpers.h"
#include "Geometry/PCGUtilsDynMeshSurfacePathing.h"
#include "Metadata/PCGMetadata.h"
#include "Metadata/PCGMetadataAttributeTpl.h"
#include "PCGContext.h"
#include "PCGPin.h"
#include "Utils/PCGLogErrors.h"

#define LOCTEXT_NAMESPACE "PCGRoutePathOnDynMesh"

namespace
{
	namespace Pathing = PCGUtilsDynMeshSurfacePathing;
	namespace Common = PCGUtilsDynMeshSurfacePathCommon;

	/** Detailed per-segment warnings past this count are replaced by one aggregate line per guide path. */
	constexpr int32 MaxDetailedSegmentWarnings = 3;

	FText RouteNodeTitle()
	{
		return LOCTEXT("NodeTitleText", "Route Path On DynMesh");
	}

	/** Reads the input path's closed-loop flag, defaulting to open when the attribute is absent. */
	bool ReadIsClosed(const UPCGBasePointData* PointData, FName AttributeName)
	{
		if (!PointData || AttributeName.IsNone())
		{
			return false;
		}

		const UPCGMetadata* Metadata = PointData->ConstMetadata();
		const FPCGMetadataDomain* DataDomain =
			Metadata ? Metadata->GetConstMetadataDomain(PCGMetadataDomainID::Data) : nullptr;
		const FPCGMetadataAttribute<bool>* Attribute =
			DataDomain ? DataDomain->GetConstTypedAttribute<bool>(AttributeName) : nullptr;
		return Attribute ? Attribute->GetValueFromItemKey(PCGInvalidEntryKey) : false;
	}

	/** One segment that could not be solved, kept so warnings can be aggregated per guide path. */
	struct FSegmentFailure
	{
		int32 SegmentIndex = INDEX_NONE;
		int32 StartGuideIndex = INDEX_NONE;
		int32 EndGuideIndex = INDEX_NONE;
		Pathing::ESurfaceRouteStatus Status = Pathing::ESurfaceRouteStatus::Ok;
	};

	FText DescribeFailure(const FSegmentFailure& Failure, double MaxProjectionDistance)
	{
		const FText DistanceText = (MaxProjectionDistance > 0.0)
			? FText::AsNumber(MaxProjectionDistance)
			: LOCTEXT("UnlimitedDistance", "unlimited");

		switch (Failure.Status)
		{
		case Pathing::ESurfaceRouteStatus::StartNotOnMesh:
			return FText::Format(
				LOCTEXT("StartNotOnMesh",
					"Route Path On DynMesh could not solve segment {0} between guide points {1} and {2} because guide point {1} has no surface within Max Projection Distance ({3})."),
				FText::AsNumber(Failure.SegmentIndex), FText::AsNumber(Failure.StartGuideIndex),
				FText::AsNumber(Failure.EndGuideIndex), DistanceText);

		case Pathing::ESurfaceRouteStatus::EndNotOnMesh:
			return FText::Format(
				LOCTEXT("EndNotOnMesh",
					"Route Path On DynMesh could not solve segment {0} between guide points {1} and {2} because guide point {2} has no surface within Max Projection Distance ({3})."),
				FText::AsNumber(Failure.SegmentIndex), FText::AsNumber(Failure.StartGuideIndex),
				FText::AsNumber(Failure.EndGuideIndex), DistanceText);

		case Pathing::ESurfaceRouteStatus::Disconnected:
		default:
			return FText::Format(
				LOCTEXT("Disconnected",
					"Route Path On DynMesh could not solve segment {0} between guide points {1} and {2} because their projected points lie on disconnected mesh components."),
				FText::AsNumber(Failure.SegmentIndex), FText::AsNumber(Failure.StartGuideIndex),
				FText::AsNumber(Failure.EndGuideIndex));
		}
	}
}

#if WITH_EDITOR
FText UPCGRoutePathOnDynMeshSettings::GetDefaultNodeTitle() const
{
	return LOCTEXT("NodeTitle", "DynMesh|Route Path");
}

FText UPCGRoutePathOnDynMeshSettings::GetNodeTooltipText() const
{
	return LOCTEXT("NodeTooltip",
		"Projects the input path's guide points onto a Dynamic Mesh and computes a shortest surface path between "
		"each consecutive pair, concatenated into one path that follows the mesh. A Selection or Selector "
		"restricts routing to the selected triangle region.");
}
#endif

TArray<FPCGPinProperties> UPCGRoutePathOnDynMeshSettings::InputPinProperties() const
{
	TArray<FPCGPinProperties> Pins = Super::InputPinProperties();

	// Pin order follows the repository rule: the data the output is a mutation of comes first, then the required
	// dependency it is mutated against, then optional pins. The base class contributes Mesh (required) and
	// Selector (optional) in that order, so Path only has to go in front of both.
	Pins.EmplaceAt_GetRef(0, PCGRoutePathOnDynMeshConstants::PathInputPin, EPCGDataType::Point, true, true)
		.SetRequiredPin();
	return Pins;
}

TArray<FPCGPinProperties> UPCGRoutePathOnDynMeshSettings::OutputPinProperties() const
{
	return {FPCGPinProperties(PCGRoutePathOnDynMeshConstants::PathsOutputPin, EPCGDataType::Point, true, true)};
}

FPCGElementPtr UPCGRoutePathOnDynMeshSettings::CreateElement() const
{
	return MakeShared<FPCGRoutePathOnDynMeshElement>();
}

bool FPCGRoutePathOnDynMeshElement::ExecuteInternal(FPCGContext* Context) const
{
	TRACE_CPUPROFILER_EVENT_SCOPE(FPCGRoutePathOnDynMeshElement::ExecuteInternal);

	check(Context);
	const UPCGRoutePathOnDynMeshSettings* Settings = Context->GetInputSettings<UPCGRoutePathOnDynMeshSettings>();
	check(Settings);

	const TArray<FPCGTaggedData> PathInputs =
		Context->InputData.GetInputsByPin(PCGRoutePathOnDynMeshConstants::PathInputPin);

	for (const FPCGTaggedData& MeshInput :
		Context->InputData.GetInputsByPin(PCGRoutePathOnDynMeshConstants::MeshInputPin))
	{
		// One resolve and one AABB tree per mesh input, reused by every guide path routed across it.
		Common::FResolvedSurface Surface =
			Common::ResolveSurface(MeshInput.Data, Settings, Context, RouteNodeTitle());
		if (!Surface.IsValid())
		{
			continue;
		}

		const FTransform MeshToOutput = PCGUtilsDynMeshSpaceHelpers::ResolveMeshActorTransform(
			Context, Surface.MeshData, Settings->bWorldSpace);
		const FTransform OutputToMesh = MeshToOutput.Inverse();

		for (const FPCGTaggedData& PathInput : PathInputs)
		{
			const UPCGBasePointData* GuideData = Cast<const UPCGBasePointData>(PathInput.Data);
			if (!GuideData || GuideData->GetNumPoints() < 2)
			{
				PCGLog::LogWarningOnGraph(
					LOCTEXT("TooFewGuidePoints",
						"Route Path On DynMesh skipped an input path with fewer than two guide points; at least two are needed to define one segment."),
					Context);
				continue;
			}

			const int32 NumGuides = GuideData->GetNumPoints();
			TArray<FVector3d> MeshLocalGuides;
			MeshLocalGuides.Reserve(NumGuides);
			{
				const TConstPCGValueRange<FTransform> GuideTransforms = GuideData->GetConstTransformValueRange();
				for (int32 Index = 0; Index < NumGuides; ++Index)
				{
					MeshLocalGuides.Add(FVector3d(
						OutputToMesh.TransformPosition(GuideTransforms[Index].GetLocation())));
				}
			}

			// Every guide point is projected exactly once, in one batched pass against the shared tree - the
			// anchor a segment ends on is the same anchor the next segment starts from.
			PCGUtilsDynMeshSurfaceCorrespondence::FProjectionOptions ProjectionOptions;
			ProjectionOptions.MaxDistance = (Settings->MaxProjectionDistance > 0.0)
				? Settings->MaxProjectionDistance
				: TNumericLimits<double>::Max();
			const PCGUtilsDynMeshSurfaceCorrespondence::FMeshSurfaceProjectionResult Projection =
				PCGUtilsDynMeshSurfaceCorrespondence::ProjectPoints(*Surface.Tree, MeshLocalGuides, ProjectionOptions);

			// One working mesh copy per guide path: the router pokes each anchor in as a real vertex, which is
			// what lets every segment of this path be solved without copying the mesh again.
			Pathing::FSurfaceRouter Router(*Surface.Mesh);
			const TArray<Pathing::FSurfaceAnchor> Anchors = Router.InsertAnchors(Projection.Projections);

			const bool bClosed = Settings->bRouteClosedPaths
				&& ReadIsClosed(GuideData, Settings->IsClosedAttributeName);
			const int32 NumSegments = bClosed ? NumGuides : NumGuides - 1;

			TArray<FVector3d> RoutedPositions;
			// Index-aligned with RoutedPositions: which guide points each routed point came from. Routing
			// inserts a point at every triangle crossing, so this correspondence is the only thing that lets the
			// routed path stay a mutation of the guide path rather than an unrelated new one.
			TArray<Common::FPathSourceRef> RoutedSources;
			TArray<FVector3d> SegmentPositions;
			TArray<double> SegmentDistances;
			TArray<FSegmentFailure> Failures;

			// Records where along [StartGuide, EndGuide] each point AppendSegment just kept actually sits.
			// The span is parameterized by arc length along the solved surface path, not by index, because the
			// solver spaces points by triangle crossings and index spacing would skew the blend.
			auto AppendSegmentSources =
				[&RoutedSources, &SegmentDistances](
					TConstArrayView<FVector3d> Segment, int32 FirstKept, int32 StartGuide, int32 EndGuide)
			{
				if (Segment.IsEmpty() || FirstKept >= Segment.Num())
				{
					return;
				}

				SegmentDistances.SetNumUninitialized(Segment.Num(), EAllowShrinking::No);
				SegmentDistances[0] = 0.0;
				for (int32 Index = 1; Index < Segment.Num(); ++Index)
				{
					SegmentDistances[Index] =
						SegmentDistances[Index - 1] + FVector3d::Distance(Segment[Index - 1], Segment[Index]);
				}

				const double TotalLength = SegmentDistances[Segment.Num() - 1];
				for (int32 Index = FMath::Max(FirstKept, 0); Index < Segment.Num(); ++Index)
				{
					// A zero-length segment has no meaningful parameter; its points all sit on the start anchor.
					const float Alpha = (TotalLength > UE_DOUBLE_SMALL_NUMBER)
						? static_cast<float>(SegmentDistances[Index] / TotalLength)
						: 0.0f;
					RoutedSources.Add(Common::FPathSourceRef{
						StartGuide, EndGuide, FMath::Clamp(Alpha, 0.0f, 1.0f)});
				}
			};

			for (int32 SegmentIndex = 0; SegmentIndex < NumSegments; ++SegmentIndex)
			{
				const int32 StartIndex = SegmentIndex;
				const int32 EndIndex = (SegmentIndex + 1) % NumGuides;

				const Pathing::ESurfaceRouteStatus Status =
					Router.RouteSegment(Anchors[StartIndex], Anchors[EndIndex], SegmentPositions);

				if (Status == Pathing::ESurfaceRouteStatus::Ok)
				{
					const int32 NumBefore = RoutedPositions.Num();
					Pathing::AppendSegment(RoutedPositions, SegmentPositions, Pathing::PathJoinTolerance);

					// AppendSegment drops the segment's leading point when it welds onto the previous segment's
					// end, so the kept run is the tail of SegmentPositions of exactly the length it added.
					const int32 NumKept = RoutedPositions.Num() - NumBefore;
					AppendSegmentSources(
						SegmentPositions, SegmentPositions.Num() - NumKept, StartIndex, EndIndex);
				}
				else if (Status == Pathing::ESurfaceRouteStatus::Coincident)
				{
					// Two guide points resolved to the same surface location. The segment is empty by
					// construction, not a failure - the anchor itself is still contributed by its neighbours.
					if (RoutedPositions.IsEmpty() && Anchors[StartIndex].bValid)
					{
						RoutedPositions.Add(Anchors[StartIndex].Position);
						RoutedSources.Add(Common::FPathSourceRef{StartIndex, StartIndex, 0.0f});
					}
				}
				else
				{
					Failures.Add(FSegmentFailure{SegmentIndex, StartIndex, EndIndex, Status});
				}
			}

			for (int32 FailureIndex = 0; FailureIndex < Failures.Num(); ++FailureIndex)
			{
				if (FailureIndex >= MaxDetailedSegmentWarnings)
				{
					PCGLog::LogWarningOnGraph(FText::Format(
						LOCTEXT("MoreSegmentFailures",
							"Route Path On DynMesh could not solve {0} further segments of this path for the same reasons; only the first {1} are reported individually."),
						FText::AsNumber(Failures.Num() - MaxDetailedSegmentWarnings),
						FText::AsNumber(MaxDetailedSegmentWarnings)), Context);
					break;
				}
				PCGLog::LogWarningOnGraph(
					DescribeFailure(Failures[FailureIndex], Settings->MaxProjectionDistance), Context);
			}

			// A closed route ends back on its first anchor; drop that repeat so the output follows the same
			// closed-path convention as DynMesh Selection To Paths (first point never repeated at the end).
			const bool bFullyRouted = Failures.IsEmpty();
			if (bClosed && bFullyRouted && RoutedPositions.Num() > 2
				&& FVector3d::DistSquared(RoutedPositions[0], RoutedPositions.Last())
					<= Pathing::PathJoinTolerance * Pathing::PathJoinTolerance)
			{
				RoutedPositions.Pop();
				RoutedSources.Pop();
			}

			if (RoutedPositions.Num() < 2)
			{
				PCGLog::LogWarningOnGraph(bFullyRouted
					? LOCTEXT("DegenerateRoutedPath",
						"Route Path On DynMesh produced no path for an input path: every one of its guide points projected onto the same surface location, leaving no geometry to route.")
					: LOCTEXT("NoRoutedPath",
						"Route Path On DynMesh produced no path for an input path: none of its segments could be solved."),
					Context);
				continue;
			}

			Common::FPathOutputOptions OutputOptions;
			OutputOptions.MeshToOutput = MeshToOutput;
			// A path whose closing segment failed is no longer a loop, and must not claim to be one.
			OutputOptions.bClosed = bClosed && bFullyRouted;
			OutputOptions.IsClosedAttributeName = Settings->IsClosedAttributeName;
			OutputOptions.PointSteepness = Settings->PointSteepness;

			// The routed path is this guide path, mutated onto the surface: it is initialized from the guide path so
			// its attributes survive, and RoutedSources says which guide points every routed point inherits from.
			if (Settings->MetadataInheritance != EPCGUtilsDynMeshRoutePathInheritance::None)
			{
				OutputOptions.Inheritance.SourceData = GuideData;
				OutputOptions.Inheritance.PointSources = RoutedSources;
				OutputOptions.Inheritance.bInterpolate =
					(Settings->MetadataInheritance == EPCGUtilsDynMeshRoutePathInheritance::Interpolate);
			}

			UPCGPointArrayData* OutputData =
				Common::BuildPathData(Context, RoutedPositions, *Surface.Tree, OutputOptions);
			if (!OutputData)
			{
				continue;
			}

			// Carry both sources' tags, so a routed path keeps its guide path's identity as well as the mesh's.
			FPCGTaggedData& Output = Context->OutputData.TaggedData.Emplace_GetRef(PathInput);
			Output.Data = OutputData;
			Output.Pin = PCGRoutePathOnDynMeshConstants::PathsOutputPin;
			Output.Tags.Append(MeshInput.Tags);
		}
	}

	return true;
}

#undef LOCTEXT_NAMESPACE
