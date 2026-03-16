#include "Components/PCGSplineComponent.h"

UPCGSplineComponent::UPCGSplineComponent(const FObjectInitializer& ObjectInitializer) : Super(ObjectInitializer)
{
    ComponentTags.Add(TEXT("pcg_spline"));
}

TArray<FPCGNamedOverrideGraph> UPCGSplineComponent::GetOverrideGraphEntries_Implementation() const
{
    return {
        { TEXT("PreProcessSplineGraph"), PreProcessSplineGraph },
        { TEXT("PostSpawnGraph"),        PostSpawnGraph        },
        { TEXT("PreBakeGraph"),          PreBakeGraph          },
        { TEXT("PostBakeGraph"),         PostBakeGraph         },
    };
}
