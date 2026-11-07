// Copyright Max Harris
//
// PCG Actor's automatic bounds box must grow to hold the PCG Marker and PCG Volume Components on the actor, and
// follow them when they are edited or moved without a construction rerun.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS && WITH_EDITOR

#include "Components/BoxComponent.h"
#include "Components/PCGMarkerComponent.h"
#include "Components/PCGVolumeComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "PCGActorBase.h"

namespace PCGActorBaseBoundsTests
{
	/** An editor world with a world context, destroyed with the fixture. */
	struct FWorldFixture
	{
		UWorld* World = nullptr;

		FWorldFixture()
		{
			World = UWorld::CreateWorld(EWorldType::Editor, false);
			FWorldContext& Context = GEngine->CreateNewWorldContext(EWorldType::Editor);
			Context.SetCurrentWorld(World);
		}

		~FWorldFixture()
		{
			GEngine->DestroyWorldContext(World);
			World->DestroyWorld(false);
		}
	};

	/** The bounds box in actor space. */
	FBox BoundsBoxInActorSpace(const APCGActorBase* Actor)
	{
		const FVector Center = Actor->BoundsBox->GetRelativeLocation();
		const FVector Extent = Actor->BoundsBox->GetUnscaledBoxExtent();
		return FBox(Center - Extent, Center + Extent);
	}

	template <typename T>
	T* AddInstanceComponent(AActor* Actor, const FVector& RelativeLocation, USceneComponent* Parent = nullptr)
	{
		T* Component = NewObject<T>(Actor);
		Component->SetupAttachment(Parent ? Parent : Actor->GetRootComponent());
		Component->SetRelativeLocation(RelativeLocation);
		Component->RegisterComponent();
		Actor->AddInstanceComponent(Component);
		return Component;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPCGActorBaseMarkerBoundsTest,
	"PCGUtils.ActorBase.Bounds.MarkerComponentsGrowTheBoundsBox",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPCGActorBaseMarkerBoundsTest::RunTest(const FString&)
{
	using namespace PCGActorBaseBoundsTests;
	FWorldFixture Fixture;

	APCGActorBase* Actor = Fixture.World->SpawnActor<APCGActorBase>(FVector(500, -200, 0), FRotator(0, 90, 0));
	UTEST_NOT_NULL(TEXT("The PCG actor spawned"), Actor);
	Actor->BoundsPadding = 0.0f;

	// A marker 1000 out along the actor's X, its own box 100 across.
	UPCGMarkerComponent* Marker = AddInstanceComponent<UPCGMarkerComponent>(Actor, FVector(1000, 0, 0));
	Actor->RerunConstructionScripts();
	const FBox Bounds = BoundsBoxInActorSpace(Actor);
	TestTrue(*FString::Printf(TEXT("The bounds box reaches the marker's far side at X 1050 (%s)"), *Bounds.ToString()),
		Bounds.Max.X >= 1049.0);

	// Moving the marker and rebuilding follows it.
	Marker->SetRelativeLocation(FVector(0, 0, 2000));
	Actor->RerunConstructionScripts();
	const FBox Moved = BoundsBoxInActorSpace(Actor);
	TestTrue(*FString::Printf(TEXT("The bounds box follows the moved marker up to Z 2050 (%s)"), *Moved.ToString()),
		Moved.Max.Z >= 2049.0);

	// Editing the marker in the details panel, with no construction rerun, must update the box too.
	Marker->BoundsMax = FVector(50, 50, 3000);
	FPropertyChangedEvent Edit(UPCGMarkerComponent::StaticClass()->FindPropertyByName(GET_MEMBER_NAME_CHECKED(UPCGMarkerComponent, BoundsMax)));
	Marker->PostEditChangeProperty(Edit);
	const FBox Edited = BoundsBoxInActorSpace(Actor);
	TestTrue(*FString::Printf(TEXT("Editing the marker's Bounds Max updates the bounds box to Z 5000 (%s)"), *Edited.ToString()),
		Edited.Max.Z >= 4999.0);

	// Moving the marker in the viewport, with no construction rerun, must update the box too.
	Marker->SetRelativeLocation(FVector(-3000, 0, 0));
	Marker->PostEditComponentMove(true);
	const FBox Dragged = BoundsBoxInActorSpace(Actor);
	TestTrue(*FString::Printf(TEXT("Dragging the marker updates the bounds box to X -3050 (%s)"), *Dragged.ToString()),
		Dragged.Min.X <= -3049.0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPCGActorBaseVolumeBoundsTest,
	"PCGUtils.ActorBase.Bounds.VolumeComponentsGrowTheBoundsBox",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPCGActorBaseVolumeBoundsTest::RunTest(const FString&)
{
	using namespace PCGActorBaseBoundsTests;
	FWorldFixture Fixture;

	APCGActorBase* Actor = Fixture.World->SpawnActor<APCGActorBase>(FVector(0, 0, 0), FRotator(0, 30, 0));
	UTEST_NOT_NULL(TEXT("The PCG actor spawned"), Actor);
	Actor->BoundsPadding = 0.0f;

	// A box volume 2000 up, 600 tall, rotated 90 degrees about X so its Z extent lies along the actor's Y.
	UPCGVolumeComponent* Volume = AddInstanceComponent<UPCGVolumeComponent>(Actor, FVector(0, 0, 2000));
	Volume->BoxExtent = FVector(100, 100, 300);
	Volume->SetRelativeRotation(FRotator(0, 0, 90));
	Actor->RerunConstructionScripts();
	const FBox Bounds = BoundsBoxInActorSpace(Actor);
	TestTrue(*FString::Printf(TEXT("The bounds box reaches the volume's top at Z 2100 (%s)"), *Bounds.ToString()), Bounds.Max.Z >= 2099.0);
	TestTrue(*FString::Printf(TEXT("The rotated volume reaches Y 300 (%s)"), *Bounds.ToString()), Bounds.Max.Y >= 299.0 && Bounds.Min.Y <= -299.0);

	// Changing the shape in the details panel updates the box with no construction rerun.
	Volume->Shape = EPCGVolumeShape::Sphere;
	Volume->Radius = 1000.0;
	FPropertyChangedEvent Edit(UPCGVolumeComponent::StaticClass()->FindPropertyByName(GET_MEMBER_NAME_CHECKED(UPCGVolumeComponent, Radius)));
	Volume->PostEditChangeProperty(Edit);
	const FBox Edited = BoundsBoxInActorSpace(Actor);
	TestTrue(*FString::Printf(TEXT("A 1000 sphere at Z 2000 reaches Z 3000 and X -1000 (%s)"), *Edited.ToString()),
		Edited.Max.Z >= 2999.0 && Edited.Min.X <= -999.0);

	// The component reports the same bounds for the actor's own bounds and selection.
	const FBox ComponentBounds = Volume->Bounds.GetBox();
	TestTrue(*FString::Printf(TEXT("The component's own bounds hold the sphere (%s)"), *ComponentBounds.ToString()),
		ComponentBounds.Max.Z >= 2999.0);

	// Owner Volume on an actor that is not a Volume contributes nothing.
	Volume->Shape = EPCGVolumeShape::OwnerVolume;
	Volume->PostEditChangeProperty(Edit);
	const FBox Owner = BoundsBoxInActorSpace(Actor);
	TestTrue(*FString::Printf(TEXT("Owner Volume on a non-volume actor adds nothing (%s)"), *Owner.ToString()), Owner.Max.Z < 100.0);
	return true;
}

#endif
