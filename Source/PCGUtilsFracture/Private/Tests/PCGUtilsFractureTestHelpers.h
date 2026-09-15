// Copyright Max Harris

#pragma once

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "Data/PCGBasePointData.h"
#include "Data/PCGDynamicMeshData.h"
#include "Data/PCGUtilsClusterInterop.h"
#include "Data/PCGGeometryCollectionData.h"
#include "Data/PCGUtilsGeometryCollectionRevisionPublisher.h"
#include "Data/PCGPointArrayData.h"
#include "DynamicMesh/DynamicMesh3.h"
#include "DynamicMesh/DynamicMeshAttributeSet.h"
#include "Elements/Conversion/PCGDynMeshToGeometryCollection.h"
#include "Elements/Conversion/PCGGeometryCollectionBonesToPoints.h"
#include "Elements/Conversion/PCGGeometryCollectionToDynMesh.h"
#include "Elements/Edit/PCGPruneGeometryCollection.h"
#include "Elements/Edit/PCGGeometryCollectionProjectBones.h"
#include "Elements/Edit/PCGGeometryCollectionTransformBones.h"
#include "Elements/Fracture/PCGFractureGeometryCollection.h"
#include "Elements/Fracture/PCGUniformVoronoiFracture.h"
#include "Elements/Fracture/PCGVoronoiFracture.h"
#include "Elements/Selections/PCGGeometryCollectionSelectionFromPoints.h"
#include "GeometryCollection/GeometryCollection.h"
#include "GeometryCollectionToDynamicMesh.h"
#include "FunctionLibraries/PCGUtilsGeometryCollectionHelpers.h"
#include "FunctionLibraries/PCGUtilsGeometryCollectionHierarchy.h"
#include "Generators/GridBoxMeshGenerator.h"
#include "Metadata/PCGMetadata.h"
#include "PCGUtilsFracture.h"
#include "PCGContext.h"
#include "UDynamicMesh.h"


/**
 * Shared fixtures for the PCGUtilsFracture automation tests: building a source solid, running one element to
 * completion outside a graph, and the short graph fragments (to collection, fracture, prune, back to DynMesh)
 * that most tests need before they get to what they are actually asserting.
 *
 * Header-only and inline so every test translation unit shares one definition of "a fractured cube".
 */
namespace PCGUtilsFractureTests
{
	constexpr double BoxSize = 100.0;

	/**
	 * A closed box, built straight from GeometryCore rather than through Geometry Script - the module has no
	 * production need for GeometryScriptingCore and a test should not add a dependency to the shipping module.
	 */
	inline UPCGDynamicMeshData* Box(double Size = BoxSize, const FVector& Center = FVector::ZeroVector)
	{
		UE::Geometry::FGridBoxMeshGenerator Generator;
		Generator.Box = UE::Geometry::FOrientedBox3d(Center, FVector3d(Size * 0.5));
		Generator.EdgeVertices = UE::Geometry::FIndex3i(1, 1, 1);
		Generator.Generate();

		UE::Geometry::FDynamicMesh3 GeneratedMesh(&Generator);
		// AppendMeshToCollection reads the attribute set unconditionally, so it must exist.
		GeneratedMesh.EnableAttributes();
		GeneratedMesh.Attributes()->EnableMaterialID();

		UPCGDynamicMeshData* Data = NewObject<UPCGDynamicMeshData>();
		Data->Initialize(MoveTemp(GeneratedMesh));
		return Data;
	}

	/** A deterministic lattice of sites filling the box, so piece counts are reproducible across runs. */
	inline UPCGPointArrayData* SiteGrid(int32 PerAxis, double Size = BoxSize)
	{
		UPCGPointArrayData* Points = NewObject<UPCGPointArrayData>();
		Points->SetNumPoints(PerAxis * PerAxis * PerAxis, /*bInitializeValues=*/false);
		auto Transforms = Points->GetTransformValueRange();
		auto Densities = Points->GetDensityValueRange();
		auto BoundsMin = Points->GetBoundsMinValueRange();
		auto BoundsMax = Points->GetBoundsMaxValueRange();

		const double Step = Size / PerAxis;
		const double Start = -Size * 0.5 + Step * 0.5;
		int32 Index = 0;
		for (int32 X = 0; X < PerAxis; ++X)
		{
			for (int32 Y = 0; Y < PerAxis; ++Y)
			{
				for (int32 Z = 0; Z < PerAxis; ++Z)
				{
					Transforms[Index] = FTransform(FVector(
						Start + X * Step, Start + Y * Step, Start + Z * Step));
					Densities[Index] = 1.0f;
					BoundsMin[Index] = FVector::ZeroVector;
					BoundsMax[Index] = FVector::ZeroVector;
					++Index;
				}
			}
		}
		return Points;
	}

	/**
	 * A box tilted about Y, for testing projection against something that is not flat.
	 *
	 * Vertices are rotated in place rather than the mesh being given a transform, because the projection node
	 * treats a Target mesh as already sharing the collection's space and applies no transform of its own.
	 */
	inline UPCGDynamicMeshData* TiltedBox(double Size, const FVector& Center, double PitchDegrees)
	{
		UE::Geometry::FGridBoxMeshGenerator Generator;
		Generator.Box = UE::Geometry::FOrientedBox3d(FVector3d::Zero(), FVector3d(Size * 0.5));
		Generator.EdgeVertices = UE::Geometry::FIndex3i(1, 1, 1);
		Generator.Generate();

		UE::Geometry::FDynamicMesh3 GeneratedMesh(&Generator);
		const FQuat Rotation(FRotator(PitchDegrees, 0.0, 0.0));
		for (const int32 VertexID : GeneratedMesh.VertexIndicesItr())
		{
			GeneratedMesh.SetVertex(VertexID, Rotation.RotateVector(GeneratedMesh.GetVertex(VertexID)) + Center);
		}
		GeneratedMesh.EnableAttributes();
		GeneratedMesh.Attributes()->EnableMaterialID();

		UPCGDynamicMeshData* Data = NewObject<UPCGDynamicMeshData>();
		Data->Initialize(MoveTemp(GeneratedMesh));
		return Data;
	}

	/** Runs one element to completion and returns its outputs. */
	inline TArray<FPCGTaggedData> Run(UPCGSettings* Settings, TArray<TPair<FName, const UPCGData*>> Inputs)
	{
		FPCGDataCollection Input;
		for (const TPair<FName, const UPCGData*>& Pair : Inputs)
		{
			FPCGTaggedData& Tagged = Input.TaggedData.Emplace_GetRef();
			Tagged.Pin = Pair.Key;
			Tagged.Data = Pair.Value;
		}
		Input.TaggedData.Emplace_GetRef().Data = Settings;

		FPCGElementPtr Element = Settings->GetElement();
		TUniquePtr<FPCGContext> Context(
			Element->Initialize(FPCGInitializeElementParams(&Input, nullptr, nullptr)));
		Context->AsyncState.bIsRunningOnMainThread = true;
		Context->AsyncState.NumAvailableTasks = 1;
		for (int32 Iteration = 0; Iteration < 32; ++Iteration)
		{
			if (Element->Execute(Context.Get()))
			{
				return Context->OutputData.TaggedData;
			}
		}
		return {};
	}

	template<typename T>
	const T* FirstOutput(const TArray<FPCGTaggedData>& Outputs)
	{
		for (const FPCGTaggedData& Tagged : Outputs)
		{
			if (const T* Typed = Cast<const T>(Tagged.Data))
			{
				return Typed;
			}
		}
		return nullptr;
	}

	inline const UPCGGeometryCollectionData* ToCollection(const UPCGDynamicMeshData* Mesh)
	{
		UPCGDynMeshToGeometryCollectionSettings* Settings = NewObject<UPCGDynMeshToGeometryCollectionSettings>();
		return FirstOutput<UPCGGeometryCollectionData>(
			Run(Settings, {{PCGDynMeshToGeometryCollectionConstants::MeshInputPin, Mesh}}));
	}

	inline const UPCGGeometryCollectionData* Fracture(
		const UPCGGeometryCollectionData* Collection, const UPCGBasePointData* Sites)
	{
		UPCGVoronoiFractureSettings* VoronoiSettings = NewObject<UPCGVoronoiFractureSettings>();
		// The test authors sites directly in collection space, so skip the world-space conversion (there is
		// no PCG target actor in a bare automation context anyway).
		VoronoiSettings->bSitesAreWorldSpace = false;
		const UPCGUtilsFractureFactoryData* Operation = FirstOutput<UPCGUtilsFractureFactoryData>(
			Run(VoronoiSettings, {{PCGVoronoiFractureConstants::SitesInputPin, Sites}}));
		if (!Operation)
		{
			return nullptr;
		}

		UPCGFractureGeometryCollectionSettings* FractureSettings = NewObject<UPCGFractureGeometryCollectionSettings>();
		return FirstOutput<UPCGGeometryCollectionData>(Run(FractureSettings, {
			{PCGFractureGeometryCollectionConstants::CollectionInputPin, Collection},
			{PCGUtilsFractureFactoryConstants::FracturesInputPin, Operation}}));
	}

	/** Explicit site list, for reproducing degenerate inputs. */
	inline UPCGPointArrayData* SitesAt(const TArray<FVector>& Locations)
	{
		UPCGPointArrayData* Points = NewObject<UPCGPointArrayData>();
		Points->SetNumPoints(Locations.Num(), /*bInitializeValues=*/true);
		auto Transforms = Points->GetTransformValueRange();
		for (int32 Index = 0; Index < Locations.Num(); ++Index)
		{
			Transforms[Index] = FTransform(Locations[Index]);
		}
		return Points;
	}

	/** Runs Voronoi Fracture + Fracture GC and reports whether a collection came out. */
	inline bool TryFracture(const UPCGGeometryCollectionData* Collection, const UPCGBasePointData* Sites)
	{
		return Fracture(Collection, Sites) != nullptr;
	}

	/** Uniform Voronoi: the Fracture Mode workflow, with no point input at all. */
	inline const UPCGGeometryCollectionData* UniformFracture(
		const UPCGGeometryCollectionData* Collection, int32 MinSites, int32 MaxSites, bool bGroupFracture = true)
	{
		UPCGUniformVoronoiFractureSettings* UniformSettings =
			NewObject<UPCGUniformVoronoiFractureSettings>();
		UniformSettings->MinVoronoiSites = MinSites;
		UniformSettings->MaxVoronoiSites = MaxSites;
		UniformSettings->bGroupFracture = bGroupFracture;

		const UPCGUtilsFractureFactoryData* Operation = FirstOutput<UPCGUtilsFractureFactoryData>(
			Run(UniformSettings, {}));
		if (!Operation)
		{
			return nullptr;
		}

		UPCGFractureGeometryCollectionSettings* FractureSettings = NewObject<UPCGFractureGeometryCollectionSettings>();
		return FirstOutput<UPCGGeometryCollectionData>(Run(FractureSettings, {
			{PCGFractureGeometryCollectionConstants::CollectionInputPin, Collection},
			{PCGUtilsFractureFactoryConstants::FracturesInputPin, Operation}}));
	}

	/**
	 * Optional attributes are opt-in by design, so a test that reads one has to ask for it - which also keeps
	 * these tests honest about what the node writes by default.
	 */
	inline const UPCGBasePointData* BonesToPoints(
		const UPCGGeometryCollectionData* Collection, bool bWithSurfaceAttributes = false)
	{
		UPCGGeometryCollectionBonesToPointsSettings* Settings = NewObject<UPCGGeometryCollectionBonesToPointsSettings>();
		Settings->bOutputToWorldSpace = false;
		Settings->bOutputIsExterior = bWithSurfaceAttributes;
		Settings->bOutputExposureRatio = bWithSurfaceAttributes;
		Settings->bOutputExteriorArea = bWithSurfaceAttributes;
		Settings->bOutputInteriorArea = bWithSurfaceAttributes;
		Settings->bOutputExteriorFaceCount = bWithSurfaceAttributes;
		Settings->bOutputInteriorFaceCount = bWithSurfaceAttributes;
		return FirstOutput<UPCGBasePointData>(
			Run(Settings, {{PCGGeometryCollectionBonesToPointsConstants::CollectionInputPin, Collection}}));
	}

	/** Stands in for the PCG/PCGEx spatial filtering step: keeps points whose centre is inside Region. */
	inline UPCGPointArrayData* FilterPointsInBox(const UPCGBasePointData* Points, const FBox& Region)
	{
		TArray<int32> Kept;
		const auto Transforms = Points->GetConstTransformValueRange();
		for (int32 Index = 0; Index < Transforms.Num(); ++Index)
		{
			if (Region.IsInsideOrOn(Transforms[Index].GetLocation()))
			{
				Kept.Add(Index);
			}
		}

		UPCGPointArrayData* Filtered = NewObject<UPCGPointArrayData>();
		// Selecting a subset means the data cannot be inherited directly; without this the source metadata
		// attributes are not carried over. Mirrors FPCGCullPointsOutsideActorBoundsElement.
		FPCGInitializeFromDataParams InitializeParams(Points);
		InitializeParams.bInheritSpatialData = false;
		Filtered->InitializeFromDataWithParams(InitializeParams);
		Filtered->SetPointsFrom(Points, Kept);
		return Filtered;
	}

	/** Keeps the points at the given indices, preserving their metadata. */
	inline UPCGPointArrayData* FilterPointsByIndex(const UPCGBasePointData* Points, const TArray<int32>& Indices)
	{
		UPCGPointArrayData* Filtered = NewObject<UPCGPointArrayData>();
		FPCGInitializeFromDataParams InitializeParams(Points);
		InitializeParams.bInheritSpatialData = false;
		Filtered->InitializeFromDataWithParams(InitializeParams);
		Filtered->SetPointsFrom(Points, Indices);
		return Filtered;
	}

	inline const UPCGUtilsGeometryCollectionSelectionFactoryData* SelectionFromPoints(const UPCGBasePointData* Points)
	{
		UPCGGeometryCollectionSelectionFromPointsSettings* Settings = NewObject<UPCGGeometryCollectionSelectionFromPointsSettings>();
		return FirstOutput<UPCGUtilsGeometryCollectionSelectionFactoryData>(
			Run(Settings, {{PCGGeometryCollectionSelectionFromPointsConstants::PointsInputPin, Points}}));
	}

	inline const UPCGGeometryCollectionData* Prune(
		const UPCGGeometryCollectionData* Collection, const UPCGUtilsGeometryCollectionSelectionFactoryData* Selection)
	{
		UPCGPruneGeometryCollectionSettings* Settings = NewObject<UPCGPruneGeometryCollectionSettings>();
		return FirstOutput<UPCGGeometryCollectionData>(Run(Settings, {
			{PCGPruneGeometryCollectionConstants::CollectionInputPin, Collection},
			{PCGUtilsGeometryCollectionSelectionFactoryConstants::SelectionInputPin, Selection}}));
	}

	/**
	 * Applies bone points back onto a collection.
	 *
	 * Local space by default, matching BonesToPoints above: an automation context has no PCG target actor, so
	 * both sides must agree to skip the world-space conversion or the round trip is not testing what it claims.
	 */
	inline const UPCGGeometryCollectionData* TransformBones(
		const UPCGGeometryCollectionData* Collection,
		const UPCGBasePointData* Points,
		TFunctionRef<void(UPCGGeometryCollectionTransformBonesSettings&)> Configure)
	{
		UPCGGeometryCollectionTransformBonesSettings* Settings =
			NewObject<UPCGGeometryCollectionTransformBonesSettings>();
		Settings->bPointsAreWorldSpace = false;
		Configure(*Settings);
		return FirstOutput<UPCGGeometryCollectionData>(Run(Settings, {
			{PCGGeometryCollectionTransformBonesConstants::CollectionInputPin, Collection},
			{PCGGeometryCollectionTransformBonesConstants::PointsInputPin, Points}}));
	}

	inline const UPCGGeometryCollectionData* TransformBones(
		const UPCGGeometryCollectionData* Collection, const UPCGBasePointData* Points)
	{
		return TransformBones(Collection, Points, [](UPCGGeometryCollectionTransformBonesSettings&) {});
	}

	/** Offsets every point's translation, standing in for whatever point processing a real graph would do. */
	inline UPCGPointArrayData* OffsetPoints(const UPCGBasePointData* Points, const FVector& Offset)
	{
		UPCGPointArrayData* Moved = NewObject<UPCGPointArrayData>();
		FPCGInitializeFromDataParams InitializeParams(Points);
		InitializeParams.bInheritSpatialData = false;
		Moved->InitializeFromDataWithParams(InitializeParams);

		TArray<int32> AllIndices;
		AllIndices.Reserve(Points->GetNumPoints());
		for (int32 Index = 0; Index < Points->GetNumPoints(); ++Index)
		{
			AllIndices.Add(Index);
		}
		Moved->SetPointsFrom(Points, AllIndices);

		auto Transforms = Moved->GetTransformValueRange();
		for (int32 Index = 0; Index < Transforms.Num(); ++Index)
		{
			Transforms[Index].AddToTranslation(Offset);
		}
		return Moved;
	}

	/**
	 * Republishes a collection with an extra placement composed onto its root bones.
	 *
	 * This is how a collection read from a placed component or imported from an asset arrives, and it is the
	 * case a parent-space transform implementation gets wrong - so anything claiming to handle non-identity
	 * roots has to be tested against one.
	 */
	inline const UPCGGeometryCollectionData* PlacedCollection(
		const UPCGGeometryCollectionData* Collection, const FTransform& Placement)
	{
		TSharedRef<FGeometryCollection> Copy = Collection->CreateMutableCopy();
		PCGUtilsGeometryCollectionHelpers::PlaceCollection(*Copy, Placement);

		FPCGUtilsGeometryCollectionMutationResult Mutation;
		Mutation.bTransformsChanged = true;
		return PCGUtilsGeometryCollectionRevisionPublisher::PublishRevision(
			/*Context=*/nullptr, Collection, Copy, Mutation);
	}

	/** Projects a collection against a Target mesh, so no world or physics scene is needed. */
	inline const UPCGGeometryCollectionData* ProjectBones(
		const UPCGGeometryCollectionData* Collection,
		const UPCGDynamicMeshData* Target,
		TFunctionRef<void(UPCGGeometryCollectionProjectBonesSettings&)> Configure)
	{
		UPCGGeometryCollectionProjectBonesSettings* Settings =
			NewObject<UPCGGeometryCollectionProjectBonesSettings>();
		Configure(*Settings);
		return FirstOutput<UPCGGeometryCollectionData>(Run(Settings, {
			{PCGGeometryCollectionProjectBonesConstants::CollectionInputPin, Collection},
			{PCGGeometryCollectionProjectBonesConstants::TargetInputPin, Target}}));
	}

	inline const UPCGGeometryCollectionData* ProjectBones(
		const UPCGGeometryCollectionData* Collection, const UPCGDynamicMeshData* Target)
	{
		return ProjectBones(Collection, Target, [](UPCGGeometryCollectionProjectBonesSettings&) {});
	}

	/** A piece's world-space bounds: its local bounds under its global transform, corner by corner. */
	inline FBox PieceWorldBounds(
		const FGeometryCollection& Collection, int32 Bone, TConstArrayView<FTransform> Globals)
	{
		FBox Bounds(ForceInit);
		const FBox Local = PCGUtilsGeometryCollectionHelpers::GetBoneLocalBounds(Collection, Bone);
		if (!Local.IsValid)
		{
			return Bounds;
		}

		const FTransform ToWorld = Globals.IsValidIndex(Bone) ? Globals[Bone] : FTransform::Identity;
		for (int32 Corner = 0; Corner < 8; ++Corner)
		{
			Bounds += ToWorld.TransformPosition(FVector(
				(Corner & 1) ? Local.Max.X : Local.Min.X,
				(Corner & 2) ? Local.Max.Y : Local.Min.Y,
				(Corner & 4) ? Local.Max.Z : Local.Min.Z));
		}
		return Bounds;
	}

	/** The lowest point of any piece's bounds, i.e. how far down the whole collection reaches. */
	inline double LowestPieceBoundsZ(const UPCGGeometryCollectionData* Data)
	{
		const FGeometryCollection& Collection = Data->GetCollection();
		TArray<FTransform> Globals;
		PCGUtilsGeometryCollectionHelpers::ComputeGlobalTransforms(Collection, Globals);

		TArray<int32> Pieces;
		PCGUtilsGeometryCollectionHierarchy::GatherPieces(Collection, Pieces);

		double Lowest = TNumericLimits<double>::Max();
		for (const int32 Piece : Pieces)
		{
			const FBox Bounds = PieceWorldBounds(Collection, Piece, Globals);
			if (Bounds.IsValid)
			{
				Lowest = FMath::Min(Lowest, Bounds.Min.Z);
			}
		}
		return Lowest;
	}

	/** The collection-space global transforms of a published collection. */
	inline TArray<FTransform> GlobalTransforms(const UPCGGeometryCollectionData* Collection)
	{
		TArray<FTransform> Out;
		PCGUtilsGeometryCollectionHelpers::ComputeGlobalTransforms(Collection->GetCollection(), Out);
		return Out;
	}

	inline const UPCGDynamicMeshData* ToDynMesh(const UPCGGeometryCollectionData* Collection)
	{
		UPCGGeometryCollectionToDynMeshSettings* Settings = NewObject<UPCGGeometryCollectionToDynMeshSettings>();
		return FirstOutput<UPCGDynamicMeshData>(
			Run(Settings, {{PCGGeometryCollectionToDynMeshConstants::CollectionInputPin, Collection}}));
	}

	inline const UE::Geometry::FDynamicMesh3& Mesh(const UPCGDynamicMeshData* Data)
	{
		return *Data->GetDynamicMesh()->GetMeshPtr();
	}

	inline int32 CountPieces(const UPCGGeometryCollectionData* Data)
	{
		TArray<int32> Bones;
		PCGUtilsGeometryCollectionHierarchy::GatherPieces(Data->GetCollection(), Bones);
		return Bones.Num();
	}

	inline const UE::Geometry::FDynamicMeshPolygroupAttribute* FindLayer(
		const UE::Geometry::FDynamicMesh3& InMesh, FName LayerName)
	{
		if (!InMesh.HasAttributes())
		{
			return nullptr;
		}
		for (int32 Index = 0; Index < InMesh.Attributes()->NumPolygroupLayers(); ++Index)
		{
			const UE::Geometry::FDynamicMeshPolygroupAttribute* Layer =
				InMesh.Attributes()->GetPolygroupLayer(Index);
			if (Layer->GetName() == LayerName)
			{
				return Layer;
			}
		}
		return nullptr;
	}
}

#endif // WITH_AUTOMATION_TESTS
