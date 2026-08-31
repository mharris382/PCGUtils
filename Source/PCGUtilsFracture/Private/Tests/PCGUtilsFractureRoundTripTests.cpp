// Copyright Max Harris

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
#include "Generators/GridBoxMeshGenerator.h"
#include "Metadata/PCGMetadata.h"
#include "PCGUtilsFracture.h"
#include "PCGContext.h"
#include "UDynamicMesh.h"

namespace PCGUtilsFractureTests
{
	constexpr double BoxSize = 100.0;

	/**
	 * A closed box, built straight from GeometryCore rather than through Geometry Script - the module has no
	 * production need for GeometryScriptingCore and a test should not add a dependency to the shipping module.
	 */
	UPCGDynamicMeshData* Box(double Size = BoxSize, const FVector& Center = FVector::ZeroVector)
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
	UPCGPointArrayData* SiteGrid(int32 PerAxis, double Size = BoxSize)
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
	TArray<FPCGTaggedData> Run(UPCGSettings* Settings, TArray<TPair<FName, const UPCGData*>> Inputs)
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

	const UPCGGeometryCollectionData* ToCollection(const UPCGDynamicMeshData* Mesh)
	{
		UPCGDynMeshToGeometryCollectionSettings* Settings = NewObject<UPCGDynMeshToGeometryCollectionSettings>();
		return FirstOutput<UPCGGeometryCollectionData>(
			Run(Settings, {{PCGDynMeshToGeometryCollectionConstants::MeshInputPin, Mesh}}));
	}

	const UPCGGeometryCollectionData* Fracture(
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
	UPCGPointArrayData* SitesAt(const TArray<FVector>& Locations)
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
	bool TryFracture(const UPCGGeometryCollectionData* Collection, const UPCGBasePointData* Sites)
	{
		return Fracture(Collection, Sites) != nullptr;
	}

	/** Uniform Voronoi: the Fracture Mode workflow, with no point input at all. */
	const UPCGGeometryCollectionData* UniformFracture(
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
	const UPCGBasePointData* BonesToPoints(
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
	UPCGPointArrayData* FilterPointsInBox(const UPCGBasePointData* Points, const FBox& Region)
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
	UPCGPointArrayData* FilterPointsByIndex(const UPCGBasePointData* Points, const TArray<int32>& Indices)
	{
		UPCGPointArrayData* Filtered = NewObject<UPCGPointArrayData>();
		FPCGInitializeFromDataParams InitializeParams(Points);
		InitializeParams.bInheritSpatialData = false;
		Filtered->InitializeFromDataWithParams(InitializeParams);
		Filtered->SetPointsFrom(Points, Indices);
		return Filtered;
	}

	const UPCGUtilsGeometryCollectionSelectionFactoryData* SelectionFromPoints(const UPCGBasePointData* Points)
	{
		UPCGGeometryCollectionSelectionFromPointsSettings* Settings = NewObject<UPCGGeometryCollectionSelectionFromPointsSettings>();
		return FirstOutput<UPCGUtilsGeometryCollectionSelectionFactoryData>(
			Run(Settings, {{PCGGeometryCollectionSelectionFromPointsConstants::PointsInputPin, Points}}));
	}

	const UPCGGeometryCollectionData* Prune(
		const UPCGGeometryCollectionData* Collection, const UPCGUtilsGeometryCollectionSelectionFactoryData* Selection)
	{
		UPCGPruneGeometryCollectionSettings* Settings = NewObject<UPCGPruneGeometryCollectionSettings>();
		return FirstOutput<UPCGGeometryCollectionData>(Run(Settings, {
			{PCGPruneGeometryCollectionConstants::CollectionInputPin, Collection},
			{PCGUtilsGeometryCollectionSelectionFactoryConstants::SelectionInputPin, Selection}}));
	}

	const UPCGDynamicMeshData* ToDynMesh(const UPCGGeometryCollectionData* Collection)
	{
		UPCGGeometryCollectionToDynMeshSettings* Settings = NewObject<UPCGGeometryCollectionToDynMeshSettings>();
		return FirstOutput<UPCGDynamicMeshData>(
			Run(Settings, {{PCGGeometryCollectionToDynMeshConstants::CollectionInputPin, Collection}}));
	}

	const UE::Geometry::FDynamicMesh3& Mesh(const UPCGDynamicMeshData* Data)
	{
		return *Data->GetDynamicMesh()->GetMeshPtr();
	}

	int32 CountGeometryBones(const UPCGGeometryCollectionData* Data)
	{
		TArray<int32> Bones;
		PCGUtilsGeometryCollectionHelpers::GatherGeometryBearingBones(Data->GetCollection(), Bones);
		return Bones.Num();
	}

	const UE::Geometry::FDynamicMeshPolygroupAttribute* FindLayer(
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

/**
 * The V1 acceptance criterion: the whole round trip, ending in a mesh that is genuinely hollowed out rather
 * than merely re-emitted.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPCGUtilsFractureRoundTripTest,
	"PCGUtils.Fracture.RoundTrip.CarvesCavity",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPCGUtilsFractureRoundTripTest::RunTest(const FString&)
{
	using namespace PCGUtilsFractureTests;

	const UPCGDynamicMeshData* SourceMesh = Box();
	const int32 SourceTriangles = Mesh(SourceMesh).TriangleCount();

	const UPCGGeometryCollectionData* Collection = ToCollection(SourceMesh);
	if (!TestNotNull(TEXT("DynMesh To GC produced a collection"), Collection))
	{
		return false;
	}
	// One geometry bone under one explicit cluster root.
	TestEqual(TEXT("One geometry bone before fracture"), CountGeometryBones(Collection), 1);
	TestEqual(TEXT("Root plus geometry bone"), Collection->NumTransforms(), 2);
	TestEqual(TEXT("Fresh lineage starts at revision 0"), Collection->GetRevision(), 0);

	const UPCGGeometryCollectionData* Fractured = Fracture(Collection, SiteGrid(4));
	if (!TestNotNull(TEXT("Fracture GC produced a collection"), Fractured))
	{
		return false;
	}
	const int32 FracturedBones = CountGeometryBones(Fractured);
	TestTrue(TEXT("Fracture produced many pieces"), FracturedBones > 8);
	TestEqual(TEXT("Fracture bumps revision"), Fractured->GetRevision(), 1);
	TestEqual(TEXT("Fracture keeps the lineage"), Fractured->GetCollectionId(), Collection->GetCollectionId());
	TestNotEqual(TEXT("Fracture mints a new state"), Fractured->GetStateId(), Collection->GetStateId());

	const UPCGBasePointData* BonePoints = BonesToPoints(Fractured);
	if (!TestNotNull(TEXT("GC Bones To Points produced points"), BonePoints))
	{
		return false;
	}
	TestEqual(TEXT("One point per fracture piece"), BonePoints->GetNumPoints(), FracturedBones);

	// Isolate a localised group of chunks in one corner - the stand-in for PCG/PCGEx spatial filtering.
	const FBox CornerRegion(FVector(-BoxSize * 0.5), FVector(0.0));
	const UPCGBasePointData* FilteredPoints = FilterPointsInBox(BonePoints, CornerRegion);
	const int32 NumSelected = FilteredPoints->GetNumPoints();
	if (!TestTrue(TEXT("Corner filter selected some but not all pieces"),
		NumSelected > 0 && NumSelected < FracturedBones))
	{
		return false;
	}

	const UPCGUtilsGeometryCollectionSelectionFactoryData* Selection = SelectionFromPoints(FilteredPoints);
	if (!TestNotNull(TEXT("Select Bones From Points produced a selection"), Selection))
	{
		return false;
	}

	const UPCGGeometryCollectionData* Pruned = Prune(Fractured, Selection);
	if (!TestNotNull(TEXT("Prune GC produced a collection"), Pruned))
	{
		return false;
	}
	const int32 RemainingBones = CountGeometryBones(Pruned);
	TestEqual(TEXT("Pruned exactly the selected pieces"), RemainingBones, FracturedBones - NumSelected);
	TestEqual(TEXT("Prune bumps revision"), Pruned->GetRevision(), 2);

	const UPCGDynamicMeshData* Result = ToDynMesh(Pruned);
	if (!TestNotNull(TEXT("GC To DynMesh produced a mesh"), Result))
	{
		return false;
	}
	const UE::Geometry::FDynamicMesh3& ResultMesh = Mesh(Result);
	TestTrue(TEXT("Result has geometry"), ResultMesh.TriangleCount() > 0);

	// The actual proof this is a modelling operation: fracturing and deleting chunks leaves far more surface
	// than the original box had, because the cavity walls are real interior geometry.
	TestTrue(TEXT("Result is a carved solid, not the original box"),
		ResultMesh.TriangleCount() > SourceTriangles);

	// Regression guard. Unreal's fracture entry point cannot disable noise, and its default point spacing of
	// 1cm remeshes every cut face - which silently turned this 12-triangle box into ~550k triangles before
	// Voronoi Fracture started passing a no-subdivision spacing. Planar Voronoi cells are convex and cheap;
	// anything near this bound means the noise path has been re-enabled by accident.
	TestTrue(FString::Printf(TEXT("Planar cuts stay cheap (%d triangles)"), ResultMesh.TriangleCount()),
		ResultMesh.TriangleCount() < 5000);

	// The result must stay in the source mesh's own local space - no recentring anywhere in the round trip.
	const FBox SourceBounds(FVector(-BoxSize * 0.5), FVector(BoxSize * 0.5));
	const UE::Geometry::FAxisAlignedBox3d ResultBounds = ResultMesh.GetBounds();
	TestTrue(TEXT("Result stays inside the source bounds"),
		SourceBounds.ExpandBy(1.0).IsInsideOrOn(FVector(ResultBounds.Min))
		&& SourceBounds.ExpandBy(1.0).IsInsideOrOn(FVector(ResultBounds.Max)));

	// Both PolyGroup layers must survive the combine, or the result cannot re-enter the DynMesh ecosystem.
	TestNotNull(TEXT("Per-bone PolyGroup layer present"),
		FindLayer(ResultMesh, PCGGeometryCollectionToDynMeshConstants::DefaultBonePolygroupLayer));
	TestNotNull(TEXT("Internal-face PolyGroup layer present"),
		FindLayer(ResultMesh, UE::Geometry::FGeometryCollectionToDynamicMeshes::InternalFacePolyGroupName()));

	return true;
}






/**
 * The cluster output must satisfy PCGEx's contract exactly, since nothing links the two plugins together and
 * a silent mismatch would only show up as PCGEx quietly refusing to see a cluster.
 *
 * Decoding here deliberately mirrors PCGEx's own reader (BuildEndpointsLookup / BuildIndexedEdges) rather than
 * re-using our writer's helpers, so the test would catch a change on either side of the convention.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPCGUtilsFractureClusterOutputTest,
	"PCGUtils.Fracture.Cluster.MatchesPCGExContract",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPCGUtilsFractureClusterOutputTest::RunTest(const FString&)
{
	using namespace PCGUtilsFractureTests;

	const UPCGGeometryCollectionData* Fractured = Fracture(ToCollection(Box()), SiteGrid(3));
	if (!TestNotNull(TEXT("Fractured"), Fractured))
	{
		return false;
	}

	UPCGGeometryCollectionBonesToPointsSettings* Settings =
		NewObject<UPCGGeometryCollectionBonesToPointsSettings>();
	Settings->bOutputToWorldSpace = false;
	Settings->bOutputCluster = true;

	const TArray<FPCGTaggedData> Outputs =
		Run(Settings, {{PCGGeometryCollectionBonesToPointsConstants::CollectionInputPin, Fractured}});

	const UPCGBasePointData* VtxData = nullptr;
	const UPCGBasePointData* EdgesData = nullptr;
	FString VtxPairTag;
	FString EdgesPairTag;
	for (const FPCGTaggedData& Tagged : Outputs)
	{
		const UPCGBasePointData* Points = Cast<const UPCGBasePointData>(Tagged.Data);
		if (!Points) { continue; }

		for (const FString& Tag : Tagged.Tags)
		{
			if (Tag.StartsWith(PCGUtilsClusterInterop::ClusterPairTagKey + TEXT(":")))
			{
				(Tagged.Pin == PCGGeometryCollectionBonesToPointsConstants::EdgesOutputPin
					? EdgesPairTag : VtxPairTag) = Tag;
			}
		}

		if (Tagged.Pin == PCGGeometryCollectionBonesToPointsConstants::EdgesOutputPin)
		{
			EdgesData = Points;
			TestTrue(TEXT("Edges data carries the PCGEx edges tag"),
				Tagged.Tags.Contains(PCGUtilsClusterInterop::EdgesTag));
		}
		else
		{
			VtxData = Points;
			TestTrue(TEXT("Vtx data carries the PCGEx vtx tag"),
				Tagged.Tags.Contains(PCGUtilsClusterInterop::VtxTag));
		}
	}

	if (!TestNotNull(TEXT("Vtx half emitted"), VtxData) || !TestNotNull(TEXT("Edges half emitted"), EdgesData))
	{
		return false;
	}

	// The pair tag is how PCGEx knows the two halves belong together.
	TestFalse(TEXT("A pair tag was written"), VtxPairTag.IsEmpty());
	TestEqual(TEXT("Both halves share one pair id"), VtxPairTag, EdgesPairTag);

	// Both halves must stay ordinary point data. A cluster is point data carrying tags and attributes; a
	// bespoke subtype would narrow the pins and stop other point nodes accepting them.
	TestTrue(TEXT("Vtx half is plain point array data"), VtxData->IsA<UPCGPointArrayData>());
	TestTrue(TEXT("Edges half is plain point array data"), EdgesData->IsA<UPCGPointArrayData>());
	TestEqual(TEXT("Vtx half is not a bespoke subtype"),
		VtxData->GetClass(), UPCGPointArrayData::StaticClass());
	TestEqual(TEXT("Edges half is not a bespoke subtype"),
		EdgesData->GetClass(), UPCGPointArrayData::StaticClass());

	// --- Decode exactly as PCGEx does -----------------------------------------------------------------
	const FPCGMetadataDomain* VtxDomain =
		VtxData->ConstMetadata()->GetConstMetadataDomain(PCGMetadataDomainID::Elements);
	const FPCGMetadataAttribute<int64>* VtxAttr =
		VtxDomain->GetConstTypedAttribute<int64>(PCGUtilsClusterInterop::VtxDataAttribute);
	const FPCGMetadataAttribute<int32>* BoneAttr =
		VtxDomain->GetConstTypedAttribute<int32>(PCGUtilsGeometryCollectionIdentity::BoneIndexAttribute);
	if (!TestNotNull(TEXT("PCGEx/VData written"), VtxAttr)
		|| !TestNotNull(TEXT("GC_BoneIndex written"), BoneAttr))
	{
		return false;
	}

	// BuildEndpointsLookup: VtxId -> point index, plus the expected degree.
	TMap<uint32, int32> EndpointsLookup;
	TArray<int32> ExpectedDegree;
	const auto VtxEntries = VtxData->GetConstMetadataEntryValueRange();
	for (int32 i = 0; i < VtxEntries.Num(); ++i)
	{
		const uint64 Packed = static_cast<uint64>(VtxAttr->GetValueFromItemKey(VtxEntries[i]));
		const uint32 VtxId = static_cast<uint32>(Packed >> 32);
		const uint32 Degree = static_cast<uint32>(Packed);
		EndpointsLookup.Add(VtxId, i);
		ExpectedDegree.Add(static_cast<int32>(Degree));

		// The whole reason a selection survives a PCGEx round trip: vtx id IS the bone index.
		TestEqual(TEXT("Vtx id equals GC_BoneIndex"),
			static_cast<int32>(VtxId), BoneAttr->GetValueFromItemKey(VtxEntries[i]));
	}
	TestEqual(TEXT("Every vertex has a unique id"), EndpointsLookup.Num(), VtxData->GetNumPoints());

	// BuildIndexedEdges: both endpoints must resolve through that lookup.
	const FPCGMetadataAttribute<int64>* EdgeAttr =
		EdgesData->ConstMetadata()->GetConstMetadataDomain(PCGMetadataDomainID::Elements)
			->GetConstTypedAttribute<int64>(PCGUtilsClusterInterop::EdgeDataAttribute);
	if (!TestNotNull(TEXT("PCGEx/EData written"), EdgeAttr))
	{
		return false;
	}

	TArray<int32> ActualDegree;
	ActualDegree.Init(0, VtxData->GetNumPoints());
	int32 NumUnresolved = 0;
	const auto EdgeEntries = EdgesData->GetConstMetadataEntryValueRange();
	for (int32 i = 0; i < EdgeEntries.Num(); ++i)
	{
		const uint64 Packed = static_cast<uint64>(EdgeAttr->GetValueFromItemKey(EdgeEntries[i]));
		const int32* StartPoint = EndpointsLookup.Find(static_cast<uint32>(Packed >> 32));
		const int32* EndPoint = EndpointsLookup.Find(static_cast<uint32>(Packed));
		if (!StartPoint || !EndPoint)
		{
			++NumUnresolved;
			continue;
		}
		TestNotEqual(TEXT("An edge does not connect a vertex to itself"), *StartPoint, *EndPoint);
		ActualDegree[*StartPoint]++;
		ActualDegree[*EndPoint]++;
	}

	TestEqual(TEXT("Every edge endpoint resolves to a vertex"), NumUnresolved, 0);
	TestTrue(TEXT("A fractured solid produces adjacency"), EdgeEntries.Num() > 0);

	// The degree PCGEx reads out of the vtx attribute has to match the edges actually present, or its
	// adjacency allocation is wrong.
	bool bDegreesMatch = true;
	for (int32 i = 0; i < ActualDegree.Num(); ++i)
	{
		bDegreesMatch &= (ActualDegree[i] == ExpectedDegree[i]);
	}
	TestTrue(TEXT("Declared vertex degree matches the emitted edges"), bDegreesMatch);

	// Every piece of a solid fractured into a 3x3x3 lattice touches something.
	int32 NumIsolated = 0;
	for (const int32 Degree : ActualDegree) { NumIsolated += (Degree == 0) ? 1 : 0; }
	TestEqual(TEXT("No fracture piece is isolated"), NumIsolated, 0);

	UE_LOG(LogPCGUtilsFracture, Log, TEXT("Cluster: %d vtx, %d edges"),
		VtxData->GetNumPoints(), EdgesData->GetNumPoints());

	// Without cluster output, neither the marking nor the edges pin should appear.
	{
		UPCGGeometryCollectionBonesToPointsSettings* Plain =
			NewObject<UPCGGeometryCollectionBonesToPointsSettings>();
		Plain->bOutputToWorldSpace = false;
		const TArray<FPCGTaggedData> PlainOutputs =
			Run(Plain, {{PCGGeometryCollectionBonesToPointsConstants::CollectionInputPin, Fractured}});
		TestEqual(TEXT("Only the points pin is emitted by default"), PlainOutputs.Num(), 1);
		if (PlainOutputs.Num() == 1)
		{
			TestFalse(TEXT("Points are not marked as cluster vtx by default"),
				PlainOutputs[0].Tags.Contains(PCGUtilsClusterInterop::VtxTag));
		}
	}

	return true;
}

/**
 * The random-damage workflow: pick exterior pieces only, prune them, and confirm the result actually changed
 * on the outside. The failure this guards against is subtle - pruning a buried piece "succeeds", reports bones
 * removed, and produces no visible damage at all.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPCGUtilsFractureSurfaceAttributesTest,
	"PCGUtils.Fracture.Bones.SurfaceAttributesDriveDamage",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPCGUtilsFractureSurfaceAttributesTest::RunTest(const FString&)
{
	using namespace PCGUtilsFractureTests;
	using namespace PCGUtilsGeometryCollectionIdentity;

	const UPCGGeometryCollectionData* Fractured = Fracture(ToCollection(Box()), SiteGrid(4));
	if (!TestNotNull(TEXT("Fractured"), Fractured))
	{
		return false;
	}

	const UPCGBasePointData* Points = BonesToPoints(Fractured, /*bWithSurfaceAttributes=*/true);
	if (!TestNotNull(TEXT("Bone points"), Points))
	{
		return false;
	}

	const FPCGMetadataDomain* Domain =
		Points->ConstMetadata()->GetConstMetadataDomain(PCGMetadataDomainID::Elements);
	const FPCGMetadataAttribute<bool>* IsExterior =
		Domain->GetConstTypedAttribute<bool>(IsExteriorAttribute);
	const FPCGMetadataAttribute<double>* ExteriorArea =
		Domain->GetConstTypedAttribute<double>(ExteriorAreaAttribute);
	const FPCGMetadataAttribute<double>* InteriorArea =
		Domain->GetConstTypedAttribute<double>(InteriorAreaAttribute);
	const FPCGMetadataAttribute<double>* ExposureRatio =
		Domain->GetConstTypedAttribute<double>(ExposureRatioAttribute);
	const FPCGMetadataAttribute<int32>* ExteriorFaces =
		Domain->GetConstTypedAttribute<int32>(ExteriorFaceCountAttribute);

	if (!TestNotNull(TEXT("GC_IsExterior written"), IsExterior)
		|| !TestNotNull(TEXT("GC_ExteriorArea written"), ExteriorArea)
		|| !TestNotNull(TEXT("GC_InteriorArea written"), InteriorArea)
		|| !TestNotNull(TEXT("GC_ExposureRatio written"), ExposureRatio)
		|| !TestNotNull(TEXT("GC_ExteriorFaceCount written"), ExteriorFaces))
	{
		return false;
	}

	// Split the pieces the way a graph would: buried versus surface-facing.
	TArray<int32> BuriedIndices;
	TArray<int32> ExteriorIndices;
	const auto Entries = Points->GetConstMetadataEntryValueRange();
	for (int32 Index = 0; Index < Entries.Num(); ++Index)
	{
		const int64 Entry = Entries[Index];
		const bool bExterior = IsExterior->GetValueFromItemKey(Entry);
		(bExterior ? ExteriorIndices : BuriedIndices).Add(Index);

		const double Exposure = ExposureRatio->GetValueFromItemKey(Entry);
		TestTrue(TEXT("Exposure ratio is a fraction"), Exposure >= 0.0 && Exposure <= 1.0);

		if (bExterior)
		{
			TestTrue(TEXT("Exterior piece has exterior area"),
				ExteriorArea->GetValueFromItemKey(Entry) > 0.0);
			TestTrue(TEXT("Exterior piece has exterior faces"),
				ExteriorFaces->GetValueFromItemKey(Entry) > 0);
			TestTrue(TEXT("Exterior piece has non-zero exposure"), Exposure > 0.0);
		}
		else
		{
			// This is the case the attribute exists to exclude.
			TestEqual(TEXT("Buried piece has zero exposure"), Exposure, 0.0);
			TestEqual(TEXT("Buried piece has zero exterior area"),
				ExteriorArea->GetValueFromItemKey(Entry), 0.0);
			TestTrue(TEXT("Buried piece is all interior surface"),
				InteriorArea->GetValueFromItemKey(Entry) > 0.0);
		}
	}

	TestTrue(TEXT("Both classes present in a 4x4x4 fracture"),
		ExteriorIndices.Num() > 0 && BuriedIndices.Num() > 0);

	// Optional attributes must not appear unless asked for: a single bulk flag writing everything is exactly
	// the UX problem the per-attribute toggles exist to avoid.
	{
		const UPCGBasePointData* Default = BonesToPoints(Fractured);
		const FPCGMetadataDomain* DefaultDomain =
			Default->ConstMetadata()->GetConstMetadataDomain(PCGMetadataDomainID::Elements);
		TestNull(TEXT("GC_IsExterior is not written by default"),
			DefaultDomain->GetConstTypedAttribute<bool>(IsExteriorAttribute));
		TestNull(TEXT("GC_BoundsVolume is not written by default"),
			DefaultDomain->GetConstTypedAttribute<double>(BoundsVolumeAttribute));
		// Identity is the contract with Select Bones From Points, so it is always present.
		TestNotNull(TEXT("GC_BoneIndex is always written"),
			DefaultDomain->GetConstTypedAttribute<int32>(BoneIndexAttribute));
		TestNotNull(TEXT("GC_SourceStateId is always written"),
			DefaultDomain->GetConstTypedAttribute<int64>(SourceStateIdAttribute));
	}

	// A renamed attribute must actually come out under the new name.
	{
		UPCGGeometryCollectionBonesToPointsSettings* Renamed = NewObject<UPCGGeometryCollectionBonesToPointsSettings>();
		Renamed->bOutputToWorldSpace = false;
		Renamed->bOutputIsExterior = true;
		Renamed->IsExteriorAttributeName = TEXT("MyExteriorFlag");
		const UPCGBasePointData* Custom = FirstOutput<UPCGBasePointData>(
			Run(Renamed, {{PCGGeometryCollectionBonesToPointsConstants::CollectionInputPin, Fractured}}));
		if (TestNotNull(TEXT("Renamed run produced points"), Custom))
		{
			const FPCGMetadataDomain* CustomDomain =
				Custom->ConstMetadata()->GetConstMetadataDomain(PCGMetadataDomainID::Elements);
			TestNotNull(TEXT("Attribute uses the user-supplied name"),
				CustomDomain->GetConstTypedAttribute<bool>(FName(TEXT("MyExteriorFlag"))));
			TestNull(TEXT("Attribute no longer uses the default name"),
				CustomDomain->GetConstTypedAttribute<bool>(IsExteriorAttribute));
		}
	}

	// Prune two exterior pieces, as a damage graph would, and confirm the silhouette really changed.
	const FBox BeforeBounds = PCGUtilsGeometryCollectionHelpers::ComputeCollectionBounds(Fractured->GetCollection());
	double BeforeExteriorArea = 0.0;
	{
		TArray<int32> Bones;
		PCGUtilsGeometryCollectionHelpers::GatherGeometryBearingBones(Fractured->GetCollection(), Bones);
		TArray<FTransform> Globals;
		PCGUtilsGeometryCollectionHelpers::ComputeGlobalTransforms(Fractured->GetCollection(), Globals);
		for (const int32 Bone : Bones)
		{
			BeforeExteriorArea += PCGUtilsGeometryCollectionHelpers::GetBoneSurfaceInfo(
				Fractured->GetCollection(), Bone, Globals[Bone]).ExteriorArea;
		}
	}

	ExteriorIndices.SetNum(2);
	const UPCGUtilsGeometryCollectionSelectionFactoryData* Selection =
		SelectionFromPoints(FilterPointsByIndex(Points, ExteriorIndices));
	const UPCGGeometryCollectionData* Damaged = Prune(Fractured, Selection);
	if (!TestNotNull(TEXT("Pruned two exterior pieces"), Damaged))
	{
		return false;
	}

	double AfterExteriorArea = 0.0;
	{
		TArray<int32> Bones;
		PCGUtilsGeometryCollectionHelpers::GatherGeometryBearingBones(Damaged->GetCollection(), Bones);
		TArray<FTransform> Globals;
		PCGUtilsGeometryCollectionHelpers::ComputeGlobalTransforms(Damaged->GetCollection(), Globals);
		for (const int32 Bone : Bones)
		{
			AfterExteriorArea += PCGUtilsGeometryCollectionHelpers::GetBoneSurfaceInfo(
				Damaged->GetCollection(), Bone, Globals[Bone]).ExteriorArea;
		}
	}

	// Removing surface-facing pieces must remove original outer surface. Pruning buried pieces would not.
	TestTrue(TEXT("Damaging exterior pieces removes original outer surface"),
		AfterExteriorArea < BeforeExteriorArea);
	TestTrue(TEXT("The solid keeps its overall footprint"),
		BeforeBounds.IsInsideOrOn(
			PCGUtilsGeometryCollectionHelpers::ComputeCollectionBounds(Damaged->GetCollection()).GetCenter()));

	return true;
}

/**
 * Interior vs exterior bone classification, which is what makes "randomly damage a mesh" safe: pruning a
 * buried bone changes no silhouette and leaves invisible interior surface behind.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPCGUtilsFractureBoneSurfaceTest,
	"PCGUtils.Fracture.Bones.ExteriorVsInterior",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPCGUtilsFractureBoneSurfaceTest::RunTest(const FString&)
{
	using namespace PCGUtilsFractureTests;

	// Before fracturing, every face came from the source mesh, so the single bone is entirely exterior.
	const UPCGGeometryCollectionData* Unfractured = ToCollection(Box());
	if (!TestNotNull(TEXT("Collection built"), Unfractured))
	{
		return false;
	}
	{
		TArray<int32> Bones;
		PCGUtilsGeometryCollectionHelpers::GatherGeometryBearingBones(Unfractured->GetCollection(), Bones);
		if (TestEqual(TEXT("One bone before fracture"), Bones.Num(), 1))
		{
			const auto Info = PCGUtilsGeometryCollectionHelpers::GetBoneSurfaceInfo(Unfractured->GetCollection(), Bones[0]);
			TestTrue(TEXT("Unfractured bone is exterior"), Info.IsExterior());
			TestEqual(TEXT("Unfractured bone has no interior faces"), Info.InteriorFaceCount, 0);
			TestTrue(TEXT("Unfractured bone has exterior area"), Info.ExteriorArea > 0.0);
		}
	}

	// A 4x4x4 lattice guarantees a shell of surface chunks around a genuinely buried core.
	const UPCGGeometryCollectionData* Fractured = Fracture(Unfractured, SiteGrid(4));
	if (!TestNotNull(TEXT("Fractured"), Fractured))
	{
		return false;
	}

	TArray<int32> Bones;
	PCGUtilsGeometryCollectionHelpers::GatherGeometryBearingBones(Fractured->GetCollection(), Bones);

	int32 NumExterior = 0;
	int32 NumInterior = 0;
	for (const int32 BoneIndex : Bones)
	{
		const auto Info = PCGUtilsGeometryCollectionHelpers::GetBoneSurfaceInfo(Fractured->GetCollection(), BoneIndex);
		if (Info.IsExterior())
		{
			++NumExterior;
			TestTrue(TEXT("An exterior bone has exterior area"), Info.ExteriorArea > 0.0);
		}
		else
		{
			++NumInterior;
			TestEqual(TEXT("A buried bone has zero exterior area"), Info.ExteriorArea, 0.0);
			TestTrue(TEXT("A buried bone is still made of interior faces"), Info.InteriorFaceCount > 0);
		}
	}

	UE_LOG(LogPCGUtilsFracture, Log,
		TEXT("Bone surface classification: %d exterior, %d buried, of %d pieces"),
		NumExterior, NumInterior, Bones.Num());

	// A 4x4x4 subdivision of a cube has a 2x2x2 fully-buried core and a 56-chunk shell around it.
	TestTrue(TEXT("Some chunks reach the original surface"), NumExterior > 0);
	TestTrue(TEXT("Some chunks are fully buried"), NumInterior > 0);
	TestEqual(TEXT("Every piece is classified"), NumExterior + NumInterior, Bones.Num());

	return true;
}

/**
 * Uniform Voronoi mirrors Fracture Mode's Uniform button: it scatters its own sites through the bounds of
 * whatever it is fracturing, so a working graph is just DynMesh -> GC -> Fracture with nothing else attached.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPCGUtilsUniformVoronoiFractureTest,
	"PCGUtils.Fracture.Voronoi.UniformNeedsNoPointInput",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPCGUtilsUniformVoronoiFractureTest::RunTest(const FString&)
{
	using namespace PCGUtilsFractureTests;

	const UPCGGeometryCollectionData* Collection = ToCollection(Box());
	if (!TestNotNull(TEXT("Collection built"), Collection))
	{
		return false;
	}
	TestEqual(TEXT("One geometry bone before fracture"), CountGeometryBones(Collection), 1);

	// The whole point: no Sites pin, no scatter, no coordinate space to get wrong.
	const UPCGGeometryCollectionData* Fractured = UniformFracture(Collection, 24, 24);
	if (!TestNotNull(TEXT("Uniform Voronoi fractured with no point input"), Fractured))
	{
		return false;
	}

	// Min == Max asks for an exact count. Sites can coincide or fall in dead space, so allow some slack, but
	// the result must clearly reflect the requested count rather than a default.
	const int32 Pieces = CountGeometryBones(Fractured);
	TestTrue(FString::Printf(TEXT("Requested 24 pieces, produced %d"), Pieces),
		Pieces > 12 && Pieces <= 24);
	TestEqual(TEXT("Fracture bumps revision"), Fractured->GetRevision(), 1);

	// Same guard as the points-driven path: planar cuts must not tessellate.
	const UPCGDynamicMeshData* Result = ToDynMesh(Fractured);
	if (!TestNotNull(TEXT("Converted back to DynMesh"), Result))
	{
		return false;
	}
	TestTrue(FString::Printf(TEXT("Planar cuts stay cheap (%d triangles)"), Mesh(Result).TriangleCount()),
		Mesh(Result).TriangleCount() < 5000);

	// Asking for more pieces must actually produce more.
	const UPCGGeometryCollectionData* Coarse = UniformFracture(Collection, 5, 5);
	const UPCGGeometryCollectionData* Fine = UniformFracture(Collection, 60, 60);
	if (TestNotNull(TEXT("Coarse fracture"), Coarse) && TestNotNull(TEXT("Fine fracture"), Fine))
	{
		TestTrue(TEXT("More sites yields more pieces"),
			CountGeometryBones(Fine) > CountGeometryBones(Coarse));
	}

	return true;
}

/**
 * The failures that are genuinely possible for a user to hit, each of which the fracture backend reports as a
 * bare INDEX_NONE. The point of these is that the node must distinguish them, not merely fail.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPCGUtilsFractureDegenerateSitesTest,
	"PCGUtils.Fracture.Voronoi.DegenerateSites",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPCGUtilsFractureDegenerateSitesTest::RunTest(const FString&)
{
	using namespace PCGUtilsFractureTests;

	const UPCGGeometryCollectionData* Collection = ToCollection(Box());
	if (!TestNotNull(TEXT("Collection built"), Collection))
	{
		return false;
	}

	// A Voronoi diagram of one site has no dividing planes, so it can never cut anything.
	AddExpectedErrorPlain(TEXT("needs at least 2 sites"), EAutomationExpectedErrorFlags::Contains, -1);
	AddExpectedErrorPlain(TEXT("applied no fracture operations successfully"),
		EAutomationExpectedErrorFlags::Contains, -1);
	TestFalse(TEXT("A single site cannot fracture"),
		TryFracture(Collection, SitesAt({FVector::ZeroVector})));

	// Sites far outside the geometry leave every triangle in one cell.
	AddExpectedErrorPlain(TEXT("inside the geometry, so there is nothing to cut"),
		EAutomationExpectedErrorFlags::Contains, -1);
	TestFalse(TEXT("Sites entirely outside the geometry cannot fracture"),
		TryFracture(Collection, SitesAt({
			FVector(10000.0, 0.0, 0.0), FVector(10100.0, 0.0, 0.0), FVector(10200.0, 0.0, 0.0)})));

	// The smallest input that genuinely should work: two sites straddling the box centre.
	TestTrue(TEXT("Two sites inside the geometry fracture it"),
		TryFracture(Collection, SitesAt({FVector(-25.0, 0.0, 0.0), FVector(25.0, 0.0, 0.0)})));

	// Fracture GC must also refuse a collection the backend would silently reject, naming the attribute.
	{
		TSharedRef<FGeometryCollection> Stripped = Collection->CreateMutableCopy();
		Stripped->RemoveAttribute(
			FGeometryCollection::BoundingBoxAttribute, FGeometryCollection::GeometryGroup);
		TArray<FString> Missing;
		TestFalse(TEXT("Stripped collection is detected as unfracturable"),
			PCGUtilsGeometryCollectionHelpers::ValidateFractureRequirements(*Stripped, Missing));
		TestTrue(TEXT("The missing attribute is named"),
			Missing.Num() == 1 && Missing[0].Contains(TEXT("BoundingBox")));
	}

	return true;
}

/** A selection authored against one collection state must never be applied to another. */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPCGUtilsFractureStaleSelectionTest,
	"PCGUtils.Fracture.Selection.RejectsStaleState",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPCGUtilsFractureStaleSelectionTest::RunTest(const FString&)
{
	using namespace PCGUtilsFractureTests;

	const UPCGGeometryCollectionData* Collection = ToCollection(Box());
	const UPCGGeometryCollectionData* FracturedOnce = Fracture(Collection, SiteGrid(3));
	if (!TestNotNull(TEXT("First fracture succeeded"), FracturedOnce))
	{
		return false;
	}

	// Points authored against revision 1 ...
	const UPCGBasePointData* StalePoints = BonesToPoints(FracturedOnce);
	const UPCGUtilsGeometryCollectionSelectionFactoryData* StaleSelection = SelectionFromPoints(StalePoints);
	if (!TestNotNull(TEXT("Selection authored"), StaleSelection))
	{
		return false;
	}

	// ... applied to revision 2, whose bone indices mean something entirely different.
	const UPCGGeometryCollectionData* FracturedTwice = Fracture(FracturedOnce, SiteGrid(2));
	if (!TestNotNull(TEXT("Second fracture succeeded"), FracturedTwice))
	{
		return false;
	}
	TestNotEqual(TEXT("Second fracture is a distinct state"),
		FracturedTwice->GetStateId(), FracturedOnce->GetStateId());

	// Occurrences < 0: tolerate the graph error however it surfaces. The assertion under test is the return
	// value below, not the exact logging path (this runs with no PCG context).
	AddExpectedErrorPlain(TEXT("authored against a different collection state"),
		EAutomationExpectedErrorFlags::Contains, -1);

	FDataflowTransformSelection Resolved;
	const FPCGUtilsGeometryCollectionSelectionEvaluationContext EvaluationContext(
		FracturedTwice, FracturedTwice->GetCollection());
	const bool bEvaluated = StaleSelection->Evaluate(EvaluationContext, nullptr, Resolved);

	TestFalse(TEXT("Stale selection is rejected rather than silently applied"), bEvaluated);
	TestFalse(TEXT("No bones were selected from stale data"), Resolved.AnySelected());
	return true;
}

/**
 * The round trip must not depend on the source sitting at the origin. A translated box must produce the same
 * topology, just offset - the failure this guards against is silent recentring.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPCGUtilsFractureTransformSpaceTest,
	"PCGUtils.Fracture.RoundTrip.PreservesSourceSpace",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPCGUtilsFractureTransformSpaceTest::RunTest(const FString&)
{
	using namespace PCGUtilsFractureTests;

	const FVector Offset(500.0, -250.0, 125.0);

	// Same box and same sites, both shifted by Offset. The result should be the origin case, translated.
	const UPCGGeometryCollectionData* Centered =
		Fracture(ToCollection(Box(BoxSize, FVector::ZeroVector)), SiteGrid(3));

	UPCGPointArrayData* OffsetSites = SiteGrid(3);
	{
		auto Transforms = OffsetSites->GetTransformValueRange();
		for (int32 Index = 0; Index < Transforms.Num(); ++Index)
		{
			Transforms[Index] = FTransform(Transforms[Index].GetLocation() + Offset);
		}
	}
	const UPCGGeometryCollectionData* Offsetted =
		Fracture(ToCollection(Box(BoxSize, Offset)), OffsetSites);

	if (!TestNotNull(TEXT("Centered fracture succeeded"), Centered)
		|| !TestNotNull(TEXT("Offset fracture succeeded"), Offsetted))
	{
		return false;
	}

	TestEqual(TEXT("Offsetting the source changes nothing structurally"),
		CountGeometryBones(Offsetted), CountGeometryBones(Centered));

	const UPCGDynamicMeshData* Result = ToDynMesh(Offsetted);
	if (!TestNotNull(TEXT("Offset round trip produced a mesh"), Result))
	{
		return false;
	}

	// The decisive check: the result is still centred on Offset, not dragged back to the origin.
	const UE::Geometry::FAxisAlignedBox3d Bounds = Mesh(Result).GetBounds();
	TestTrue(TEXT("Result is not recentred on the origin"),
		FVector(Bounds.Center()).Equals(Offset, 1.0));
	return true;
}

#endif // WITH_AUTOMATION_TESTS
