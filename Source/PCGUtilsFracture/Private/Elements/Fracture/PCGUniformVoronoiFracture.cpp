// Copyright Max Harris

#include "Elements/Fracture/PCGUniformVoronoiFracture.h"

#include "FractureEngineFracturing.h"
#include "FunctionLibraries/PCGUtilsGeometryCollectionHelpers.h"
#include "GeometryCollection/GeometryCollection.h"
#include "PCGContext.h"
#include "PCGUtilsFracture.h"
#include "PlanarCut.h"
#include "Serialization/ArchiveCrc32.h"
#include "Utils/PCGLogErrors.h"

#define LOCTEXT_NAMESPACE "PCGUniformVoronoiFracture"

bool UPCGUniformVoronoiFractureFactoryData::Fracture(
	FGeometryCollection& InOutCollection,
	const FDataflowTransformSelection& InTargetBones,
	FPCGContext* InContext) const
{
	// Checked here rather than left to the backend: FFractureEngineFracturing reports every failure as a bare
	// INDEX_NONE, so anything not caught up front becomes indistinguishable afterwards.
	if (!InTargetBones.AnySelected())
	{
		PCGLog::LogErrorOnGraph(
			LOCTEXT("NoTargets", "Uniform Voronoi Fracture was given no target bones."), InContext);
		return false;
	}

	TArray<FString> MissingAttributes;
	if (!PCGUtilsGeometryCollectionHelpers::ValidateFractureRequirements(InOutCollection, MissingAttributes))
	{
		PCGLog::LogErrorOnGraph(FText::Format(
			LOCTEXT("MalformedCollection",
				"Uniform Voronoi Fracture received a Geometry Collection missing the attribute(s) it requires: "
				"{0}. This is a bug in whichever node produced the collection, not a setting you can change."),
			FText::FromString(FString::Join(MissingAttributes, TEXT(", ")))), InContext);
		return false;
	}

	// A Voronoi diagram of one site has no dividing planes, so it can never cut anything.
	const int32 MinSites = FMath::Max(1, MinVoronoiSites);
	const int32 MaxSites = FMath::Max(MinSites, MaxVoronoiSites);
	if (MaxSites < 2)
	{
		PCGLog::LogErrorOnGraph(
			LOCTEXT("TooFewSites",
				"Uniform Voronoi Fracture needs at least 2 sites to define a cut. Raise Max Voronoi Sites."),
			InContext);
		return false;
	}

	const int32 BonesBefore = InOutCollection.NumElements(FGeometryCollection::TransformGroup);

	FUniformFractureSettings Settings;
	// Identity: the collection already lives in the source DynMesh's local space, and the sites are generated
	// from the collection's own bounds, so there is no space to reconcile.
	Settings.Transform = FTransform::Identity;
	Settings.MinVoronoiSites = MinSites;
	Settings.MaxVoronoiSites = MaxSites;
	// Left at the engine default; Fracture GC owns the internal-material override so that one control applies
	// to every fracture operation rather than each type exposing its own.
	Settings.InternalMaterialID = 0;
	Settings.RandomSeed = RandomSeed;
	Settings.ChanceToFracture = ChanceToFracture;
	Settings.GroupFracture = bGroupFracture;
	Settings.SplitIslands = bSplitIslands;
	Settings.CloseVertexDistance = static_cast<double>(CloseVertexDistance);
	Settings.VertexToSurfaceBridgeDistance = static_cast<double>(VertexToSurfaceBridgeDistance);
	Settings.Grout = Grout;
	Noise.ApplyTo(Settings.NoiseSettings, bAddSurfaceNoise);
	// Collision sampling seeds Chaos simulation, which a transient modelling intermediate never reaches.
	Settings.AddSamplesForCollision = false;
	Settings.CollisionSampleSpacing = 0.0f;

	const int32 ResultGeometryIndex =
		FFractureEngineFracturing::UniformFracture(InOutCollection, InTargetBones, Settings);

	if (ResultGeometryIndex == INDEX_NONE)
	{
		const FBox CollectionBounds = PCGUtilsGeometryCollectionHelpers::ComputeCollectionBounds(InOutCollection);
		PCGLog::LogErrorOnGraph(FText::Format(
			LOCTEXT("CutProducedNothing",
				"Uniform Voronoi Fracture cut nothing across {0} target bone(s) occupying {1} to {2}. A Grout "
				"of {3} large enough to consume every piece, or a Chance To Fracture of {4} filtering every "
				"target, will both do this."),
			FText::AsNumber(InTargetBones.NumSelected()),
			FText::FromString(CollectionBounds.Min.ToCompactString()),
			FText::FromString(CollectionBounds.Max.ToCompactString()),
			FText::AsNumber(Grout), FText::AsNumber(ChanceToFracture)), InContext);
		return false;
	}

	const int32 BonesAfter = InOutCollection.NumElements(FGeometryCollection::TransformGroup);
	UE_LOG(LogPCGUtilsFracture, Verbose,
		TEXT("Uniform Voronoi Fracture: %d-%d sites, group=%d, bones %d -> %d"),
		MinSites, MaxSites, bGroupFracture ? 1 : 0, BonesBefore, BonesAfter);
	return true;
}

FString UPCGUniformVoronoiFractureFactoryData::GetOperationDescription() const
{
	return MinVoronoiSites == MaxVoronoiSites
		? FString::Printf(TEXT("Uniform Voronoi (%d sites)"), MinVoronoiSites)
		: FString::Printf(TEXT("Uniform Voronoi (%d-%d sites)"), MinVoronoiSites, MaxVoronoiSites);
}

void UPCGUniformVoronoiFractureFactoryData::AddToCrc(FArchiveCrc32& Ar, bool bFullDataCrc) const
{
	Super::AddToCrc(Ar, bFullDataCrc);
	if (!bFullDataCrc)
	{
		return;
	}

	int32 LocalMin = MinVoronoiSites;
	int32 LocalMax = MaxVoronoiSites;
	int32 LocalSeed = RandomSeed;
	float LocalChance = ChanceToFracture;
	float LocalGrout = Grout;
	bool bLocalGroup = bGroupFracture;
	bool bLocalSplit = bSplitIslands;
	float LocalCloseVertex = CloseVertexDistance;
	float LocalBridge = VertexToSurfaceBridgeDistance;
	Ar << LocalMin;
	Ar << LocalMax;
	Ar << LocalSeed;
	Ar << LocalChance;
	Ar << LocalGrout;
	Ar << bLocalGroup;
	Ar << bLocalSplit;
	Ar << LocalCloseVertex;
	Ar << LocalBridge;

	Noise.AddToCrc(Ar, bAddSurfaceNoise);
}

#if WITH_EDITOR
FText UPCGUniformVoronoiFractureSettings::GetDefaultNodeTitle() const
{
	return LOCTEXT("Title", "Uniform Voronoi Fracture");
}

FText UPCGUniformVoronoiFractureSettings::GetNodeTooltipText() const
{
	return LOCTEXT("Tooltip",
		"Breaks a Geometry Collection into roughly evenly-sized pieces, scattering its own Voronoi sites "
		"through the bounds of whatever it is fracturing. This is the equivalent of Fracture Mode's Uniform "
		"button and needs no point input. Emits a Fracture operation - connect it to Fracture GC, which "
		"decides which bones it applies to. Use Voronoi Fracture From Points instead when you want to place "
		"the cells yourself from a PCG or PCGEx scatter.");
}

FString UPCGUniformVoronoiFractureSettings::GetAdditionalTitleInformation() const
{
	return MinVoronoiSites == MaxVoronoiSites
		? FString::Printf(TEXT("%d sites"), MinVoronoiSites)
		: FString::Printf(TEXT("%d-%d sites"), MinVoronoiSites, MaxVoronoiSites);
}
#endif

FName UPCGUniformVoronoiFractureSettings::GetMainOutputPin() const
{
	return PCGUtilsFractureFactoryConstants::OutputPin;
}

const FPCGDataTypeBaseId& UPCGUniformVoronoiFractureSettings::GetFactoryTypeId() const
{
	return FPCGUtilsFractureFactoryDataTypeInfo::AsId();
}

UPCGUtilsGeometryCollectionFactoryData* UPCGUniformVoronoiFractureSettings::CreateFactory(
	FPCGContext* InContext, UPCGUtilsGeometryCollectionFactoryData* InFactory) const
{
	if (MaxVoronoiSites < MinVoronoiSites)
	{
		PCGLog::LogWarningOnGraph(FText::Format(
			LOCTEXT("SwappedSiteRange",
				"Uniform Voronoi Fracture has Max Voronoi Sites ({0}) below Min ({1}); using {1} for both."),
			FText::AsNumber(MaxVoronoiSites), FText::AsNumber(MinVoronoiSites)), InContext);
	}

	UPCGUniformVoronoiFractureFactoryData* Factory = InFactory
		? Cast<UPCGUniformVoronoiFractureFactoryData>(InFactory)
		: FPCGContext::NewObject_AnyThread<UPCGUniformVoronoiFractureFactoryData>(InContext);
	if (!Factory)
	{
		return nullptr;
	}

	Factory->Priority = Priority;
	Factory->MinVoronoiSites = FMath::Max(1, MinVoronoiSites);
	Factory->MaxVoronoiSites = FMath::Max(Factory->MinVoronoiSites, MaxVoronoiSites);
	Factory->RandomSeed = RandomSeed;
	Factory->ChanceToFracture = ChanceToFracture;
	Factory->Grout = Grout;
	Factory->bGroupFracture = bGroupFracture;
	Factory->bSplitIslands = bSplitIslands;
	Factory->CloseVertexDistance = CloseVertexDistance;
	Factory->VertexToSurfaceBridgeDistance = VertexToSurfaceBridgeDistance;
	Factory->bAddSurfaceNoise = bAddSurfaceNoise;
	Factory->Noise = Noise;
	return Super::CreateFactory(InContext, Factory);
}

#undef LOCTEXT_NAMESPACE
