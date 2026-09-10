// Copyright Max Harris

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "Elements/Selections/PCGDynMeshNormalSelectionFactory.h"
#include "Elements/Selections/PCGGeometryCollectionSelectionFromDynMesh.h"
#include "Factories/PCGUtilsDynMeshSelectionFactory.h"
#include "FunctionLibraries/PCGUtilsGeometryCollectionHierarchy.h"
#include "Tests/PCGUtilsFractureTestHelpers.h"

namespace PCGUtilsFractureMeshPredicateTests
{
	using namespace PCGUtilsFractureTests;

	/**
	 * A face-domain DynMesh selector: triangles whose normal points along Direction.
	 *
	 * Note the operation clamps DotThreshold into [-1, 1] at runtime, so there is no "impossible" threshold to
	 * test the empty case with - restrict the eligible set instead.
	 */
	const UPCGUtilsDynMeshSelectionFactoryData* NormalSelector(
		const FVector& Direction, float DotThreshold = 0.9f)
	{
		UPCGDynMeshNormalSelectionFactoryProviderSettings* Settings =
			NewObject<UPCGDynMeshNormalSelectionFactoryProviderSettings>();
		Settings->ReferenceDirection = Direction;
		Settings->DotThreshold = DotThreshold;
		Settings->Representation = EPCGUtilsDynMeshSelectionRepresentation::Selector;
		return FirstOutput<UPCGUtilsDynMeshSelectionFactoryData>(Run(Settings, {}));
	}

	/** Runs the adapter node over a DynMesh selector and returns the GC selection factory it emits. */
	const UPCGUtilsGeometryCollectionSelectionFactoryData* MeshPredicate(
		const UPCGUtilsDynMeshSelectionFactoryData* MeshSelector,
		EPCGGeometryCollectionMeshPredicateAggregation Aggregation,
		EPCGUtilsGeometryCollectionSurfaceTarget SurfaceTarget =
			EPCGUtilsGeometryCollectionSurfaceTarget::All)
	{
		UPCGGeometryCollectionSelectionFromDynMeshSettings* Settings =
			NewObject<UPCGGeometryCollectionSelectionFromDynMeshSettings>();
		Settings->Aggregation = Aggregation;
		Settings->SurfaceTarget = SurfaceTarget;
		return FirstOutput<UPCGUtilsGeometryCollectionSelectionFactoryData>(Run(Settings, {
			{PCGGeometryCollectionSelectionFromDynMeshConstants::SelectorInputPin, MeshSelector}}));
	}

	TArray<int32> Resolve(
		const UPCGUtilsGeometryCollectionSelectionFactoryData* Factory,
		const UPCGGeometryCollectionData* Collection,
		bool& bOutSucceeded)
	{
		bOutSucceeded = false;
		if (!Factory || !Collection)
		{
			return {};
		}

		const FPCGUtilsGeometryCollectionSelectionEvaluationContext EvaluationContext(
			*Collection, Collection->GetCollection());
		FDataflowTransformSelection Selection;
		bOutSucceeded = Factory->Evaluate(EvaluationContext, nullptr, Selection);
		if (!bOutSucceeded)
		{
			return {};
		}

		TArray<int32> Bones = Selection.AsArrayValidated(Collection->GetCollection());
		Bones.Sort();
		return Bones;
	}

	bool IsSubsetOf(const TArray<int32>& Inner, const TArray<int32>& Outer)
	{
		for (const int32 Bone : Inner)
		{
			if (!Outer.Contains(Bone))
			{
				return false;
			}
		}
		return true;
	}
}

/**
 * The adapter is the one place the DynMesh and Geometry Collection selection domains meet, so what is asserted
 * here is the contract rather than a particular Voronoi result: which bones are even eligible, how Any and All
 * relate, and that the interior/exterior split actually partitions the surface.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPCGUtilsFractureMeshPredicateTest,
	"PCGUtils.Fracture.Selectors.MeshPredicate",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPCGUtilsFractureMeshPredicateTest::RunTest(const FString&)
{
	using namespace PCGUtilsFractureTests;
	using namespace PCGUtilsFractureMeshPredicateTests;

	const UPCGGeometryCollectionData* Fractured = UniformFracture(ToCollection(Box()), 8, 8);
	if (!TestNotNull(TEXT("A fractured collection was produced"), Fractured))
	{
		return false;
	}

	const FGeometryCollection& Collection = Fractured->GetCollection();
	TArray<int32> Pieces;
	PCGUtilsGeometryCollectionHierarchy::GatherPieces(Collection, Pieces);
	if (!TestTrue(TEXT("The fracture produced several pieces"), Pieces.Num() > 2))
	{
		return false;
	}

	const UPCGUtilsDynMeshSelectionFactoryData* Up = NormalSelector(FVector::UpVector);
	if (!TestNotNull(TEXT("The upward normal Selector was authored"), Up))
	{
		return false;
	}

	bool bSucceeded = false;
	const TArray<int32> AnyUp = Resolve(MeshPredicate(
		Up, EPCGGeometryCollectionMeshPredicateAggregation::Any), Fractured, bSucceeded);
	TestTrue(TEXT("Any evaluated successfully"), bSucceeded);
	TestTrue(TEXT("Some piece has an upward-facing face"), AnyUp.Num() > 0);

	// Only pieces are eligible. The root cluster has no geometry of its own, and a cluster's shape is just the
	// union of its pieces - lifting a result to it is GC | Select | Parent's job, not this node's.
	for (const int32 Bone : AnyUp)
	{
		TestTrue(*FString::Printf(TEXT("Selected bone %d is a piece"), Bone), Pieces.Contains(Bone));
	}

	const TArray<int32> AllUp = Resolve(MeshPredicate(
		Up, EPCGGeometryCollectionMeshPredicateAggregation::All), Fractured, bSucceeded);
	TestTrue(TEXT("All evaluated successfully"), bSucceeded);
	TestTrue(TEXT("All is never broader than Any"), IsSubsetOf(AllUp, AnyUp));
	// A fracture piece is a closed solid, so its faces cannot all point the same way.
	TestEqual(TEXT("No closed piece has every face pointing up"), AllUp.Num(), 0);

	// Interior and exterior are complementary views of the same surface, so each is a subset of All and
	// together they must cover it - that is what makes "the pieces whose *original* surface faces up"
	// expressible at all.
	const TArray<int32> AnyUpExterior = Resolve(MeshPredicate(
		Up, EPCGGeometryCollectionMeshPredicateAggregation::Any,
		EPCGUtilsGeometryCollectionSurfaceTarget::Exterior), Fractured, bSucceeded);
	TestTrue(TEXT("Exterior-only evaluated successfully"), bSucceeded);
	const TArray<int32> AnyUpInterior = Resolve(MeshPredicate(
		Up, EPCGGeometryCollectionMeshPredicateAggregation::Any,
		EPCGUtilsGeometryCollectionSurfaceTarget::Interior), Fractured, bSucceeded);
	TestTrue(TEXT("Interior-only evaluated successfully"), bSucceeded);

	TestTrue(TEXT("Exterior-only never exceeds the unrestricted result"), IsSubsetOf(AnyUpExterior, AnyUp));
	TestTrue(TEXT("Interior-only never exceeds the unrestricted result"), IsSubsetOf(AnyUpInterior, AnyUp));
	for (const int32 Bone : AnyUp)
	{
		TestTrue(
			*FString::Printf(TEXT("Bone %d's upward face is either exterior or interior"), Bone),
			AnyUpExterior.Contains(Bone) || AnyUpInterior.Contains(Bone));
	}

	// The box's own top face is the only upward exterior surface, so strictly fewer pieces qualify under
	// Exterior than under the cut faces a uniform fracture scatters in every direction.
	TestTrue(TEXT("Only some pieces retain upward original surface"), AnyUpExterior.Num() < Pieces.Num());

	// No vacuous truth. An unfractured box has no interior surface at all, so restricting the predicate to
	// interior faces leaves nothing eligible - and All over an empty set must still answer false, which is the
	// one case where "every element passed" would otherwise select everything.
	const UPCGGeometryCollectionData* Unfractured = ToCollection(Box());
	if (TestNotNull(TEXT("An unfractured collection was produced"), Unfractured))
	{
		const TArray<int32> AnyInteriorOfSolid = Resolve(MeshPredicate(
			Up, EPCGGeometryCollectionMeshPredicateAggregation::Any,
			EPCGUtilsGeometryCollectionSurfaceTarget::Interior), Unfractured, bSucceeded);
		TestTrue(TEXT("Any over an empty eligible set evaluated successfully"), bSucceeded);
		TestEqual(TEXT("Any selects nothing when no element is eligible"), AnyInteriorOfSolid.Num(), 0);

		const TArray<int32> AllInteriorOfSolid = Resolve(MeshPredicate(
			Up, EPCGGeometryCollectionMeshPredicateAggregation::All,
			EPCGUtilsGeometryCollectionSurfaceTarget::Interior), Unfractured, bSucceeded);
		TestTrue(TEXT("All over an empty eligible set evaluated successfully"), bSucceeded);
		TestEqual(TEXT("All selects nothing when no element is eligible"), AllInteriorOfSolid.Num(), 0);

		// The same solid still answers the unrestricted predicate, so the empty results above are the surface
		// restriction talking rather than the adapter failing to evaluate the piece at all.
		const TArray<int32> AnyUpOfSolid = Resolve(MeshPredicate(
			Up, EPCGGeometryCollectionMeshPredicateAggregation::Any), Unfractured, bSucceeded);
		TestEqual(TEXT("The solid's own top face is still found"), AnyUpOfSolid.Num(), 1);
	}

	// A missing Selector is a graph error, not an empty selection that silently does nothing downstream.
	AddExpectedMessagePlain(
		TEXT("The mesh predicate has no DynMesh Selector to evaluate."), ELogVerbosity::Error);
	UPCGGeometryCollectionSelectionFromDynMeshFactoryData* Orphan =
		NewObject<UPCGGeometryCollectionSelectionFromDynMeshFactoryData>();
	Resolve(Orphan, Fractured, bSucceeded);
	TestFalse(TEXT("A predicate with no Selector fails rather than selecting nothing"), bSucceeded);

	return true;
}

#endif
