#include "Components/PCGSplineComponent.h"
#include "Components/BoxComponent.h"

#if WITH_EDITOR
void UPCGSplineComponent::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
    Super::PostEditChangeProperty(PropertyChangedEvent);
    FLinearColor selectedColor;
    FLinearColor unselectedColor;
    FLinearColor tangentColor;
    GetSplineEditorColors(selectedColor, unselectedColor, tangentColor);
#if WITH_EDITORONLY_DATA
    EditorUnselectedSplineSegmentColor = unselectedColor;
    EditorSelectedSplineSegmentColor = selectedColor;
    EditorTangentColor = tangentColor;
#endif
}
#endif


void UPCGSplineComponent::GetSplineEditorColors_Implementation(FLinearColor& UnselectedColor,
    FLinearColor& SelectedColor, FLinearColor& TangentColor) const
{
    UnselectedColor =FLinearColor(0.1f, 0.8f, 0.1f, 1.f);
    SelectedColor  = FLinearColor(1.0f, 0.9f, 0.1f, 1.f);
    TangentColor = FLinearColor(1.0f, 0.9f, 0.1f, 1.f);
}


UPCGSplineComponent::UPCGSplineComponent(const FObjectInitializer& ObjectInitializer) : Super(ObjectInitializer)
{
    ComponentTags.Add(TEXT("pcg_spline"));
}

namespace
{
    static TArray<USplineComponent*> GetActorSplineComponents(const AActor* actor, const USplineComponent* filterFor)
    {
        if (!actor) return TArray<USplineComponent*>();
        TArray<USplineComponent*> res;
        for (UActorComponent* Comp : actor->GetComponents())
        {
            USplineComponent* SplineComp = Cast<USplineComponent>(Comp);
            if (!SplineComp || SplineComp == filterFor) continue;
            res.Add(SplineComp);
        }
        return res;
    }
}


TArray<USplineComponent*> UPCGSplineComponent::GetSnapTargets() const
{
    TArray<USplineComponent*> res;

    UE_LOG(LogTemp, Warning, TEXT("PCGSnap [%s] GetSnapTargets: SnapTargetMode=%d"), *GetName(), (int32)SnapTargetMode);

    switch (SnapTargetMode)
    {
    case EPCGSplineSnapTargetMode::ComponentReferences:
        UE_LOG(LogTemp, Warning, TEXT("PCGSnap [%s]   Mode=ComponentReferences, SnapTargetComponents.Num()=%d"), *GetName(), SnapTargetComponents.Num());
        for (int32 i = 0; i < SnapTargetComponents.Num(); ++i)
        {
            UE_LOG(LogTemp, Warning, TEXT("PCGSnap [%s]   SnapTargetComponents[%d] = %s"),
                *GetName(), i, SnapTargetComponents[i] ? *SnapTargetComponents[i]->GetName() : TEXT("NULL"));
        }
        res = SnapTargetComponents;
        break;

    case EPCGSplineSnapTargetMode::ActorReferences:
        UE_LOG(LogTemp, Warning, TEXT("PCGSnap [%s]   Mode=ActorReferences, SnapTargetActors.Num()=%d"), *GetName(), SnapTargetActors.Num());
        for (auto actor : SnapTargetActors)
        {
            if (!actor)
            {
                UE_LOG(LogTemp, Warning, TEXT("PCGSnap [%s]   Skipping null actor entry"), *GetName());
                continue;
            }
            TArray<USplineComponent*> found = GetActorSplineComponents(actor, this);
            UE_LOG(LogTemp, Warning, TEXT("PCGSnap [%s]   Actor [%s] yielded %d spline(s)"), *GetName(), *actor->GetName(), found.Num());
            res.Append(found);
        }
        break;

    case EPCGSplineSnapTargetMode::Siblings:
        UE_LOG(LogTemp, Warning, TEXT("PCGSnap [%s]   Mode=Siblings, Owner=%s"),
            *GetName(), GetOwner() ? *GetOwner()->GetName() : TEXT("NULL"));
        if (!GetOwner()) break;
        {
            TArray<USplineComponent*> siblings = GetActorSplineComponents(GetOwner(), this);
            UE_LOG(LogTemp, Warning, TEXT("PCGSnap [%s]   Found %d sibling spline(s)"), *GetName(), siblings.Num());
            res.Append(siblings);
        }
        break;

    case EPCGSplineSnapTargetMode::OverlappingWorldActors:
        UE_LOG(LogTemp, Warning, TEXT("PCGSnap [%s]   Mode=OverlappingWorldActors (not yet implemented)"), *GetName());
        if (!GetOwner() || !GetWorld()) break;
        return res;
    }

    // Tag filtering
    if (!SnapTargetComponentTag.IsEmpty() && SnapTargetMode != EPCGSplineSnapTargetMode::ComponentReferences)
    {
        const int32 beforeFilter = res.Num();
        const FName TagName(*SnapTargetComponentTag);
        res.RemoveAll([&TagName](const USplineComponent* spline)
        {
            return !spline || !spline->ComponentHasTag(TagName);
        });
        UE_LOG(LogTemp, Warning, TEXT("PCGSnap [%s]   Tag filter '%s': %d -> %d splines"),
            *GetName(), *SnapTargetComponentTag, beforeFilter, res.Num());
    }
    else
    {
        UE_LOG(LogTemp, Warning, TEXT("PCGSnap [%s]   No tag filter applied (tag='%s', mode=%d)"),
            *GetName(), *SnapTargetComponentTag, (int32)SnapTargetMode);
    }

    UE_LOG(LogTemp, Warning, TEXT("PCGSnap [%s] GetSnapTargets returning %d target(s)"), *GetName(), res.Num());
    return res;
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

void UPCGSplineComponent::RefreshSnappedSplinePoints()
{
    const int32 cnt = GetNumberOfSplinePoints();
    const TArray<USplineComponent*> Targets = GetSnapTargets();

    UE_LOG(LogTemp, Warning, TEXT("PCGSnap [%s] RefreshSnappedSplinePoints: SnapMode=%d, SplinePoints=%d, Targets=%d"),
        *GetName(), (int32)SnapMode, cnt, Targets.Num());

    if (SnapMode == EPCGSplineSnapMode::None)
    {
        UE_LOG(LogTemp, Warning, TEXT("PCGSnap [%s]   SnapMode is None — nothing to do"), *GetName());
    }
    if (Targets.Num() == 0)
    {
        UE_LOG(LogTemp, Warning, TEXT("PCGSnap [%s]   No targets — snapping will be skipped for all points"), *GetName());
    }

    if (SnappedSplinePoints.Num() != cnt)
    {
        SnappedSplinePoints.Empty();
        for (int i = 0; i < cnt; ++i)
        {
            SnappedSplinePoints.Add(FindSnappedSplinePointFor(i, Targets));
        }
    }
    else
    {
        for (int i = 0; i < cnt; ++i)
        {
            SnappedSplinePoints[i] = FindSnappedSplinePointFor(i, Targets);
        }
    }

    UE_LOG(LogTemp, Warning, TEXT("PCGSnap [%s] RefreshSnappedSplinePoints done. SnappedSplinePoints.Num()=%d"), *GetName(), SnappedSplinePoints.Num());
}

FSplinePoint UPCGSplineComponent::FindSnappedSplinePointFor(const int splinePointIndex, const TArray<USplineComponent*> targets) const
{
    FSplinePoint point = GetSplinePointAt(splinePointIndex, ESplineCoordinateSpace::Local);

    if (targets.Num() == 0)
    {
        UE_LOG(LogTemp, Warning, TEXT("PCGSnap [%s] point %d: skipped (no targets)"), *GetName(), splinePointIndex);
        return point;
    }
    if (SnapMode == EPCGSplineSnapMode::None)
    {
        UE_LOG(LogTemp, Warning, TEXT("PCGSnap [%s] point %d: skipped (SnapMode=None)"), *GetName(), splinePointIndex);
        return point;
    }

    const int32 lastIdx = GetNumberOfSplinePoints() - 1;
    switch (SnapMode)
    {
        case EPCGSplineSnapMode::Start:
            if (splinePointIndex != 0)
            {
                UE_LOG(LogTemp, Warning, TEXT("PCGSnap [%s] point %d: skipped (mode=Start, not first point)"), *GetName(), splinePointIndex);
                return point;
            }
            break;
        case EPCGSplineSnapMode::End:
            if (splinePointIndex != lastIdx)
            {
                UE_LOG(LogTemp, Warning, TEXT("PCGSnap [%s] point %d: skipped (mode=End, not last point)"), *GetName(), splinePointIndex);
                return point;
            }
            break;
        case EPCGSplineSnapMode::StartAndEnd:
            if (splinePointIndex != 0 && splinePointIndex != lastIdx)
            {
                UE_LOG(LogTemp, Warning, TEXT("PCGSnap [%s] point %d: skipped (mode=StartAndEnd, not first or last)"), *GetName(), splinePointIndex);
                return point;
            }
            break;
        case EPCGSplineSnapMode::AllPoints:
            break;
        default:
            return point;
    }

    UE_LOG(LogTemp, Warning, TEXT("PCGSnap [%s] point %d: entering snap search (localPos=%.1f,%.1f,%.1f)"),
        *GetName(), splinePointIndex, point.Position.X, point.Position.Y, point.Position.Z);
    return GetSnappedSplinePoint(splinePointIndex, point, targets);
}

FSplinePoint UPCGSplineComponent::GetSnappedSplinePoint(const int splinePointIndex,
    const FSplinePoint& SplinePoint,
    const TArray<USplineComponent*>& TargetSplines) const
{
    FSplinePoint Result = SplinePoint;

    const FTransform MyTransform = GetComponentTransform();
    UE_LOG(LogTemp, Warning, TEXT("PCGSnap [%s] point %d: MyTransform valid=%s, Location=%.1f,%.1f,%.1f"),
        *GetName(), splinePointIndex,
        MyTransform.IsValid() ? TEXT("yes") : TEXT("NO"),
        MyTransform.GetLocation().X, MyTransform.GetLocation().Y, MyTransform.GetLocation().Z);

    const FVector SourceWorldPos = MyTransform.TransformPosition(SplinePoint.Position);
    UE_LOG(LogTemp, Warning, TEXT("PCGSnap [%s] point %d: SourceWorldPos=%.1f,%.1f,%.1f"),
        *GetName(), splinePointIndex, SourceWorldPos.X, SourceWorldPos.Y, SourceWorldPos.Z);

    float NearestDistSq = TNumericLimits<float>::Max();
    USplineComponent* NearestSpline = nullptr;
    float NearestT = -1.0f;

    for (int32 si = 0; si < TargetSplines.Num(); ++si)
    {
        USplineComponent* s = TargetSplines[si];
        if (!s)
        {
            UE_LOG(LogTemp, Warning, TEXT("PCGSnap [%s] point %d:   target[%d] is NULL — skipping"), *GetName(), splinePointIndex, si);
            continue;
        }

        const int32 targetPtCount = s->GetNumberOfSplinePoints();
        UE_LOG(LogTemp, Warning, TEXT("PCGSnap [%s] point %d:   target[%d]='%s' pts=%d"),
            *GetName(), splinePointIndex, si, *s->GetName(), targetPtCount);

        if (targetPtCount < 2)
        {
            UE_LOG(LogTemp, Warning, TEXT("PCGSnap [%s] point %d:   target[%d] has < 2 points, skipping"), *GetName(), splinePointIndex, si);
            continue;
        }

        // Sample the target spline at fixed intervals to find the nearest point.
        // Avoids FindNearest which is deprecated and unreliable on non-uniform splines.
        const float SplineLength = s->GetSplineLength();
        const float StepSize = FMath::Max(SnapSampleInterval, 1.f);
        float BestT = 0.f;
        float BestWorldDistSq = TNumericLimits<float>::Max();

        for (float Dist = 0.f; ; Dist += StepSize)
        {
            const float ClampedDist = FMath::Min(Dist, SplineLength);
            const FVector SampleWorldPos = s->GetLocationAtDistanceAlongSpline(ClampedDist, ESplineCoordinateSpace::World);
            const float SampleDistSq = FVector::DistSquared(SourceWorldPos, SampleWorldPos);
            if (SampleDistSq < BestWorldDistSq)
            {
                BestWorldDistSq = SampleDistSq;
                BestT = s->GetInputKeyValueAtDistanceAlongSpline(ClampedDist);
            }
            if (ClampedDist >= SplineLength) break;
        }

        const float T = BestT;
        const float WorldDistSq = BestWorldDistSq;
        const FVector SnappedWorldPos = s->GetLocationAtSplineInputKey(T, ESplineCoordinateSpace::World);

        UE_LOG(LogTemp, Warning, TEXT("PCGSnap [%s] point %d:   target[%d] T=%.3f WorldDist=%.1f SnappedWorld=%.1f,%.1f,%.1f"),
            *GetName(), splinePointIndex, si, T, FMath::Sqrt(WorldDistSq),
            SnappedWorldPos.X, SnappedWorldPos.Y, SnappedWorldPos.Z);

        if (WorldDistSq < NearestDistSq)
        {
            NearestDistSq = WorldDistSq;
            NearestSpline = s;
            NearestT = T;
        }
    }

    if (!NearestSpline)
    {
        UE_LOG(LogTemp, Warning, TEXT("PCGSnap [%s] point %d: NearestSpline is NULL after checking %d targets — no snap applied"),
            *GetName(), splinePointIndex, TargetSplines.Num());
        return Result;
    }

    FVector SnappedWorldLoc = NearestSpline->GetLocationAtSplineInputKey(NearestT, ESplineCoordinateSpace::World);
    const FRotator SnappedWorldRot = NearestSpline->GetRotationAtSplineInputKey(NearestT, ESplineCoordinateSpace::World);
    SnappedWorldLoc += bIsAbsoluteOffset ? SnappingOffset : SnappedWorldRot.RotateVector(SnappingOffset);

    const FVector SnappedLocalPos = GetComponentTransform().InverseTransformPosition(SnappedWorldLoc);

    FVector AcceptableOffset;
    if (bIsSoftRadiusAbsolute)
    {
        AcceptableOffset = SnappingSoftRadius;
    }
    else
    {
        const FVector WorldRadius = SnappedWorldRot.RotateVector(SnappingSoftRadius);
        AcceptableOffset = GetComponentTransform().GetRotation().Inverse().RotateVector(WorldRadius).GetAbs();
    }

    FVector Displacement = SnappedLocalPos - SplinePoint.Position;
    if (AcceptableOffset.X > 0.0f) Displacement.X = FMath::Clamp(Displacement.X, -AcceptableOffset.X, AcceptableOffset.X);
    if (AcceptableOffset.Y > 0.0f) Displacement.Y = FMath::Clamp(Displacement.Y, -AcceptableOffset.Y, AcceptableOffset.Y);
    if (AcceptableOffset.Z > 0.0f) Displacement.Z = FMath::Clamp(Displacement.Z, -AcceptableOffset.Z, AcceptableOffset.Z);

    Result.Position = SplinePoint.Position + Displacement;

    UE_LOG(LogTemp, Warning, TEXT("PCGSnap [%s] point %d: snapped to '%s' T=%.3f | orig=(%.1f,%.1f,%.1f) -> snapped=(%.1f,%.1f,%.1f)"),
        *GetName(), splinePointIndex, *NearestSpline->GetName(), NearestT,
        SplinePoint.Position.X, SplinePoint.Position.Y, SplinePoint.Position.Z,
        Result.Position.X, Result.Position.Y, Result.Position.Z);

    return PostProcessSnappedSplinePoint(splinePointIndex, Result, NearestSpline, NearestT);
}

