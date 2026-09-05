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

	auto* Connected = NewObject<UPCGDynMeshExpandToConnectedSelectionSettings>();
	const TArray<FPCGPreConfiguredSettingsInfo> ConnectedPresets = Connected->GetPreconfiguredInfo();
	TestEqual(TEXT("Connected exposes exactly two representations"), ConnectedPresets.Num(), 2);
	if (ConnectedPresets.Num() == 2)
	{
		TestTrue(TEXT("Selector preset has a visible suffix"), ConnectedPresets[0].Label.ToString().EndsWith(TEXT("(Selector)")));
		TestTrue(TEXT("Selection preset has a visible suffix"), ConnectedPresets[1].Label.ToString().EndsWith(TEXT("(Selection)")));
		TestTrue(TEXT("Selection preset is searchable as Selection"), ConnectedPresets[1].SearchHints.ToString().Contains(TEXT("selection")));

		Connected->ApplyPreconfiguredSettings(ConnectedPresets[0]);
		TestEqual(TEXT("Selector preset selects deferred representation"), Connected->OperationMode,
			EPCGUtilsDynMeshSelectionOperationMode::Selector);
		Connected->ApplyPreconfiguredSettings(ConnectedPresets[1]);
		TestEqual(TEXT("Selection preset selects materialized representation"), Connected->OperationMode,
			EPCGUtilsDynMeshSelectionOperationMode::Selection);
	}

	const auto* ExpandContract = NewObject<UPCGDynMeshExpandContractSelectionSettings>();
	TestEqual(TEXT("Expand and Contract each expose both representations"),
		ExpandContract->GetPreconfiguredInfo().Num(), 4);

	auto* Bounds = NewObject<UPCGDynMeshBoundsSelectionFactoryProviderSettings>();
	const TArray<FPCGPreConfiguredSettingsInfo> BoundsPresets = Bounds->GetPreconfiguredInfo();
	TestEqual(TEXT("A source selection exposes exactly two representations"), BoundsPresets.Num(), 2);
	if (BoundsPresets.Num() == 2)
	{
		Bounds->ApplyPreconfiguredSettings(BoundsPresets[1]);
		TestEqual(TEXT("Source Selection preset selects materialized representation"), Bounds->Representation,
			EPCGUtilsDynMeshSelectionRepresentation::Selection);
		TestEqual(TEXT("Source Selection preset outputs the Selection pin"), Bounds->GetMainOutputPin(),
			PCGUtilsDynMeshSelectionSourceConstants::SelectionPin);
	}

	const auto* Logic = NewObject<UPCGDynMeshSelectionFactoryGroupProviderSettings>();
	TestEqual(TEXT("AND, OR, and NOT each expose both representations"),
		Logic->GetPreconfiguredInfo().Num(), 6);

	auto VerifyConvertedSource = [this](UPCGUtilsDynMeshSelectionSourceSettings* Settings, const TCHAR* Name)
	{
		const TArray<FPCGPreConfiguredSettingsInfo> Presets = Settings->GetPreconfiguredInfo();
		TestEqual(*FString::Printf(TEXT("%s exposes Selector and Selection"), Name), Presets.Num(), 2);
		if (Presets.Num() != 2)
		{
			return;
		}

		TestTrue(*FString::Printf(TEXT("%s Selector suffix is centralized"), Name),
			Presets[0].Label.ToString().EndsWith(TEXT("(Selector)")));
		TestTrue(*FString::Printf(TEXT("%s Selection suffix is centralized"), Name),
			Presets[1].Label.ToString().EndsWith(TEXT("(Selection)")));
		Settings->ApplyPreconfiguredSettings(Presets[0]);
		TestEqual(*FString::Printf(TEXT("%s deferred output is Selector"), Name),
			Settings->GetMainOutputPin(), PCGUtilsDynMeshSelectionFactoryConstants::OutputPin);
		Settings->ApplyPreconfiguredSettings(Presets[1]);
		TestEqual(*FString::Printf(TEXT("%s inline output is Selection"), Name),
			Settings->GetMainOutputPin(), PCGUtilsDynMeshSelectionSourceConstants::SelectionPin);
	};

	auto* SelfOcclusion = NewObject<UPCGSelectSelfOcclusionSettings>();
	VerifyConvertedSource(SelfOcclusion, TEXT("Self Occlusion"));
	auto* Spline = NewObject<UPCGSelectionFromSplineSettings>();
	VerifyConvertedSource(Spline, TEXT("Spline"));
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
