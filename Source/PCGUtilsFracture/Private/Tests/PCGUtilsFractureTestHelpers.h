// Copyright Max Harris

#pragma once

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "Data/PCGBasePointData.h"
#include "Data/PCGDynamicMeshData.h"
#include "Data/PCGUtilsClusterInterop.h"
#include "Data/PCGGeometryCollectionData.h"
#include "Data/PCGPointArrayData.h"
#include "DynamicMesh/DynamicMesh3.h"
#include "DynamicMesh/DynamicMeshAttributeSet.h"
#include "Elements/Conversion/PCGDynMeshToGeometryCollection.h"
#include "Elements/Conversion/PCGGeometryCollectionBonesToPoints.h"
#include "Elements/Conversion/PCGGeometryCollectionToDynMesh.h"
#include "Elements/Edit/PCGPruneGeometryCollection.h"
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
