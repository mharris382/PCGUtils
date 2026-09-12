// Copyright Max Harris

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Chaos/ChaosSolverActor.h"
#include "Data/PCGBasePointData.h"
#include "Elements/Conversion/PCGSaveGeometryCollectionToAsset.h"
#include "Elements/PCGSpawnGeometryCollectionComponent.h"
#include "Engine/World.h"
#include "Field/FieldSystemActor.h"
#include "GeometryCollection/GeometryCollectionComponent.h"
#include "GeometryCollection/GeometryCollectionObject.h"
#include "Misc/ScopeExit.h"
#include "PCGPin.h"

/**
 * The node's contract with the two nodes it is wired to - GC | Save Asset upstream, Spawn Actor on the field pin -
 * is attribute names, and its pins are conditional. Both are settings-level facts and are pinned down here.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPCGSpawnGeometryCollectionComponentContractTest,
	"PCGUtils.Simulation.SpawnGCComponent.Contract",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPCGSpawnGeometryCollectionComponentContractTest::RunTest(const FString&)
{
	using namespace PCGSpawnGeometryCollectionComponentConstants;

	const UPCGSpawnGeometryCollectionComponentSettings* Defaults =
		GetDefault<UPCGSpawnGeometryCollectionComponentSettings>();

	TestEqual(TEXT("Palette title"), Defaults->GetDefaultNodeTitle().ToString(), FString(TEXT("GC | Spawn Component")));
	TestEqual(TEXT("Reads the attribute GC | Save Asset writes"), Defaults->AssetAttribute.GetAttributeName(),
		PCGSaveGeometryCollectionToAssetConstants::AssetPathAttribute);
	TestEqual(TEXT("Reads the actor references Spawn Actor writes"), Defaults->FieldActorAttribute.GetAttributeName(),
		PCGPointDataConstants::ActorReferenceAttribute);
	TestTrue(TEXT("Asset defaults apply by default"), Defaults->bApplyAssetDefaults);
	TestNotNull(TEXT("A template component exists"), Defaults->TemplateComponent.Get());

	// AllInputPinProperties also carries PCG's own override pins for PCG_Overridable settings; those are advanced
	// pins, so the node's own pins are exactly the non-advanced ones.
	auto NodePins = [](const UPCGSettings* Settings)
	{
		TArray<FPCGPinProperties> Pins = Settings->AllInputPinProperties();
		Pins.RemoveAll([](const FPCGPinProperties& Pin) { return Pin.IsAdvancedPin(); });
		return Pins;
	};

	const TArray<FPCGPinProperties> DefaultPins = NodePins(Defaults);
	if (TestEqual(TEXT("Only the Asset pin by default"), DefaultPins.Num(), 1))
	{
		TestEqual(TEXT("It is the Asset pin"), DefaultPins[0].Label, AssetInputPin);
		TestTrue(TEXT("Asset is required"), DefaultPins[0].IsRequiredPin());
	}

	UPCGSpawnGeometryCollectionComponentSettings* Wired =
		NewObject<UPCGSpawnGeometryCollectionComponentSettings>(GetTransientPackage());
	Wired->bUseInitializationFields = true;
	Wired->bUseSolver = true;

	const TArray<FPCGPinProperties> WiredPins = NodePins(Wired);
	TestEqual(TEXT("Both toggles add a pin"), WiredPins.Num(), 3);
	TestTrue(TEXT("Init Fields pin present"),
		WiredPins.ContainsByPredicate([](const FPCGPinProperties& Pin) { return Pin.Label == FieldsInputPin; }));
	TestTrue(TEXT("Solver pin present"),
		WiredPins.ContainsByPredicate([](const FPCGPinProperties& Pin) { return Pin.Label == SolverInputPin; }));

	// Every node owns its own template; sharing the CDO's would make one node's edits every node's.
	TestTrue(TEXT("Each node has its own template"),
		Wired->TemplateComponent && Wired->TemplateComponent != Defaults->TemplateComponent);
	return true;
}

/**
 * What the physics proxy reads at creation must be on the component before it registers. The configure step is
 * the part that decides that, so it is exercised on a real, unregistered component.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPCGSpawnGeometryCollectionComponentConfigureTest,
	"PCGUtils.Simulation.SpawnGCComponent.Configure",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPCGSpawnGeometryCollectionComponentConfigureTest::RunTest(const FString&)
{
	UWorld* World = UWorld::CreateWorld(EWorldType::Editor, /*bInformEngineOfWorld=*/false);
	if (!TestNotNull(TEXT("A test world was created"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	AActor* Host = World->SpawnActor<AActor>();
	AFieldSystemActor* Field = World->SpawnActor<AFieldSystemActor>();
	AChaosSolverActor* Solver = World->SpawnActor<AChaosSolverActor>();
	if (!TestTrue(TEXT("Test actors were spawned"), Host && Field && Solver))
	{
		return false;
	}

	UGeometryCollection* Asset = NewObject<UGeometryCollection>(GetTransientPackage());
	Asset->DamageThreshold = {123.0f};

	const TArray<TObjectPtr<const AFieldSystemActor>> Fields = {Field};

	UGeometryCollectionComponent* Component = NewObject<UGeometryCollectionComponent>(Host);
	PCGSpawnGeometryCollectionComponent::ConfigureComponent(
		*Component, Asset, /*bApplyAssetDefaults=*/true, &Fields, /*bSetSolver=*/true, Solver);

	TestFalse(TEXT("Configuring does not register"), Component->IsRegistered());
	TestTrue(TEXT("The asset is the rest collection"), Component->GetRestCollection() == Asset);
	TestTrue(TEXT("The field is an initialization field"),
		Component->InitializationFields.Num() == 1 && Component->InitializationFields[0] == Field);
	TestTrue(TEXT("The solver is assigned"), Component->ChaosSolverActor == Solver);
	TestEqual(TEXT("The asset's damage thresholds apply"), Component->DamageThreshold, TArray<float>{123.0f});

	// Opting out keeps the component's own damage settings, and null/false leave fields and solver alone.
	UGeometryCollectionComponent* Plain = NewObject<UGeometryCollectionComponent>(Host);
	const TArray<float> OwnThreshold = Plain->DamageThreshold;
	PCGSpawnGeometryCollectionComponent::ConfigureComponent(
		*Plain, Asset, /*bApplyAssetDefaults=*/false, nullptr, /*bSetSolver=*/false, nullptr);

	TestTrue(TEXT("The asset is still assigned"), Plain->GetRestCollection() == Asset);
	TestEqual(TEXT("The component keeps its own damage thresholds"), Plain->DamageThreshold, OwnThreshold);
	TestTrue(TEXT("No fields were assigned"), Plain->InitializationFields.IsEmpty());
	TestTrue(TEXT("No solver was assigned"), Plain->ChaosSolverActor == nullptr);

	// The diagnostic behind the "has no construction fields" warning.
	TestFalse(TEXT("A bare Field System Actor has no construction fields"),
		PCGSpawnGeometryCollectionComponent::HasConstructionFields(*Field));

	return true;
}

#endif
