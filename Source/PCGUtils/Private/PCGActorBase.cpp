#include "PCGActorBase.h"
#include "Components/BoxComponent.h"
#include "Components/SceneComponent.h"
#include "Components/SplineComponent.h"
#include "UObject/ConstructorHelpers.h"
#include "Interfaces/IPCGOutputDataConsumer.h"
#include "PCGGraph.h"
#include "PCGComponent.h"
#include "PCGUtilsHelpers.h"

#if WITH_EDITOR
#include "ScopedTransaction.h"
#endif

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
    BoundsBox->ShapeColor = GetBoxEditorColor();
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

void APCGActorBase::RecenterActorToBounds()
{
#if WITH_EDITOR
    // --- 1. Compute the local-space bounding box (same logic as ApplyBoundsToBox) ---
    FVector LocalMin, LocalMax;
    ComputeLocalBounds(LocalMin, LocalMax);
    FBox CombinedBox(LocalMin, LocalMax);

    const FBox SplineBox = UPCGUtilsHelpers::ComputeActorSplineBoundingBox(this, /*bLocalSpace=*/true);
    if (SplineBox.IsValid)
    {
        CombinedBox += SplineBox;
    }

    const FVector LocalBoundsCenter = CombinedBox.GetCenter();
    if (LocalBoundsCenter.IsNearlyZero())
    {
        // Pivot is already at the bounds center — nothing to do.
        return;
    }

    // --- 2. Determine the new world location for the actor pivot ---
    const FTransform OldActorTransform = GetActorTransform();
    const FVector NewWorldLocation = OldActorTransform.TransformPosition(LocalBoundsCenter);

    // --- 3. Build the correction transform for local-space data ---
    // This is a pure translation because only the actor's origin moves (no rotation/scale change).
    // Applying this to a local-space position preserves its world-space location after the pivot shifts.
    const FTransform LocalDeltaTransform(FQuat::Identity, -LocalBoundsCenter, FVector::OneVector);

    FScopedTransaction Transaction(NSLOCTEXT("PCGUtils", "RecenterActorToBounds", "Recenter Actor To Bounds"));
    Modify();

    // --- 4. Cache world-space spline point positions before the actor moves ---
    // We work in world space so the result is correct regardless of each spline
    // component's own relative transform.
    TArray<USplineComponent*> SplineComponents;
    GetComponents<USplineComponent>(SplineComponents);

    struct FSplinePointCache
    {
        FVector WorldPos;
        FVector WorldArriveTangent;
        FVector WorldLeaveTangent;
        ESplinePointType::Type PointType;
    };
    TArray<TArray<FSplinePointCache>> CachedSplineData;
    CachedSplineData.SetNum(SplineComponents.Num());

    for (int32 s = 0; s < SplineComponents.Num(); ++s)
    {
        USplineComponent* Spline = SplineComponents[s];
        if (!Spline) { continue; }

        const int32 N = Spline->GetNumberOfSplinePoints();
        CachedSplineData[s].SetNum(N);
        for (int32 i = 0; i < N; ++i)
        {
            CachedSplineData[s][i].WorldPos            = Spline->GetLocationAtSplinePoint(i, ESplineCoordinateSpace::World);
            CachedSplineData[s][i].WorldArriveTangent  = Spline->GetArriveTangentAtSplinePoint(i, ESplineCoordinateSpace::World);
            CachedSplineData[s][i].WorldLeaveTangent   = Spline->GetLeaveTangentAtSplinePoint(i, ESplineCoordinateSpace::World);
            CachedSplineData[s][i].PointType           = Spline->GetSplinePointType(i);
        }
    }

    // --- 5. Notify Blueprint subclasses to remap their custom local-space data ---
    // This happens before the actor moves so BPs receive a valid local-space delta.
    OnLocalSpaceDataRemapped(LocalDeltaTransform);

    // --- 6. Move the actor pivot to the bounds center ---
    SetActorLocation(NewWorldLocation);

    // --- 7. Restore spline points in world space ---
    // Now that the actor (and therefore each spline component) has moved, converting
    // the unchanged world positions back to component-local space gives the correct offsets.
    for (int32 s = 0; s < SplineComponents.Num(); ++s)
    {
        USplineComponent* Spline = SplineComponents[s];
        if (!Spline) { continue; }

        Spline->Modify();
        const int32 N = CachedSplineData[s].Num();
        for (int32 i = 0; i < N; ++i)
        {
            const FSplinePointCache& Pt = CachedSplineData[s][i];
            Spline->SetSplinePointType(i, Pt.PointType, /*bUpdateSpline=*/false);
            Spline->SetLocationAtSplinePoint(i, Pt.WorldPos, ESplineCoordinateSpace::World, /*bUpdateSpline=*/false);
            Spline->SetTangentsAtSplinePoint(i, Pt.WorldArriveTangent, Pt.WorldLeaveTangent, ESplineCoordinateSpace::World, /*bUpdateSpline=*/false);
        }
        Spline->UpdateSpline();
    }

    // --- 8. Recompute the bounds box (its relative location should now be near zero) ---
    ApplyBoundsToBox();

    MarkPackageDirty();
#endif
}

void APCGActorBase::OnLocalSpaceDataRemapped_Implementation(const FTransform& LocalDeltaTransform)
{
    // Base implementation is intentionally empty.
    // Override in Blueprint to remap custom local-space data such as
    // FVector properties with ShowAsWidgets enabled:
    //   MyLocalVector = MyLocalVector + LocalDeltaTransform.GetTranslation()
}
