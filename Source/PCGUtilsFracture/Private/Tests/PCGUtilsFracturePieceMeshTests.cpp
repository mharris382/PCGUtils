// Copyright Max Harris

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "Data/PCGUtilsGeometryCollectionPieceMesh.h"
#include "FunctionLibraries/PCGUtilsGeometryCollectionMeshPresentation.h"
#include "FunctionLibraries/PCGUtilsGeometryCollectionSurface.h"
#include "Tests/PCGUtilsFractureTestHelpers.h"

namespace PCGUtilsFracturePieceMeshTests
{
	using namespace PCGUtilsFractureTests;

	/** Geometry indices of every piece in a collection. */
	TArray<int32> PieceGeometryIndices(const UPCGGeometryCollectionData* Data)
	{
		const FGeometryCollection& Collection = Data->GetCollection();
		TArray<int32> Bones;
		PCGUtilsGeometryCollectionHierarchy::GatherPieces(Collection, Bones);

		TArray<int32> GeometryIndices;
		GeometryIndices.Reserve(Bones.Num());
		for (const int32 Bone : Bones)
		{
			GeometryIndices.Add(Collection.TransformToGeometryIndex[Bone]);
		}
		return GeometryIndices;
	}
}

/**
 * The whole point of a canonical view is that a consumer can go from a triangle or vertex of the Dynamic Mesh
 * straight back to the face or vertex of the Geometry Collection it came from. If that mapping is wrong, a
 * selector reading the collection's per-face interior flag reads the wrong face and nothing says so.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPCGUtilsFracturePieceMeshProvenanceTest,
	"PCGUtils.Fracture.PieceMesh.ProvenanceIsExact",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPCGUtilsFracturePieceMeshProvenanceTest::RunTest(const FString&)
{
	using namespace PCGUtilsFractureTests;
	using namespace PCGUtilsFracturePieceMeshTests;

	const UPCGGeometryCollectionData* Fractured = Fracture(ToCollection(Box()), SiteGrid(3));
	if (!TestNotNull(TEXT("Fractured"), Fractured))
	{
		return false;
	}

	const FGeometryCollection& Collection = Fractured->GetCollection();
	const TArray<int32> GeometryIndices = PieceGeometryIndices(Fractured);
	if (!TestTrue(TEXT("There are pieces to convert"), GeometryIndices.Num() > 0))
	{
		return false;
	}

	int32 NumChecked = 0;
	for (const int32 GeometryIndex : GeometryIndices)
	{
		const TSharedPtr<const FPCGUtilsGeometryCollectionPieceMeshView> View =
			Fractured->GetPieceMeshCache().GetOrBuild(Collection, GeometryIndex);
		if (!View.IsValid())
		{
			AddError(FString::Printf(TEXT("Geometry %d produced no view"), GeometryIndex));
			return false;
		}

		const UE::Geometry::FDynamicMesh3& Mesh = *View->Mesh;

		// Nothing is dropped, so the counts have to line up exactly with the collection's ranges.
		TestEqual(TEXT("Every face of the piece is present"), Mesh.TriangleCount(), View->FaceCount);
		TestEqual(TEXT("Every vertex of the piece is present, plus any splits"),
			Mesh.VertexCount(), View->VertexCount + View->DuplicatedVertexSource.Num());

		// Triangle -> face, and the face's own interior flag must match what the mesh reports.
		for (const int32 TriangleID : Mesh.TriangleIndicesItr())
		{
			const int32 CollectionFace = View->GetCollectionFaceIndex(TriangleID);
			if (!Collection.Internal.IsValidIndex(CollectionFace))
			{
				AddError(TEXT("A triangle mapped outside the collection's face range"));
				return false;
			}

			if (Collection.Internal[CollectionFace]
				!= PCGUtilsGeometryCollectionSurface::IsInteriorTriangle(Mesh, TriangleID))
			{
				AddError(TEXT("A triangle's interior flag disagrees with the collection face it came from"));
				return false;
			}

			// Vertex -> vertex, checked by position: the view is bone-local, and so is the collection.
			const UE::Geometry::FIndex3i Triangle = Mesh.GetTriangle(TriangleID);
			for (int32 Corner = 0; Corner < 3; ++Corner)
			{
				const int32 CollectionVertex = View->GetCollectionVertexIndex(Triangle[Corner]);
				if (!Collection.Vertex.IsValidIndex(CollectionVertex))
				{
					AddError(TEXT("A vertex mapped outside the collection's vertex range"));
					return false;
				}

				const FVector Expected(Collection.Vertex[CollectionVertex]);
				if (!FVector(Mesh.GetVertex(Triangle[Corner])).Equals(Expected, UE_KINDA_SMALL_NUMBER))
				{
					AddError(TEXT("A vertex does not sit where the collection vertex it maps to does"));
					return false;
				}
			}
			++NumChecked;
		}
	}

	TestTrue(TEXT("Something was actually checked"), NumChecked > 0);

	// A cluster's hidden pre-fracture geometry is not a piece, and must not be convertible as one.
	{
		const int32 NumGeometry = Collection.NumElements(FGeometryCollection::GeometryGroup);
		for (int32 GeometryIndex = 0; GeometryIndex < NumGeometry; ++GeometryIndex)
		{
			const int32 Bone = Collection.TransformIndex[GeometryIndex];
			if (PCGUtilsGeometryCollectionHierarchy::IsPiece(Collection, Bone))
			{
				continue;
			}
			FPCGUtilsGeometryCollectionPieceMeshView Rejected;
			TestFalse(TEXT("Non-piece geometry is refused"),
				PCGUtilsGeometryCollectionPieceMesh::BuildPieceMeshView(Collection, GeometryIndex, Rejected));
		}
	}

	return true;
}

/**
 * The cache exists so several consumers of one collection state convert nothing twice, and so a revision that
 * cannot have changed the geometry does not throw the work away.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPCGUtilsFracturePieceMeshCacheTest,
	"PCGUtils.Fracture.PieceMesh.CacheReuse",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPCGUtilsFracturePieceMeshCacheTest::RunTest(const FString&)
{
	using namespace PCGUtilsFractureTests;
	using namespace PCGUtilsFracturePieceMeshTests;

	const UPCGGeometryCollectionData* Fractured = Fracture(ToCollection(Box()), SiteGrid(3));
	if (!TestNotNull(TEXT("Fractured"), Fractured))
	{
		return false;
	}

	const FGeometryCollection& Collection = Fractured->GetCollection();
	const TArray<int32> GeometryIndices = PieceGeometryIndices(Fractured);

	TestEqual(TEXT("The cache starts empty"), Fractured->GetPieceMeshCache().Num(), 0);
	TestFalse(TEXT("Find does not build"),
		Fractured->GetPieceMeshCache().Find(GeometryIndices[0]).IsValid());

	const TSharedPtr<const FPCGUtilsGeometryCollectionPieceMeshView> First =
		Fractured->GetPieceMeshCache().GetOrBuild(Collection, GeometryIndices[0]);
	const TSharedPtr<const FPCGUtilsGeometryCollectionPieceMeshView> Second =
		Fractured->GetPieceMeshCache().GetOrBuild(Collection, GeometryIndices[0]);

	if (!TestTrue(TEXT("A view was built"), First.IsValid()))
	{
		return false;
	}
	// The same object, not an equal one: a second consumer must not pay for a second conversion.
	TestEqual(TEXT("A second request returns the cached view"), First.Get(), Second.Get());
	TestEqual(TEXT("Only one entry was added"), Fractured->GetPieceMeshCache().Num(), 1);

	// Duplicated data is the same collection state, so it shares the cache rather than starting over.
	{
		const UPCGGeometryCollectionData* Duplicate =
			Cast<UPCGGeometryCollectionData>(Fractured->DuplicateData(nullptr));
		if (TestNotNull(TEXT("Duplicated"), Duplicate))
		{
			TestEqual(TEXT("A duplicate shares the built views"),
				Duplicate->GetPieceMeshCache().Find(GeometryIndices[0]).Get(), First.Get());
		}
	}

	// Build the rest, then check what survives each kind of revision.
	for (const int32 GeometryIndex : GeometryIndices)
	{
		Fractured->GetPieceMeshCache().GetOrBuild(Collection, GeometryIndex);
	}
	const int32 NumBuilt = Fractured->GetPieceMeshCache().Num();
	TestEqual(TEXT("Every piece was built"), NumBuilt, GeometryIndices.Num());

	// A hierarchy-only revision cannot have moved a vertex, so every mesh carries over - re-keyed by BoneId,
	// which is the whole reason that attribute exists.
	{
		TSharedRef<FGeometryCollection> Copy = Fractured->CreateMutableCopy();
		FPCGUtilsGeometryCollectionMutationResult Mutation;
		Mutation.bHierarchyChanged = true;

		UPCGGeometryCollectionData* Revised = PCGUtilsGeometryCollectionRevisionPublisher::PublishRevision(
			nullptr, Fractured, Copy, Mutation);
		if (TestNotNull(TEXT("Hierarchy-only revision published"), Revised))
		{
			TestEqual(TEXT("A hierarchy-only revision keeps every piece mesh"),
				Revised->GetPieceMeshCache().Num(), NumBuilt);

			// Carried over, not shared: the two states are different objects with their own caches.
			TestNotEqual(TEXT("The revision has its own cache"),
				&Revised->GetPieceMeshCache(), &Fractured->GetPieceMeshCache());
		}
	}

	// Anything that could have touched geometry drops them, because nothing currently reports *which* pieces
	// a cutter changed - and a stale mesh is worse than a rebuilt one.
	{
		TSharedRef<FGeometryCollection> Copy = Fractured->CreateMutableCopy();
		UPCGGeometryCollectionData* Revised = PCGUtilsGeometryCollectionRevisionPublisher::PublishRevision(
			nullptr, Fractured, Copy, FPCGUtilsGeometryCollectionMutationResult::Structural());
		if (TestNotNull(TEXT("Structural revision published"), Revised))
		{
			TestEqual(TEXT("A structural revision keeps no piece mesh"),
				Revised->GetPieceMeshCache().Num(), 0);
		}
	}

	return true;
}

/**
 * Interior/exterior is a fact the collection records per face; vertices and edges sit between faces and have
 * to be given a rule. The rule is exterior-biased, which is what makes Exterior and Interior complementary
 * instead of overlapping on every cut rim.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPCGUtilsFractureSurfaceClassificationTest,
	"PCGUtils.Fracture.PieceMesh.SurfaceClassification",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPCGUtilsFractureSurfaceClassificationTest::RunTest(const FString&)
{
	using namespace PCGUtilsFractureTests;
	using namespace PCGUtilsFracturePieceMeshTests;
	using ETarget = EPCGUtilsGeometryCollectionSurfaceTarget;

	const UPCGGeometryCollectionData* Fractured = Fracture(ToCollection(Box()), SiteGrid(3));
	if (!TestNotNull(TEXT("Fractured"), Fractured))
	{
		return false;
	}

	const FGeometryCollection& Collection = Fractured->GetCollection();
	const TArray<int32> GeometryIndices = PieceGeometryIndices(Fractured);

	// A corner piece of a fractured box has both original surface and cut surface, which is what makes the
	// boundary rule observable at all.
	int32 MixedPieceGeometry = INDEX_NONE;
	for (const int32 GeometryIndex : GeometryIndices)
	{
		const TSharedPtr<const FPCGUtilsGeometryCollectionPieceMeshView> View =
			Fractured->GetPieceMeshCache().GetOrBuild(Collection, GeometryIndex);
		const PCGUtilsGeometryCollectionSurface::FSurfaceCounts Counts =
			PCGUtilsGeometryCollectionSurface::MeasureSurface(*View->Mesh);
		if (Counts.ExteriorTriangles > 0 && Counts.InteriorTriangles > 0)
		{
			MixedPieceGeometry = GeometryIndex;
			break;
		}
	}

	if (!TestTrue(TEXT("A piece with both kinds of surface exists"), MixedPieceGeometry != INDEX_NONE))
	{
		return false;
	}

	const TSharedPtr<const FPCGUtilsGeometryCollectionPieceMeshView> View =
		Fractured->GetPieceMeshCache().GetOrBuild(Collection, MixedPieceGeometry);
	const UE::Geometry::FDynamicMesh3& Mesh = *View->Mesh;

	TestTrue(TEXT("The mesh carries a classification"),
		PCGUtilsGeometryCollectionSurface::HasClassification(Mesh));

	// Triangles: the two targets partition the mesh exactly.
	{
		int32 NumExterior = 0;
		int32 NumInterior = 0;
		for (const int32 TriangleID : Mesh.TriangleIndicesItr())
		{
			NumExterior += PCGUtilsGeometryCollectionSurface::TriangleMatchesTarget(Mesh, TriangleID, ETarget::Exterior) ? 1 : 0;
			NumInterior += PCGUtilsGeometryCollectionSurface::TriangleMatchesTarget(Mesh, TriangleID, ETarget::Interior) ? 1 : 0;
		}
		TestEqual(TEXT("Every triangle is exterior or interior, never both"),
			NumExterior + NumInterior, Mesh.TriangleCount());
		TestTrue(TEXT("The piece has both kinds of triangle"), NumExterior > 0 && NumInterior > 0);
	}

	// The canonical view is unwelded, and an original surface meeting a cut surface is a hard normal seam, so
	// the collection stores those corners as separate vertices. No vertex or edge is therefore shared between
	// the two classes here, and the boundary rule has nothing to decide. Worth asserting rather than assuming:
	// it is the reason anything evaluating a vertex or edge predicate must weld first.
	{
		int32 NumAmbiguousVertices = 0;
		for (const int32 VertexID : Mesh.VertexIndicesItr())
		{
			bool bAnyExterior = false;
			bool bAnyInterior = false;
			for (const int32 TriangleID : Mesh.VtxTrianglesItr(VertexID))
			{
				(PCGUtilsGeometryCollectionSurface::IsInteriorTriangle(Mesh, TriangleID)
					? bAnyInterior : bAnyExterior) = true;
			}
			NumAmbiguousVertices += (bAnyExterior && bAnyInterior) ? 1 : 0;
		}
		TestEqual(TEXT("An unwelded piece has no vertex shared between the two surfaces"),
			NumAmbiguousVertices, 0);
	}

	// Welded is what a consumer actually evaluates a vertex or edge predicate against, and it is where the
	// two surfaces genuinely meet. This is the decisive test of the exterior bias.
	{
		UE::Geometry::FDynamicMesh3 Welded;
		PCGUtilsGeometryCollectionMeshPresentation::PresentPiece(
			*View, FTransform::Identity, PCGUtilsGeometryCollectionMeshPresentation::FPresentationOptions(),
			Welded);

		TestTrue(TEXT("Welding preserves the classification"),
			PCGUtilsGeometryCollectionSurface::HasClassification(Welded));

		int32 NumBoundaryVertices = 0;
		for (const int32 VertexID : Welded.VertexIndicesItr())
		{
			bool bAnyExterior = false;
			bool bAnyInterior = false;
			for (const int32 TriangleID : Welded.VtxTrianglesItr(VertexID))
			{
				(PCGUtilsGeometryCollectionSurface::IsInteriorTriangle(Welded, TriangleID)
					? bAnyInterior : bAnyExterior) = true;
			}

			const bool bInterior = PCGUtilsGeometryCollectionSurface::IsInteriorVertex(Welded, VertexID);
			if (bAnyExterior && bAnyInterior)
			{
				++NumBoundaryVertices;
				// The decisive assertion: on the seam, exterior wins.
				TestFalse(TEXT("A vertex on the seam counts as exterior"), bInterior);
			}
			else if (bAnyInterior)
			{
				TestTrue(TEXT("A vertex with only cut faces is interior"), bInterior);
			}

			TestEqual(TEXT("Vertex targets partition the mesh"),
				PCGUtilsGeometryCollectionSurface::VertexMatchesTarget(Welded, VertexID, ETarget::Exterior),
				!bInterior);
		}
		TestTrue(TEXT("A welded piece has vertices where the two surfaces meet"), NumBoundaryVertices > 0);

		int32 NumBoundaryEdges = 0;
		for (const int32 EdgeID : Welded.EdgeIndicesItr())
		{
			const UE::Geometry::FIndex2i EdgeTriangles = Welded.GetEdgeT(EdgeID);
			bool bAnyExterior = false;
			bool bAnyInterior = false;
			for (int32 Side = 0; Side < 2; ++Side)
			{
				if (EdgeTriangles[Side] == UE::Geometry::FDynamicMesh3::InvalidID) { continue; }
				(PCGUtilsGeometryCollectionSurface::IsInteriorTriangle(Welded, EdgeTriangles[Side])
					? bAnyInterior : bAnyExterior) = true;
			}

			if (bAnyExterior && bAnyInterior)
			{
				++NumBoundaryEdges;
				TestFalse(TEXT("An edge on the seam counts as exterior"),
					PCGUtilsGeometryCollectionSurface::IsInteriorEdge(Welded, EdgeID));
			}
		}
		TestTrue(TEXT("A welded piece has edges where the two surfaces meet"), NumBoundaryEdges > 0);
	}

	// All is everything, whichever domain.
	TestEqual(TEXT("Target All accepts every triangle"),
		[&]
		{
			int32 Count = 0;
			for (const int32 TriangleID : Mesh.TriangleIndicesItr())
			{
				Count += PCGUtilsGeometryCollectionSurface::TriangleMatchesTarget(Mesh, TriangleID, ETarget::All) ? 1 : 0;
			}
			return Count;
		}(),
		Mesh.TriangleCount());

	return true;
}

/**
 * Presentation is what a conversion node applies on the way out. The canonical view has to survive it and
 * still produce the same mesh the existing GC To DynMesh node emits today.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPCGUtilsFracturePresentationTest,
	"PCGUtils.Fracture.PieceMesh.Presentation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPCGUtilsFracturePresentationTest::RunTest(const FString&)
{
	using namespace PCGUtilsFractureTests;
	using namespace PCGUtilsFracturePieceMeshTests;
	namespace Presentation = PCGUtilsGeometryCollectionMeshPresentation;

	const UPCGGeometryCollectionData* Fractured = Fracture(ToCollection(Box()), SiteGrid(3));
	if (!TestNotNull(TEXT("Fractured"), Fractured))
	{
		return false;
	}

	const FGeometryCollection& Collection = Fractured->GetCollection();
	const TArray<int32> GeometryIndices = PieceGeometryIndices(Fractured);

	TArray<UE::Geometry::FDynamicMesh3> Presented;
	TArray<const UE::Geometry::FDynamicMesh3*> PresentedPointers;
	TArray<Presentation::FCombinedPieceRange> Identities;
	Presented.SetNum(GeometryIndices.Num());

	Presentation::FPresentationOptions Options;
	for (int32 Index = 0; Index < GeometryIndices.Num(); ++Index)
	{
		const TSharedPtr<const FPCGUtilsGeometryCollectionPieceMeshView> View =
			Fractured->GetPieceMeshCache().GetOrBuild(Collection, GeometryIndices[Index]);

		// Bone transforms are identity throughout this module's round trip, so collection space is the
		// piece's own space and no transform is applied.
		Presentation::PresentPiece(*View, FTransform::Identity, Options, Presented[Index]);

		Presentation::FCombinedPieceRange Identity;
		Identity.TransformIndex = View->TransformIndex;
		Identity.GeometryIndex = View->GeometryIndex;
		Identities.Add(Identity);
	}

	for (const UE::Geometry::FDynamicMesh3& Mesh : Presented)
	{
		PresentedPointers.Add(&Mesh);
		TestTrue(TEXT("A presented piece has geometry"), Mesh.TriangleCount() > 0);
		TestTrue(TEXT("A presented piece is compact"), Mesh.IsCompact());
		// Welding must not have destroyed the classification the selector layer depends on.
		TestTrue(TEXT("A presented piece keeps its interior/exterior layer"),
			PCGUtilsGeometryCollectionSurface::HasClassification(Mesh));
	}

	UE::Geometry::FDynamicMesh3 Combined;
	TArray<Presentation::FCombinedPieceRange> Ranges;
	Presentation::CombinePieces(PresentedPointers, Identities, Combined, Ranges);

	TestEqual(TEXT("Every piece contributed a range"), Ranges.Num(), GeometryIndices.Num());
	TestTrue(TEXT("The combined mesh has geometry"), Combined.TriangleCount() > 0);

	// The ranges have to actually address the combined mesh, which is what the bone layer relies on.
	int32 NumRangeTriangles = 0;
	for (const Presentation::FCombinedPieceRange& Range : Ranges)
	{
		for (int32 TriangleID = Range.TriangleStart; TriangleID < Range.TriangleEnd; ++TriangleID)
		{
			NumRangeTriangles += Combined.IsTriangle(TriangleID) ? 1 : 0;
		}
	}
	TestEqual(TEXT("The ranges cover the combined mesh exactly"),
		NumRangeTriangles, Combined.TriangleCount());

	// The bone layer is what makes a fracture piece selectable again once it is back in the DynMesh world.
	const FName BoneLayerName = TEXT("GC_Bone");
	TestTrue(TEXT("The bone layer was written"),
		Presentation::WriteBonePolygroupLayer(Combined, BoneLayerName, Ranges));

	const UE::Geometry::FDynamicMeshPolygroupAttribute* BoneLayer = FindLayer(Combined, BoneLayerName);
	if (TestNotNull(TEXT("The bone layer is present"), BoneLayer))
	{
		TSet<int32> BonesSeen;
		for (const int32 TriangleID : Combined.TriangleIndicesItr())
		{
			BonesSeen.Add(BoneLayer->GetValue(TriangleID));
		}
		TestEqual(TEXT("Every piece is distinguishable in the combined mesh"),
			BonesSeen.Num(), GeometryIndices.Num());
	}

	// And the interior/exterior layer survives the combine, which is what Select by PolyGroup reads.
	TestNotNull(TEXT("The internal-face layer survives combining"),
		FindLayer(Combined, PCGUtilsGeometryCollectionPieceMesh::InternalFacePolygroupLayerName()));

	// The existing node's output is the reference: same triangle count, reached a different way.
	{
		const UPCGDynamicMeshData* NodeOutput = ToDynMesh(Fractured);
		if (TestNotNull(TEXT("GC To DynMesh produced a mesh"), NodeOutput))
		{
			TestEqual(TEXT("Presentation matches the existing conversion's triangle count"),
				Combined.TriangleCount(), Mesh(NodeOutput).TriangleCount());
		}
	}

	return true;
}

#endif // WITH_AUTOMATION_TESTS
