// Copyright Max Harris

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "Elements/Selections/PCGGeometryCollectionSelectBones.h"
#include "Elements/Selections/PCGGeometryCollectionSelectContact.h"
#include "Elements/Selections/PCGGeometryCollectionSelectRandom.h"
#include "Elements/Selections/PCGGeometryCollectionSelectSurface.h"
#include "Elements/Selections/PCGGeometryCollectionSelectionHierarchy.h"
#include "Elements/Selections/PCGGeometryCollectionSelectionLogic.h"
#include "Elements/Edit/PCGSeparateGeometryCollectionSelection.h"
#include "FunctionLibraries/PCGUtilsGeometryCollectionHelpers.h"
#include "Tests/PCGUtilsFractureTestHelpers.h"

namespace PCGUtilsFractureSelectorTests
{
	using namespace PCGUtilsFractureTests;

	/** Runs a base selector node and returns its factory. */
	const UPCGUtilsGeometryCollectionSelectionFactoryData* SelectBones(
		EPCGGeometryCollectionBoneSelectionMode Mode, int32 Level = 1)
	{
		UPCGGeometryCollectionSelectBonesSettings* Settings =
			NewObject<UPCGGeometryCollectionSelectBonesSettings>();
		Settings->Mode = Mode;
		Settings->Level = Level;
		return FirstOutput<UPCGUtilsGeometryCollectionSelectionFactoryData>(Run(Settings, {}));
	}

	/** Runs a hierarchy decorator over an upstream selector. */
	const UPCGUtilsGeometryCollectionSelectionFactoryData* Hierarchy(
		const UPCGUtilsGeometryCollectionSelectionFactoryData* Child,
		EPCGGeometryCollectionHierarchyOperation Operation,
		TFunctionRef<void(UPCGGeometryCollectionSelectionHierarchySettings&)> Configure)
	{
		UPCGGeometryCollectionSelectionHierarchySettings* Settings =
			NewObject<UPCGGeometryCollectionSelectionHierarchySettings>();
		Settings->Operation = Operation;
		Configure(*Settings);
		return FirstOutput<UPCGUtilsGeometryCollectionSelectionFactoryData>(Run(Settings, {
			{PCGUtilsGeometryCollectionSelectionFactoryConstants::SelectionInputPin, Child}}));
	}

	const UPCGUtilsGeometryCollectionSelectionFactoryData* Hierarchy(
		const UPCGUtilsGeometryCollectionSelectionFactoryData* Child,
		EPCGGeometryCollectionHierarchyOperation Operation)
	{
		return Hierarchy(Child, Operation, [](UPCGGeometryCollectionSelectionHierarchySettings&) {});
	}

	const UPCGUtilsGeometryCollectionSelectionFactoryData* Logic(
		const UPCGUtilsGeometryCollectionSelectionFactoryData* A,
		const UPCGUtilsGeometryCollectionSelectionFactoryData* B,
		EPCGGeometryCollectionSelectionLogicMode Mode)
	{
		UPCGGeometryCollectionSelectionLogicSettings* Settings =
			NewObject<UPCGGeometryCollectionSelectionLogicSettings>();
		Settings->Mode = Mode;
		return FirstOutput<UPCGUtilsGeometryCollectionSelectionFactoryData>(Run(Settings, {
			{PCGGeometryCollectionSelectionLogicConstants::SelectionAInputPin, A},
			{PCGGeometryCollectionSelectionLogicConstants::SelectionBInputPin, B}}));
	}

	/** Evaluates a selector against a collection and returns the selected bone indices, sorted. */
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
		const TArray<TObjectPtr<const UPCGUtilsGeometryCollectionSelectionFactoryData>> Factories = {Factory};
		bOutSucceeded = PCGUtilsGeometryCollectionSelectionFactories::EvaluateAndUnion(
			Factories, EvaluationContext, nullptr, Selection);
		if (!bOutSucceeded)
		{
			return {};
		}

		TArray<int32> Bones = Selection.AsArrayValidated(Collection->GetCollection());
		Bones.Sort();
		return Bones;
	}

	TArray<int32> Resolve(
		const UPCGUtilsGeometryCollectionSelectionFactoryData* Factory,
		const UPCGGeometryCollectionData* Collection)
	{
		bool bSucceeded = false;
		return Resolve(Factory, Collection, bSucceeded);
	}
}

/**
 * The base selectors are the entry points of every selection graph, and each one has to mean exactly what its
 * name says on a collection whose shape the test knows independently.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPCGUtilsFractureSelectBonesTest,
	"PCGUtils.Fracture.Selectors.BaseSelectors",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPCGUtilsFractureSelectBonesTest::RunTest(const FString&)
{
	using namespace PCGUtilsFractureTests;
	using namespace PCGUtilsFractureSelectorTests;

	const UPCGGeometryCollectionData* Fractured = Fracture(ToCollection(Box()), SiteGrid(3));
	if (!TestNotNull(TEXT("Fractured"), Fractured))
	{
		return false;
	}

	const FGeometryCollection& Collection = Fractured->GetCollection();
	const int32 NumTransforms = Fractured->NumTransforms();
	const int32 NumPieces = CountPieces(Fractured);

	// DynMesh To GC adds one cluster root above the geometry, and fracture turns the bone it cut into another
	// cluster - so the collection is roots + clusters + pieces and every bone is exactly one of them.
	TestEqual(TEXT("All selects every transform"),
		Resolve(SelectBones(EPCGGeometryCollectionBoneSelectionMode::All), Fractured).Num(), NumTransforms);
	TestEqual(TEXT("None selects nothing"),
		Resolve(SelectBones(EPCGGeometryCollectionBoneSelectionMode::None), Fractured).Num(), 0);
	TestEqual(TEXT("Pieces matches the module's own piece count"),
		Resolve(SelectBones(EPCGGeometryCollectionBoneSelectionMode::Pieces), Fractured).Num(), NumPieces);
	{
		UPCGGeometryCollectionSelectBonesSettings* InvertedSettings =
			NewObject<UPCGGeometryCollectionSelectBonesSettings>();
		InvertedSettings->Mode = EPCGGeometryCollectionBoneSelectionMode::Pieces;
		InvertedSettings->bInvertSelection = true;
		const auto* Inverted = FirstOutput<UPCGUtilsGeometryCollectionSelectionFactoryData>(
			Run(InvertedSettings, {}));
		TestEqual(TEXT("The shared GC invert toggle complements a base selector"),
			Resolve(Inverted, Fractured).Num(), NumTransforms - NumPieces);
	}

	const TArray<int32> Roots = Resolve(SelectBones(EPCGGeometryCollectionBoneSelectionMode::Root), Fractured);
	TestEqual(TEXT("A DynMesh-sourced collection has one root"), Roots.Num(), 1);
	if (Roots.Num() == 1)
	{
		TestEqual(TEXT("The root has no parent"), Collection.Parent[Roots[0]], int32(INDEX_NONE));
	}

	const TArray<int32> Clusters =
		Resolve(SelectBones(EPCGGeometryCollectionBoneSelectionMode::Clusters), Fractured);
	TestTrue(TEXT("Fracture produced at least one cluster"), Clusters.Num() > 0);

	// Every bone is a piece or a cluster, and never both. This is the invariant the whole vocabulary rests on.
	{
		const TSet<int32> PieceSet(Resolve(SelectBones(EPCGGeometryCollectionBoneSelectionMode::Pieces), Fractured));
		const TSet<int32> ClusterSet(Clusters);
		TestEqual(TEXT("Pieces and clusters do not overlap"), PieceSet.Intersect(ClusterSet).Num(), 0);
		TestEqual(TEXT("Pieces and clusters account for every bone"),
			PieceSet.Num() + ClusterSet.Num(), NumTransforms);
	}

	// Level 0 is the root; level 1 is what it contains.
	const TArray<int32> Level0 =
		Resolve(SelectBones(EPCGGeometryCollectionBoneSelectionMode::AtLevel, 0), Fractured);
	TestEqual(TEXT("Level 0 is the root"), Level0, Roots);

	const TArray<int32> Level1 =
		Resolve(SelectBones(EPCGGeometryCollectionBoneSelectionMode::AtLevel, 1), Fractured);
	TestTrue(TEXT("Level 1 is non-empty"), Level1.Num() > 0);
	for (const int32 Bone : Level1)
	{
		TestEqual(TEXT("A level 1 bone's parent is the root"), Collection.Parent[Bone], Roots[0]);
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPCGUtilsFractureSurfaceAndRandomSelectorsTest,
	"PCGUtils.Fracture.Selectors.SurfaceAndRandom",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPCGUtilsFractureSurfaceAndRandomSelectorsTest::RunTest(const FString&)
{
	using namespace PCGUtilsFractureTests;
	using namespace PCGUtilsFractureSelectorTests;

	const UPCGGeometryCollectionData* Fractured = Fracture(ToCollection(Box()), SiteGrid(3));
	if (!TestNotNull(TEXT("Fractured"), Fractured)) { return false; }
	const int32 NumPieces = CountPieces(Fractured);

	auto MakeSurface = [](EPCGGeometryCollectionSurfaceClass SurfaceClass,
		EPCGGeometryCollectionSurfaceMatch Match)
	{
		auto* Settings = NewObject<UPCGGeometryCollectionSelectSurfaceSettings>();
		Settings->SurfaceClass = SurfaceClass;
		Settings->Match = Match;
		return FirstOutput<UPCGUtilsGeometryCollectionSelectionFactoryData>(Run(Settings, {}));
	};
	const TArray<int32> WithExterior = Resolve(MakeSurface(
		EPCGGeometryCollectionSurfaceClass::Exterior,
		EPCGGeometryCollectionSurfaceMatch::AnyFace), Fractured);
	const TArray<int32> WithInterior = Resolve(MakeSurface(
		EPCGGeometryCollectionSurfaceClass::Interior,
		EPCGGeometryCollectionSurfaceMatch::AnyFace), Fractured);
	TestTrue(TEXT("At least one fractured piece retains exterior faces"), WithExterior.Num() > 0);
	TestTrue(TEXT("At least one fractured piece has interior faces"), WithInterior.Num() > 0);

	auto* RandomSettings = NewObject<UPCGGeometryCollectionSelectRandomSettings>();
	RandomSettings->Mode = EPCGGeometryCollectionRandomSelectionMode::BoneCount;
	RandomSettings->BoneCount = 2;
	RandomSettings->RandomSeed = 1234;
	const auto* Random = FirstOutput<UPCGUtilsGeometryCollectionSelectionFactoryData>(Run(RandomSettings, {}));
	const TArray<int32> FirstRandom = Resolve(Random, Fractured);
	const TArray<int32> SecondRandom = Resolve(Random, Fractured);
	TestEqual(TEXT("Random Bone Count selects the requested number"), FirstRandom.Num(), FMath::Min(2, NumPieces));
	TestEqual(TEXT("Random selection is deterministic for one seed"), FirstRandom, SecondRandom);

	RandomSettings->bInvertSelection = true;
	const auto* InvertedRandom = FirstOutput<UPCGUtilsGeometryCollectionSelectionFactoryData>(Run(RandomSettings, {}));
	TestEqual(TEXT("Random inversion stays within its piece candidate domain"),
		Resolve(InvertedRandom, Fractured).Num(), NumPieces - FirstRandom.Num());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPCGUtilsSeparateGeometryCollectionSelectionTest,
	"PCGUtils.Fracture.Edit.SeparateSelection",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPCGUtilsSeparateGeometryCollectionSelectionTest::RunTest(const FString&)
{
	using namespace PCGUtilsFractureTests;
	using namespace PCGUtilsFractureSelectorTests;

	const UPCGGeometryCollectionData* Fractured = Fracture(ToCollection(Box()), SiteGrid(3));
	if (!TestNotNull(TEXT("Fractured"), Fractured)) { return false; }
	auto* RandomSettings = NewObject<UPCGGeometryCollectionSelectRandomSettings>();
	RandomSettings->Mode = EPCGGeometryCollectionRandomSelectionMode::BoneCount;
	RandomSettings->BoneCount = 1;
	const auto* OnePiece = FirstOutput<UPCGUtilsGeometryCollectionSelectionFactoryData>(Run(RandomSettings, {}));

	auto* Separate = NewObject<UPCGSeparateGeometryCollectionSelectionSettings>();
	const TArray<FPCGTaggedData> Outputs = Run(Separate, {
		{PCGSeparateGeometryCollectionSelectionConstants::CollectionInputPin, Fractured},
		{PCGSeparateGeometryCollectionSelectionConstants::SelectionInputPin, OnePiece}});
	const UPCGGeometryCollectionData* Selected = nullptr;
	const UPCGGeometryCollectionData* Unselected = nullptr;
	for (const FPCGTaggedData& Output : Outputs)
	{
		if (Output.Pin == PCGSeparateGeometryCollectionSelectionConstants::SelectedOutputPin)
		{
			Selected = Cast<const UPCGGeometryCollectionData>(Output.Data);
		}
		else if (Output.Pin == PCGSeparateGeometryCollectionSelectionConstants::UnselectedOutputPin)
		{
			Unselected = Cast<const UPCGGeometryCollectionData>(Output.Data);
		}
	}
	if (!TestNotNull(TEXT("Selected GC output"), Selected) ||
		!TestNotNull(TEXT("Unselected GC output"), Unselected))
	{
		return false;
	}
	TestEqual(TEXT("Selected output contains one piece"), CountPieces(Selected), 1);
	TestEqual(TEXT("Unselected output contains the complementary pieces"),
		CountPieces(Unselected), CountPieces(Fractured) - 1);
	return true;
}

/**
 * The decorators are the reason geometric selectors do not need to know about the hierarchy, so each one is
 * checked against the parent/child arrays directly rather than against another decorator.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPCGUtilsFractureHierarchyDecoratorTest,
	"PCGUtils.Fracture.Selectors.HierarchyDecorators",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPCGUtilsFractureHierarchyDecoratorTest::RunTest(const FString&)
{
	using namespace PCGUtilsFractureTests;
	using namespace PCGUtilsFractureSelectorTests;
	using EOperation = EPCGGeometryCollectionHierarchyOperation;

	const UPCGGeometryCollectionData* Fractured = Fracture(ToCollection(Box()), SiteGrid(3));
	if (!TestNotNull(TEXT("Fractured"), Fractured))
	{
		return false;
	}

	const FGeometryCollection& Collection = Fractured->GetCollection();
	const UPCGUtilsGeometryCollectionSelectionFactoryData* Pieces =
		SelectBones(EPCGGeometryCollectionBoneSelectionMode::Pieces);
	const TArray<int32> PieceBones = Resolve(Pieces, Fractured);
	if (!TestTrue(TEXT("There are pieces to work from"), PieceBones.Num() > 1))
	{
		return false;
	}

	// Parent: every piece's cluster, and nothing else.
	{
		const TArray<int32> Parents = Resolve(Hierarchy(Pieces, EOperation::Parent), Fractured);
		TSet<int32> Expected;
		for (const int32 Piece : PieceBones)
		{
			if (Collection.Parent[Piece] != INDEX_NONE)
			{
				Expected.Add(Collection.Parent[Piece]);
			}
		}
		TestEqual(TEXT("Parent selects each piece's cluster"), Parents.Num(), Expected.Num());
		TestFalse(TEXT("Parent replaces rather than grows the selection"),
			Parents.Contains(PieceBones[0]) && Expected.Contains(PieceBones[0]) == false);
	}

	// Include Original is what turns replacement into growth.
	{
		const TArray<int32> Grown = Resolve(
			Hierarchy(Pieces, EOperation::Parent,
				[](UPCGGeometryCollectionSelectionHierarchySettings& S) { S.bIncludeOriginal = true; }),
			Fractured);
		for (const int32 Piece : PieceBones)
		{
			if (!Grown.Contains(Piece))
			{
				AddError(TEXT("Include Original dropped a bone from the input selection"));
				break;
			}
		}
	}

	// Parent in All Children mode: a cluster qualifies only when every child was selected. Selecting all
	// pieces satisfies that for any cluster whose children are all pieces; dropping one piece must remove
	// that piece's cluster from the result.
	{
		const TArray<int32> AllChildren = Resolve(
			Hierarchy(Pieces, EOperation::Parent,
				[](UPCGGeometryCollectionSelectionHierarchySettings& S)
				{ S.ParentMode = EPCGGeometryCollectionParentMode::AllChildren; }),
			Fractured);
		const TArray<int32> AnyChild = Resolve(Hierarchy(Pieces, EOperation::Parent), Fractured);

		TestTrue(TEXT("All Children never selects more than Any Child"),
			AllChildren.Num() <= AnyChild.Num());

		// With every piece selected, the two agree on any cluster whose children are all pieces.
		for (const int32 Cluster : AnyChild)
		{
			bool bAllChildrenArePieces = true;
			for (const int32 Child : Collection.Children[Cluster])
			{
				bAllChildrenArePieces &= PCGUtilsGeometryCollectionHierarchy::IsPiece(Collection, Child);
			}
			if (bAllChildrenArePieces && !AllChildren.Contains(Cluster))
			{
				AddError(TEXT("All Children rejected a cluster whose children were all selected pieces"));
				break;
			}
		}
	}

	// Children of the root are exactly the root's children.
	{
		const UPCGUtilsGeometryCollectionSelectionFactoryData* Root =
			SelectBones(EPCGGeometryCollectionBoneSelectionMode::Root);
		const TArray<int32> RootBones = Resolve(Root, Fractured);
		const TArray<int32> Children = Resolve(Hierarchy(Root, EOperation::Children), Fractured);

		TArray<int32> Expected = Collection.Children[RootBones[0]].Array();
		Expected.Sort();
		TestEqual(TEXT("Children of the root are the root's children"), Children, Expected);
	}

	// Siblings includes the queried bone, matching the facade and Fracture Mode.
	{
		const TArray<int32> Siblings = Resolve(Hierarchy(Pieces, EOperation::Siblings), Fractured);
		TestTrue(TEXT("Siblings of every piece includes those pieces"),
			Siblings.Num() >= PieceBones.Num());
	}

	// Ancestors of a piece walk to the root and never include the piece itself.
	{
		const TArray<int32> Ancestors = Resolve(Hierarchy(Pieces, EOperation::Ancestors), Fractured);
		const TArray<int32> Roots = Resolve(SelectBones(EPCGGeometryCollectionBoneSelectionMode::Root), Fractured);
		TestTrue(TEXT("Ancestors reaches the root"), Ancestors.Contains(Roots[0]));
		TestFalse(TEXT("Ancestors excludes the bones it started from"), Ancestors.Contains(PieceBones[0]));
	}

	// Descendants of the root is everything else; with Pieces Only it is exactly the pieces.
	{
		const UPCGUtilsGeometryCollectionSelectionFactoryData* Root =
			SelectBones(EPCGGeometryCollectionBoneSelectionMode::Root);
		const TArray<int32> All = Resolve(Hierarchy(Root, EOperation::Descendants), Fractured);
		TestEqual(TEXT("Descendants of the root is every other bone"),
			All.Num(), Fractured->NumTransforms() - 1);

		const TArray<int32> OnlyPieces = Resolve(
			Hierarchy(Root, EOperation::Descendants,
				[](UPCGGeometryCollectionSelectionHierarchySettings& S) { S.bPiecesOnly = true; }),
			Fractured);
		TestEqual(TEXT("Pieces Only descendants are exactly the pieces"), OnlyPieces, PieceBones);
	}

	// To Pieces on the root resolves the whole collection down to what actually carries geometry.
	{
		const TArray<int32> ToPieces = Resolve(
			Hierarchy(SelectBones(EPCGGeometryCollectionBoneSelectionMode::Root), EOperation::ToPieces),
			Fractured);
		TestEqual(TEXT("To Pieces from the root gives every piece"), ToPieces, PieceBones);
	}

	// To Clusters lifts pieces to their containing clusters.
	{
		const TArray<int32> ToClusters = Resolve(Hierarchy(Pieces, EOperation::ToClusters), Fractured);
		for (const int32 Bone : ToClusters)
		{
			if (!PCGUtilsGeometryCollectionHierarchy::IsCluster(Collection, Bone))
			{
				AddError(TEXT("To Clusters produced a bone that is not a cluster"));
				break;
			}
		}
		TestTrue(TEXT("To Clusters produced something"), ToClusters.Num() > 0);
	}

	// Same Level of the root is every bone at level 0.
	{
		const TArray<int32> SameLevel = Resolve(
			Hierarchy(SelectBones(EPCGGeometryCollectionBoneSelectionMode::Root), EOperation::SameLevel),
			Fractured);
		const TArray<int32> AtLevel0 =
			Resolve(SelectBones(EPCGGeometryCollectionBoneSelectionMode::AtLevel, 0), Fractured);
		TestEqual(TEXT("Same Level from the root equals level 0"), SameLevel, AtLevel0);
	}

	// To Level lifts every piece to its ancestor at level 1.
	{
		const TArray<int32> ToLevel = Resolve(
			Hierarchy(Pieces, EOperation::ToLevel,
				[](UPCGGeometryCollectionSelectionHierarchySettings& S) { S.Level = 1; }),
			Fractured);
		for (const int32 Bone : ToLevel)
		{
			TestEqual(TEXT("To Level produces bones at the requested level"),
				PCGUtilsGeometryCollectionHierarchy::GetLevel(Collection, Bone), 1);
		}
		TestTrue(TEXT("To Level produced something"), ToLevel.Num() > 0);
	}

	// Invert over all bones is the complement; over pieces it is the unselected pieces.
	{
		const TArray<int32> Inverted = Resolve(Hierarchy(Pieces, EOperation::Invert), Fractured);
		TestEqual(TEXT("Inverting the pieces leaves the non-pieces"),
			Inverted.Num(), Fractured->NumTransforms() - PieceBones.Num());

		const TArray<int32> InvertedPieces = Resolve(
			Hierarchy(Pieces, EOperation::Invert,
				[](UPCGGeometryCollectionSelectionHierarchySettings& S)
				{ S.InvertDomain = EPCGGeometryCollectionInvertDomain::PiecesOnly; }),
			Fractured);
		TestEqual(TEXT("Inverting every piece within the pieces domain leaves nothing"),
			InvertedPieces.Num(), 0);
	}

	return true;
}

/** Set algebra has to behave like set algebra, including the asymmetry of Subtract. */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPCGUtilsFractureSelectionLogicTest,
	"PCGUtils.Fracture.Selectors.SelectionLogic",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPCGUtilsFractureSelectionLogicTest::RunTest(const FString&)
{
	using namespace PCGUtilsFractureTests;
	using namespace PCGUtilsFractureSelectorTests;

	const UPCGGeometryCollectionData* Fractured = Fracture(ToCollection(Box()), SiteGrid(3));
	if (!TestNotNull(TEXT("Fractured"), Fractured))
	{
		return false;
	}

	const UPCGUtilsGeometryCollectionSelectionFactoryData* All =
		SelectBones(EPCGGeometryCollectionBoneSelectionMode::All);
	const UPCGUtilsGeometryCollectionSelectionFactoryData* Pieces =
		SelectBones(EPCGGeometryCollectionBoneSelectionMode::Pieces);
	const UPCGUtilsGeometryCollectionSelectionFactoryData* Clusters =
		SelectBones(EPCGGeometryCollectionBoneSelectionMode::Clusters);

	const TArray<int32> AllBones = Resolve(All, Fractured);
	const TArray<int32> PieceBones = Resolve(Pieces, Fractured);
	const TArray<int32> ClusterBones = Resolve(Clusters, Fractured);

	using EMode = EPCGGeometryCollectionSelectionLogicMode;

	TestEqual(TEXT("Pieces AND All is the pieces"),
		Resolve(Logic(Pieces, All, EMode::And), Fractured), PieceBones);
	TestEqual(TEXT("Pieces AND Clusters is empty"),
		Resolve(Logic(Pieces, Clusters, EMode::And), Fractured).Num(), 0);
	TestEqual(TEXT("Pieces OR Clusters is every bone"),
		Resolve(Logic(Pieces, Clusters, EMode::Or), Fractured), AllBones);
	TestEqual(TEXT("Pieces XOR Clusters is every bone when they are disjoint"),
		Resolve(Logic(Pieces, Clusters, EMode::Xor), Fractured), AllBones);
	TestEqual(TEXT("All minus Pieces is the clusters"),
		Resolve(Logic(All, Pieces, EMode::Subtract), Fractured), ClusterBones);

	// Subtract is the one operation where the order of the inputs matters, so it is worth stating.
	TestEqual(TEXT("Pieces minus All is empty"),
		Resolve(Logic(Pieces, All, EMode::Subtract), Fractured).Num(), 0);

	return true;
}

/**
 * Contact is the spatial member of the family. Its defining property is that it grows a selection only into
 * bones that genuinely touch, which the adjacency graph the module already emits can be checked against.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPCGUtilsFractureSelectContactTest,
	"PCGUtils.Fracture.Selectors.Contact",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPCGUtilsFractureSelectContactTest::RunTest(const FString&)
{
	using namespace PCGUtilsFractureTests;
	using namespace PCGUtilsFractureSelectorTests;

	// 3x3x3 gives a piece with neighbours in every direction and a clear notion of "further away".
	const UPCGGeometryCollectionData* Fractured = Fracture(ToCollection(Box()), SiteGrid(3));
	if (!TestNotNull(TEXT("Fractured"), Fractured))
	{
		return false;
	}

	const FGeometryCollection& Collection = Fractured->GetCollection();
	const TArray<int32> PieceBones =
		Resolve(SelectBones(EPCGGeometryCollectionBoneSelectionMode::Pieces), Fractured);
	if (!TestTrue(TEXT("Enough pieces to have neighbours"), PieceBones.Num() > 4))
	{
		return false;
	}

	// The adjacency the module already computes, as the independent answer to compare against.
	TArray<PCGUtilsGeometryCollectionHelpers::FBoneAdjacencyEdge> Edges;
	if (!TestTrue(TEXT("Adjacency computed"),
		PCGUtilsGeometryCollectionHelpers::BuildBoneAdjacency(Collection, /*bComputeContact=*/false, Edges)))
	{
		return false;
	}

	const int32 SeedBone = PieceBones[0];
	TSet<int32> ExpectedNeighbors;
	for (const PCGUtilsGeometryCollectionHelpers::FBoneAdjacencyEdge& Edge : Edges)
	{
		if (Edge.BoneA == SeedBone) { ExpectedNeighbors.Add(Edge.BoneB); }
		else if (Edge.BoneB == SeedBone) { ExpectedNeighbors.Add(Edge.BoneA); }
	}
	if (!TestTrue(TEXT("The seed piece touches something"), ExpectedNeighbors.Num() > 0))
	{
		return false;
	}

	TArray<int32> Neighbors;
	if (!TestTrue(TEXT("Contact neighbours computed"),
		PCGUtilsGeometryCollectionHelpers::GatherContactNeighbors(
			Collection, {SeedBone}, /*bIncludeNeighborsInParentLevels=*/true, /*InIterations=*/1, Neighbors)))
	{
		return false;
	}

	TArray<int32> ExpectedSorted = ExpectedNeighbors.Array();
	ExpectedSorted.Sort();
	TestEqual(TEXT("Contact matches the adjacency graph"), Neighbors, ExpectedSorted);
	TestFalse(TEXT("Contact does not report the bone it started from"), Neighbors.Contains(SeedBone));

	// A second step must reach at least as far, and on a 3x3x3 lattice strictly further.
	TArray<int32> TwoSteps;
	PCGUtilsGeometryCollectionHelpers::GatherContactNeighbors(
		Collection, {SeedBone}, true, /*InIterations=*/2, TwoSteps);
	TestTrue(TEXT("Two steps reach further than one"), TwoSteps.Num() > Neighbors.Num());
	for (const int32 Bone : Neighbors)
	{
		if (!TwoSteps.Contains(Bone))
		{
			AddError(TEXT("A second contact step lost a bone the first step had found"));
			break;
		}
	}

	// The node grows the selection by default, which is what Fracture Mode's Contact button does.
	{
		UPCGGeometryCollectionSelectContactSettings* Settings =
			NewObject<UPCGGeometryCollectionSelectContactSettings>();
		TestTrue(TEXT("Select Contact keeps the original selection by default"), Settings->bIncludeOriginal);
	}

	return true;
}

/**
 * The composition the whole architecture exists to support: a per-piece test lifted to the clusters that
 * matched it completely, with no hierarchy knowledge anywhere in the predicate.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPCGUtilsFractureSelectorCompositionTest,
	"PCGUtils.Fracture.Selectors.Composition",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPCGUtilsFractureSelectorCompositionTest::RunTest(const FString&)
{
	using namespace PCGUtilsFractureTests;
	using namespace PCGUtilsFractureSelectorTests;
	using EOperation = EPCGGeometryCollectionHierarchyOperation;
	using EMode = EPCGGeometryCollectionSelectionLogicMode;

	const UPCGGeometryCollectionData* Fractured = Fracture(ToCollection(Box()), SiteGrid(3));
	if (!TestNotNull(TEXT("Fractured"), Fractured))
	{
		return false;
	}

	// Pieces -> Parent(All Children) -> Descendants(Pieces Only) must land back on the pieces of exactly
	// those clusters, which for a fully-selected input is every piece again. A chain that silently dropped or
	// duplicated bones would not round-trip.
	const UPCGUtilsGeometryCollectionSelectionFactoryData* Pieces =
		SelectBones(EPCGGeometryCollectionBoneSelectionMode::Pieces);
	const TArray<int32> PieceBones = Resolve(Pieces, Fractured);

	const UPCGUtilsGeometryCollectionSelectionFactoryData* FullClusters =
		Hierarchy(Pieces, EOperation::Parent,
			[](UPCGGeometryCollectionSelectionHierarchySettings& S)
			{ S.ParentMode = EPCGGeometryCollectionParentMode::AllChildren; });

	const TArray<int32> BackToPieces = Resolve(
		Hierarchy(FullClusters, EOperation::Descendants,
			[](UPCGGeometryCollectionSelectionHierarchySettings& S) { S.bPiecesOnly = true; }),
		Fractured);

	TestEqual(TEXT("Pieces -> full clusters -> pieces round-trips"), BackToPieces, PieceBones);

	// And the difference form: everything except one cluster's worth of pieces.
	{
		const UPCGUtilsGeometryCollectionSelectionFactoryData* OneCluster =
			SelectBones(EPCGGeometryCollectionBoneSelectionMode::AtLevel, 1);
		const UPCGUtilsGeometryCollectionSelectionFactoryData* ThosePieces =
			Hierarchy(OneCluster, EOperation::ToPieces);

		const TArray<int32> Remaining = Resolve(Logic(Pieces, ThosePieces, EMode::Subtract), Fractured);
		const TArray<int32> Removed = Resolve(ThosePieces, Fractured);
		TestEqual(TEXT("Subtracting a level's pieces removes exactly those"),
			Remaining.Num() + Removed.Num(), PieceBones.Num());
	}

	return true;
}

#endif // WITH_AUTOMATION_TESTS
