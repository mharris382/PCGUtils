// Copyright Max Harris

#include "Elements/Selections/PCGGeometryCollectionSelectSurface.h"

#include "FunctionLibraries/PCGUtilsGeometryCollectionHelpers.h"
#include "FunctionLibraries/PCGUtilsGeometryCollectionHierarchy.h"
#include "GeometryCollection/GeometryCollection.h"
#include "PCGContext.h"
#include "Serialization/ArchiveCrc32.h"

#define LOCTEXT_NAMESPACE "PCGGCSelectSurface"

namespace
{
	constexpr int32 SurfacePreconfiguredExterior = 0;
	constexpr int32 SurfacePreconfiguredInterior = 1;

	FText SurfaceTitle(EPCGGeometryCollectionSurfaceClass SurfaceClass)
	{
		return SurfaceClass == EPCGGeometryCollectionSurfaceClass::Exterior
			? LOCTEXT("ExteriorTitle", "GC | Select | With Exterior")
			: LOCTEXT("InteriorTitle", "GC | Select | With Interior");
	}
}

bool UPCGGeometryCollectionSelectSurfaceFactoryData::Evaluate(
	const FPCGUtilsGeometryCollectionSelectionEvaluationContext& InEvaluationContext,
	FPCGContext*, FDataflowTransformSelection& OutSelection) const
{
	const FGeometryCollection& Collection = InEvaluationContext.Collection;
	OutSelection.InitializeFromCollection(Collection, false);

	TArray<int32> Pieces;
	PCGUtilsGeometryCollectionHierarchy::GatherPieces(Collection, Pieces);
	TArray<int32> Matches;
	for (const int32 Bone : Pieces)
	{
		const PCGUtilsGeometryCollectionHelpers::FBoneSurfaceInfo Surface =
			PCGUtilsGeometryCollectionHelpers::GetBoneSurfaceInfo(Collection, Bone);
		const int32 TargetFaces = SurfaceClass == EPCGGeometryCollectionSurfaceClass::Exterior
			? Surface.ExteriorFaceCount : Surface.InteriorFaceCount;
		const bool bMatches = Match == EPCGGeometryCollectionSurfaceMatch::AnyFace
			? TargetFaces > 0
			: Surface.TotalFaceCount() > 0 && TargetFaces == Surface.TotalFaceCount();
		if (bMatches)
		{
			Matches.Add(Bone);
		}
	}
	OutSelection.SetFromArray(Matches);
	return true;
}

void UPCGGeometryCollectionSelectSurfaceFactoryData::ApplyInversion(
	const FPCGUtilsGeometryCollectionSelectionEvaluationContext& InEvaluationContext,
	FDataflowTransformSelection& InOutSelection) const
{
	const TSet<int32> Selected(InOutSelection.AsArrayValidated(InEvaluationContext.Collection));
	TArray<int32> Pieces;
	PCGUtilsGeometryCollectionHierarchy::GatherPieces(InEvaluationContext.Collection, Pieces);
	Pieces.RemoveAll([&Selected](int32 Bone) { return Selected.Contains(Bone); });
	InOutSelection.InitializeFromCollection(InEvaluationContext.Collection, false);
	InOutSelection.SetFromArray(Pieces);
}

void UPCGGeometryCollectionSelectSurfaceFactoryData::AddToCrc(
	FArchiveCrc32& Ar, bool bFullDataCrc) const
{
	Super::AddToCrc(Ar, bFullDataCrc);
	if (bFullDataCrc)
	{
		uint8 LocalClass = static_cast<uint8>(SurfaceClass);
		uint8 LocalMatch = static_cast<uint8>(Match);
		Ar << LocalClass << LocalMatch;
	}
}

#if WITH_EDITOR
FText UPCGGeometryCollectionSelectSurfaceSettings::GetDefaultNodeTitle() const
{
	return SurfaceTitle(SurfaceClass);
}

FText UPCGGeometryCollectionSelectSurfaceSettings::GetNodeTooltipText() const
{
	return LOCTEXT("Tooltip",
		"Selects GC pieces by their stored exterior or fracture-generated interior faces. Any Face keeps pieces "
		"with at least one matching face; All Faces keeps pieces made entirely from that surface class.");
}

FString UPCGGeometryCollectionSelectSurfaceSettings::GetAdditionalTitleInformation() const
{
	return Match == EPCGGeometryCollectionSurfaceMatch::AllFaces ? TEXT("All Faces") : FString();
}

TArray<FPCGPreConfiguredSettingsInfo> UPCGGeometryCollectionSelectSurfaceSettings::GetPreconfiguredInfo() const
{
	return {
		FPCGPreConfiguredSettingsInfo(SurfacePreconfiguredExterior,
			SurfaceTitle(EPCGGeometryCollectionSurfaceClass::Exterior)),
		FPCGPreConfiguredSettingsInfo(SurfacePreconfiguredInterior,
			SurfaceTitle(EPCGGeometryCollectionSurfaceClass::Interior))
	};
}

void UPCGGeometryCollectionSelectSurfaceSettings::ApplyPreconfiguredSettings(
	const FPCGPreConfiguredSettingsInfo& PreconfiguredInfo)
{
	if (PreconfiguredInfo.PreconfiguredIndex == SurfacePreconfiguredExterior)
	{
		SurfaceClass = EPCGGeometryCollectionSurfaceClass::Exterior;
	}
	else if (PreconfiguredInfo.PreconfiguredIndex == SurfacePreconfiguredInterior)
	{
		SurfaceClass = EPCGGeometryCollectionSurfaceClass::Interior;
	}
}
#endif

FName UPCGGeometryCollectionSelectSurfaceSettings::GetMainOutputPin() const
{
	return PCGUtilsGeometryCollectionSelectionFactoryConstants::OutputPin;
}

const FPCGDataTypeBaseId& UPCGGeometryCollectionSelectSurfaceSettings::GetFactoryTypeId() const
{
	return FPCGUtilsGeometryCollectionSelectionFactoryDataTypeInfo::AsId();
}

UPCGUtilsGeometryCollectionFactoryData* UPCGGeometryCollectionSelectSurfaceSettings::CreateFactory(
	FPCGContext* InContext, UPCGUtilsGeometryCollectionFactoryData* InFactory) const
{
	auto* Factory = InFactory ? Cast<UPCGGeometryCollectionSelectSurfaceFactoryData>(InFactory)
		: FPCGContext::NewObject_AnyThread<UPCGGeometryCollectionSelectSurfaceFactoryData>(InContext);
	if (!Factory) { return nullptr; }
	Factory->Priority = Priority;
	Factory->SurfaceClass = SurfaceClass;
	Factory->Match = Match;
	return Super::CreateFactory(InContext, Factory);
}

#undef LOCTEXT_NAMESPACE
