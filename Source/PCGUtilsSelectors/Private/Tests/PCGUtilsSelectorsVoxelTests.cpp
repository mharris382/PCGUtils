// Copyright Max Harris
//
// A point selector as a Voxel Selector, through Voxel | Apply Patterns: the In Bounds versions of the selector
// checks in PCGUtils.Voxel.Patterns (containment, invert, scope, empty cells for Add patterns), and the
// Selector | In Bounds node itself for every domain.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "Elements/PCGSelectorInBounds.h"
#include "Elements/PCGVoxelApplyPatterns.h"
#include "Elements/PCGVoxelPattern.h"
#include "Helpers/PCGUtilsDataDomainHelpers.h"
#include "Metadata/PCGMetadata.h"
#include "PCGUtilsSelectorsTestHelpers.h"
#include "Voxel/PCGVoxelGrid.h"
#include "Voxel/PCGVoxelPatterns.h"

namespace PCGUtilsSelectorsVoxelTests
{
	using namespace PCGUtilsSelectorsTests;

	/** A voxel set: one point per cell, cells 100 across, @Data.VoxelSize set. Cell (x, y, z) is centred at 100 * (x, y, z). */
	UPCGPointArrayData* MakeVoxels(const TArray<FIntVector>& Cells)
	{
		UPCGPointArrayData* Points = NewObject<UPCGPointArrayData>();
		Points->SetNumPoints(Cells.Num());
		Points->AllocateProperties(EPCGPointNativeProperties::All);
		if (FPCGMetadataDomain* Data = PCGUtilsDataDomain::GetMutableDataDomain(Points))
		{
			PCGUtilsDataDomain::WriteValue(*Data, TEXT("VoxelSize"), FVector(100.0));
		}
		auto Transforms = Points->GetTransformValueRange();
		auto Mins = Points->GetBoundsMinValueRange();
		auto Maxs = Points->GetBoundsMaxValueRange();
		for (int32 Index = 0; Index < Cells.Num(); ++Index)
		{
			Transforms[Index] = FTransform(FVector(Cells[Index]) * 100.0);
			Mins[Index] = FVector(-50.0);
			Maxs[Index] = FVector(50.0);
		}
		return Points;
	}

	FPCGVoxelPattern Strip(const FIntVector& Span)
	{
		FPCGVoxelPattern Pattern;
		Pattern.Boxes[0].Span = Span;
		Pattern.Turns = EPCGVoxelRuleTurns::Exact;
		return Pattern;
	}

	/** Runs Voxel | Apply Patterns with one pattern limited by Selector, and returns how many chunks came out. */
	int32 CountChunks(const TArray<FIntVector>& Cells, const FPCGVoxelPattern& Pattern, const UPCGVoxelSelectorData* Selector,
		EPCGVoxelPatternSelectorScope Scope = EPCGVoxelPatternSelectorScope::Cells)
	{
		UPCGVoxelPatternData* PatternData = NewObject<UPCGVoxelPatternData>();
		PatternData->Pattern = Pattern;
		PatternData->Selectors.Add(Selector);
		PatternData->SelectorScope = Scope;

		UPCGVoxelApplyPatternsSettings* Settings = NewObject<UPCGVoxelApplyPatternsSettings>();
		for (const FPCGTaggedData& Output : Run(Settings, {
			{ PCGPinConstants::DefaultInputLabel, MakeVoxels(Cells) },
			{ PCGVoxelPatternPins::PatternsLabel, PatternData } }))
		{
			if (Output.Pin != TEXT("Unmatched"))
			{
				if (const UPCGBasePointData* Out = Cast<UPCGBasePointData>(Output.Data))
				{
					return Out->GetNumPoints();
				}
			}
		}
		return 0;
	}

	FPCGVoxelPointConversion CellConversion(EPCGVoxelCellPoints CellPoints)
	{
		FPCGVoxelPointConversion Out;
		Out.CellPoints = CellPoints;
		return Out;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPCGUtilsSelectorsVoxelTest,
	"PCGUtils.Selectors.Voxel", PCGUtilsSelectorsTests::Flags)

bool FPCGUtilsSelectorsVoxelTest::RunTest(const FString&)
{
	using namespace PCGUtilsSelectorsVoxelTests;
	using EContain = EPCGUtilsSelectorContainment;
	const int32 BuildsBefore = FPCGUtilsElementPoints::GetTotalPointDataBuildCount();

	// Four cells in a row, centres at X 0, 100, 200, 300. A pattern only takes the cells its selector selects.
	TArray<FIntVector> Row;
	for (int32 X = 0; X < 4; ++X)
	{
		Row.Add(FIntVector(X, 0, 0));
	}
	const FPCGVoxelPattern Single = Strip(FIntVector(1));
	const FVector Center(50.0, 0.0, 0.0);
	const FVector Wide(110.0, 60.0, 60.0);

	// Center With Bounds (the default): a cell is its centre and its box, so Containment decides.
	TestTrue(TEXT("Default conversion is Center With Bounds"), FPCGVoxelPointConversion().CellPoints == EPCGVoxelCellPoints::CenterWithBounds);
	TestEqual(TEXT("Centre: the box from -60 to 160 holds two cell centres"), CountChunks(Row, Single, WrapForVoxel(MakeInBounds(Center, Wide, EContain::Center))), 2);
	TestEqual(TEXT("Any Part: it also touches the third cell"), CountChunks(Row, Single, WrapForVoxel(MakeInBounds(Center, Wide, EContain::AnyPart))), 3);
	TestEqual(TEXT("Fully Inside: the box from -60 to 140 holds one whole cell"),
		CountChunks(Row, Single, WrapForVoxel(MakeInBounds(FVector(40.0, 0.0, 0.0), FVector(100.0, 60.0, 60.0), EContain::FullyInside))), 1);
	TestEqual(TEXT("Invert: the other two cells"),
		CountChunks(Row, Single, WrapForVoxel(MakeInBounds(Center, Wide, EContain::Center), FPCGVoxelPointConversion(), true)), 2);

	// Center: the cell is only its centre, so Any Part no longer reaches the third cell.
	TestEqual(TEXT("Center only, Any Part: two cells"),
		CountChunks(Row, Single, WrapForVoxel(MakeInBounds(Center, Wide, EContain::AnyPart), CellConversion(EPCGVoxelCellPoints::Center))), 2);

	// Scope: a two-cell strip that starts inside the box but reaches outside it.
	const FPCGVoxelPattern Pair = Strip(FIntVector(2, 1, 1));
	TestEqual(TEXT("Cells: a strip reaching out of the selection is not made"),
		CountChunks(Row, Pair, WrapForVoxel(MakeInBounds(FVector::ZeroVector, FVector(60.0), EContain::Center)), EPCGVoxelPatternSelectorScope::Cells), 0);
	TestEqual(TEXT("Origin: only the starting cell has to be selected"),
		CountChunks(Row, Pair, WrapForVoxel(MakeInBounds(FVector::ZeroVector, FVector(60.0), EContain::Center)), EPCGVoxelPatternSelectorScope::Origin), 1);

	// An Add pattern asks about EMPTY cells. An L of three cells, two layers: the empty inside corner is (1,1,*).
	TArray<FIntVector> Corner;
	for (int32 Z = 0; Z < 2; ++Z)
	{
		Corner.Append({ FIntVector(0, 0, Z), FIntVector(1, 0, Z), FIntVector(0, 1, Z) });
	}
	const FPCGVoxelPattern Fill = PCGVoxelPatterns::Presets::InsideCornerFill();
	TestEqual(TEXT("Add: a box over everything lets the empty corner be filled"),
		CountChunks(Corner, Fill, WrapForVoxel(MakeInBounds(FVector(50.0), FVector(300.0), EContain::Center))), 1);
	// Inverted, a thin box up the corner column selects every cell but the empty corner cells.
	const UPCGVoxelSelectorData* NotTheCorner = WrapForVoxel(MakeInBounds(FVector(100.0, 100.0, 50.0), FVector(40.0, 40.0, 300.0), EContain::Center), FPCGVoxelPointConversion(), true);
	TestEqual(TEXT("Add, Cells scope: an unselected empty cell is not filled"), CountChunks(Corner, Fill, NotTheCorner), 0);
	TestEqual(TEXT("Add, Origin scope: only the filled cell the pattern starts on has to be selected"),
		CountChunks(Corner, Fill, NotTheCorner, EPCGVoxelPatternSelectorScope::Origin), 1);

	// The wrapper answers for any coordinate, straight from the operation.
	{
		FPCGVoxelGrid Grid;
		Grid.PointFirst = Row;
		Grid.PointSpan.Init(FIntVector(1), Row.Num());
		Grid.PointUsed.Init(true, Row.Num());
		Grid.BuildCells();
		const UPCGVoxelPointSelectorData* Wrapper = WrapForVoxel(MakeInBounds(FVector(0.0, 100.0, 0.0), FVector(60.0), EContain::Center));
		const TSharedPtr<FPCGVoxelSelectorOperation> Operation = Wrapper->CreateOperation(nullptr, Grid);
		if (TestTrue(TEXT("The wrapper prepares against a grid"), Operation.IsValid()))
		{
			TestFalse(TEXT("A filled cell outside the box"), Operation->Test(FIntVector(0, 0, 0)));
			TestTrue(TEXT("An empty cell inside the box"), Operation->Test(FIntVector(0, 1, 0)));
			TestTrue(TEXT("Asked again: the same answer"), Operation->Test(FIntVector(0, 1, 0)));
			TestFalse(TEXT("An empty cell far away"), Operation->Test(FIntVector(-40, 7, 3)));
		}
	}

	TestEqual(TEXT("In Bounds never made point data"), FPCGUtilsElementPoints::GetTotalPointDataBuildCount(), BuildsBefore);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPCGUtilsSelectorsNodeTest,
	"PCGUtils.Selectors.Node", PCGUtilsSelectorsTests::Flags)

bool FPCGUtilsSelectorsNodeTest::RunTest(const FString&)
{
	using namespace PCGUtilsSelectorsVoxelTests;
	const UPCGPointArrayData* Bounds = MakeBoxPoint(FVector(50.0, 0.0, 0.0), FVector(110.0, 60.0, 60.0));

	auto RunNode = [&](EPCGUtilsSelectorDomain Domain, TFunctionRef<void(UPCGSelectorInBoundsSettings&)> Edit) -> FPCGTaggedData
	{
		UPCGSelectorInBoundsSettings* Settings = NewObject<UPCGSelectorInBoundsSettings>();
		Settings->Domain = Domain;
		Edit(*Settings);
		const TArray<FPCGTaggedData> Outputs = Run(Settings, { { PCGSelectorInBoundsPins::BoundsLabel, Bounds } });
		return Outputs.IsEmpty() ? FPCGTaggedData() : Outputs[0];
	};
	auto OutputPin = [](EPCGUtilsSelectorDomain Domain) -> FPCGPinProperties
	{
		UPCGSelectorInBoundsSettings* Settings = NewObject<UPCGSelectorInBoundsSettings>();
		Settings->Domain = Domain;
		const TArray<FPCGPinProperties> Pins = Settings->AllOutputPinProperties();
		return Pins.IsEmpty() ? FPCGPinProperties(NAME_None, FPCGDataTypeIdentifier()) : Pins[0];
	};

	// Saved nodes keep loading as they were: Voxel is the default domain.
	TestTrue(TEXT("Domain defaults to Voxel"), GetDefault<UPCGSelectorInBoundsSettings>()->Domain == EPCGUtilsSelectorDomain::Voxel);

	// The output pin is the chosen domain's own selector pin: its type and its label.
	TestTrue(TEXT("Voxel: a Voxel Selector pin"), OutputPin(EPCGUtilsSelectorDomain::Voxel).AllowedTypes == FPCGDataTypeIdentifier(FPCGVoxelSelectorDataTypeInfo::AsId()));
	TestEqual(TEXT("Voxel: the pin is named Selector"), OutputPin(EPCGUtilsSelectorDomain::Voxel).Label, FName(TEXT("Selector")));
	TestTrue(TEXT("DynMesh: a DynMesh Selector pin"), OutputPin(EPCGUtilsSelectorDomain::DynMesh).AllowedTypes == FPCGDataTypeIdentifier(FPCGUtilsDynMeshSelectionFactoryDataTypeInfo::AsId()));
	TestEqual(TEXT("DynMesh: the pin is named as DynMesh selector nodes name theirs"), OutputPin(EPCGUtilsSelectorDomain::DynMesh).Label, PCGUtilsDynMeshSelectionFactoryConstants::OutputPin);
	TestTrue(TEXT("GC: a GC Selection pin"), OutputPin(EPCGUtilsSelectorDomain::GC).AllowedTypes == FPCGDataTypeIdentifier(FPCGUtilsGeometryCollectionSelectionFactoryDataTypeInfo::AsId()));
	TestEqual(TEXT("GC: the pin is named as GC selection nodes name theirs"), OutputPin(EPCGUtilsSelectorDomain::GC).Label, PCGUtilsGeometryCollectionSelectionFactoryConstants::OutputPin);

	// What comes out is the domain's wrapper, carrying the node's settings.
	{
		const FPCGTaggedData Out = RunNode(EPCGUtilsSelectorDomain::Voxel, [](UPCGSelectorInBoundsSettings& Settings)
		{
			Settings.bInvert = true;
			Settings.Priority = 3;
			Settings.Containment = EPCGUtilsSelectorContainment::AnyPart;
			Settings.VoxelConversion.CellPoints = EPCGVoxelCellPoints::Center;
		});
		const UPCGVoxelPointSelectorData* Wrapper = Cast<UPCGVoxelPointSelectorData>(Out.Data);
		if (TestNotNull(TEXT("Voxel: the node emits a voxel selector"), Wrapper))
		{
			TestEqual(TEXT("On the Selector pin"), Out.Pin, FName(TEXT("Selector")));
			TestTrue(TEXT("Invert goes into the voxel selector's own flag"), Wrapper->bInvert);
			TestEqual(TEXT("Priority is passed on"), Wrapper->Priority, 3);
			TestTrue(TEXT("The Voxel Conversion is carried"), Wrapper->Conversion.CellPoints == EPCGVoxelCellPoints::Center);
			const UPCGUtilsInBoundsPointSelectorData* PointSelector = Cast<UPCGUtilsInBoundsPointSelectorData>(Wrapper->PointSelector);
			if (TestNotNull(TEXT("Holding the In Bounds point selector"), PointSelector))
			{
				TestTrue(TEXT("With the node's Containment"), PointSelector->Containment == EPCGUtilsSelectorContainment::AnyPart);
				TestEqual(TEXT("And the Bounds pin's points"), PointSelector->PointData.Num(), 1);
			}
		}
	}
	{
		const FPCGTaggedData Out = RunNode(EPCGUtilsSelectorDomain::DynMesh, [](UPCGSelectorInBoundsSettings& Settings)
		{
			Settings.bInvert = true;
			Settings.Priority = 5;
			Settings.DynMeshConversion.TrianglePoints = EPCGUtilsDynMeshTrianglePoints::Vertices;
			Settings.DynMeshConversion.Match = EPCGUtilsElementPointMatch::All;
		});
		const UPCGUtilsDynMeshPointSelectorData* Wrapper = Cast<UPCGUtilsDynMeshPointSelectorData>(Out.Data);
		if (TestNotNull(TEXT("DynMesh: the node emits a DynMesh selector"), Wrapper))
		{
			TestEqual(TEXT("On the DynMesh selector output pin"), Out.Pin, PCGUtilsDynMeshSelectionFactoryConstants::OutputPin);
			TestTrue(TEXT("Invert goes into the DynMesh selector's own flag"), Wrapper->bInvertSelection);
			TestEqual(TEXT("Priority is passed on"), Wrapper->Priority, 5);
			TestTrue(TEXT("The DynMesh Conversion is carried"),
				Wrapper->Conversion.TrianglePoints == EPCGUtilsDynMeshTrianglePoints::Vertices && Wrapper->Conversion.Match == EPCGUtilsElementPointMatch::All);
		}
	}
	{
		const FPCGTaggedData Out = RunNode(EPCGUtilsSelectorDomain::GC, [](UPCGSelectorInBoundsSettings& Settings)
		{
			Settings.bInvert = true;
			Settings.GCConversion.BonePoints = EPCGUtilsGeometryCollectionBonePoints::BoundsCorners;
		});
		const UPCGUtilsGeometryCollectionPointSelectorData* Wrapper = Cast<UPCGUtilsGeometryCollectionPointSelectorData>(Out.Data);
		if (TestNotNull(TEXT("GC: the node emits a GC selection"), Wrapper))
		{
			TestEqual(TEXT("On the GC selection output pin"), Out.Pin, PCGUtilsGeometryCollectionSelectionFactoryConstants::OutputPin);
			TestTrue(TEXT("Invert goes into the GC selection's own flag"), Wrapper->bInvertSelection);
			TestTrue(TEXT("The GC Conversion is carried"), Wrapper->Conversion.BonePoints == EPCGUtilsGeometryCollectionBonePoints::BoundsCorners);
		}
	}

	// End to end through the voxel nodes: Selector | In Bounds (Voxel) -> Voxel | Pattern -> Voxel | Apply Patterns.
	{
		const FPCGTaggedData Selector = RunNode(EPCGUtilsSelectorDomain::Voxel, [](UPCGSelectorInBoundsSettings&) {});
		UPCGVoxelPatternSettings* PatternSettings = NewObject<UPCGVoxelPatternSettings>();
		PatternSettings->Pattern = Strip(FIntVector(1));
		const UPCGVoxelPatternData* Pattern = FirstOutput<UPCGVoxelPatternData>(Run(PatternSettings, { { PCGVoxelSelectorPins::SelectorLabel, Selector.Data } }));
		if (TestNotNull(TEXT("Voxel | Pattern takes the selector on its Selector pin"), Pattern))
		{
			TestEqual(TEXT("And keeps it"), Pattern->Selectors.Num(), 1);
			TArray<FIntVector> Row;
			for (int32 X = 0; X < 4; ++X)
			{
				Row.Add(FIntVector(X, 0, 0));
			}
			UPCGVoxelApplyPatternsSettings* Apply = NewObject<UPCGVoxelApplyPatternsSettings>();
			int32 Chunks = 0;
			for (const FPCGTaggedData& Output : Run(Apply, { { PCGPinConstants::DefaultInputLabel, MakeVoxels(Row) }, { PCGVoxelPatternPins::PatternsLabel, Pattern } }))
			{
				if (Output.Pin != TEXT("Unmatched"))
				{
					const UPCGBasePointData* Out = Cast<UPCGBasePointData>(Output.Data);
					Chunks = Out ? Out->GetNumPoints() : 0;
				}
			}
			TestEqual(TEXT("The box from -60 to 160 limits the pattern to two cells"), Chunks, 2);
		}
		const TArray<FPCGPinProperties> PatternInputs = GetDefault<UPCGVoxelPatternSettings>()->AllInputPinProperties();
		const FPCGPinProperties* SelectorPin = PatternInputs.FindByPredicate([](const FPCGPinProperties& Pin) { return Pin.Label == FName(TEXT("Selector")); });
		if (TestNotNull(TEXT("Voxel | Pattern keeps its Selector pin"), SelectorPin))
		{
			TestTrue(TEXT("Which is a Voxel Selector pin"), SelectorPin->AllowedTypes == FPCGDataTypeIdentifier(FPCGVoxelSelectorDataTypeInfo::AsId()));
		}
	}

	// A Match Fraction outside 0 to 1 is refused on the node, by name; nothing is emitted.
	{
		AddExpectedError(TEXT("DynMesh Conversion > Match Fraction is 1.5; expected a value from 0 to 1"), EAutomationExpectedErrorFlags::Contains, 1);
		const FPCGTaggedData Out = RunNode(EPCGUtilsSelectorDomain::DynMesh, [](UPCGSelectorInBoundsSettings& Settings)
		{
			Settings.DynMeshConversion.Match = EPCGUtilsElementPointMatch::AtLeastFraction;
			Settings.DynMeshConversion.MatchFraction = 1.5;
		});
		TestNull(TEXT("No selector comes out"), Out.Data.Get());
	}

#if WITH_EDITOR
	// The palette: one entry per domain, each only setting Domain.
	{
		const UPCGSelectorInBoundsSettings* Default = GetDefault<UPCGSelectorInBoundsSettings>();
		const TArray<FPCGPreConfiguredSettingsInfo> Entries = Default->GetPreconfiguredInfo();
		TestTrue(TEXT("Only the per-domain entries are in the palette"), Default->OnlyExposePreconfiguredSettings());
		if (TestEqual(TEXT("Three entries"), Entries.Num(), 3))
		{
			TestEqual(TEXT("Voxel entry"), Entries[0].Label.ToString(), FString(TEXT("Selector | In Bounds (Voxel)")));
			TestEqual(TEXT("DynMesh entry"), Entries[1].Label.ToString(), FString(TEXT("Selector | In Bounds (DynMesh)")));
			TestEqual(TEXT("GC entry"), Entries[2].Label.ToString(), FString(TEXT("Selector | In Bounds (GC)")));
			for (const FPCGPreConfiguredSettingsInfo& Entry : Entries)
			{
				UPCGSelectorInBoundsSettings* Placed = NewObject<UPCGSelectorInBoundsSettings>();
				Placed->ApplyPreconfiguredSettings(Entry);
				TestEqual(TEXT("Placing an entry sets Domain to it"), static_cast<int32>(Placed->Domain), Entry.PreconfiguredIndex);
			}
			UPCGSelectorInBoundsSettings* Placed = NewObject<UPCGSelectorInBoundsSettings>();
			Placed->ApplyPreconfiguredSettings(Entries[2]);
			TestEqual(TEXT("The node shows its domain under the title"), Placed->GetAdditionalTitleInformation(), FString(TEXT("GC")));
		}
	}
#endif
	return true;
}

#endif // WITH_AUTOMATION_TESTS
