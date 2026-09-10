// Copyright Max Harris

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "Elements/Conversion/PCGGeometryCollectionFromAsset.h"
#include "Elements/Fracture/PCGPlanarFracture.h"
#include "Elements/Selections/PCGGeometryCollectionSelectBones.h"
#include "GeometryCollection/GeometryCollection.h"
#include "GeometryCollection/GeometryCollectionObject.h"
#include "FunctionLibraries/PCGUtilsGeometryCollectionHierarchy.h"
#include "Materials/MaterialInterface.h"
#include "Tests/PCGUtilsFractureTestHelpers.h"

namespace PCGUtilsFractureAssetImportTests
{
	using namespace PCGUtilsFractureTests;

	/**
	 * A stand-in Geometry Collection asset, built by copying a collection this module already knows how to
	 * produce. A fresh UGeometryCollection always owns a valid (empty) FGeometryCollection, so there is nothing
	 * to construct beyond the copy.
	 */
	UGeometryCollection* MakeAsset(const UPCGGeometryCollectionData* Source)
	{
		UGeometryCollection* Asset = NewObject<UGeometryCollection>();
		if (!Asset || !Source)
		{
			return nullptr;
		}
		Source->GetCollection().CopyTo(Asset->GetGeometryCollection().Get());
		return Asset;
	}

	const UPCGGeometryCollectionData* Import(UGeometryCollection* Asset, bool bExtractMaterials = true)
	{
		UPCGGeometryCollectionFromAssetSettings* Settings =
			NewObject<UPCGGeometryCollectionFromAssetSettings>();
		Settings->Asset = Asset;
		Settings->bExtractMaterials = bExtractMaterials;
		Settings->bSynchronousLoad = true;
		return FirstOutput<UPCGGeometryCollectionData>(Run(Settings, {}));
	}

	const UPCGUtilsGeometryCollectionSelectionFactoryData* SelectPieces()
	{
		UPCGGeometryCollectionSelectBonesSettings* Settings =
			NewObject<UPCGGeometryCollectionSelectBonesSettings>();
		Settings->Mode = EPCGGeometryCollectionBoneSelectionMode::Pieces;
		return FirstOutput<UPCGUtilsGeometryCollectionSelectionFactoryData>(Run(Settings, {}));
	}

	/**
	 * One slicing plane on X. Deliberately not a Voronoi re-fracture: a grouped Voronoi diagram over already
	 * small cells can legitimately cut nothing, so it cannot carry an assertion.
	 */
	const UPCGGeometryCollectionData* SliceOnce(const UPCGGeometryCollectionData* Collection)
	{
		UPCGSliceFractureSettings* SliceSettings = NewObject<UPCGSliceFractureSettings>();
		SliceSettings->SlicesX = 1;
		SliceSettings->SlicesY = 0;
		SliceSettings->SlicesZ = 0;
		const UPCGUtilsFractureFactoryData* Operation =
			FirstOutput<UPCGUtilsFractureFactoryData>(Run(SliceSettings, {}));
		if (!Operation)
		{
			return nullptr;
		}

		UPCGFractureGeometryCollectionSettings* FractureSettings =
			NewObject<UPCGFractureGeometryCollectionSettings>();
		return FirstOutput<UPCGGeometryCollectionData>(Run(FractureSettings, {
			{PCGFractureGeometryCollectionConstants::CollectionInputPin, Collection},
			{PCGUtilsFractureFactoryConstants::FracturesInputPin, Operation}}));
	}

	int32 AssetBoneCount(const UGeometryCollection* Asset)
	{
		const TSharedPtr<FGeometryCollection, ESPMode::ThreadSafe> Collection =
			Asset ? Asset->GetGeometryCollection() : nullptr;
		return Collection.IsValid() ? Collection->NumElements(FGeometryCollection::TransformGroup) : 0;
	}
}

/**
 * The point of this node is that an immutable asset becomes mutable PCG data without the asset changing, so the
 * assertion that matters is the one made *after* mutating the import.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPCGUtilsFractureAssetImportTest,
	"PCGUtils.Fracture.Conversion.FromAsset",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPCGUtilsFractureAssetImportTest::RunTest(const FString&)
{
	using namespace PCGUtilsFractureTests;
	using namespace PCGUtilsFractureAssetImportTests;

	// A fractured source, so the asset has a real hierarchy rather than a single bone.
	const UPCGGeometryCollectionData* Source = UniformFracture(ToCollection(Box()), 4, 4);
	if (!TestNotNull(TEXT("A source collection was produced"), Source))
	{
		return false;
	}

	UGeometryCollection* Asset = MakeAsset(Source);
	if (!TestNotNull(TEXT("A stand-in asset was built"), Asset))
	{
		return false;
	}
	const int32 AssetBonesBefore = AssetBoneCount(Asset);
	TestTrue(TEXT("The asset holds a hierarchy"), AssetBonesBefore > 2);

	const UPCGGeometryCollectionData* Imported = Import(Asset);
	if (!TestNotNull(TEXT("The asset imported as GC data"), Imported))
	{
		return false;
	}

	TestEqual(TEXT("Every bone came across"),
		Imported->GetCollection().NumElements(FGeometryCollection::TransformGroup), AssetBonesBefore);

	// A new lineage, not a revision of something: nothing upstream produced this collection.
	TestEqual(TEXT("An import starts a fresh lineage"), Imported->GetRevision(), 0);

	{
		TArray<int32> ImportedPieces;
		PCGUtilsGeometryCollectionHierarchy::GatherPieces(Imported->GetCollection(), ImportedPieces);
		TestTrue(TEXT("The import exposes pieces"), ImportedPieces.Num() > 0);
	}

	// --- The contract. Mutate the import destructively, then prove the asset is untouched. Pruning is the
	// sharpest test available: it removes bones *and* reindexes the rest, so any sharing would show up.
	const UPCGGeometryCollectionData* Pruned = Prune(Imported, SelectPieces());
	if (TestNotNull(TEXT("The imported collection can be pruned"), Pruned))
	{
		TestTrue(TEXT("Pruning removed bones from the copy"),
			Pruned->GetCollection().NumElements(FGeometryCollection::TransformGroup) < AssetBonesBefore);
	}
	TestEqual(TEXT("The asset still has its original bone count"), AssetBoneCount(Asset), AssetBonesBefore);
	TestEqual(TEXT("The import itself is also unchanged by the prune"),
		Imported->GetCollection().NumElements(FGeometryCollection::TransformGroup), AssetBonesBefore);

	// Two imports of one asset are independent states, so a selection authored against one is correctly
	// rejected against the other rather than silently applying to the wrong bones.
	const UPCGGeometryCollectionData* SecondImport = Import(Asset);
	if (TestNotNull(TEXT("The asset imported a second time"), SecondImport))
	{
		TestTrue(TEXT("Each import is its own lineage"),
			Imported->GetCollectionId() != SecondImport->GetCollectionId());
		TestTrue(TEXT("Each import is its own state"),
			Imported->GetStateId() != SecondImport->GetStateId());
	}

	// --- An import is fracture-ready, which is the other half of being usable at all. Asserted on a solid,
	// where a single slicing plane must cut.
	{
		UGeometryCollection* SolidAsset = MakeAsset(ToCollection(Box()));
		const UPCGGeometryCollectionData* ImportedSolid = Import(SolidAsset);
		if (TestNotNull(TEXT("A solid asset imported"), ImportedSolid))
		{
			const UPCGGeometryCollectionData* Sliced = SliceOnce(ImportedSolid);
			if (TestNotNull(TEXT("The imported solid can be fractured"), Sliced))
			{
				TestEqual(TEXT("One plane halves the imported solid"), CountPieces(Sliced), 2);
			}
			TestEqual(TEXT("Fracturing the import left the asset alone"),
				AssetBoneCount(SolidAsset),
				ImportedSolid->GetCollection().NumElements(FGeometryCollection::TransformGroup));
		}
	}

	// --- Materials ride along when asked, and are left off when not.
	{
		UGeometryCollection* MaterialAsset = MakeAsset(Source);
		if (TestNotNull(TEXT("A second stand-in asset was built"), MaterialAsset))
		{
			MaterialAsset->Materials.Add(nullptr);
			MaterialAsset->Materials.Add(nullptr);

			const UPCGGeometryCollectionData* WithMaterials = Import(MaterialAsset, /*bExtractMaterials=*/true);
			if (TestNotNull(TEXT("The asset imported with materials"), WithMaterials))
			{
				TestEqual(TEXT("The asset's material list came across"),
					WithMaterials->GetMaterials().Num(), 2);
			}

			const UPCGGeometryCollectionData* WithoutMaterials = Import(MaterialAsset, /*bExtractMaterials=*/false);
			if (TestNotNull(TEXT("The asset imported without materials"), WithoutMaterials))
			{
				TestEqual(TEXT("Materials are left behind when not requested"),
					WithoutMaterials->GetMaterials().Num(), 0);
			}
		}
	}

	// --- An empty asset is a graph error, not an empty collection that fails further downstream.
	{
		UGeometryCollection* EmptyAsset = NewObject<UGeometryCollection>();
		AddExpectedMessagePlain(TEXT("holds no bones, so there is nothing to import"), ELogVerbosity::Error);
		TestNull(TEXT("An empty asset produces no data"), Import(EmptyAsset));
	}

	// --- So is no asset at all.
	{
		UPCGGeometryCollectionFromAssetSettings* Settings =
			NewObject<UPCGGeometryCollectionFromAssetSettings>();
		AddExpectedMessagePlain(
			TEXT("has no Geometry Collection asset selected"), ELogVerbosity::Error);
		TestNull(TEXT("No asset produces no data"),
			FirstOutput<UPCGGeometryCollectionData>(Run(Settings, {})));
	}

	return true;
}

#endif
