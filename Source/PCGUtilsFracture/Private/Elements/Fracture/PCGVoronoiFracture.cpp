// Copyright Max Harris

#include "Elements/Fracture/PCGVoronoiFracture.h"

#include "Data/PCGBasePointData.h"
#include "Elements/PCGUtilsDynMeshSpaceHelpers.h"
#include "FractureEngineFracturing.h"
#include "FunctionLibraries/PCGUtilsGCHelpers.h"
#include "GeometryCollection/GeometryCollection.h"
#include "PCGContext.h"
#include "PCGPin.h"
#include "PCGUtilsFracture.h"
#include "PlanarCut.h"
#include "Serialization/ArchiveCrc32.h"
#include "Utils/PCGLogErrors.h"

#define LOCTEXT_NAMESPACE "PCGVoronoiFracture"

bool UPCGVoronoiFractureFactoryData::Fracture(
	FGeometryCollection& InOutCollection,
	const FDataflowTransformSelection& InTargetBones,
	FPCGContext* InContext) const
{
	// Everything below is checked before calling the backend, because FFractureEngineFracturing::VoronoiFracture
	// reports every one of these failures the same way - a bare INDEX_NONE - and the caller cannot tell them
	// apart afterwards.

	if (!InTargetBones.AnySelected())
	{
		PCGLog::LogErrorOnGraph(
			LOCTEXT("NoTargets", "Voronoi Fracture was given no target bones."), InContext);
		return false;
	}

	TArray<FString> MissingAttributes;
	if (!PCGUtilsGCHelpers::ValidateFractureRequirements(InOutCollection, MissingAttributes))
	{
		PCGLog::LogErrorOnGraph(FText::Format(
			LOCTEXT("MalformedCollection",
				"Voronoi Fracture received a Geometry Collection missing the attribute(s) it requires: {0}. "
				"This is a bug in whichever node produced the collection, not a setting you can change."),
			FText::FromString(FString::Join(MissingAttributes, TEXT(", ")))), InContext);
		return false;
	}

	// A Voronoi diagram of one site has no dividing planes at all, so there is literally nothing to cut with.
	if (Sites.Num() < 2)
	{
		PCGLog::LogErrorOnGraph(FText::Format(
			LOCTEXT("TooFewSites",
				"Voronoi Fracture needs at least 2 sites to define a cut, but received {0}. Each site becomes "
				"one fracture piece, so scatter a point per piece you want."),
			FText::AsNumber(Sites.Num())), InContext);
		return false;
	}

	const FBox CollectionBounds = PCGUtilsGCHelpers::ComputeCollectionBounds(InOutCollection);
	const FBox SiteBounds(Sites);

	// Sites are cell centres: a cell only splits geometry if some geometry is nearer to it than to any other
	// site. Sites entirely off to one side leave every triangle in a single cell and cut nothing - which the
	// backend also reports as a bare INDEX_NONE.
	int32 NumSitesInsideBounds = 0;
	if (CollectionBounds.IsValid)
	{
		for (const FVector& Site : Sites)
		{
			NumSitesInsideBounds += CollectionBounds.IsInsideOrOn(Site) ? 1 : 0;
		}
	}

	if (CollectionBounds.IsValid && NumSitesInsideBounds < 2)
	{
		// Before reporting, check whether the opposite space interpretation would have landed the sites on the
		// geometry. If it would, the answer is a single setting rather than a debugging session.
		int32 NumSitesInsideUnderOtherSpace = 0;
		if (!SiteSpaceTransform.Equals(FTransform::Identity))
		{
			for (const FVector& Site : Sites)
			{
				// Undo what we applied and apply the opposite.
				const FVector Original = bSitesWereTreatedAsWorldSpace
					? SiteSpaceTransform.TransformPosition(Site) : Site;
				const FVector Alternate = bSitesWereTreatedAsWorldSpace
					? Original : SiteSpaceTransform.InverseTransformPosition(Original);
				NumSitesInsideUnderOtherSpace += CollectionBounds.IsInsideOrOn(Alternate) ? 1 : 0;
			}
		}

		if (NumSitesInsideUnderOtherSpace >= 2)
		{
			PCGLog::LogErrorOnGraph(FText::Format(
				LOCTEXT("SitesInWrongSpace",
					"Voronoi Fracture: the sites are in the wrong coordinate space. Only {0} of {1} land on the "
					"geometry as interpreted, but {2} would if Sites Are World Space were {3}. Turn it {3} on "
					"this node."),
				FText::AsNumber(NumSitesInsideBounds), FText::AsNumber(Sites.Num()),
				FText::AsNumber(NumSitesInsideUnderOtherSpace),
				bSitesWereTreatedAsWorldSpace
					? LOCTEXT("SpaceOff", "off") : LOCTEXT("SpaceOn", "on")), InContext);
			return false;
		}

		PCGLog::LogErrorOnGraph(FText::Format(
			LOCTEXT("SitesOutsideGeometry",
				"Voronoi Fracture found only {0} of {1} site(s) inside the geometry, so there is nothing to "
				"cut. Geometry occupies {2} to {3}; the sites span {4} to {5}. Scatter the sites through the "
				"volume of the mesh you are fracturing."),
			FText::AsNumber(NumSitesInsideBounds), FText::AsNumber(Sites.Num()),
			FText::FromString(CollectionBounds.Min.ToCompactString()),
			FText::FromString(CollectionBounds.Max.ToCompactString()),
			FText::FromString(SiteBounds.Min.ToCompactString()),
			FText::FromString(SiteBounds.Max.ToCompactString())), InContext);
		return false;
	}

	const int32 BonesBefore = InOutCollection.NumElements(FGeometryCollection::TransformGroup);

	// Routed through ApplyTo so the no-subdivision workaround lives in exactly one place - see its comment.
	FNoiseSettings NoiseSettings;
	Noise.ApplyTo(NoiseSettings, bAddSurfaceNoise);

	// FFractureEngineFracturing::VoronoiFracture offsets sites by InTransform's translation and passes it on
	// to CutMultipleWithPlanarCells as the cut origin. Our canonical space is the collection's own space and
	// the sites are already in it, so identity is correct here - passing anything else would double-apply.
	const int32 ResultGeometryIndex = FFractureEngineFracturing::VoronoiFracture(
		InOutCollection,
		InTargetBones,
		Sites,
		FTransform::Identity,
		RandomSeed,
		ChanceToFracture,
		FIslandSplitSettings(bSplitIslands),
		Grout,
		NoiseSettings.Amplitude,
		NoiseSettings.Frequency,
		NoiseSettings.Persistence,
		NoiseSettings.Lacunarity,
		NoiseSettings.Octaves,
		NoiseSettings.PointSpacing,
		// Collision sampling exists to seed Chaos simulation, which a transient modelling intermediate never
		// reaches. Skipping it avoids generating sample points nothing will read.
		/*InAddSamplesForCollision=*/false,
		/*InCollisionSampleSpacing=*/0.0f);

	if (ResultGeometryIndex == INDEX_NONE)
	{
		// Everything the backend checks has already been verified above, so reaching here means the cut itself
		// produced no new geometry rather than that the inputs were rejected. Report what we know instead of
		// listing possibilities.
		PCGLog::LogErrorOnGraph(FText::Format(
			LOCTEXT("CutProducedNothing",
				"Voronoi Fracture cut nothing: {0} site(s) ({1} inside the geometry) across {2} target bone(s) "
				"produced no new pieces. Geometry occupies {3} to {4}. Sites clustered very close together, or "
				"a Grout of {5} large enough to consume every piece, will both do this."),
			FText::AsNumber(Sites.Num()), FText::AsNumber(NumSitesInsideBounds),
			FText::AsNumber(InTargetBones.NumSelected()),
			FText::FromString(CollectionBounds.Min.ToCompactString()),
			FText::FromString(CollectionBounds.Max.ToCompactString()),
			FText::AsNumber(Grout)), InContext);
		return false;
	}

	const int32 BonesAfter = InOutCollection.NumElements(FGeometryCollection::TransformGroup);
	if (BonesAfter == BonesBefore)
	{
		PCGLog::LogWarningOnGraph(LOCTEXT("NoGeometryChange",
			"Voronoi Fracture produced no new pieces. Check that the sites actually fall inside the geometry "
			"and that Chance To Fracture is not filtering every target."), InContext);
	}

	UE_LOG(LogPCGUtilsFracture, Verbose,
		TEXT("Voronoi Fracture: %d site(s) (%d inside geometry), bones %d -> %d"),
		Sites.Num(), NumSitesInsideBounds, BonesBefore, BonesAfter);
	return true;
}

FString UPCGVoronoiFractureFactoryData::GetOperationDescription() const
{
	return FString::Printf(TEXT("Voronoi (%d sites)"), Sites.Num());
}

void UPCGVoronoiFractureFactoryData::AddToCrc(FArchiveCrc32& Ar, bool bFullDataCrc) const
{
	Super::AddToCrc(Ar, bFullDataCrc);
	if (!bFullDataCrc)
	{
		return;
	}

	int32 LocalSeed = RandomSeed;
	float LocalChance = ChanceToFracture;
	float LocalGrout = Grout;
	bool bLocalSplit = bSplitIslands;
	Ar << LocalSeed;
	Ar << LocalChance;
	Ar << LocalGrout;
	Ar << bLocalSplit;

	Noise.AddToCrc(Ar, bAddSurfaceNoise);

	for (const FVector& Site : Sites)
	{
		FVector LocalSite = Site;
		Ar << LocalSite;
	}
}

#if WITH_EDITOR
FText UPCGVoronoiFractureSettings::GetDefaultNodeTitle() const
{
	return LOCTEXT("Title", "Voronoi Fracture From Points");
}

FText UPCGVoronoiFractureSettings::GetNodeTooltipText() const
{
	return LOCTEXT("Tooltip",
		"Fractures using PCG points as the Voronoi cell centres, so the fracture pattern follows whatever "
		"scatter, density field or filter produced the points - one piece per site. Use Uniform Voronoi "
		"Fracture instead when you just want N evenly-sized pieces and do not care where the cells land. "
		"Emits a Fracture operation - connect it to Fracture GC, which decides which bones it applies to.");
}

FString UPCGVoronoiFractureSettings::GetAdditionalTitleInformation() const
{
	return Grout > 0.0f ? FString::Printf(TEXT("Grout %.2f"), Grout) : FString();
}
#endif

FName UPCGVoronoiFractureSettings::GetMainOutputPin() const
{
	return PCGUtilsFractureFactoryConstants::OutputPin;
}

const FPCGDataTypeBaseId& UPCGVoronoiFractureSettings::GetFactoryTypeId() const
{
	return FPCGUtilsFractureFactoryDataTypeInfo::AsId();
}

TArray<FPCGPinProperties> UPCGVoronoiFractureSettings::InputPinProperties() const
{
	TArray<FPCGPinProperties> Pins;
	Pins.Emplace_GetRef(
		PCGVoronoiFractureConstants::SitesInputPin, EPCGDataType::Point, true, true).SetRequiredPin();
	return Pins;
}

UPCGUtilsGCFactoryData* UPCGVoronoiFractureSettings::CreateFactory(
	FPCGContext* InContext, UPCGUtilsGCFactoryData* InFactory) const
{
	// PCG points are world-space by convention while the collection lives in the source DynMesh's local space.
	// Resolving that here, once, is what makes the node work at non-identity source transforms.
	// Resolved unconditionally, not just when converting: keeping it lets the fracture step diagnose a
	// space mismatch precisely instead of guessing.
	const FTransform LocalToWorld = PCGUtilsDynMeshSpaceHelpers::ResolveMeshActorTransform(
		InContext, /*MeshData=*/nullptr, /*bConvertToLocalSpace=*/true);

	TArray<FVector> ResolvedSites;
	for (const FPCGTaggedData& Input :
		InContext->InputData.GetInputsByPin(PCGVoronoiFractureConstants::SitesInputPin))
	{
		const UPCGBasePointData* Points = Cast<const UPCGBasePointData>(Input.Data);
		if (!Points)
		{
			continue;
		}
		const auto Transforms = Points->GetConstTransformValueRange();
		ResolvedSites.Reserve(ResolvedSites.Num() + Transforms.Num());
		for (const FTransform& PointTransform : Transforms)
		{
			ResolvedSites.Add(bSitesAreWorldSpace
				? LocalToWorld.InverseTransformPosition(PointTransform.GetLocation())
				: PointTransform.GetLocation());
		}
	}

	if (ResolvedSites.IsEmpty())
	{
		PCGLog::LogErrorOnGraph(
			LOCTEXT("MissingSites", "Voronoi Fracture requires point data on its Sites pin."), InContext);
		return nullptr;
	}

	UPCGVoronoiFractureFactoryData* Factory = InFactory
		? Cast<UPCGVoronoiFractureFactoryData>(InFactory)
		: FPCGContext::NewObject_AnyThread<UPCGVoronoiFractureFactoryData>(InContext);
	if (!Factory)
	{
		return nullptr;
	}

	Factory->Priority = Priority;
	Factory->Sites = MoveTemp(ResolvedSites);
	Factory->RandomSeed = RandomSeed;
	Factory->ChanceToFracture = ChanceToFracture;
	Factory->Grout = Grout;
	Factory->bSplitIslands = bSplitIslands;
	Factory->bAddSurfaceNoise = bAddSurfaceNoise;
	Factory->Noise = Noise;
	Factory->SiteSpaceTransform = LocalToWorld;
	Factory->bSitesWereTreatedAsWorldSpace = bSitesAreWorldSpace;
	return Super::CreateFactory(InContext, Factory);
}

#undef LOCTEXT_NAMESPACE
