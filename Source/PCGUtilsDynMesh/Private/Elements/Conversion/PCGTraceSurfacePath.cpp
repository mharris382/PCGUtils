// Copyright Max Harris

#include "Elements/Conversion/PCGTraceSurfacePath.h"

#include "Data/PCGBasePointData.h"
#include "Data/PCGPointArrayData.h"
#include "Elements/Conversion/PCGUtilsDynMeshSurfacePathCommon.h"
#include "Elements/PCGUtilsDynMeshSpaceHelpers.h"
#include "Geometry/PCGUtilsDynMeshSurfacePathing.h"
#include "Metadata/Accessors/IPCGAttributeAccessor.h"
#include "Metadata/Accessors/PCGAttributeAccessorHelpers.h"
#include "Metadata/Accessors/PCGAttributeAccessorKeys.h"
#include "Metadata/PCGMetadata.h"
#include "Metadata/PCGMetadataAttributeTpl.h"
#include "PCGContext.h"
#include "PCGPin.h"
#include "Utils/PCGLogErrors.h"

#define LOCTEXT_NAMESPACE "PCGTraceSurfacePath"

namespace
{
	namespace Pathing = PCGUtilsDynMeshSurfacePathing;
	namespace Common = PCGUtilsDynMeshSurfacePathCommon;

	/** Detailed per-seed warnings past this count are replaced by one aggregate line per seed dataset. */
	constexpr int32 MaxDetailedSeedWarnings = 3;

	FText TraceNodeTitle()
	{
		return LOCTEXT("NodeTitleText", "Trace Surface Path");
	}

	FVector AxisVector(EPCGUtilsDynMeshTraceDirectionAxis Axis)
	{
		switch (Axis)
		{
		case EPCGUtilsDynMeshTraceDirectionAxis::Backward: return -FVector::ForwardVector;
		case EPCGUtilsDynMeshTraceDirectionAxis::Right: return FVector::RightVector;
		case EPCGUtilsDynMeshTraceDirectionAxis::Left: return -FVector::RightVector;
		case EPCGUtilsDynMeshTraceDirectionAxis::Up: return FVector::UpVector;
		case EPCGUtilsDynMeshTraceDirectionAxis::Down: return -FVector::UpVector;
		case EPCGUtilsDynMeshTraceDirectionAxis::Forward:
		default: return FVector::ForwardVector;
		}
	}

	/** Writes one @Data-domain attribute onto path data. Does nothing when the name is None. */
	template <typename T>
	void SetDataAttribute(UPCGPointArrayData* PathData, FName AttributeName, const T& Value)
	{
		if (!PathData || AttributeName.IsNone())
		{
			return;
		}
		UPCGMetadata* Metadata = PathData->MutableMetadata();
		if (!Metadata)
		{
			return;
		}
		// bOverrideParent is required: a path that inherited from its seed data may already carry a @Data
		// attribute of this name, and this node's value for this path - not the seed dataset's - is the right one.
		if (FPCGMetadataAttribute<T>* Attribute = Metadata->FindOrCreateAttribute<T>(
			FPCGAttributeIdentifier(AttributeName, PCGMetadataDomainID::Data),
			Value, /*bAllowsInterpolation=*/false, /*bOverrideParent=*/true, /*bOverwriteIfTypeMismatch=*/true))
		{
			Attribute->SetValue(PCGInvalidEntryKey, Value);
		}
	}
}

#if WITH_EDITOR
FText UPCGTraceSurfacePathSettings::GetDefaultNodeTitle() const
{
	return LOCTEXT("NodeTitle", "DynMesh|Trace Surface Path");
}

FText UPCGTraceSurfacePathSettings::GetNodeTooltipText() const
{
	return LOCTEXT("NodeTooltip",
		"Projects input seed points onto a Dynamic Mesh and traces a straight path across the mesh surface in "
		"each seed's direction, until the requested distance or a mesh boundary is reached. One independent path "
		"is produced per seed.");
}
#endif

TArray<FPCGPinProperties> UPCGTraceSurfacePathSettings::InputPinProperties() const
{
	TArray<FPCGPinProperties> Pins = Super::InputPinProperties();
	Pins.Emplace_GetRef(PCGTraceSurfacePathConstants::SeedsInputPin, EPCGDataType::Point, true, true)
		.SetRequiredPin();
	return Pins;
}

TArray<FPCGPinProperties> UPCGTraceSurfacePathSettings::OutputPinProperties() const
{
	return {FPCGPinProperties(PCGTraceSurfacePathConstants::PathsOutputPin, EPCGDataType::Point, true, true)};
}

FPCGElementPtr UPCGTraceSurfacePathSettings::CreateElement() const
{
	return MakeShared<FPCGTraceSurfacePathElement>();
}

bool FPCGTraceSurfacePathElement::ExecuteInternal(FPCGContext* Context) const
{
	TRACE_CPUPROFILER_EVENT_SCOPE(FPCGTraceSurfacePathElement::ExecuteInternal);

	check(Context);
	const UPCGTraceSurfacePathSettings* Settings = Context->GetInputSettings<UPCGTraceSurfacePathSettings>();
	check(Settings);

	if (!(Settings->MaxPathLength > 0.0))
	{
		PCGLog::LogErrorOnGraph(
			LOCTEXT("InvalidMaxPathLength",
				"Trace Surface Path requires Max Path Length to be greater than zero; nothing can be traced with a zero-length budget."),
			Context);
		return true;
	}

	if (Settings->bOutputSeedIndex && Settings->SeedIndexAttributeName.IsNone())
	{
		PCGLog::LogWarningOnGraph(
			LOCTEXT("EmptySeedIndexName",
				"Trace Surface Path has Output Seed Index enabled but Seed Index Attribute Name is None; no seed index is written."),
			Context);
	}
	if (Settings->bOutputReachedBoundary && Settings->ReachedBoundaryAttributeName.IsNone())
	{
		PCGLog::LogWarningOnGraph(
			LOCTEXT("EmptyReachedBoundaryName",
				"Trace Surface Path has Output Reached Boundary enabled but Reached Boundary Attribute Name is None; no boundary flag is written."),
			Context);
	}

	const TArray<FPCGTaggedData> SeedInputs =
		Context->InputData.GetInputsByPin(PCGTraceSurfacePathConstants::SeedsInputPin);

	for (const FPCGTaggedData& MeshInput :
		Context->InputData.GetInputsByPin(PCGTraceSurfacePathConstants::MeshInputPin))
	{
		// One resolve and one AABB tree per mesh input, reused for every seed projection and every output-point
		// normal lookup. The tracer needs no acceleration structure of its own and copies nothing.
		Common::FResolvedSurface Surface =
			Common::ResolveSurface(MeshInput.Data, Settings, Context, TraceNodeTitle());
		if (!Surface.IsValid())
		{
			continue;
		}

		const FTransform MeshToOutput = PCGUtilsDynMeshSpaceHelpers::ResolveMeshActorTransform(
			Context, Surface.MeshData, Settings->bWorldSpace);
		const FTransform OutputToMesh = MeshToOutput.Inverse();

		for (const FPCGTaggedData& SeedInput : SeedInputs)
		{
			const UPCGBasePointData* SeedData = Cast<const UPCGBasePointData>(SeedInput.Data);
			if (!SeedData || SeedData->GetNumPoints() == 0)
			{
				continue;
			}
			const int32 NumSeeds = SeedData->GetNumPoints();

			// Resolve the direction source once per seed dataset rather than per seed.
			TUniquePtr<const IPCGAttributeAccessor> DirectionAccessor;
			TUniquePtr<const IPCGAttributeAccessorKeys> DirectionKeys;
			if (Settings->bUseDirectionAttribute)
			{
				const FPCGAttributePropertyInputSelector FixedSelector =
					Settings->DirectionAttribute.CopyAndFixLast(SeedData);
				DirectionAccessor = PCGAttributeAccessorHelpers::CreateConstAccessor(SeedData, FixedSelector);
				DirectionKeys = PCGAttributeAccessorHelpers::CreateConstKeys(SeedData, FixedSelector);
				if (!DirectionAccessor || !DirectionKeys)
				{
					PCGLog::LogErrorOnGraph(FText::Format(
						LOCTEXT("InvalidDirectionSelector",
							"Trace Surface Path could not read the trace direction from '{0}' on a seed dataset; the selector must resolve to a Vector-convertible attribute or property."),
						FText::FromString(Settings->DirectionAttribute.ToString())), Context);
					continue;
				}
			}

			const TConstPCGValueRange<FTransform> SeedTransforms = SeedData->GetConstTransformValueRange();

			TArray<FVector3d> MeshLocalSeeds;
			TArray<FVector3d> MeshLocalDirections;
			MeshLocalSeeds.Reserve(NumSeeds);
			MeshLocalDirections.Reserve(NumSeeds);

			bool bAnyUnreadableDirection = false;
			for (int32 SeedIndex = 0; SeedIndex < NumSeeds; ++SeedIndex)
			{
				const FTransform& SeedTransform = SeedTransforms[SeedIndex];
				MeshLocalSeeds.Add(FVector3d(OutputToMesh.TransformPosition(SeedTransform.GetLocation())));

				FVector Direction = FVector::ZeroVector;
				if (DirectionAccessor)
				{
					if (!DirectionAccessor->Get<FVector>(Direction, SeedIndex, *DirectionKeys,
						EPCGAttributeAccessorFlags::AllowBroadcast | EPCGAttributeAccessorFlags::AllowConstructible))
					{
						bAnyUnreadableDirection = true;
						Direction = FVector::ZeroVector;
					}
				}
				else
				{
					Direction = SeedTransform.GetRotation().RotateVector(AxisVector(Settings->DirectionAxis));
				}

				MeshLocalDirections.Add(FVector3d(OutputToMesh.TransformVectorNoScale(Direction)));
			}

			if (bAnyUnreadableDirection)
			{
				PCGLog::LogWarningOnGraph(FText::Format(
					LOCTEXT("UnreadableDirectionValues",
						"Trace Surface Path could not convert one or more values of '{0}' to a Vector; those seeds fall back to the surface projection of the up vector."),
					FText::FromString(Settings->DirectionAttribute.ToString())), Context);
			}

			// All seeds projected in one batched pass against the shared tree.
			PCGUtilsDynMeshSurfaceCorrespondence::FProjectionOptions ProjectionOptions;
			ProjectionOptions.MaxDistance = (Settings->MaxProjectionDistance > 0.0)
				? Settings->MaxProjectionDistance
				: TNumericLimits<double>::Max();
			const PCGUtilsDynMeshSurfaceCorrespondence::FMeshSurfaceProjectionResult Projection =
				PCGUtilsDynMeshSurfaceCorrespondence::ProjectPoints(*Surface.Tree, MeshLocalSeeds, ProjectionOptions);

			int32 NumUnprojectedSeeds = 0;
			int32 NumDegenerateTraces = 0;
			int32 NumDetailedWarnings = 0;
			TArray<FVector3d> TracedPositions;
			// Every point of one traced path comes from the same single seed, so this is one repeated reference
			// rather than a per-point correspondence. Declared out here to be reused across seeds.
			TArray<Common::FPathSourceRef> TracedSources;

			for (int32 SeedIndex = 0; SeedIndex < NumSeeds; ++SeedIndex)
			{
				bool bReachedBoundary = false;
				const Pathing::ESurfaceTraceStatus Status = Pathing::TraceSurfacePath(
					*Surface.Mesh, Projection.Projections[SeedIndex], MeshLocalDirections[SeedIndex],
					Settings->MaxPathLength, TracedPositions, bReachedBoundary);

				if (Status != Pathing::ESurfaceTraceStatus::Ok)
				{
					if (Status == Pathing::ESurfaceTraceStatus::SeedNotOnMesh)
					{
						++NumUnprojectedSeeds;
						if (NumDetailedWarnings++ < MaxDetailedSeedWarnings)
						{
							PCGLog::LogWarningOnGraph(FText::Format(
								LOCTEXT("SeedNotOnMesh",
									"Trace Surface Path produced no path for seed {0} because it has no surface within Max Projection Distance ({1})."),
								FText::AsNumber(SeedIndex),
								Settings->MaxProjectionDistance > 0.0
									? FText::AsNumber(Settings->MaxProjectionDistance)
									: LOCTEXT("UnlimitedDistance", "unlimited")), Context);
						}
					}
					else
					{
						++NumDegenerateTraces;
						if (NumDetailedWarnings++ < MaxDetailedSeedWarnings)
						{
							PCGLog::LogWarningOnGraph(FText::Format(
								LOCTEXT("DegenerateTrace",
									"Trace Surface Path produced no path for seed {0} because the trace left its start triangle immediately, yielding fewer than two surface points."),
								FText::AsNumber(SeedIndex)), Context);
						}
					}
					continue;
				}

				Common::FPathOutputOptions OutputOptions;
				OutputOptions.MeshToOutput = MeshToOutput;
				// A surface trace runs from its seed outwards and never closes on itself.
				OutputOptions.bClosed = false;
				OutputOptions.IsClosedAttributeName = Settings->IsClosedAttributeName;
				OutputOptions.PointSteepness = Settings->PointSteepness;

				// The traced path belongs to its seed: it is initialized from the seed data so the dataset's
				// attributes, tags and target actor survive, and every traced point takes this seed point's own
				// attributes on the element domain.
				if (Settings->bInheritSeedAttributes)
				{
					TracedSources.Reset(TracedPositions.Num());
					TracedSources.Init(
						Common::FPathSourceRef{SeedIndex, SeedIndex, 0.0f}, TracedPositions.Num());

					OutputOptions.Inheritance.SourceData = SeedData;
					OutputOptions.Inheritance.PointSources = TracedSources;
					// One source point per path: there is no second point to blend towards.
					OutputOptions.Inheritance.bInterpolate = false;
				}

				UPCGPointArrayData* OutputData =
					Common::BuildPathData(Context, TracedPositions, *Surface.Tree, OutputOptions);
				if (!OutputData)
				{
					continue;
				}

				if (Settings->bOutputSeedIndex)
				{
					SetDataAttribute<int32>(OutputData, Settings->SeedIndexAttributeName, SeedIndex);
				}
				if (Settings->bOutputReachedBoundary)
				{
					SetDataAttribute<bool>(OutputData, Settings->ReachedBoundaryAttributeName, bReachedBoundary);
				}

				// Carry both sources' tags, so a traced path keeps its seed data's identity as well as the mesh's.
				FPCGTaggedData& Output = Context->OutputData.TaggedData.Emplace_GetRef(SeedInput);
				Output.Data = OutputData;
				Output.Pin = PCGTraceSurfacePathConstants::PathsOutputPin;
				Output.Tags.Append(MeshInput.Tags);
			}

			if (NumDetailedWarnings > MaxDetailedSeedWarnings)
			{
				PCGLog::LogWarningOnGraph(FText::Format(
					LOCTEXT("MoreSeedFailures",
						"Trace Surface Path produced no path for {0} further seeds of this dataset ({1} unprojected, {2} degenerate in total); only the first {3} are reported individually."),
					FText::AsNumber(NumDetailedWarnings - MaxDetailedSeedWarnings),
					FText::AsNumber(NumUnprojectedSeeds), FText::AsNumber(NumDegenerateTraces),
					FText::AsNumber(MaxDetailedSeedWarnings)), Context);
			}
		}
	}

	return true;
}

#undef LOCTEXT_NAMESPACE
