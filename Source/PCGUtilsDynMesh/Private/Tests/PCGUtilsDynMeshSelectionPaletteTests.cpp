// Copyright Max Harris

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS && WITH_EDITOR

#include "Elements/Selections/PCGDynMeshBoundsSelectionFactory.h"
#include "Elements/Selections/PCGDynMeshDistanceSelectionFactory.h"
#include "Elements/Selections/PCGDynMeshExpandContractSelection.h"
#include "Elements/Selections/PCGDynMeshExpandToConnectedSelection.h"
#include "Elements/Selections/PCGDynMeshPolygroupSelectionFactory.h"
#include "Elements/Selections/PCGDynMeshSelectionBoundaryFactory.h"
#include "Elements/Selections/PCGDynMeshSelectionFactoryGroup.h"
#include "Elements/Selections/PCGDynMeshSelectionFromPointsFactory.h"
#include "Elements/Selections/PCGDynMeshVertexColorSelectionFactory.h"
#include "Elements/Selections/PCGSelectionBoundaryEdges.h"
#include "Elements/Selections/PCGSelectionFromPoints.h"
#include "Elements/Selections/PCGSelectionFromSpline.h"
#include "Elements/Selections/PCGSelectByNormal.h"
#include "Elements/Selections/PCGSelectInPointBounds.h"
#include "Elements/Selections/PCGSelectSelfOcclusion.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPCGDynMeshSelectionPaletteContractTest,
	"PCGUtils.DynMesh.Selection.PaletteContract",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPCGDynMeshSelectionPaletteContractTest::RunTest(const FString&)
{
	const TArray<UPCGSettings*> PublicSettings = {
		NewObject<UPCGDynMeshBoundsSelectionFactoryProviderSettings>(),
		NewObject<UPCGDynMeshDistanceSelectionFactoryProviderSettings>(),
		NewObject<UPCGDynMeshExpandContractSelectionSettings>(),
		NewObject<UPCGDynMeshExpandToConnectedSelectionSettings>(),
		NewObject<UPCGDynMeshPolygroupSelectionFactoryProviderSettings>(),
		NewObject<UPCGDynMeshSelectionFactoryGroupProviderSettings>(),
		NewObject<UPCGDynMeshSelectionFromPointsFactoryProviderSettings>(),
		NewObject<UPCGDynMeshVertexColorSelectionFactoryProviderSettings>(),
		NewObject<UPCGSelectionBoundaryEdgesSettings>(),
		NewObject<UPCGSelectionFromSplineSettings>(),
		NewObject<UPCGSelectSelfOcclusionSettings>()
	};

	for (const UPCGSettings* Settings : PublicSettings)
	{
		TestTrue(*FString::Printf(TEXT("%s is exposed"), *Settings->GetClass()->GetName()), Settings->bExposeToLibrary);
		TestEqual(*FString::Printf(TEXT("%s has no alias actions"), *Settings->GetClass()->GetName()),
			Settings->GetNodeTitleAliases().Num(), 0);
	}

	const TArray<UPCGSettings*> CompatibilitySettings = {
		NewObject<UPCGDynMeshExpandToConnectedSelectionFactoryProviderSettings>(),
		NewObject<UPCGDynMeshSelectionBoundaryFactoryProviderSettings>(),
		NewObject<UPCGSelectionFromPointsSettings>(),
		NewObject<UPCGSelectByNormalSettings>(),
		NewObject<UPCGSelectInPointBoundsSettings>()
	};

	for (const UPCGSettings* Settings : CompatibilitySettings)
	{
		TestFalse(*FString::Printf(TEXT("%s is hidden"), *Settings->GetClass()->GetName()), Settings->bExposeToLibrary);
	}

	// One palette entry per selection query. The materialized Selection representation is still reachable
	// through the Representation property on a placed node, but it no longer duplicates every entry in the
	// context menu - that duplication was the single biggest source of palette noise in the DynMesh library.
	auto* Connected = NewObject<UPCGDynMeshExpandToConnectedSelectionSettings>();
	const TArray<FPCGPreConfiguredSettingsInfo> ConnectedPresets = Connected->GetPreconfiguredInfo();
	TestEqual(TEXT("Connected exposes exactly one representation"), ConnectedPresets.Num(), 1);
	if (ConnectedPresets.Num() == 1)
	{
		const FString Label = ConnectedPresets[0].Label.ToString();
		TestFalse(TEXT("No Selector suffix survives"), Label.Contains(TEXT("(Selector)")));
		TestFalse(TEXT("No Selection alias survives"), Label.Contains(TEXT("(Selection)")));
		TestTrue(TEXT("Selection queries carry the Select family prefix"), Label.StartsWith(TEXT("Select|")));

		Connected->ApplyPreconfiguredSettings(ConnectedPresets[0]);
		TestEqual(TEXT("The remaining preset selects the deferred representation"), Connected->OperationMode,
			EPCGUtilsDynMeshSelectionOperationMode::Selector);
	}

	const auto* ExpandContract = NewObject<UPCGDynMeshExpandContractSelectionSettings>();
	TestEqual(TEXT("Expand and Contract are two entries, not four"),
		ExpandContract->GetPreconfiguredInfo().Num(), 2);

	auto* Bounds = NewObject<UPCGDynMeshBoundsSelectionFactoryProviderSettings>();
	const TArray<FPCGPreConfiguredSettingsInfo> BoundsPresets = Bounds->GetPreconfiguredInfo();
	TestEqual(TEXT("A source selection exposes exactly one representation"), BoundsPresets.Num(), 1);
	if (BoundsPresets.Num() == 1)
	{
		Bounds->ApplyPreconfiguredSettings(BoundsPresets[0]);
		TestEqual(TEXT("Source preset selects the deferred representation"), Bounds->Representation,
			EPCGUtilsDynMeshSelectionRepresentation::Selector);
		TestEqual(TEXT("Source preset outputs the Selector pin"), Bounds->GetMainOutputPin(),
			PCGUtilsDynMeshSelectionFactoryConstants::OutputPin);

		// The representation itself is intact - only its palette alias went away.
		Bounds->Representation = EPCGUtilsDynMeshSelectionRepresentation::Selection;
		TestEqual(TEXT("Selection representation still resolves to the Selection pin"), Bounds->GetMainOutputPin(),
			PCGUtilsDynMeshSelectionSourceConstants::SelectionPin);
	}

	const auto* Logic = NewObject<UPCGDynMeshSelectionFactoryGroupProviderSettings>();
	TestEqual(TEXT("AND, OR, and NOT are three entries, not six"),
		Logic->GetPreconfiguredInfo().Num(), 3);

	auto VerifyConvertedSource = [this](UPCGUtilsDynMeshSelectionSourceSettings* Settings, const TCHAR* Name)
	{
		const TArray<FPCGPreConfiguredSettingsInfo> Presets = Settings->GetPreconfiguredInfo();
		TestEqual(*FString::Printf(TEXT("%s exposes one entry"), Name), Presets.Num(), 1);
		if (Presets.Num() != 1)
		{
			return;
		}

		TestFalse(*FString::Printf(TEXT("%s carries no representation suffix"), Name),
			Presets[0].Label.ToString().Contains(TEXT("(")));
		Settings->ApplyPreconfiguredSettings(Presets[0]);
		TestEqual(*FString::Printf(TEXT("%s deferred output is Selector"), Name),
			Settings->GetMainOutputPin(), PCGUtilsDynMeshSelectionFactoryConstants::OutputPin);
		Settings->Representation = EPCGUtilsDynMeshSelectionRepresentation::Selection;
		TestEqual(*FString::Printf(TEXT("%s inline output is Selection"), Name),
			Settings->GetMainOutputPin(), PCGUtilsDynMeshSelectionSourceConstants::SelectionPin);
	};

	auto* SelfOcclusion = NewObject<UPCGSelectSelfOcclusionSettings>();
	VerifyConvertedSource(SelfOcclusion, TEXT("Self Occlusion"));
	auto* Spline = NewObject<UPCGSelectionFromSplineSettings>();
	VerifyConvertedSource(Spline, TEXT("Spline"));

	// VerifyConvertedSource leaves Spline in the materialized representation.
	const TArray<FPCGPinProperties> SplineSelectionInputs = Spline->AllInputPinProperties();
	TestTrue(TEXT("Spline Selection mode has Candidates and Spline inputs"),
		SplineSelectionInputs.ContainsByPredicate([](const FPCGPinProperties& Pin)
		{
			return Pin.Label == PCGUtilsDynMeshSelectionSourceConstants::CandidatesPin;
		}) && SplineSelectionInputs.ContainsByPredicate([](const FPCGPinProperties& Pin)
		{
			return Pin.Label == PCGSelectionFromSplineConstants::SplineInputPin;
		}));
	Spline->ApplyPreconfiguredSettings(Spline->GetPreconfiguredInfo()[0]);
	const TArray<FPCGPinProperties> SplineSelectorInputs = Spline->AllInputPinProperties();
	TestTrue(TEXT("Spline Selector mode keeps Spline input and omits Candidates"),
		!SplineSelectorInputs.ContainsByPredicate([](const FPCGPinProperties& Pin)
		{
			return Pin.Label == PCGUtilsDynMeshSelectionSourceConstants::CandidatesPin;
		}) && SplineSelectorInputs.ContainsByPredicate([](const FPCGPinProperties& Pin)
		{
			return Pin.Label == PCGSelectionFromSplineConstants::SplineInputPin;
		}));

	auto* EqualPriorityA = NewObject<UPCGDynMeshSelectionFactoryGroupData>();
	auto* HighPriority = NewObject<UPCGDynMeshSelectionFactoryGroupData>();
	auto* EqualPriorityB = NewObject<UPCGDynMeshSelectionFactoryGroupData>();
	auto* LowPriority = NewObject<UPCGDynMeshSelectionFactoryGroupData>();
	EqualPriorityA->Priority = 5;
	HighPriority->Priority = 20;
	EqualPriorityB->Priority = 5;
	LowPriority->Priority = -10;
	TArray<TObjectPtr<const UPCGUtilsDynMeshSelectionFactoryData>> OrderedSelectors = {
		EqualPriorityA, HighPriority, EqualPriorityB, LowPriority};
	PCGUtilsDynMeshSelectionFactories::SortByPriority(OrderedSelectors);
	TestTrue(TEXT("Higher-priority selectors are evaluated first"), OrderedSelectors[0] == HighPriority);
	TestTrue(TEXT("Equal-priority selectors retain connection order"),
		OrderedSelectors[1] == EqualPriorityA && OrderedSelectors[2] == EqualPriorityB);
	TestTrue(TEXT("Lower-priority selectors are evaluated last"), OrderedSelectors[3] == LowPriority);
	return true;
}

#endif
