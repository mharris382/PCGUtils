// Copyright Max Harris

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "Data/PCGUtilsGeometryCollectionPieceMesh.h"
#include "FunctionLibraries/PCGUtilsGeometryCollectionSurface.h"
#include "Metadata/PCGMetadataDomain.h"
#include "Tests/PCGUtilsFractureTestHelpers.h"

namespace PCGUtilsFractureConversionTests
{
	using namespace PCGUtilsFractureTests;

	/** Runs GC To DynMesh with the settings a test wants and returns every mesh it emitted. */
	TArray<const UPCGDynamicMeshData*> Convert(
		const UPCGGeometryCollectionData* Collection,
		TFunctionRef<void(UPCGGeometryCollectionToDynMeshSettings&)> Configure)
	{
		UPCGGeometryCollectionToDynMeshSettings* Settings =
			NewObject<UPCGGeometryCollectionToDynMeshSettings>();
		Configure(*Settings);

		TArray<const UPCGDynamicMeshData*> Meshes;
		for (const FPCGTaggedData& Tagged :
			Run(Settings, {{PCGGeometryCollectionToDynMeshConstants::CollectionInputPin, Collection}}))
		{
			if (const UPCGDynamicMeshData* MeshData = Cast<const UPCGDynamicMeshData>(Tagged.Data))
			{
				Meshes.Add(MeshData);
			}
		}
		return Meshes;
	}

	/** Reads one data-domain attribute from a converted piece. */
	template<typename ValueType>
	bool TryReadDataAttribute(const UPCGDynamicMeshData* InData, FName InName, ValueType& OutValue)
	{
		const UPCGMetadata* Metadata = InData ? InData->ConstMetadata() : nullptr;
		const FPCGMetadataDomain* DataDomain =
			Metadata ? Metadata->GetConstMetadataDomain(PCGMetadataDomainID::Data) : nullptr;
		const FPCGMetadataAttribute<ValueType>* Attribute =
			DataDomain ? DataDomain->GetConstTypedAttribute<ValueType>(InName) : nullptr;
		if (!Attribute)
		{
			return false;
		}
		OutValue = Attribute->GetValueFromItemKey(PCGFirstEntryKey);
		return true;
	}
}

/**
 * Combined output is the round trip the module was built for, and migrating it onto the shared view layer must
 * not have changed what it emits. The contract is the two PolyGroup layers and a mesh with a real cavity.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPCGUtilsFractureCombinedConversionTest,
	"PCGUtils.Fracture.Conversion.CombinedContract",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPCGUtilsFractureCombinedConversionTest::RunTest(const FString&)
{
	using namespace PCGUtilsFractureTests;
	using namespace PCGUtilsFractureConversionTests;

	const UPCGGeometryCollectionData* Fractured = Fracture(ToCollection(Box()), SiteGrid(3));
	if (!TestNotNull(TEXT("Fractured"), Fractured))
	{
		return false;
	}

	const TArray<const UPCGDynamicMeshData*> Combined =
		Convert(Fractured, [](UPCGGeometryCollectionToDynMeshSettings&) {});
	if (!TestEqual(TEXT("Combined emits exactly one mesh"), Combined.Num(), 1))
	{
		return false;
	}

	const UE::Geometry::FDynamicMesh3& CombinedMesh = Mesh(Combined[0]);
	TestTrue(TEXT("The combined mesh has geometry"), CombinedMesh.TriangleCount() > 0);

	// Both layers are the bridge back into the DynMesh ecosystem; dropping either would break Select by
	// PolyGroup without failing anything.
	const UE::Geometry::FDynamicMeshPolygroupAttribute* BoneLayer =
		FindLayer(CombinedMesh, PCGGeometryCollectionToDynMeshConstants::DefaultBonePolygroupLayer);
	TestNotNull(TEXT("Per-bone PolyGroup layer present"), BoneLayer);
	TestNotNull(TEXT("Internal-face PolyGroup layer present"),
		FindLayer(CombinedMesh, PCGUtilsGeometryCollectionPieceMesh::InternalFacePolygroupLayerName()));

	// Hidden faces are excluded by default, so the visibility layer would say nothing and is not emitted.
	TestNull(TEXT("No visible-face layer when hidden faces are excluded"),
		FindLayer(CombinedMesh, PCGUtilsGeometryCollectionPieceMesh::VisibleFacePolygroupLayerName()));

	// Every piece must be distinguishable in the combined mesh, which is what the bone layer is for.
	if (BoneLayer)
	{
		TSet<int32> BonesSeen;
		for (const int32 TriangleID : CombinedMesh.TriangleIndicesItr())
		{
			BonesSeen.Add(BoneLayer->GetValue(TriangleID));
		}
		TestEqual(TEXT("One PolyGroup value per piece"), BonesSeen.Num(), CountPieces(Fractured));
	}

	// Turning the layers off has to actually remove them, not merely leave them unwritten.
	{
		const TArray<const UPCGDynamicMeshData*> NoLayers = Convert(Fractured,
			[](UPCGGeometryCollectionToDynMeshSettings& S)
			{
				S.bSetPolygroupPerBone = false;
				S.bTagInternalFaces = false;
			});
		if (TestEqual(TEXT("One mesh without layers"), NoLayers.Num(), 1))
		{
			TestNull(TEXT("Bone layer removed on request"),
				FindLayer(Mesh(NoLayers[0]), PCGGeometryCollectionToDynMeshConstants::DefaultBonePolygroupLayer));
			TestNull(TEXT("Internal-face layer removed on request"),
				FindLayer(Mesh(NoLayers[0]),
					PCGUtilsGeometryCollectionPieceMesh::InternalFacePolygroupLayerName()));
			TestEqual(TEXT("Removing layers does not change the geometry"),
				Mesh(NoLayers[0]).TriangleCount(), CombinedMesh.TriangleCount());
		}
	}

	// Including hidden faces keeps the visibility layer, which is then the only way to tell them apart.
	{
		const TArray<const UPCGDynamicMeshData*> WithHidden = Convert(Fractured,
			[](UPCGGeometryCollectionToDynMeshSettings& S) { S.bIncludeHiddenFaces = true; });
		if (TestEqual(TEXT("One mesh including hidden faces"), WithHidden.Num(), 1))
		{
			TestNotNull(TEXT("Visible-face layer present when hidden faces are included"),
				FindLayer(Mesh(WithHidden[0]),
					PCGUtilsGeometryCollectionPieceMesh::VisibleFacePolygroupLayerName()));
			TestTrue(TEXT("Including hidden faces never emits fewer triangles"),
				Mesh(WithHidden[0]).TriangleCount() >= CombinedMesh.TriangleCount());
		}
	}

	return true;
}

/**
 * Per Piece is the new mode, and the thing that makes it useful rather than just "several meshes" is that each
 * output can still be traced back to the bone it came from.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPCGUtilsFracturePerPieceConversionTest,
	"PCGUtils.Fracture.Conversion.PerPiece",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPCGUtilsFracturePerPieceConversionTest::RunTest(const FString&)
{
	using namespace PCGUtilsFractureTests;
	using namespace PCGUtilsFractureConversionTests;
	using namespace PCGUtilsGeometryCollectionIdentity;

	const UPCGGeometryCollectionData* Fractured = Fracture(ToCollection(Box()), SiteGrid(3));
	if (!TestNotNull(TEXT("Fractured"), Fractured))
	{
		return false;
	}

	const int32 NumPieces = CountPieces(Fractured);
	const TArray<const UPCGDynamicMeshData*> PerPiece = Convert(Fractured,
		[](UPCGGeometryCollectionToDynMeshSettings& S)
		{
			S.OutputMode = EPCGGeometryCollectionToDynMeshOutputMode::PerPiece;
		});

	TestEqual(TEXT("One mesh per piece"), PerPiece.Num(), NumPieces);
	if (PerPiece.IsEmpty())
	{
		return false;
	}

	// Identity is unconditional: without it a downstream selection could not be resolved back.
	const int64 ExpectedSourceId = FoldGuid(Fractured->GetCollectionId());
	const int64 ExpectedStateId = FoldGuid(Fractured->GetStateId());

	TSet<int32> BonesSeen;
	int32 TotalTriangles = 0;
	for (const UPCGDynamicMeshData* PieceData : PerPiece)
	{
		TestTrue(TEXT("A per-piece mesh has geometry"), Mesh(PieceData).TriangleCount() > 0);
		TotalTriangles += Mesh(PieceData).TriangleCount();

		int32 BoneIndex = INDEX_NONE;
		int64 SourceId = 0;
		int64 StateId = 0;
		int32 Revision = INDEX_NONE;
		if (!TestTrue(TEXT("GC_BoneIndex is written"),
				TryReadDataAttribute(PieceData, BoneIndexAttribute, BoneIndex))
			|| !TestTrue(TEXT("GC_SourceId is written"),
				TryReadDataAttribute(PieceData, SourceIdAttribute, SourceId))
			|| !TestTrue(TEXT("GC_SourceStateId is written"),
				TryReadDataAttribute(PieceData, SourceStateIdAttribute, StateId))
			|| !TestTrue(TEXT("GC_SourceRevision is written"),
				TryReadDataAttribute(PieceData, SourceRevisionAttribute, Revision)))
		{
			return false;
		}

		TestEqual(TEXT("The piece names the collection it came from"), SourceId, ExpectedSourceId);
		TestEqual(TEXT("The piece names the exact state it came from"), StateId, ExpectedStateId);
		TestEqual(TEXT("The piece names the revision it came from"), Revision, Fractured->GetRevision());

		// The bone it claims must actually be a piece of that collection.
		TestTrue(TEXT("The recorded bone is a piece"),
			PCGUtilsGeometryCollectionHierarchy::IsPiece(Fractured->GetCollection(), BoneIndex));
		BonesSeen.Add(BoneIndex);

		// Optional attributes stay off unless asked for - the same rule the points node follows.
		double Unused = 0.0;
		TestFalse(TEXT("GC_ExposureRatio is not written by default"),
			TryReadDataAttribute(PieceData, ExposureRatioAttribute, Unused));
	}

	TestEqual(TEXT("Every piece is accounted for exactly once"), BonesSeen.Num(), NumPieces);

	// Splitting must not lose or duplicate surface: the pieces together are the combined mesh.
	{
		const TArray<const UPCGDynamicMeshData*> Combined =
			Convert(Fractured, [](UPCGGeometryCollectionToDynMeshSettings&) {});
		if (TestEqual(TEXT("Combined emitted one mesh"), Combined.Num(), 1))
		{
			TestEqual(TEXT("Per Piece and Combined carry the same triangles"),
				TotalTriangles, Mesh(Combined[0]).TriangleCount());
		}
	}

	// Opt-in attributes, and the surface values agreeing with what GC Bones To Points reports for the same
	// bone - the two nodes describing a piece differently would be a trap.
	{
		const TArray<const UPCGDynamicMeshData*> WithAttributes = Convert(Fractured,
			[](UPCGGeometryCollectionToDynMeshSettings& S)
			{
				S.OutputMode = EPCGGeometryCollectionToDynMeshOutputMode::PerPiece;
				S.bOutputIsExterior = true;
				S.bOutputExposureRatio = true;
				S.bOutputGeometryIndex = true;
				S.bOutputHierarchyLevel = true;
			});

		const UPCGBasePointData* Points = BonesToPoints(Fractured, /*bWithSurfaceAttributes=*/true);
		const FPCGMetadataDomain* PointDomain =
			Points->ConstMetadata()->GetConstMetadataDomain(PCGMetadataDomainID::Elements);
		const FPCGMetadataAttribute<int32>* PointBone =
			PointDomain->GetConstTypedAttribute<int32>(BoneIndexAttribute);
		const FPCGMetadataAttribute<double>* PointExposure =
			PointDomain->GetConstTypedAttribute<double>(ExposureRatioAttribute);

		TMap<int32, double> ExposureByBone;
		const auto Entries = Points->GetConstMetadataEntryValueRange();
		for (int32 Index = 0; Index < Entries.Num(); ++Index)
		{
			ExposureByBone.Add(
				PointBone->GetValueFromItemKey(Entries[Index]),
				PointExposure->GetValueFromItemKey(Entries[Index]));
		}

		int32 NumCompared = 0;
		for (const UPCGDynamicMeshData* PieceData : WithAttributes)
		{
			int32 BoneIndex = INDEX_NONE;
			double Exposure = -1.0;
			bool bIsExterior = false;
			int32 GeometryIndex = INDEX_NONE;
			int32 Level = INDEX_NONE;

			TryReadDataAttribute(PieceData, BoneIndexAttribute, BoneIndex);
			TestTrue(TEXT("GC_ExposureRatio is written when asked for"),
				TryReadDataAttribute(PieceData, ExposureRatioAttribute, Exposure));
			TestTrue(TEXT("GC_IsExterior is written when asked for"),
				TryReadDataAttribute(PieceData, IsExteriorAttribute, bIsExterior));
			TestTrue(TEXT("GC_GeometryIndex is written when asked for"),
				TryReadDataAttribute(PieceData, GeometryIndexAttribute, GeometryIndex));
			TestTrue(TEXT("GC_HierarchyLevel is written when asked for"),
				TryReadDataAttribute(PieceData, HierarchyLevelAttribute, Level));

			TestEqual(TEXT("Geometry index matches the collection"),
				GeometryIndex, Fractured->GetCollection().TransformToGeometryIndex[BoneIndex]);
			TestEqual(TEXT("Hierarchy level matches the collection"),
				Level, PCGUtilsGeometryCollectionHierarchy::GetLevel(Fractured->GetCollection(), BoneIndex));
			TestEqual(TEXT("Is Exterior agrees with the exposure ratio"), bIsExterior, Exposure > 0.0);

			if (const double* PointValue = ExposureByBone.Find(BoneIndex))
			{
				TestTrue(TEXT("Exposure matches what GC Bones To Points reports for the same bone"),
					FMath::IsNearlyEqual(Exposure, *PointValue, UE_KINDA_SMALL_NUMBER));
				++NumCompared;
			}
		}
		TestEqual(TEXT("Every piece was compared against its point"), NumCompared, WithAttributes.Num());
	}

	// Piece Local re-centres each piece on its own origin, which is only meaningful per piece.
	{
		const TArray<const UPCGDynamicMeshData*> Local = Convert(Fractured,
			[](UPCGGeometryCollectionToDynMeshSettings& S)
			{
				S.OutputMode = EPCGGeometryCollectionToDynMeshOutputMode::PerPiece;
				S.Space = EPCGGeometryCollectionToDynMeshSpace::PieceLocal;
			});
		TestEqual(TEXT("Piece Local emits the same number of meshes"), Local.Num(), PerPiece.Num());

		// Bone transforms are identity throughout this module's round trip, so the two spaces coincide - and
		// saying so here is what would catch a future node that starts moving bones.
		int32 TotalLocalTriangles = 0;
		for (const UPCGDynamicMeshData* PieceData : Local)
		{
			TotalLocalTriangles += Mesh(PieceData).TriangleCount();
		}
		TestEqual(TEXT("Space does not change how much geometry is emitted"),
			TotalLocalTriangles, TotalTriangles);
	}

	return true;
}

#endif // WITH_AUTOMATION_TESTS
