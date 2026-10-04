// Copyright Max Harris

#include "Elements/Creation/PCGDynMeshLoftPaths.h"

#include "Data/PCGBasePointData.h"
#include "Data/PCGDynamicMeshData.h"
#include "Elements/PCGUtilsDynMeshSpaceHelpers.h"
#include "Geometry/PCGUtilsDynMeshLoft.h"
#include "Metadata/PCGMetadata.h"
#include "Metadata/PCGMetadataAttributeTpl.h"
#include "PCGContext.h"
#include "PCGPin.h"
#include "Utils/PCGLogErrors.h"

#define LOCTEXT_NAMESPACE "PCGDynMeshLoftPaths"

namespace
{
	/** Path point positions in the output mesh's space. */
	TArray<FVector3d> ReadLoftPathPositions(const UPCGBasePointData* PointData, const FTransform& ActorTransform)
	{
		TArray<FVector3d> Positions;
		const TConstPCGValueRange<FTransform> Transforms = PointData->GetConstTransformValueRange();
		Positions.Reserve(Transforms.Num());
		for (int32 Index = 0; Index < Transforms.Num(); ++Index)
		{
			Positions.Add(ActorTransform.InverseTransformPosition(Transforms[Index].GetLocation()));
		}
		return Positions;
	}

	bool ReadLoftPathClosedAttribute(const UPCGBasePointData* PointData, FName AttributeName)
	{
		if (AttributeName.IsNone())
		{
			return false;
		}
		const UPCGMetadata* Metadata = PointData->ConstMetadata();
		const FPCGMetadataAttribute<bool>* Attribute = Metadata
			? Metadata->GetConstTypedAttribute<bool>(FPCGAttributeIdentifier(AttributeName, PCGMetadataDomainID::Data))
			: nullptr;
		return Attribute && Attribute->GetValueFromItemKey(PCGInvalidEntryKey);
	}

	TFunction<double(double)> MakeLoftHeightProfile(const UPCGDynMeshLoftPathsSettings& Settings)
	{
		switch (Settings.Profile)
		{
		case EPCGUtilsLoftProfile::Smooth:
			return [](double T) { return T * T * (3.0 - 2.0 * T); };
		case EPCGUtilsLoftProfile::LevelAtA:
			return [](double T) { return T * T; };
		case EPCGUtilsLoftProfile::LevelAtB:
			return [](double T) { return 1.0 - (1.0 - T) * (1.0 - T); };
		case EPCGUtilsLoftProfile::Custom:
			if (const FRichCurve* Curve = Settings.CustomProfile.GetRichCurveConst(); Curve && Curve->GetNumKeys() > 0)
			{
				// Copied so the profile does not outlive the settings object it was read from.
				return [CurveCopy = *Curve](double T) { return static_cast<double>(CurveCopy.Eval(static_cast<float>(T))); };
			}
			return {};
		case EPCGUtilsLoftProfile::Linear:
		default:
			return {};
		}
	}
}

#if WITH_EDITOR
FText UPCGDynMeshLoftPathsSettings::GetDefaultNodeTitle() const
{
	return LOCTEXT("NodeTitle", "DynMesh | Loft Paths");
}

FText UPCGDynMeshLoftPathsSettings::GetNodeTooltipText() const
{
	return LOCTEXT("NodeTooltip",
		"Builds a structured surface between two paths: one column of quads per path point and a set number of "
		"rows across, with height following a profile. Path points are used exactly as given, so a loft whose "
		"path was read off a mesh boundary can be welded to that mesh. The second rail is either an offset of "
		"Path A or a path supplied on Path B.");
}
#endif

TArray<FPCGPinProperties> UPCGDynMeshLoftPathsSettings::InputPinProperties() const
{
	TArray<FPCGPinProperties> Pins;
	Pins.Emplace_GetRef(PCGDynMeshLoftPathsConstants::PathAInputPin, EPCGDataType::Point, true, true,
		LOCTEXT("PathATooltip", "One path per loft. Its points become the loft's first row, exactly as given."))
		.SetRequiredPin();
	Pins.Emplace(PCGDynMeshLoftPathsConstants::PathBInputPin, EPCGDataType::Point, true, true,
		LOCTEXT("PathBTooltip", "Used when Second Rail is Path B: one path per Path A path, forming the loft's last row."));
	return Pins;
}

TArray<FPCGPinProperties> UPCGDynMeshLoftPathsSettings::OutputPinProperties() const
{
	TArray<FPCGPinProperties> Pins;
	Pins.Emplace(PCGPinConstants::DefaultOutputLabel, EPCGDataType::DynamicMesh, true, true);
	return Pins;
}

FPCGElementPtr UPCGDynMeshLoftPathsSettings::CreateElement() const
{
	return MakeShared<FPCGDynMeshLoftPathsElement>();
}

bool FPCGDynMeshLoftPathsElement::ExecuteInternal(FPCGContext* Context) const
{
	TRACE_CPUPROFILER_EVENT_SCOPE(FPCGDynMeshLoftPathsElement::ExecuteInternal);
	check(Context);

	const UPCGDynMeshLoftPathsSettings* Settings = Context->GetInputSettings<UPCGDynMeshLoftPathsSettings>();
	check(Settings);

	if (Settings->Rows < 1)
	{
		PCGLog::LogErrorOnGraph(FText::Format(
			LOCTEXT("InvalidRows", "Loft Paths: Rows is {0}; it must be at least 1."),
			FText::AsNumber(Settings->Rows)), Context);
		return true;
	}
	if (FVector(Settings->UpAxis).IsNearlyZero())
	{
		PCGLog::LogErrorOnGraph(
			LOCTEXT("InvalidUpAxis", "Loft Paths: Up Axis is a zero vector; it must have a direction."), Context);
		return true;
	}
	if (Settings->Profile == EPCGUtilsLoftProfile::Custom)
	{
		const FRichCurve* Curve = Settings->CustomProfile.GetRichCurveConst();
		if (!Curve || Curve->GetNumKeys() == 0)
		{
			PCGLog::LogWarningOnGraph(
				LOCTEXT("EmptyCustomProfile", "Loft Paths: Profile is Custom but Custom Profile has no keys; using a linear profile."),
				Context);
		}
	}

	const bool bUsePathB = Settings->SecondRail == EPCGUtilsLoftSecondRail::PathB;
	const TArray<FPCGTaggedData> PathAInputs = Context->InputData.GetInputsByPin(PCGDynMeshLoftPathsConstants::PathAInputPin);
	const TArray<FPCGTaggedData> PathBInputs = Context->InputData.GetInputsByPin(PCGDynMeshLoftPathsConstants::PathBInputPin);

	if (bUsePathB && PathBInputs.Num() != PathAInputs.Num())
	{
		PCGLog::LogErrorOnGraph(FText::Format(
			LOCTEXT("PathCountMismatch",
				"Loft Paths: Second Rail is Path B, but the Path B pin has {0} paths and the Path A pin has {1}; "
				"they must have the same number, paired in order."),
			FText::AsNumber(PathBInputs.Num()), FText::AsNumber(PathAInputs.Num())), Context);
		return true;
	}

	const FTransform ActorTransform = PCGUtilsDynMeshSpaceHelpers::ResolveMeshActorTransform(
		Context, nullptr, Settings->bConvertWorldToActorLocal);

	const TFunction<double(double)> HeightProfile = MakeLoftHeightProfile(*Settings);

	for (int32 PathIndex = 0; PathIndex < PathAInputs.Num(); ++PathIndex)
	{
		const FPCGTaggedData& PathAInput = PathAInputs[PathIndex];
		const UPCGBasePointData* PathAData = Cast<const UPCGBasePointData>(PathAInput.Data);
		if (!PathAData)
		{
			PCGLog::LogWarningOnGraph(FText::Format(
				LOCTEXT("PathANotPoints", "Loft Paths: Path A input {0} is not point data; skipped."),
				FText::AsNumber(PathIndex)), Context);
			continue;
		}

		bool bClosed = Settings->Closure == EPCGUtilsLoftClosure::Closed;
		if (Settings->Closure == EPCGUtilsLoftClosure::FromAttribute)
		{
			bClosed = ReadLoftPathClosedAttribute(PathAData, Settings->IsClosedAttributeName);
		}

		const TArray<FVector3d> RailA = ReadLoftPathPositions(PathAData, ActorTransform);

		TArray<FVector3d> RailB;
		if (bUsePathB)
		{
			const UPCGBasePointData* PathBData = Cast<const UPCGBasePointData>(PathBInputs[PathIndex].Data);
			if (!PathBData)
			{
				PCGLog::LogWarningOnGraph(FText::Format(
					LOCTEXT("PathBNotPoints", "Loft Paths: Path B input {0} is not point data; skipped."),
					FText::AsNumber(PathIndex)), Context);
				continue;
			}

			RailB = ReadLoftPathPositions(PathBData, ActorTransform);
			if (Settings->Correspondence == EPCGUtilsLoftCorrespondence::Index)
			{
				if (RailB.Num() != RailA.Num())
				{
					PCGLog::LogErrorOnGraph(FText::Format(
						LOCTEXT("PointCountMismatch",
							"Loft Paths: Correspondence is Index, but path {0} has {1} points on Path B and {2} on "
							"Path A; they must match. Use Arc Length correspondence to resample Path B."),
						FText::AsNumber(PathIndex), FText::AsNumber(RailB.Num()), FText::AsNumber(RailA.Num())), Context);
					continue;
				}
			}
			else
			{
				if (RailB.Num() < 2)
				{
					PCGLog::LogErrorOnGraph(FText::Format(
						LOCTEXT("PathBTooShort", "Loft Paths: path {0} has {1} points on Path B; at least 2 are needed to resample it."),
						FText::AsNumber(PathIndex), FText::AsNumber(RailB.Num())), Context);
					continue;
				}
				PCGUtilsDynMeshLoft::AlignRail(RailA, bClosed, Settings->UpAxis, RailB);
				TArray<FVector3d> Resampled;
				PCGUtilsDynMeshLoft::ResampleByArcLength(RailB, bClosed, RailA.Num(), Resampled);
				RailB = MoveTemp(Resampled);
			}
		}
		else
		{
			PCGUtilsDynMeshLoft::FOffsetOptions OffsetOptions;
			OffsetOptions.bClosed = bClosed;
			OffsetOptions.UpAxis = Settings->UpAxis;
			OffsetOptions.Distance = Settings->OffsetDistance;
			OffsetOptions.bPositiveIsOutward = Settings->bPositiveOffsetIsOutward;
			OffsetOptions.Height = Settings->OffsetHeight;
			OffsetOptions.MiterLimit = Settings->MiterLimit;

			const PCGUtilsDynMeshLoft::FOffsetResult OffsetResult =
				PCGUtilsDynMeshLoft::OffsetRail(RailA, OffsetOptions, RailB);
			if (OffsetResult.NumInvertedSegments > 0)
			{
				PCGLog::LogWarningOnGraph(FText::Format(
					LOCTEXT("InvertedOffset",
						"Loft Paths: path {0} has {1} segments where Offset Distance ({2}) is larger than the corner "
						"can absorb, so the loft folds over itself; the first starts at point {3}. Reduce Offset "
						"Distance or round the corner."),
					FText::AsNumber(PathIndex), FText::AsNumber(OffsetResult.NumInvertedSegments),
					FText::AsNumber(Settings->OffsetDistance), FText::AsNumber(OffsetResult.FirstInvertedSegment)), Context);
			}
		}

		PCGUtilsDynMeshLoft::FLoftOptions LoftOptions;
		LoftOptions.NumRows = Settings->Rows;
		LoftOptions.bClosed = bClosed;
		LoftOptions.UpAxis = Settings->UpAxis;
		LoftOptions.HeightProfile = HeightProfile;
		LoftOptions.bFlipFaces = Settings->bFlipFaces;
		LoftOptions.UVScale = Settings->UVScale;

		UE::Geometry::FDynamicMesh3 Mesh;
		PCGUtilsDynMeshLoft::FLoftResult LoftResult;
		FString LoftError;
		if (!PCGUtilsDynMeshLoft::BuildLoft(RailA, RailB, LoftOptions, Mesh, LoftResult, LoftError))
		{
			PCGLog::LogErrorOnGraph(FText::Format(
				LOCTEXT("LoftFailed", "Loft Paths: path {0} could not be lofted. {1}"),
				FText::AsNumber(PathIndex), FText::FromString(LoftError)), Context);
			continue;
		}

		UPCGDynamicMeshData* OutputData = FPCGContext::NewObject_AnyThread<UPCGDynamicMeshData>(Context);
		OutputData->Initialize(MoveTemp(Mesh));
		FPCGTaggedData& Output = Context->OutputData.TaggedData.Emplace_GetRef(PathAInput);
		Output.Data = OutputData;
		Output.Pin = PCGPinConstants::DefaultOutputLabel;
	}

	return true;
}

#undef LOCTEXT_NAMESPACE
