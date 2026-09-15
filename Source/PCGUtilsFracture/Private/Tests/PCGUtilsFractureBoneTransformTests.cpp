// Copyright Max Harris

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "Tests/PCGUtilsFractureTestHelpers.h"

#include "Algo/Reverse.h"
#include "FunctionLibraries/PCGUtilsGeometryCollectionHierarchy.h"
#include "FunctionLibraries/PCGUtilsGeometryCollectionTransforms.h"

/**
 * Phase 1: the transform primitives, exercised directly on a collection rather than through a node.
 *
 * These are the assertions that make the node above them safe to write. Everything here is about the one thing
 * the engine's own helper gets wrong for our purposes - that stored transforms are parent-relative, so a
 * collection-space request has to be resolved against the parent before it is written.
 */
namespace PCGUtilsBoneTransformTests
{
	using namespace PCGUtilsFractureTests;

	/** Tolerance for a transform that has been through the collection's float storage. */
	constexpr double Tolerance = 0.01;

	bool TransformsNearlyEqual(const FTransform& A, const FTransform& B, double InTolerance = Tolerance)
	{
		return A.GetTranslation().Equals(B.GetTranslation(), InTolerance)
			&& A.GetRotation().Equals(B.GetRotation(), InTolerance)
			&& A.GetScale3D().Equals(B.GetScale3D(), InTolerance);
	}

	/** A fractured box, which is the only easy way to get a collection with real clusters and depth. */
	const UPCGGeometryCollectionData* FracturedBox()
	{
		const UPCGGeometryCollectionData* Collection = ToCollection(Box());
		return Collection ? Fracture(Collection, SiteGrid(2)) : nullptr;
	}

	/** Every bone's global transform, recomputed from the collection as it currently stands. */
	TArray<FTransform> Globals(const FGeometryCollection& Collection)
	{
		TArray<FTransform> Out;
		PCGUtilsGeometryCollectionHelpers::ComputeGlobalTransforms(Collection, Out);
		return Out;
	}
}

/**
 * Setting a leaf bone's collection-space transform puts it exactly there, whatever its parent is doing.
 *
 * The collection is deliberately placed at a non-identity, rotated, scaled root first: that is the case
 * FCollectionTransformFacade::Transform gets wrong, and the whole reason this library exists.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPCGUtilsBoneTransformLeafTest,
	"PCGUtils.Fracture.BoneTransform.SetLeafGlobal",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPCGUtilsBoneTransformLeafTest::RunTest(const FString&)
{
	using namespace PCGUtilsFractureTests;
	using namespace PCGUtilsBoneTransformTests;

	const UPCGGeometryCollectionData* Source = FracturedBox();
	if (!TestNotNull(TEXT("Fractured a box"), Source))
	{
		return false;
	}

	TSharedRef<FGeometryCollection> Collection = Source->CreateMutableCopy();

	// A root that is translated, rotated and scaled, so nothing below it is accidentally in collection space.
	PCGUtilsGeometryCollectionHelpers::PlaceCollection(*Collection,
		FTransform(FRotator(15.0, 40.0, -25.0), FVector(1000.0, -250.0, 75.0), FVector(2.0)));

	TArray<int32> Pieces;
	PCGUtilsGeometryCollectionHierarchy::GatherPieces(*Collection, Pieces);
	if (!TestTrue(TEXT("The fractured box has pieces"), Pieces.Num() > 1))
	{
		return false;
	}

	const int32 Target = Pieces[0];
	const TArray<FTransform> Before = Globals(*Collection);

	const FTransform Desired(FRotator(0.0, 90.0, 0.0), FVector(123.0, -45.0, 67.0), FVector(1.5));
	TestTrue(TEXT("SetBoneGlobalTransform succeeded"),
		PCGUtilsGeometryCollectionTransforms::SetBoneGlobalTransform(*Collection, Target, Before, Desired));

	const TArray<FTransform> After = Globals(*Collection);
	TestTrue(TEXT("The bone reached exactly the requested collection-space transform"),
		TransformsNearlyEqual(After[Target], Desired));

	// The parent is non-identity, so "wrote the local transform verbatim" and "resolved it through the parent"
	// give visibly different answers. Assert the wrong one did not happen.
	const FTransform ParentGlobal =
		PCGUtilsGeometryCollectionTransforms::GetParentGlobalTransform(*Collection, Target, Before);
	TestFalse(TEXT("The test is meaningful: the target's parent is not at identity"),
		ParentGlobal.Equals(FTransform::Identity));
	TestFalse(TEXT("The desired transform was not written straight into the local slot"),
		TransformsNearlyEqual(FTransform(Collection->Transform[Target]), Desired));

	for (const int32 Piece : Pieces)
	{
		if (Piece != Target)
		{
			TestTrue(TEXT("Untargeted bones did not move"),
				TransformsNearlyEqual(After[Piece], Before[Piece]));
		}
	}

	return true;
}

/**
 * Moving a cluster carries its descendants and preserves every relative transform between them exactly.
 *
 * This is the property the whole cluster story rests on, and it holds because descendant locals are simply not
 * written - so it should survive float storage untouched, not merely approximately.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPCGUtilsBoneTransformClusterTest,
	"PCGUtils.Fracture.BoneTransform.ClusterCarriesDescendants",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPCGUtilsBoneTransformClusterTest::RunTest(const FString&)
{
	using namespace PCGUtilsFractureTests;
	using namespace PCGUtilsBoneTransformTests;

	const UPCGGeometryCollectionData* Source = FracturedBox();
	if (!TestNotNull(TEXT("Fractured a box"), Source))
	{
		return false;
	}

	TSharedRef<FGeometryCollection> Collection = Source->CreateMutableCopy();

	TArray<int32> Roots;
	PCGUtilsGeometryCollectionHierarchy::GatherRoots(*Collection, Roots);
	if (!TestEqual(TEXT("One root cluster"), Roots.Num(), 1))
	{
		return false;
	}
	const int32 Cluster = Roots[0];

	TArray<int32> Descendants;
	PCGUtilsGeometryCollectionHierarchy::GetDescendants(*Collection, Cluster, Descendants);
	if (!TestTrue(TEXT("The cluster has descendants"), Descendants.Num() > 1))
	{
		return false;
	}

	const TArray<FTransform> Before = Globals(*Collection);
	// Snapshot every descendant's placement *relative to the cluster*, which is what must be invariant.
	TArray<FTransform> RelativeBefore;
	for (const int32 Bone : Descendants)
	{
		RelativeBefore.Add(Before[Bone].GetRelativeTransform(Before[Cluster]));
	}

	const FTransform Desired(FRotator(0.0, 37.0, 12.0), FVector(500.0, 200.0, -80.0), FVector::OneVector);
	TestTrue(TEXT("Moving the cluster succeeded"),
		PCGUtilsGeometryCollectionTransforms::SetBoneGlobalTransform(*Collection, Cluster, Before, Desired));

	const TArray<FTransform> After = Globals(*Collection);
	TestTrue(TEXT("The cluster reached the requested transform"),
		TransformsNearlyEqual(After[Cluster], Desired));

	for (int32 Index = 0; Index < Descendants.Num(); ++Index)
	{
		const int32 Bone = Descendants[Index];
		TestTrue(TEXT("Descendants kept their transform relative to the cluster"),
			TransformsNearlyEqual(After[Bone].GetRelativeTransform(After[Cluster]), RelativeBefore[Index]));
		TestFalse(TEXT("Descendants did in fact move in collection space"),
			TransformsNearlyEqual(After[Bone], Before[Bone]));
	}

	// Nothing below the cluster should have had its stored local transform touched at all.
	TSharedRef<FGeometryCollection> Untouched = Source->CreateMutableCopy();
	for (const int32 Bone : Descendants)
	{
		TestTrue(TEXT("Descendant local transforms were not rewritten"),
			Collection->Transform[Bone].Equals(Untouched->Transform[Bone]));
	}

	return true;
}

/** A delta composes onto the bone's current collection-space transform, not its parent-relative one. */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPCGUtilsBoneTransformDeltaTest,
	"PCGUtils.Fracture.BoneTransform.DeltaIsCollectionSpace",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPCGUtilsBoneTransformDeltaTest::RunTest(const FString&)
{
	using namespace PCGUtilsFractureTests;
	using namespace PCGUtilsBoneTransformTests;

	const UPCGGeometryCollectionData* Source = FracturedBox();
	if (!TestNotNull(TEXT("Fractured a box"), Source))
	{
		return false;
	}

	TSharedRef<FGeometryCollection> Collection = Source->CreateMutableCopy();
	PCGUtilsGeometryCollectionHelpers::PlaceCollection(*Collection,
		FTransform(FRotator(0.0, 90.0, 0.0), FVector(10.0, 20.0, 30.0), FVector::OneVector));

	TArray<int32> Pieces;
	PCGUtilsGeometryCollectionHierarchy::GatherPieces(*Collection, Pieces);
	if (!TestTrue(TEXT("The fractured box has pieces"), !Pieces.IsEmpty()))
	{
		return false;
	}

	const int32 Target = Pieces[0];
	const TArray<FTransform> Before = Globals(*Collection);

	// A pure collection-space translation along X. Under a root rotated 90 degrees about Z, a parent-space
	// interpretation of the same delta would move the piece along Y instead - which is exactly the bug.
	const FVector Offset(100.0, 0.0, 0.0);
	TestTrue(TEXT("ApplyBoneDelta succeeded"),
		PCGUtilsGeometryCollectionTransforms::ApplyBoneDelta(
			*Collection, Target, Before, FTransform(Offset)));

	const TArray<FTransform> After = Globals(*Collection);
	TestTrue(TEXT("The piece moved along collection-space X"),
		After[Target].GetTranslation().Equals(Before[Target].GetTranslation() + Offset, Tolerance));
	TestTrue(TEXT("A translation-only delta left the rotation alone"),
		After[Target].GetRotation().Equals(Before[Target].GetRotation(), Tolerance));

	return true;
}

/** ReduceToAntichain drops any bone that has a targeted ancestor, and is order-independent. */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPCGUtilsBoneTransformAntichainTest,
	"PCGUtils.Fracture.BoneTransform.ReduceToAntichain",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPCGUtilsBoneTransformAntichainTest::RunTest(const FString&)
{
	using namespace PCGUtilsFractureTests;
	using namespace PCGUtilsBoneTransformTests;

	const UPCGGeometryCollectionData* Source = FracturedBox();
	if (!TestNotNull(TEXT("Fractured a box"), Source))
	{
		return false;
	}

	const FGeometryCollection& Collection = Source->GetCollection();

	TArray<int32> Roots;
	PCGUtilsGeometryCollectionHierarchy::GatherRoots(Collection, Roots);
	TArray<int32> Pieces;
	PCGUtilsGeometryCollectionHierarchy::GatherPieces(Collection, Pieces);
	if (!TestTrue(TEXT("A root and some pieces"), Roots.Num() == 1 && Pieces.Num() > 1))
	{
		return false;
	}

	// Root plus everything under it, listed descendants-first and with a duplicate, to prove none of that
	// changes the answer.
	TArray<int32> Request = Pieces;
	Request.Add(Roots[0]);
	Request.Add(Pieces[0]);
	Algo::Reverse(Request);

	TArray<int32> Dropped;
	PCGUtilsGeometryCollectionTransforms::ReduceToAntichain(Collection, Request, &Dropped);

	TestEqual(TEXT("Only the root survives"), Request.Num(), 1);
	if (Request.Num() == 1)
	{
		TestEqual(TEXT("And it is the root"), Request[0], Roots[0]);
	}
	TestEqual(TEXT("Every piece was reported as dropped"), Dropped.Num(), Pieces.Num());

	// Pieces alone are already an antichain and must survive untouched.
	TArray<int32> PiecesOnly = Pieces;
	TArray<int32> NoneDropped;
	PCGUtilsGeometryCollectionTransforms::ReduceToAntichain(Collection, PiecesOnly, &NoneDropped);
	TestEqual(TEXT("Sibling pieces are already an antichain"), PiecesOnly.Num(), Pieces.Num());
	TestEqual(TEXT("Nothing was dropped"), NoneDropped.Num(), 0);

	return true;
}

/**
 * Independent nesting gives every bone exactly the transform asked for it, rather than that transform composed
 * on top of whatever its ancestor did.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPCGUtilsBoneTransformNestedTest,
	"PCGUtils.Fracture.BoneTransform.NestedHandling",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPCGUtilsBoneTransformNestedTest::RunTest(const FString&)
{
	using namespace PCGUtilsFractureTests;
	using namespace PCGUtilsBoneTransformTests;

	const UPCGGeometryCollectionData* Source = FracturedBox();
	if (!TestNotNull(TEXT("Fractured a box"), Source))
	{
		return false;
	}

	TArray<int32> Roots;
	TArray<int32> Pieces;
	PCGUtilsGeometryCollectionHierarchy::GatherRoots(Source->GetCollection(), Roots);
	PCGUtilsGeometryCollectionHierarchy::GatherPieces(Source->GetCollection(), Pieces);
	if (!TestTrue(TEXT("A root and some pieces"), Roots.Num() == 1 && Pieces.Num() > 1))
	{
		return false;
	}

	const int32 Root = Roots[0];
	const int32 Piece = Pieces[0];

	const FTransform RootDesired(FQuat::Identity, FVector(0.0, 0.0, 500.0));
	const FTransform PieceDesired(FQuat::Identity, FVector(300.0, 0.0, 0.0));
	const TArray<int32> Bones = {Root, Piece};
	const TArray<FTransform> Desired = {RootDesired, PieceDesired};

	// --- Topmost: the descendant is dropped and the root's move carries it ----------------------------
	{
		TSharedRef<FGeometryCollection> Collection = Source->CreateMutableCopy();
		FPCGUtilsGeometryCollectionBoneTransformResult Result;
		TestTrue(TEXT("Topmost succeeded"),
			PCGUtilsGeometryCollectionTransforms::SetBoneGlobalTransforms(
				*Collection, Bones, Desired, EPCGGeometryCollectionNestedBoneHandling::Topmost, Result));

		TestTrue(TEXT("Topmost noticed the nesting"), Result.bNestedTargetsFound);
		TestEqual(TEXT("Topmost dropped the descendant"), Result.NestedBonesDropped.Num(), 1);
		TestEqual(TEXT("Topmost applied only the root"), Result.NumApplied, 1);

		const TArray<FTransform> After = Globals(*Collection);
		TestTrue(TEXT("The root reached its transform"), TransformsNearlyEqual(After[Root], RootDesired));
		TestFalse(TEXT("The piece did not reach its own request"),
			TransformsNearlyEqual(After[Piece], PieceDesired));
	}

	// --- Independent: both land exactly where they were asked to ------------------------------------
	{
		TSharedRef<FGeometryCollection> Collection = Source->CreateMutableCopy();
		FPCGUtilsGeometryCollectionBoneTransformResult Result;
		TestTrue(TEXT("Independent succeeded"),
			PCGUtilsGeometryCollectionTransforms::SetBoneGlobalTransforms(
				*Collection, Bones, Desired, EPCGGeometryCollectionNestedBoneHandling::Independent, Result));

		TestEqual(TEXT("Independent applied both"), Result.NumApplied, 2);
		TestEqual(TEXT("Independent dropped nothing"), Result.NestedBonesDropped.Num(), 0);

		const TArray<FTransform> After = Globals(*Collection);
		TestTrue(TEXT("The root reached its transform"), TransformsNearlyEqual(After[Root], RootDesired));
		TestTrue(TEXT("The piece reached its own transform despite the root moving"),
			TransformsNearlyEqual(After[Piece], PieceDesired));
	}

	// --- Error: refused outright, collection untouched ----------------------------------------------
	{
		TSharedRef<FGeometryCollection> Collection = Source->CreateMutableCopy();
		const TArray<FTransform3f> BeforeLocals(Collection->Transform.GetConstArray());

		FPCGUtilsGeometryCollectionBoneTransformResult Result;
		TestFalse(TEXT("Error refused the nested request"),
			PCGUtilsGeometryCollectionTransforms::SetBoneGlobalTransforms(
				*Collection, Bones, Desired, EPCGGeometryCollectionNestedBoneHandling::Error, Result));
		TestEqual(TEXT("Error applied nothing"), Result.NumApplied, 0);

		for (int32 Bone = 0; Bone < BeforeLocals.Num(); ++Bone)
		{
			TestTrue(TEXT("A refused operation left the collection untouched"),
				Collection->Transform[Bone].Equals(BeforeLocals[Bone]));
		}
	}

	return true;
}

/** Deltas are resolved against the collection as it was on entry, so ordering cannot change the result. */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPCGUtilsBoneTransformBulkDeltaTest,
	"PCGUtils.Fracture.BoneTransform.BulkDeltasAreOrderIndependent",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPCGUtilsBoneTransformBulkDeltaTest::RunTest(const FString&)
{
	using namespace PCGUtilsFractureTests;
	using namespace PCGUtilsBoneTransformTests;

	const UPCGGeometryCollectionData* Source = FracturedBox();
	if (!TestNotNull(TEXT("Fractured a box"), Source))
	{
		return false;
	}

	TArray<int32> Pieces;
	PCGUtilsGeometryCollectionHierarchy::GatherPieces(Source->GetCollection(), Pieces);
	if (!TestTrue(TEXT("Several pieces"), Pieces.Num() > 2))
	{
		return false;
	}

	TArray<FTransform> Deltas;
	for (int32 Index = 0; Index < Pieces.Num(); ++Index)
	{
		Deltas.Add(FTransform(FVector(0.0, 0.0, 10.0 * (Index + 1))));
	}

	auto ApplyAndRead = [&](const TArray<int32>& Bones, const TArray<FTransform>& InDeltas)
	{
		TSharedRef<FGeometryCollection> Collection = Source->CreateMutableCopy();
		FPCGUtilsGeometryCollectionBoneTransformResult Result;
		PCGUtilsGeometryCollectionTransforms::ApplyBoneDeltas(
			*Collection, Bones, InDeltas, EPCGGeometryCollectionNestedBoneHandling::Topmost, Result);
		return Globals(*Collection);
	};

	const TArray<FTransform> Forward = ApplyAndRead(Pieces, Deltas);

	TArray<int32> ReversedBones = Pieces;
	TArray<FTransform> ReversedDeltas = Deltas;
	Algo::Reverse(ReversedBones);
	Algo::Reverse(ReversedDeltas);
	const TArray<FTransform> Reversed = ApplyAndRead(ReversedBones, ReversedDeltas);

	const TArray<FTransform> Before = Globals(Source->GetCollection());
	for (int32 Index = 0; Index < Pieces.Num(); ++Index)
	{
		const int32 Bone = Pieces[Index];
		TestTrue(TEXT("Each piece moved by its own delta"),
			Forward[Bone].GetTranslation().Equals(
				Before[Bone].GetTranslation() + Deltas[Index].GetTranslation(), Tolerance));
		TestTrue(TEXT("Listing the bones in the opposite order gives the same result"),
			TransformsNearlyEqual(Forward[Bone], Reversed[Bone]));
	}

	return true;
}

/** An invalid or unrepresentable request is reported and skipped rather than corrupting the collection. */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPCGUtilsBoneTransformValidationTest,
	"PCGUtils.Fracture.BoneTransform.RejectsBadRequests",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPCGUtilsBoneTransformValidationTest::RunTest(const FString&)
{
	using namespace PCGUtilsFractureTests;
	using namespace PCGUtilsBoneTransformTests;

	const UPCGGeometryCollectionData* Source = FracturedBox();
	if (!TestNotNull(TEXT("Fractured a box"), Source))
	{
		return false;
	}

	TSharedRef<FGeometryCollection> Collection = Source->CreateMutableCopy();
	const TArray<FTransform> Before = Globals(*Collection);
	const int32 NumTransforms = Collection->NumElements(FGeometryCollection::TransformGroup);

	TestFalse(TEXT("A negative bone index is refused"),
		PCGUtilsGeometryCollectionTransforms::SetBoneGlobalTransform(
			*Collection, -1, Before, FTransform::Identity));
	TestFalse(TEXT("An out-of-range bone index is refused"),
		PCGUtilsGeometryCollectionTransforms::SetBoneGlobalTransform(
			*Collection, NumTransforms, Before, FTransform::Identity));

	TArray<int32> Pieces;
	PCGUtilsGeometryCollectionHierarchy::GatherPieces(*Collection, Pieces);
	if (!TestTrue(TEXT("Some pieces"), !Pieces.IsEmpty()))
	{
		return false;
	}

	// Zero scale makes the bone's frame non-invertible, so a child could never be placed relative to it.
	TestFalse(TEXT("A zero-scale transform is refused"),
		PCGUtilsGeometryCollectionTransforms::SetBoneGlobalTransform(
			*Collection, Pieces[0], Before, FTransform(FQuat::Identity, FVector::ZeroVector, FVector::ZeroVector)));

	const TArray<FTransform> After = Globals(*Collection);
	for (int32 Bone = 0; Bone < NumTransforms; ++Bone)
	{
		TestTrue(TEXT("No refused request changed anything"), TransformsNearlyEqual(After[Bone], Before[Bone]));
	}

	// Mismatched array lengths are a programming error and are refused before anything is written.
	FPCGUtilsGeometryCollectionBoneTransformResult Result;
	const TArray<int32> Bones = {Pieces[0]};
	const TArray<FTransform> TooMany = {FTransform::Identity, FTransform::Identity};
	TestFalse(TEXT("Mismatched array lengths are refused"),
		PCGUtilsGeometryCollectionTransforms::SetBoneGlobalTransforms(
			*Collection, Bones, TooMany, EPCGGeometryCollectionNestedBoneHandling::Topmost, Result));

	// An out-of-range bone in a bulk request is reported, not fatal.
	const TArray<int32> MixedBones = {Pieces[0], NumTransforms + 5};
	const TArray<FTransform> MixedTransforms = {Before[Pieces[0]], FTransform::Identity};
	TestTrue(TEXT("A bulk request with one bad bone still runs"),
		PCGUtilsGeometryCollectionTransforms::SetBoneGlobalTransforms(
			*Collection, MixedBones, MixedTransforms,
			EPCGGeometryCollectionNestedBoneHandling::Topmost, Result));
	TestEqual(TEXT("The bad bone was reported"), Result.InvalidBones.Num(), 1);
	TestEqual(TEXT("The good bone was applied"), Result.NumApplied, 1);

	return true;
}

/**
 * ComputeBonePointTransform is the pivot convention, and it is not the bone's own transform.
 *
 * If these two ever coincide for a piece with off-centre geometry, the apply side has lost the offset it exists
 * to compensate for.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPCGUtilsBonePointPivotTest,
	"PCGUtils.Fracture.BoneTransform.PointPivotIsPieceCentre",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPCGUtilsBonePointPivotTest::RunTest(const FString&)
{
	using namespace PCGUtilsFractureTests;
	using namespace PCGUtilsBoneTransformTests;

	// A box centred away from the origin, so its geometry bounds centre is nowhere near its bone origin.
	const UPCGGeometryCollectionData* Source = ToCollection(Box(BoxSize, FVector(250.0, 0.0, 0.0)));
	if (!TestNotNull(TEXT("Built a collection"), Source))
	{
		return false;
	}

	const FGeometryCollection& Collection = Source->GetCollection();
	const TArray<FTransform> GlobalsArray = Globals(Collection);

	TArray<int32> Pieces;
	PCGUtilsGeometryCollectionHierarchy::GatherPieces(Collection, Pieces);
	if (!TestEqual(TEXT("One geometry bone"), Pieces.Num(), 1))
	{
		return false;
	}
	const int32 Piece = Pieces[0];

	const FTransform PointTransform = PCGUtilsGeometryCollectionTransforms::ComputeBonePointTransform(
		Collection, Piece, GlobalsArray, FTransform::Identity);
	const FTransform OriginTransform = PCGUtilsGeometryCollectionTransforms::ComputeBoneOriginTransform(
		Piece, GlobalsArray, FTransform::Identity);

	TestTrue(TEXT("The point sits at the piece's bounds centre"),
		PointTransform.GetTranslation().Equals(FVector(250.0, 0.0, 0.0), 1.0));
	TestFalse(TEXT("Which is not the bone origin - the offset the apply side must undo"),
		PointTransform.GetTranslation().Equals(OriginTransform.GetTranslation(), 1.0));
	TestTrue(TEXT("Rotation and scale come straight from the bone"),
		PointTransform.GetRotation().Equals(OriginTransform.GetRotation())
			&& PointTransform.GetScale3D().Equals(OriginTransform.GetScale3D()));

	// And the emitted points must agree with the library, or the round trip is measuring the wrong reference.
	const UPCGBasePointData* Points = BonesToPoints(Source);
	if (!TestNotNull(TEXT("Bones To Points produced points"), Points))
	{
		return false;
	}
	const auto PointTransforms = Points->GetConstTransformValueRange();
	if (TestEqual(TEXT("One point per piece"), PointTransforms.Num(), 1))
	{
		TestTrue(TEXT("GC Bones To Points and the transforms library agree on the pivot"),
			TransformsNearlyEqual(PointTransforms[0], PointTransform));
	}

	return true;
}

#endif // WITH_AUTOMATION_TESTS
