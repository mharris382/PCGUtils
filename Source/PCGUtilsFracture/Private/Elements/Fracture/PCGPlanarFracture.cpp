// Copyright Max Harris

#include "Elements/Fracture/PCGPlanarFracture.h"

#include "Data/PCGBasePointData.h"
#include "Elements/PCGUtilsDynMeshSpaceHelpers.h"
#include "FractureEngineFracturing.h"
#include "FunctionLibraries/PCGUtilsGeometryCollectionHelpers.h"
#include "GeometryCollection/GeometryCollection.h"
#include "PCGContext.h"
#include "PCGPin.h"
#include "PCGUtilsFracture.h"
#include "PlanarCut.h"
#include "Serialization/ArchiveCrc32.h"
#include "Utils/PCGLogErrors.h"

#define LOCTEXT_NAMESPACE "PCGPlanarFracture"

namespace
{
	/** Everything the three planar cutters share, unpacked into the arguments they actually take. */
	struct FResolvedCommon
	{
		FBox Bounds = FBox(ForceInit);
		FIslandSplitSettings IslandSplit;
		FNoiseSettings Noise;
		int32 BonesBefore = 0;
	};

	/**
	 * Validates the collection and target set the same way every fracture operation must, and resolves the
	 * shared arguments.
	 *
	 * The checks are here rather than left to the backend because FFractureEngineFracturing reports every
	 * failure as a bare INDEX_NONE - anything not caught up front is indistinguishable afterwards.
	 */
	bool ResolveCommon(
		const FGeometryCollection& InCollection,
		const FDataflowTransformSelection& InTargetBones,
		const FPCGPlanarFractureCommonSettings& InCommon,
		const FText& InOperationName,
		FPCGContext* InContext,
		FResolvedCommon& OutResolved)
	{
		if (!InTargetBones.AnySelected())
		{
			PCGLog::LogErrorOnGraph(FText::Format(
				LOCTEXT("NoTargets", "{0} was given no target bones."), InOperationName), InContext);
			return false;
		}

		TArray<FString> MissingAttributes;
		if (!PCGUtilsGeometryCollectionHelpers::ValidateFractureRequirements(InCollection, MissingAttributes))
		{
			PCGLog::LogErrorOnGraph(FText::Format(
				LOCTEXT("MalformedCollection",
					"{0} received a Geometry Collection missing the attribute(s) it requires: {1}. This is a bug "
					"in whichever node produced the collection, not a setting you can change."),
				InOperationName, FText::FromString(FString::Join(MissingAttributes, TEXT(", ")))), InContext);
			return false;
		}

		OutResolved.Bounds = PCGUtilsGeometryCollectionHelpers::ComputeCollectionBounds(InCollection);
		if (!OutResolved.Bounds.IsValid)
		{
			PCGLog::LogErrorOnGraph(FText::Format(
				LOCTEXT("NoBounds", "{0} could not compute bounds for the collection."), InOperationName), InContext);
			return false;
		}

		OutResolved.IslandSplit = FIslandSplitSettings(
			InCommon.bSplitIslands,
			static_cast<double>(InCommon.CloseVertexDistance),
			static_cast<double>(InCommon.VertexToSurfaceBridgeDistance));

		// Always through ApplyTo, for one source of truth across every operation - though note these three
		// cutters gate on `InAmplitude > 0` and leave FInternalSurfaceMaterials::NoiseSettings unset otherwise,
		// so unlike the Voronoi entry point they do reach PlanarCut's cheap meshing path on their own. The
		// suppressing point spacing ApplyTo returns when noise is off is therefore belt-and-braces here rather
		// than load-bearing. See FPCGFractureNoiseSettings for the path where it is essential.
		InCommon.Noise.ApplyTo(OutResolved.Noise, InCommon.bAddSurfaceNoise);

		OutResolved.BonesBefore = InCollection.NumElements(FGeometryCollection::TransformGroup);
		return true;
	}

	/** Turns a cutter's INDEX_NONE into a message that says what could have caused it. */
	void LogCutProducedNothing(
		const FText& InOperationName,
		const FDataflowTransformSelection& InTargetBones,
		const FPCGPlanarFractureCommonSettings& InCommon,
		const FBox& InBounds,
		FPCGContext* InContext)
	{
		PCGLog::LogErrorOnGraph(FText::Format(
			LOCTEXT("CutProducedNothing",
				"{0} cut nothing across {1} target bone(s) occupying {2} to {3}. A Grout of {4} large enough to "
				"consume every piece, a Chance To Fracture of {5} filtering every target, or cutting planes that "
				"all miss the geometry will each do this."),
			InOperationName,
			FText::AsNumber(InTargetBones.NumSelected()),
			FText::FromString(InBounds.Min.ToCompactString()),
			FText::FromString(InBounds.Max.ToCompactString()),
			FText::AsNumber(InCommon.Grout),
			FText::AsNumber(InCommon.ChanceToFracture)), InContext);
	}

	/** Same reporting contract as the Voronoi paths: the cutters append, so only the cut bones changed. */
	FPCGUtilsGeometryCollectionMutationResult MakeMutation(const FGeometryCollection& InCollection, int32 InBonesBefore)
	{
		const int32 BonesAfter = InCollection.NumElements(FGeometryCollection::TransformGroup);
		return FPCGUtilsGeometryCollectionMutationResult::Fracture(
			BonesAfter > InBonesBefore ? InBonesBefore : INDEX_NONE);
	}
}

void FPCGPlanarFractureCommonSettings::AddToCrc(FArchiveCrc32& Ar) const
{
	FPCGPlanarFractureCommonSettings Local = *this;
	Ar << Local.RandomSeed;
	Ar << Local.ChanceToFracture;
	Ar << Local.Grout;
	Ar << Local.bSplitIslands;
	Ar << Local.CloseVertexDistance;
	Ar << Local.VertexToSurfaceBridgeDistance;
	Noise.AddToCrc(Ar, bAddSurfaceNoise);
}

// --- Planar ----------------------------------------------------------------------------------------------

bool UPCGPlanarFractureFactoryData::Fracture(
	FGeometryCollection& InOutCollection,
	const FDataflowTransformSelection& InTargetBones,
	FPCGContext* InContext,
	FPCGUtilsGeometryCollectionMutationResult& OutMutation) const
{
	const FText OperationName = LOCTEXT("PlanarName", "Planar Fracture");
	FResolvedCommon Resolved;
	if (!ResolveCommon(InOutCollection, InTargetBones, Common, OperationName, InContext, Resolved))
	{
		return false;
	}

	// PlaneCutter seeds its list with InCutPlaneTransforms and then *appends* InNumPlanes generated ones, so
	// asking for any generated planes alongside supplied ones would quietly add random cuts to a deliberate
	// pattern. Zero is what makes the points the whole cut pattern.
	TArray<FTransform> ResolvedCutPlanes = CutPlaneTransforms;
	if (TransformMode == EPCGUtilsPlaneTransformMode::BoundsRelative)
	{
		ResolvedCutPlanes = { BoundsPlacement.ComputeTransform(Resolved.Bounds) };
	}
	const int32 NumGeneratedPlanes = ResolvedCutPlanes.IsEmpty() ? FMath::Max(1, NumPlanes) : 0;

	const int32 ResultGeometryIndex = FFractureEngineFracturing::PlaneCutter(
		InOutCollection,
		InTargetBones,
		Resolved.Bounds,
		// Identity: the collection is already in its own space, and the planes were converted at authoring time.
		FTransform::Identity,
		NumGeneratedPlanes,
		Common.RandomSeed,
		Common.ChanceToFracture,
		Resolved.IslandSplit,
		Common.Grout,
		Resolved.Noise.Amplitude,
		Resolved.Noise.Frequency,
		Resolved.Noise.Persistence,
		Resolved.Noise.Lacunarity,
		Resolved.Noise.Octaves,
		Resolved.Noise.PointSpacing,
		/*InAddSamplesForCollision=*/false,
		/*InCollisionSampleSpacing=*/0.0f,
		ResolvedCutPlanes);

	if (ResultGeometryIndex == INDEX_NONE)
	{
		LogCutProducedNothing(OperationName, InTargetBones, Common, Resolved.Bounds, InContext);
		return false;
	}

	OutMutation = MakeMutation(InOutCollection, Resolved.BonesBefore);
	UE_LOG(LogPCGUtilsFracture, Verbose, TEXT("Planar Fracture: %d plane(s)%s, bones %d -> %d"),
		ResolvedCutPlanes.IsEmpty() ? NumGeneratedPlanes : ResolvedCutPlanes.Num(),
		ResolvedCutPlanes.IsEmpty() ? TEXT(" (generated)")
			: TransformMode == EPCGUtilsPlaneTransformMode::BoundsRelative ? TEXT(" (bounds relative)") : TEXT(" (from points)"),
		Resolved.BonesBefore, InOutCollection.NumElements(FGeometryCollection::TransformGroup));
	return true;
}

FString UPCGPlanarFractureFactoryData::GetOperationDescription() const
{
	if (TransformMode == EPCGUtilsPlaneTransformMode::BoundsRelative)
	{
		return TEXT("Planar (bounds relative)");
	}
	return CutPlaneTransforms.IsEmpty()
		? FString::Printf(TEXT("Planar (%d planes)"), NumPlanes)
		: FString::Printf(TEXT("Planar (%d planes from points)"), CutPlaneTransforms.Num());
}

void UPCGPlanarFractureFactoryData::AddToCrc(FArchiveCrc32& Ar, bool bFullDataCrc) const
{
	Super::AddToCrc(Ar, bFullDataCrc);
	if (!bFullDataCrc)
	{
		return;
	}

	int32 LocalNumPlanes = NumPlanes;
	Ar << LocalNumPlanes;
	uint8 LocalTransformMode = static_cast<uint8>(TransformMode);
	Ar << LocalTransformMode;
	if (TransformMode == EPCGUtilsPlaneTransformMode::BoundsRelative)
	{
		BoundsPlacement.AddToCrc(Ar);
	}
	if (TransformMode == EPCGUtilsPlaneTransformMode::Explicit)
	{
		for (const FTransform& Plane : CutPlaneTransforms)
		{
			FTransform LocalPlane = Plane;
			Ar << LocalPlane;
		}
	}
	Common.AddToCrc(Ar);
}

#if WITH_EDITOR
FText UPCGPlanarFractureSettings::GetDefaultNodeTitle() const
{
	return LOCTEXT("PlanarTitle", "Fracture | Planar");
}

FText UPCGPlanarFractureSettings::GetNodeTooltipText() const
{
	return LOCTEXT("PlanarTooltip",
		"Cuts the targeted bones with flat planes. Explicit Transform mode preserves point-driven or random "
		"placement: with nothing on the Planes pin the planes are scattered "
		"randomly through the target's bounds, matching Fracture Mode's Planar tool. Connect points to Planes "
		"to place them instead: each point's transform becomes one cutting plane, oriented by the point's Z "
		"axis. Bounds Relative mode instead places one plane against the target collection with Builder-style "
		"alignment, asymmetric padding, and a local offset/rotation. Emits a Fracture "
		"operation - connect it to GC | Fracture, which decides which bones it applies to.");
}

FString UPCGPlanarFractureSettings::GetAdditionalTitleInformation() const
{
	return TransformMode == EPCGUtilsPlaneTransformMode::BoundsRelative
		? TEXT("Bounds Relative") : FString::Printf(TEXT("%d planes"), NumPlanes);
}
#endif

TArray<FPCGPinProperties> UPCGPlanarFractureSettings::InputPinProperties() const
{
	TArray<FPCGPinProperties> Pins;
	// Bounds Relative mode resolves its plane entirely from the target collection's bounds and Bounds Placement,
	// so it needs no external input at all. Only Explicit mode - where an unconnected pin means "generate the
	// planes", the Fracture Mode behaviour - exposes Planes.
	if (TransformMode != EPCGUtilsPlaneTransformMode::BoundsRelative)
	{
		Pins.Emplace(PCGPlanarFractureConstants::PlanesInputPin, EPCGDataType::Point, true, true);
	}
	return Pins;
}

UPCGUtilsFractureFactoryData* UPCGPlanarFractureSettings::CreateFractureFactory(
	FPCGContext* InContext, UPCGUtilsGeometryCollectionFactoryData* InFactory) const
{
	UPCGPlanarFractureFactoryData* Factory = InFactory
		? Cast<UPCGPlanarFractureFactoryData>(InFactory)
		: FPCGContext::NewObject_AnyThread<UPCGPlanarFractureFactoryData>(InContext);
	if (!Factory)
	{
		return nullptr;
	}

	TArray<FTransform> Planes;
	if (TransformMode == EPCGUtilsPlaneTransformMode::Explicit)
	{
		// PCG points are world-space by convention while the collection lives in the source DynMesh's local space.
		const FTransform LocalToWorld = PCGUtilsDynMeshSpaceHelpers::ResolveMeshActorTransform(
			InContext, /*MeshData=*/nullptr, /*bConvertToLocalSpace=*/true);
		for (const FPCGTaggedData& Input :
			InContext->InputData.GetInputsByPin(PCGPlanarFractureConstants::PlanesInputPin))
		{
			const UPCGBasePointData* Points = Cast<const UPCGBasePointData>(Input.Data);
			if (!Points) { continue; }
			const auto Transforms = Points->GetConstTransformValueRange();
			Planes.Reserve(Planes.Num() + Transforms.Num());
			for (const FTransform& PointTransform : Transforms)
			{
				Planes.Add(bConvertPlanesToLocalSpace
					? PointTransform.GetRelativeTransform(LocalToWorld) : PointTransform);
			}
		}
	}

	Factory->NumPlanes = FMath::Max(1, NumPlanes);
	Factory->CutPlaneTransforms = MoveTemp(Planes);
	Factory->TransformMode = TransformMode;
	Factory->BoundsPlacement = BoundsPlacement;
	Factory->Common = Common;
	return Factory;
}

// --- Slice -----------------------------------------------------------------------------------------------

bool UPCGSliceFractureFactoryData::Fracture(
	FGeometryCollection& InOutCollection,
	const FDataflowTransformSelection& InTargetBones,
	FPCGContext* InContext,
	FPCGUtilsGeometryCollectionMutationResult& OutMutation) const
{
	const FText OperationName = LOCTEXT("SliceName", "Slice Fracture");
	FResolvedCommon Resolved;
	if (!ResolveCommon(InOutCollection, InTargetBones, Common, OperationName, InContext, Resolved))
	{
		return false;
	}

	const int32 X = FMath::Max(0, SlicesX);
	const int32 Y = FMath::Max(0, SlicesY);
	const int32 Z = FMath::Max(0, SlicesZ);
	if (X + Y + Z == 0)
	{
		PCGLog::LogErrorOnGraph(
			LOCTEXT("NoSlices",
				"Slice Fracture has 0 cutting planes on every axis, which describes no cut at all. Raise Slices "
				"on at least one axis."), InContext);
		return false;
	}

	const int32 ResultGeometryIndex = FFractureEngineFracturing::SliceCutter(
		InOutCollection,
		InTargetBones,
		Resolved.Bounds,
		X, Y, Z,
		SliceAngleVariation,
		SliceOffsetVariation,
		Common.RandomSeed,
		Common.ChanceToFracture,
		Resolved.IslandSplit,
		Common.Grout,
		Resolved.Noise.Amplitude,
		Resolved.Noise.Frequency,
		Resolved.Noise.Persistence,
		Resolved.Noise.Lacunarity,
		Resolved.Noise.Octaves,
		Resolved.Noise.PointSpacing,
		/*InAddSamplesForCollision=*/false,
		/*InCollisionSampleSpacing=*/0.0f);

	if (ResultGeometryIndex == INDEX_NONE)
	{
		LogCutProducedNothing(OperationName, InTargetBones, Common, Resolved.Bounds, InContext);
		return false;
	}

	OutMutation = MakeMutation(InOutCollection, Resolved.BonesBefore);
	UE_LOG(LogPCGUtilsFracture, Verbose, TEXT("Slice Fracture: %dx%dx%d, bones %d -> %d"),
		X, Y, Z, Resolved.BonesBefore, InOutCollection.NumElements(FGeometryCollection::TransformGroup));
	return true;
}

FString UPCGSliceFractureFactoryData::GetOperationDescription() const
{
	return FString::Printf(TEXT("Slice (%dx%dx%d)"), SlicesX, SlicesY, SlicesZ);
}

void UPCGSliceFractureFactoryData::AddToCrc(FArchiveCrc32& Ar, bool bFullDataCrc) const
{
	Super::AddToCrc(Ar, bFullDataCrc);
	if (!bFullDataCrc)
	{
		return;
	}

	int32 LocalX = SlicesX;
	int32 LocalY = SlicesY;
	int32 LocalZ = SlicesZ;
	float LocalAngle = SliceAngleVariation;
	float LocalOffset = SliceOffsetVariation;
	Ar << LocalX;
	Ar << LocalY;
	Ar << LocalZ;
	Ar << LocalAngle;
	Ar << LocalOffset;
	Common.AddToCrc(Ar);
}

#if WITH_EDITOR
FText UPCGSliceFractureSettings::GetDefaultNodeTitle() const
{
	return LOCTEXT("SliceTitle", "Fracture | Slice");
}

FText UPCGSliceFractureSettings::GetNodeTooltipText() const
{
	return LOCTEXT("SliceTooltip",
		"Divides the targeted bones on a regular X/Y/Z grid, matching Fracture Mode's Slice tool. This is the "
		"one to reach for when you want even pieces - panels, floor tiles, a wall split into courses - rather "
		"than the irregular cells Voronoi produces. Leave an axis at 1 to leave it uncut, and use Slice Angle "
		"and Offset Variation to take the regularity off. Emits a Fracture operation for GC | Fracture to run.");
}

FString UPCGSliceFractureSettings::GetAdditionalTitleInformation() const
{
	return FString::Printf(TEXT("%dx%dx%d"), SlicesX, SlicesY, SlicesZ);
}
#endif

UPCGUtilsFractureFactoryData* UPCGSliceFractureSettings::CreateFractureFactory(
	FPCGContext* InContext, UPCGUtilsGeometryCollectionFactoryData* InFactory) const
{
	UPCGSliceFractureFactoryData* Factory = InFactory
		? Cast<UPCGSliceFractureFactoryData>(InFactory)
		: FPCGContext::NewObject_AnyThread<UPCGSliceFractureFactoryData>(InContext);
	if (!Factory)
	{
		return nullptr;
	}

	Factory->SlicesX = FMath::Max(0, SlicesX);
	Factory->SlicesY = FMath::Max(0, SlicesY);
	Factory->SlicesZ = FMath::Max(0, SlicesZ);
	Factory->SliceAngleVariation = SliceAngleVariation;
	Factory->SliceOffsetVariation = SliceOffsetVariation;
	Factory->Common = Common;
	return Factory;
}

// --- Brick -----------------------------------------------------------------------------------------------

bool UPCGBrickFractureFactoryData::Fracture(
	FGeometryCollection& InOutCollection,
	const FDataflowTransformSelection& InTargetBones,
	FPCGContext* InContext,
	FPCGUtilsGeometryCollectionMutationResult& OutMutation) const
{
	const FText OperationName = LOCTEXT("BrickName", "Brick Fracture");
	FResolvedCommon Resolved;
	if (!ResolveCommon(InOutCollection, InTargetBones, Common, OperationName, InContext, Resolved))
	{
		return false;
	}

	// Not a guess: the bond pattern is defined by the mortar between the bricks, so with no gap the cutter has
	// nothing to cut along and returns INDEX_NONE with no explanation.
	if (Common.Grout <= 0.0f)
	{
		PCGLog::LogErrorOnGraph(
			LOCTEXT("BrickNeedsGrout",
				"Brick Fracture needs a Grout above zero - the bond is the mortar between the bricks, so with "
				"no gap there is nothing to cut along."), InContext);
		return false;
	}

	const int32 ResultGeometryIndex = FFractureEngineFracturing::BrickCutter(
		InOutCollection,
		InTargetBones,
		Resolved.Bounds,
		FTransform::Identity,
		Bond,
		FMath::Max(UE_KINDA_SMALL_NUMBER, BrickLength),
		FMath::Max(UE_KINDA_SMALL_NUMBER, BrickHeight),
		FMath::Max(UE_KINDA_SMALL_NUMBER, BrickDepth),
		Common.RandomSeed,
		Common.ChanceToFracture,
		Resolved.IslandSplit,
		Common.Grout,
		Resolved.Noise.Amplitude,
		Resolved.Noise.Frequency,
		Resolved.Noise.Persistence,
		Resolved.Noise.Lacunarity,
		Resolved.Noise.Octaves,
		Resolved.Noise.PointSpacing,
		/*InAddSamplesForCollision=*/false,
		/*InCollisionSampleSpacing=*/0.0f);

	if (ResultGeometryIndex == INDEX_NONE)
	{
		LogCutProducedNothing(OperationName, InTargetBones, Common, Resolved.Bounds, InContext);
		return false;
	}

	OutMutation = MakeMutation(InOutCollection, Resolved.BonesBefore);
	UE_LOG(LogPCGUtilsFracture, Verbose, TEXT("Brick Fracture: %.1fx%.1fx%.1f, bones %d -> %d"),
		BrickLength, BrickHeight, BrickDepth,
		Resolved.BonesBefore, InOutCollection.NumElements(FGeometryCollection::TransformGroup));
	return true;
}

FString UPCGBrickFractureFactoryData::GetOperationDescription() const
{
	return FString::Printf(TEXT("Brick (%.0fx%.0fx%.0f)"), BrickLength, BrickHeight, BrickDepth);
}

void UPCGBrickFractureFactoryData::AddToCrc(FArchiveCrc32& Ar, bool bFullDataCrc) const
{
	Super::AddToCrc(Ar, bFullDataCrc);
	if (!bFullDataCrc)
	{
		return;
	}

	uint8 LocalBond = static_cast<uint8>(Bond);
	float LocalLength = BrickLength;
	float LocalHeight = BrickHeight;
	float LocalDepth = BrickDepth;
	Ar << LocalBond;
	Ar << LocalLength;
	Ar << LocalHeight;
	Ar << LocalDepth;
	Common.AddToCrc(Ar);
}

UPCGBrickFractureSettings::UPCGBrickFractureSettings()
{
	// Brick is the one operation that cannot work at the shared default of zero, so it ships with a usable one
	// rather than failing on first use.
	Common.Grout = 1.0f;
}

#if WITH_EDITOR
FText UPCGBrickFractureSettings::GetDefaultNodeTitle() const
{
	return LOCTEXT("BrickTitle", "Fracture | Brick");
}

FText UPCGBrickFractureSettings::GetNodeTooltipText() const
{
	return LOCTEXT("BrickTooltip",
		"Divides the targeted bones into a masonry bond - stretcher, stack, English, header or Flemish - "
		"matching Fracture Mode's Brick tool. Needs a Grout above zero, since the bond is defined by the mortar "
		"between the bricks. The brick dimensions are absolute, so the piece count follows from how big the "
		"target is. Emits a Fracture operation for GC | Fracture to run.");
}

FString UPCGBrickFractureSettings::GetAdditionalTitleInformation() const
{
	const UEnum* BondEnum = StaticEnum<EFractureBrickBondEnum>();
	return BondEnum
		? BondEnum->GetDisplayNameTextByValue(static_cast<int64>(Bond)).ToString() : FString();
}
#endif

UPCGUtilsFractureFactoryData* UPCGBrickFractureSettings::CreateFractureFactory(
	FPCGContext* InContext, UPCGUtilsGeometryCollectionFactoryData* InFactory) const
{
	UPCGBrickFractureFactoryData* Factory = InFactory
		? Cast<UPCGBrickFractureFactoryData>(InFactory)
		: FPCGContext::NewObject_AnyThread<UPCGBrickFractureFactoryData>(InContext);
	if (!Factory)
	{
		return nullptr;
	}

	Factory->Bond = Bond;
	Factory->BrickLength = BrickLength;
	Factory->BrickHeight = BrickHeight;
	Factory->BrickDepth = BrickDepth;
	Factory->Common = Common;
	return Factory;
}

#undef LOCTEXT_NAMESPACE
