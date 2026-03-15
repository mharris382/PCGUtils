#include "PCGActorBase.h"
#include "Components/BoxComponent.h"
#include "Components/SceneComponent.h"
#include "UObject/ConstructorHelpers.h"
#include "Interfaces/IPCGOutputDataConsumer.h"
#include "PCGGraph.h"
#include "PCGComponent.h"

APCGActorBase::APCGActorBase()
{
    PrimaryActorTick.bCanEverTick = false;
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
    ApplyBoundsToBox();
    
    BakedAssetSaveName = GetAssetSaveGroupName() + TEXT("_") + GetActorGuid().ToString();
	EmitPCGOutputDataToConsumers(true);
}

void APCGActorBase::ComputeLocalBounds_Implementation(FVector& OutMin, FVector& OutMax) const
{
    OutMin = FVector(-50.f);
    OutMax = FVector( 50.f);
}

void APCGActorBase::ApplyBoundsToBox()
{
    FVector LocalMin, LocalMax;
    ComputeLocalBounds(LocalMin, LocalMax);  // polymorphic — calls BP override if present

    const FVector Padding(BoundsPadding);
    LocalMin -= Padding;
    LocalMax += Padding;

    const FVector Center  = (LocalMin + LocalMax) * 0.5f;
    const FVector HalfExt = (LocalMax - LocalMin) * 0.5f;

    BoundsBox->SetRelativeLocation(Center);
    BoundsBox->SetBoxExtent(HalfExt, /*bUpdateOverlaps=*/false);
}


void APCGActorBase::BeginPlay()
{
    Super::BeginPlay();
    EmitPCGOutputDataToConsumers(false);
}

void APCGActorBase::EmitPCGOutputDataToConsumers(bool isConstructor)
{
    const TArray<UObject*> targets = ResolveAllPCGOutputDataConsumers();
    if(targets.IsEmpty())
    {
        return;
    }
    //TODO get PCG output data
	for (const TObjectPtr<UObject>& Target : targets)
    {
        if(Target)
        {
            if (Target->GetClass()->ImplementsInterface(UPCGOutputDataConsumer::StaticClass()))
            {
                TArray<FString> ConsumerTargetPins = IPCGOutputDataConsumer::Execute_GetTargetDataPins(Target);
                if (ConsumerTargetPins.IsEmpty())
                {
                    //TOOD: emit all PCG data to consumer
                }
                else
                {
					//TODO: selectively emit PCG data based on pin names

                }
            }
        }
    }
}






const TArray<UObject*> APCGActorBase::ResolveAllPCGOutputDataConsumers_Implementation() const
{
    TArray<UObject*> consumers;
    TArray<UActorComponent*> Components =  GetComponentsByInterface(UPCGOutputDataConsumer::StaticClass());
    for (UActorComponent* Comp : Components)
    {
		consumers.Add(Comp);
    }
    return consumers;
}