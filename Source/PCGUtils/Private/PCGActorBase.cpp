#include "PCGActorBase.h"
#include "Components/BoxComponent.h"
#include "Components/SceneComponent.h"
#include "UObject/ConstructorHelpers.h"
#include "Interfaces/IPCGOutputDataConsumer.h"
#include "PCGGraph.h"
#include "PCGComponent.h"
#include "PCGUtilsHelpers.h"

APCGActorBase::APCGActorBase()
{
    PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = false;
	USceneComponent* Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	SetRootComponent(Root);

    BoundsBox = CreateDefaultSubobject<UBoxComponent>(TEXT("BoundsBox"));
    BoundsBox->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    BoundsBox->SetCollisionResponseToAllChannels(ECR_Ignore);
    BoundsBox->SetLineThickness(0.0f);
    
	BoundsBox->SetupAttachment(RootComponent);
    ///PCGUtils/PCG/Templates/Template_PostBake_DynMesh.Template_PostBake_DynMesh
    ///PCGUtils/PCG/Templates/Template_PreBake_DynMesh.Template_PreBake_DynMesh
	
    PCGComponent = CreateDefaultSubobject<UPCGComponent>(TEXT("PCGComponent"));
}

void APCGActorBase::OnConstruction(const FTransform& Transform)
{
    Super::OnConstruction(Transform);
#if WITH_EDITOR
    ApplyBoundsToBox();
    BakedAssetSaveName = GetAssetSaveGroupName() + TEXT("_") + GetActorGuid().ToString();
#endif
}

void APCGActorBase::ComputeLocalBounds_Implementation(FVector& OutMin, FVector& OutMax) const
{
    OutMin = FVector(-50.f);
    OutMax = FVector( 50.f);
}

FBox APCGActorBase::GetPCGBounds() const
{
	if (!BoundsBox)
		return FBox();
	FVector extents = BoundsBox->GetScaledBoxExtent();
	FVector origin = BoundsBox->GetComponentLocation();
	return FBox(origin, extents);
}

void APCGActorBase::ApplyBoundsToBox()
{
	if (!BoundsBox)
		return;

    // Base bounds from the virtual/BP-overridable hook.
    FVector LocalMin, LocalMax;
    ComputeLocalBounds(LocalMin, LocalMax);  // polymorphic — calls BP override if present
    FBox CombinedBox(LocalMin, LocalMax);

    // Expand by any spline components attached to this actor (local space).
    const FBox SplineBox = UPCGUtilsHelpers::ComputeActorSplineBoundingBox(this, /*bLocalSpace=*/true);
    if (SplineBox.IsValid)
    {
        CombinedBox += SplineBox;
    }

    const FVector Padding(BoundsPadding);
    CombinedBox.Min -= Padding;
    CombinedBox.Max += Padding;

    const FVector Center  = CombinedBox.GetCenter();
    const FVector HalfExt = CombinedBox.GetExtent();

    BoundsBox->SetRelativeLocation(Center);
    BoundsBox->SetBoxExtent(HalfExt, /*bUpdateOverlaps=*/false);
}
