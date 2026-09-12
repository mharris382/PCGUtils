// Copyright Max Harris

#include "Elements/Fracture/PCGMeshFracture.h"

#include "Data/PCGBasePointData.h"
#include "Data/PCGDynamicMeshData.h"
#include "DynamicMesh/DynamicMesh3.h"
#include "DynamicMesh/DynamicMeshAttributeSet.h"
#include "DynamicMesh/MeshNormals.h"
#include "DynamicMesh/MeshTransforms.h"
#include "DynamicMesh/Operations/MergeCoincidentMeshEdges.h"
#include "DynamicMeshEditor.h"
#include "Elements/PCGUtilsDynMeshSpaceHelpers.h"
#include "Engine/StaticMesh.h"
#include "FractureEngineFracturing.h"
#include "FunctionLibraries/PCGUtilsGeometryCollectionHelpers.h"
#include "GeometryCollection/GeometryCollection.h"
#include "MeshDescription.h"
#include "MeshDescriptionToDynamicMesh.h"
#include "Metadata/Accessors/PCGAttributeAccessorHelpers.h"
#include "Operations/MeshSelfUnion.h"
#include "PCGContext.h"
#include "PCGPin.h"
#include "PCGUtilsFracture.h"
#include "PlanarCut.h"
#include "Serialization/ArchiveCrc32.h"
#include "StaticMeshLODResourcesToDynamicMesh.h"
#include "StaticMeshResources.h"
#include "UDynamicMesh.h"
#include "Utils/PCGLogErrors.h"

#define LOCTEXT_NAMESPACE "PCGMeshFracture"

namespace
{
	/**
	 * Gives a cutter a complete, non-degenerate normal overlay.
	 *
	 * PlanarCut augments a raw cutter itself (FCellMeshes), copying the primary normal overlay into per-vertex
	 * normals and reading an unset element as zero - so a DynMesh without normals would give every face the cut
	 * creates a zero normal. UVs need no such care: unset reads as zero, which is all an internal face gets anyway.
	 */
	void EnsureMeshFractureCutterNormals(UE::Geometry::FDynamicMesh3& Mesh)
	{
		if (!Mesh.HasAttributes())
		{
			Mesh.EnableAttributes();
		}
		if (Mesh.Attributes()->NumNormalLayers() == 0)
		{
			Mesh.Attributes()->SetNumNormalLayers(1);
		}

		UE::Geometry::FDynamicMeshNormalOverlay* Normals = Mesh.Attributes()->PrimaryNormals();
		bool bNormalsUsable = true;
		for (const int32 TriangleID : Mesh.TriangleIndicesItr())
		{
			if (!Normals->IsSetTriangle(TriangleID))
			{
				bNormalsUsable = false;
				break;
			}
			const UE::Geometry::FIndex3i Elements = Normals->GetTriangle(TriangleID);
			for (int32 Corner = 0; Corner < 3 && bNormalsUsable; ++Corner)
			{
				bNormalsUsable = Normals->GetElement(Elements[Corner]).SquaredLength() > UE_SMALL_NUMBER;
			}
			if (!bNormalsUsable)
			{
				break;
			}
		}

		if (!bNormalsUsable)
		{
			UE::Geometry::FMeshNormals::InitializeOverlayToPerVertexNormals(Normals, /*bUseMeshVertexNormalsIfAvailable=*/false);
		}
	}

	/**
	 * Converts a Static Mesh into cutter geometry in the mesh's own space.
	 *
	 * In the editor this reads the source mesh description, exactly as Fracture Mode's Mesh tool and the MeshCutter
	 * Dataflow node do. Outside it - or for a mesh with no source - it falls back to render data, which Epic's
	 * Dataflow node deliberately does not attempt; that needs Allow CPU Access in a cooked build, and a weld,
	 * because render data splits every vertex along its UV and normal seams.
	 */
	bool ConvertStaticMeshForMeshFracture(
		UStaticMesh* StaticMesh, bool bUseHiRes, int32 LODLevel, UE::Geometry::FDynamicMesh3& OutMesh, FText& OutError)
	{
#if WITH_EDITORONLY_DATA
		const FMeshDescription* Description = bUseHiRes ? StaticMesh->GetHiResMeshDescription() : nullptr;
		if (!Description || Description->Vertices().Num() == 0)
		{
			Description = StaticMesh->GetMeshDescription(FMath::Max(0, LODLevel));
		}
		if (Description && Description->Vertices().Num() > 0)
		{
			FMeshDescriptionToDynamicMesh Converter;
			Converter.Convert(Description, OutMesh, /*bCopyTangents=*/true);
			if (OutMesh.TriangleCount() > 0)
			{
				return true;
			}
		}
#endif

		const FStaticMeshRenderData* RenderData = StaticMesh->GetRenderData();
		if (!RenderData || RenderData->LODResources.Num() == 0)
		{
			OutError = LOCTEXT("NoStaticMeshGeometry", "has no geometry to cut with");
			return false;
		}

#if !WITH_EDITOR
		if (!StaticMesh->bAllowCPUAccess)
		{
			OutError = LOCTEXT("NoCPUAccess",
				"has no CPU-accessible geometry. Enable Allow CPU Access on the asset to cut with it at runtime");
			return false;
		}
#endif

		const int32 LODIndex = FMath::Clamp(LODLevel, 0, RenderData->LODResources.Num() - 1);
		UE::Geometry::FStaticMeshLODResourcesToDynamicMesh::ConversionOptions Options;
		Options.bWantVertexColors = false;
		if (!UE::Geometry::FStaticMeshLODResourcesToDynamicMesh::Convert(
			&RenderData->LODResources[LODIndex], Options, OutMesh) || OutMesh.TriangleCount() == 0)
		{
			OutError = LOCTEXT("RenderDataConversionFailed", "could not be converted from its render data");
			return false;
		}

		UE::Geometry::FMergeCoincidentMeshEdges Weld(&OutMesh);
		Weld.Apply();
		return true;
	}
}

// --- Factory ---------------------------------------------------------------------------------------------

bool UPCGMeshFractureFactoryData::Fracture(
	FGeometryCollection& InOutCollection,
	const FDataflowTransformSelection& InTargetBones,
	FPCGContext* InContext,
	FPCGUtilsGeometryCollectionMutationResult& OutMutation) const
{
	// Checked up front because FFractureEngineFracturing::MeshCutter reports every failure as a bare INDEX_NONE.

	if (!InTargetBones.AnySelected())
	{
		PCGLog::LogErrorOnGraph(LOCTEXT("NoTargets", "Mesh Fracture was given no target bones."), InContext);
		return false;
	}

	if (!CuttingMesh || CuttingMesh->TriangleCount() == 0)
	{
		PCGLog::LogErrorOnGraph(LOCTEXT("NoCutterAtFracture", "Mesh Fracture has no cutter geometry."), InContext);
		return false;
	}

	TArray<FString> MissingAttributes;
	if (!PCGUtilsGeometryCollectionHelpers::ValidateFractureRequirements(InOutCollection, MissingAttributes))
	{
		PCGLog::LogErrorOnGraph(FText::Format(
			LOCTEXT("MalformedCollection",
				"Mesh Fracture received a Geometry Collection missing the attribute(s) it requires: {0}. This is a "
				"bug in whichever node produced the collection, not a setting you can change."),
			FText::FromString(FString::Join(MissingAttributes, TEXT(", ")))), InContext);
		return false;
	}

	const FBox CollectionBounds = PCGUtilsGeometryCollectionHelpers::ComputeCollectionBounds(InOutCollection);
	if (!CollectionBounds.IsValid)
	{
		PCGLog::LogErrorOnGraph(LOCTEXT("NoBounds", "Mesh Fracture could not compute bounds for the collection."), InContext);
		return false;
	}

	TArray<FTransform> CutTransforms;
	if (CutDistribution == EMeshCutterCutDistribution::SingleCut)
	{
		// The cutter was built in collection space, so it cuts exactly where it already is.
		CutTransforms.Add(FTransform::Identity);

		if (!CutterBounds.Intersect(CollectionBounds))
		{
			PCGLog::LogErrorOnGraph(FText::Format(
				LOCTEXT("CutterMissesGeometry",
					"Mesh Fracture: the cutter does not overlap the geometry, so there is nothing to cut. The cutter "
					"occupies {0} to {1}; the geometry {2} to {3}. A cutter built from points in the wrong space does "
					"this - check Convert Points To Local Space."),
				FText::FromString(CutterBounds.Min.ToCompactString()),
				FText::FromString(CutterBounds.Max.ToCompactString()),
				FText::FromString(CollectionBounds.Min.ToCompactString()),
				FText::FromString(CollectionBounds.Max.ToCompactString())), InContext);
			return false;
		}
	}
	else
	{
		FFractureEngineFracturing::GenerateMeshTransforms(
			CutTransforms,
			CollectionBounds,
			RandomSeed,
			CutDistribution,
			FMath::Max(1, NumberToScatter),
			FMath::Max(1, GridX),
			FMath::Max(1, GridY),
			FMath::Max(1, GridZ),
			FMath::Max(0.0f, Variability),
			FMath::Max(0.001f, MinScaleFactor),
			FMath::Max(0.001f, MaxScaleFactor),
			bRandomOrientation,
			RollRange,
			PitchRange,
			YawRange);
	}

	const int32 BonesBefore = InOutCollection.NumElements(FGeometryCollection::TransformGroup);

	const int32 ResultGeometryIndex = FFractureEngineFracturing::MeshCutter(
		CutTransforms,
		InOutCollection,
		InTargetBones,
		*CuttingMesh,
		RandomSeed,
		ChanceToFracture,
		FIslandSplitSettings(
			bSplitIslands, static_cast<double>(CloseVertexDistance), static_cast<double>(VertexToSurfaceBridgeDistance)),
		// Collision sampling seeds Chaos simulation, which a transient modelling intermediate never reaches.
		/*InCollisionSampleSpacing=*/0.0f);

	if (ResultGeometryIndex == INDEX_NONE)
	{
		PCGLog::LogErrorOnGraph(FText::Format(
			LOCTEXT("CutProducedNothing",
				"Mesh Fracture cut nothing: {0} cut(s) across {1} target bone(s) occupying {2} to {3} produced no new "
				"pieces. A cutter that is not a closed volume, cuts that all miss the geometry, or a Chance To "
				"Fracture of {4} filtering every target will each do this."),
			FText::AsNumber(CutTransforms.Num()),
			FText::AsNumber(InTargetBones.NumSelected()),
			FText::FromString(CollectionBounds.Min.ToCompactString()),
			FText::FromString(CollectionBounds.Max.ToCompactString()),
			FText::AsNumber(ChanceToFracture)), InContext);
		return false;
	}

	// The cutter appends new pieces and removes only the intermediate clusters it created itself, so every bone
	// below BonesBefore still means what it did - the same contract as the other cutters.
	const int32 BonesAfter = InOutCollection.NumElements(FGeometryCollection::TransformGroup);
	OutMutation = FPCGUtilsGeometryCollectionMutationResult::Fracture(BonesAfter > BonesBefore ? BonesBefore : INDEX_NONE);

	UE_LOG(LogPCGUtilsFracture, Verbose, TEXT("Mesh Fracture: %d cut(s), cutter %d tri(s), bones %d -> %d"),
		CutTransforms.Num(), CuttingMesh->TriangleCount(), BonesBefore, BonesAfter);
	return true;
}

FString UPCGMeshFractureFactoryData::GetOperationDescription() const
{
	switch (CutDistribution)
	{
	case EMeshCutterCutDistribution::UniformRandom:
		return FString::Printf(TEXT("Mesh (%d scattered)"), NumberToScatter);
	case EMeshCutterCutDistribution::Grid:
		return FString::Printf(TEXT("Mesh (%dx%dx%d grid)"), GridX, GridY, GridZ);
	default:
		return TEXT("Mesh (single cut)");
	}
}

void UPCGMeshFractureFactoryData::AddToCrc(FArchiveCrc32& Ar, bool bFullDataCrc) const
{
	Super::AddToCrc(Ar, bFullDataCrc);
	if (!bFullDataCrc)
	{
		return;
	}

	// The DynMesh and point inputs are folded in as data dependencies by the base. The Static Mesh assets those
	// points name are not, so the resolved cutter's own shape stands in for them.
	FBox LocalBounds = CutterBounds;
	int32 LocalTriangleCount = CuttingMesh ? CuttingMesh->TriangleCount() : 0;
	int32 LocalVertexCount = CuttingMesh ? CuttingMesh->VertexCount() : 0;
	uint8 LocalDistribution = static_cast<uint8>(CutDistribution);
	int32 LocalNumberToScatter = NumberToScatter;
	int32 LocalGridX = GridX;
	int32 LocalGridY = GridY;
	int32 LocalGridZ = GridZ;
	float LocalVariability = Variability;
	float LocalMinScale = MinScaleFactor;
	float LocalMaxScale = MaxScaleFactor;
	bool bLocalRandomOrientation = bRandomOrientation;
	float LocalRoll = RollRange;
	float LocalPitch = PitchRange;
	float LocalYaw = YawRange;
	int32 LocalSeed = RandomSeed;
	float LocalChance = ChanceToFracture;
	bool bLocalSplit = bSplitIslands;
	float LocalCloseVertexDistance = CloseVertexDistance;
	float LocalBridgeDistance = VertexToSurfaceBridgeDistance;
	bool bLocalSelfUnion = bSelfUnionInput;

	Ar << LocalBounds << LocalTriangleCount << LocalVertexCount << LocalDistribution << LocalNumberToScatter;
	Ar << LocalGridX << LocalGridY << LocalGridZ << LocalVariability << LocalMinScale << LocalMaxScale;
	Ar << bLocalRandomOrientation << LocalRoll << LocalPitch << LocalYaw << LocalSeed << LocalChance;
	Ar << bLocalSplit << LocalCloseVertexDistance << LocalBridgeDistance << bLocalSelfUnion;
}

// --- Settings --------------------------------------------------------------------------------------------

UPCGMeshFractureSettings::UPCGMeshFractureSettings()
{
	MeshAttribute.SetAttributeName(TEXT("Mesh"));
}

#if WITH_EDITOR
FText UPCGMeshFractureSettings::GetDefaultNodeTitle() const
{
	return LOCTEXT("Title", "Fracture | Mesh");
}

FText UPCGMeshFractureSettings::GetNodeTooltipText() const
{
	return LOCTEXT("Tooltip",
		"Cuts the targeted bones with an arbitrary closed mesh, matching Fracture Mode's Mesh tool. Connect the "
		"cutter as DynMesh data, as points carrying a Static Mesh attribute (one instance per point, at the point's "
		"transform), or both: everything connected is combined into one cutter and, with Self Union Input on, "
		"self-unioned so overlapping inputs act as one volume. Single Cut uses the cutter where it is; Uniform "
		"Random and Grid scatter copies of it through the target's bounds. Emits a Fracture operation - connect it "
		"to GC | Fracture, which decides which bones it applies to.");
}

FString UPCGMeshFractureSettings::GetAdditionalTitleInformation() const
{
	switch (CutDistribution)
	{
	case EMeshCutterCutDistribution::UniformRandom:
		return FString::Printf(TEXT("%d cuts"), NumberToScatter);
	case EMeshCutterCutDistribution::Grid:
		return FString::Printf(TEXT("%dx%dx%d"), GridX, GridY, GridZ);
	default:
		return FString();
	}
}
#endif

bool UPCGMeshFractureSettings::RequiresMainThread(FPCGContext* InContext) const
{
	return InContext && !InContext->InputData.GetInputsByPin(PCGMeshFractureConstants::PointsInputPin).IsEmpty();
}

TArray<FPCGPinProperties> UPCGMeshFractureSettings::InputPinProperties() const
{
	TArray<FPCGPinProperties> Pins;
	Pins.Emplace(PCGMeshFractureConstants::DynMeshInputPin, EPCGDataType::DynamicMesh, true, true);
	Pins.Emplace(PCGMeshFractureConstants::PointsInputPin, EPCGDataType::Point, true, true);
	return Pins;
}

UPCGUtilsFractureFactoryData* UPCGMeshFractureSettings::CreateFractureFactory(
	FPCGContext* InContext, UPCGUtilsGeometryCollectionFactoryData* InFactory) const
{
	TSharedRef<UE::Geometry::FDynamicMesh3> Cutter = MakeShared<UE::Geometry::FDynamicMesh3>();
	Cutter->EnableAttributes();
	Cutter->Attributes()->EnableMaterialID();
	int32 NumParts = 0;

	auto AppendPart = [&Cutter, &NumParts](UE::Geometry::FDynamicMesh3& Part)
	{
		if (Part.TriangleCount() == 0)
		{
			return;
		}
		EnsureMeshFractureCutterNormals(Part);
		UE::Geometry::FMeshIndexMappings Mappings;
		UE::Geometry::FDynamicMeshEditor(&Cutter.Get()).AppendMesh(&Part, Mappings);
		++NumParts;
	};

	// DynMesh cutters share the collection's space by the module's convention, so they are used as they are.
	for (const FPCGTaggedData& Input : InContext->InputData.GetInputsByPin(PCGMeshFractureConstants::DynMeshInputPin))
	{
		const UPCGDynamicMeshData* MeshData = Cast<const UPCGDynamicMeshData>(Input.Data);
		const UDynamicMesh* DynamicMesh = MeshData ? MeshData->GetDynamicMesh() : nullptr;
		if (DynamicMesh && DynamicMesh->GetMeshPtr())
		{
			UE::Geometry::FDynamicMesh3 Part(*DynamicMesh->GetMeshPtr());
			AppendPart(Part);
		}
	}

	const TArray<FPCGTaggedData> PointInputs = InContext->InputData.GetInputsByPin(PCGMeshFractureConstants::PointsInputPin);
	if (!PointInputs.IsEmpty())
	{
		const FTransform LocalToWorld = PCGUtilsDynMeshSpaceHelpers::ResolveMeshActorTransform(
			InContext, /*MeshData=*/nullptr, bConvertPointsToLocalSpace);

		// One conversion per distinct asset, however many points place it. A null entry records a failure, so each
		// unusable asset is reported once rather than once per point.
		TMap<FSoftObjectPath, TSharedPtr<const UE::Geometry::FDynamicMesh3>> ConvertedMeshes;
		auto ResolveCutterMesh = [this, InContext](const FSoftObjectPath& MeshPath)
			-> TSharedPtr<const UE::Geometry::FDynamicMesh3>
		{
			if (MeshPath.IsNull())
			{
				return nullptr;
			}

			UObject* Object = MeshPath.ResolveObject();
			if (!Object)
			{
				Object = MeshPath.TryLoad();
			}

			UStaticMesh* StaticMesh = Cast<UStaticMesh>(Object);
			if (!StaticMesh)
			{
				PCGLog::LogWarningOnGraph(FText::Format(
					LOCTEXT("NotAStaticMesh", "Mesh Fracture: '{0}' is not a Static Mesh; points naming it are skipped."),
					FText::FromString(MeshPath.ToString())), InContext);
				return nullptr;
			}

			TSharedRef<UE::Geometry::FDynamicMesh3> Mesh = MakeShared<UE::Geometry::FDynamicMesh3>();
			FText Error;
			if (!ConvertStaticMeshForMeshFracture(StaticMesh, bUseHiRes, LODLevel, *Mesh, Error))
			{
				PCGLog::LogWarningOnGraph(FText::Format(
					LOCTEXT("StaticMeshUnusable", "Mesh Fracture: Static Mesh '{0}' {1}; points naming it are skipped."),
					FText::FromString(MeshPath.ToString()), Error), InContext);
				return nullptr;
			}
			return Mesh;
		};

		int32 NumSkippedPoints = 0;
		for (const FPCGTaggedData& Input : PointInputs)
		{
			const UPCGBasePointData* Points = Cast<const UPCGBasePointData>(Input.Data);
			if (!Points || Points->GetNumPoints() == 0)
			{
				continue;
			}

			TArray<FSoftObjectPath> MeshPaths;
			if (!PCGAttributeAccessorHelpers::ExtractAllValues<FSoftObjectPath>(
				Points, MeshAttribute, MeshPaths, InContext,
				EPCGAttributeAccessorFlags::AllowBroadcastAndConstructible, /*bQuiet=*/true))
			{
				PCGLog::LogErrorOnGraph(FText::Format(
					LOCTEXT("MissingMeshAttribute",
						"Mesh Fracture could not read a Static Mesh from '{0}' on the Points input. Set Mesh "
						"Attribute to the attribute holding each point's Static Mesh path."),
					MeshAttribute.GetDisplayText()), InContext);
				continue;
			}

			const auto Transforms = Points->GetConstTransformValueRange();
			for (int32 Index = 0; Index < Transforms.Num(); ++Index)
			{
				const FSoftObjectPath& MeshPath = MeshPaths[Index];
				const TSharedPtr<const UE::Geometry::FDynamicMesh3>* Source = ConvertedMeshes.Find(MeshPath);
				if (!Source)
				{
					Source = &ConvertedMeshes.Add(MeshPath, ResolveCutterMesh(MeshPath));
				}
				if (!Source->IsValid())
				{
					++NumSkippedPoints;
					continue;
				}

				const FTransform PointTransform = bConvertPointsToLocalSpace
					? Transforms[Index].GetRelativeTransform(LocalToWorld) : Transforms[Index];
				UE::Geometry::FDynamicMesh3 Part(**Source);
				MeshTransforms::ApplyTransform(
					Part, UE::Geometry::FTransformSRT3d(PointTransform), /*bReverseOrientationIfNeeded=*/true);
				AppendPart(Part);
			}
		}

		if (NumSkippedPoints > 0)
		{
			PCGLog::LogWarningOnGraph(FText::Format(
				LOCTEXT("SkippedPoints",
					"Mesh Fracture skipped {0} point(s) with no usable Static Mesh, so the cutter is missing their "
					"geometry."),
				FText::AsNumber(NumSkippedPoints)), InContext);
		}
	}

	if (NumParts == 0)
	{
		PCGLog::LogErrorOnGraph(FText::Format(
			LOCTEXT("NoCutter",
				"Mesh Fracture needs a cutter: connect DynMesh data to DynMesh, or points carrying a Static Mesh "
				"attribute ('{0}') to Points."),
			MeshAttribute.GetDisplayText()), InContext);
		return nullptr;
	}

	if (bSelfUnionInput)
	{
		UE::Geometry::FMeshSelfUnion Union(&Cutter.Get());
		// Compute() reports failure for many inputs it handled perfectly well - Geometry Script's own wrapper
		// ignores it for that reason - so the result is judged by what is left instead.
		Union.Compute();
		if (Cutter->TriangleCount() == 0)
		{
			PCGLog::LogErrorOnGraph(LOCTEXT("SelfUnionEmptied",
				"Mesh Fracture: self-union left nothing of the cutter. The inputs are probably open surfaces rather "
				"than closed volumes; turn Self Union Input off to cut with them as they are."), InContext);
			return nullptr;
		}
		EnsureMeshFractureCutterNormals(Cutter.Get());
	}

	// Fracture Mode's cutter conversion tags every cutter face with PlanarCut's "internal material" convention, a
	// negative material ID. Without it the faces the cut creates would inherit whatever material the cutter had.
	UE::Geometry::FDynamicMeshMaterialAttribute* MaterialIDs = Cutter->Attributes()->GetMaterialID();
	for (const int32 TriangleID : Cutter->TriangleIndicesItr())
	{
		MaterialIDs->SetValue(TriangleID, -1);
	}

	UPCGMeshFractureFactoryData* Factory = InFactory
		? Cast<UPCGMeshFractureFactoryData>(InFactory)
		: FPCGContext::NewObject_AnyThread<UPCGMeshFractureFactoryData>(InContext);
	if (!Factory)
	{
		return nullptr;
	}

	const UE::Geometry::FAxisAlignedBox3d Bounds = Cutter->GetBounds();
	Factory->CutterBounds = FBox(Bounds.Min, Bounds.Max);
	Factory->CuttingMesh = Cutter;
	Factory->CutDistribution = CutDistribution;
	Factory->NumberToScatter = FMath::Max(1, NumberToScatter);
	Factory->GridX = FMath::Max(1, GridX);
	Factory->GridY = FMath::Max(1, GridY);
	Factory->GridZ = FMath::Max(1, GridZ);
	Factory->Variability = Variability;
	Factory->MinScaleFactor = MinScaleFactor;
	Factory->MaxScaleFactor = MaxScaleFactor;
	Factory->bRandomOrientation = bRandomOrientation;
	Factory->RollRange = RollRange;
	Factory->PitchRange = PitchRange;
	Factory->YawRange = YawRange;
	Factory->RandomSeed = RandomSeed;
	Factory->ChanceToFracture = ChanceToFracture;
	Factory->bSplitIslands = bSplitIslands;
	Factory->CloseVertexDistance = CloseVertexDistance;
	Factory->VertexToSurfaceBridgeDistance = VertexToSurfaceBridgeDistance;
	Factory->bSelfUnionInput = bSelfUnionInput;

	UE_LOG(LogPCGUtilsFracture, Verbose, TEXT("Mesh Fracture: cutter built from %d part(s), %d tri(s)%s"),
		NumParts, Cutter->TriangleCount(), bSelfUnionInput ? TEXT(" (self-unioned)") : TEXT(""));
	return Factory;
}

#undef LOCTEXT_NAMESPACE
