#include "Elements/PCGGetSplineDataWithOverrides.h"

#include "OverrideGraphs.h"
#include "Components/PCGSplineComponent.h"
#include "Data/PCGSplineData.h"
#include "Metadata/PCGMetadata.h"

#include "Algo/Transform.h"
#include "GameFramework/Actor.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(PCGGetSplineDataWithOverrides)

#define LOCTEXT_NAMESPACE "PCGGetSplineDataWithOverridesElement"

DEFINE_LOG_CATEGORY_STATIC(LogPCGGetSplineDataWithOverrides, Log, All);

UPCGGetSplineDataWithOverridesSettings::UPCGGetSplineDataWithOverridesSettings()
{
    Mode = EPCGGetDataFromActorMode::ParseActorComponents;
}

#if WITH_EDITOR
FText UPCGGetSplineDataWithOverridesSettings::GetNodeTooltipText() const
{
    return LOCTEXT("Tooltip",
        "Gets spline data from actors. If the spline component is a UPCGSplineComponent, "
        "active override graphs are stamped into the @Data domain for downstream resolution.");
}
#endif

FPCGElementPtr UPCGGetSplineDataWithOverridesSettings::CreateElement() const
{
    return MakeShared<FPCGGetSplineDataWithOverridesElement>();
}

void FPCGGetSplineDataWithOverridesElement::ProcessActor(
    FPCGContext* Context,
    const UPCGDataFromActorSettings* Settings,
    AActor* FoundActor) const
{
    check(Context && Settings);

    if (!FoundActor || !IsValid(FoundActor))
    {
        return;
    }
    const UPCGGetSplineDataWithOverridesSettings* OverrideSettings =
        CastChecked<UPCGGetSplineDataWithOverridesSettings>(Settings);

    const EPCGSplineOverrideGraphUsageFlags Flags = OverrideSettings->OverrideGraphUsageFlags;

    auto NameTagsToStringTags = [](const FName& InName) { return InName.ToString(); };
    TSet<FString> ActorTags;
    Algo::Transform(FoundActor->Tags, ActorTags, NameTagsToStringTags);

    TInlineComponentArray<USplineComponent*, 4> Splines;
    FoundActor->GetComponents(Splines);

    for (USplineComponent* SplineComponent : Splines)
    {
        UPCGSplineComponent* PCGSpline = Cast<UPCGSplineComponent>(SplineComponent);
        if (!PCGSpline)
        {
            UE_LOG(LogPCGGetSplineDataWithOverrides, Warning,
                TEXT("Actor '%s' has a spline component that is not a UPCGSplineComponent. "
                    "Override graphs will not be extracted."),
                *FoundActor->GetName());
            continue;
        }

        UPCGSplineData* SplineData = NewObject<UPCGSplineData>();
        SplineData->Initialize(PCGSpline);

        UPCGMetadata* Metadata = SplineData->Metadata;
        if (Metadata)
        {
            struct FOverrideEntry
            {
                const FPCGOverrideGraph& Override;
                const TCHAR* AttributePrefix;
                EPCGSplineOverrideGraphUsageFlags Flag;
            };

            const FOverrideEntry Overrides[] =
            {
                { PCGSpline->PreProcessSplineGraph, TEXT("PreProcessSplineGraph"), EPCGSplineOverrideGraphUsageFlags::PreProcessSpline },
                { PCGSpline->PostSpawnGraph,         TEXT("PostSpawnGraph"),        EPCGSplineOverrideGraphUsageFlags::PostSpawn        },
                { PCGSpline->PreBakeGraph,           TEXT("PreBakeGraph"),          EPCGSplineOverrideGraphUsageFlags::PreBake          },
                { PCGSpline->PostBakeGraph,          TEXT("PostBakeGraph"),         EPCGSplineOverrideGraphUsageFlags::PostBake         },
            };

            for (const FOverrideEntry& Entry : Overrides)
            {
                if (!EnumHasAnyFlags(Flags, Entry.Flag))
                {
                    continue;
                }
                if (Entry.Override.IsActive())
                {
                    StampOverrideToMetadata(Metadata,
                        Entry.AttributePrefix,
                        Entry.Override.bUseGraph,
                        FSoftObjectPath(Entry.Override.Graph.Get()));
                }
            }
        }

        FPCGTaggedData& TaggedData = Context->OutputData.TaggedData.Emplace_GetRef();
        TaggedData.Data = SplineData;
        Algo::Transform(PCGSpline->ComponentTags, TaggedData.Tags, NameTagsToStringTags);
        TaggedData.Tags.Append(ActorTags);
    }
}

void FPCGGetSplineDataWithOverridesElement::StampOverrideToMetadata(
    UPCGMetadata* Metadata,
    const FString& AttributePrefix,
    bool bUseGraph,
    const FSoftObjectPath& GraphPath)
{
    check(Metadata);

    const FPCGAttributeIdentifier UseGraphAttr(
        FName(TEXT("bUse") + AttributePrefix),
        PCGMetadataDomainID::Data);

    const FPCGAttributeIdentifier GraphAttr(
        FName(AttributePrefix),
        PCGMetadataDomainID::Data);

    // FindOrCreate is safer than HasAttribute + CreateAttribute — avoids the
    // TOCTOU gap and handles the first-write-wins contract cleanly.
    if (FPCGMetadataAttribute<bool>* UseGraphAttribute =
        Metadata->FindOrCreateAttribute<bool>(UseGraphAttr, false,
            /*bAllowsInterpolation=*/false, /*bOverrideParent=*/false,
            /*bOverwriteIfTypeMismatch=*/false))
    {
        UseGraphAttribute->SetValue(PCGInvalidEntryKey, bUseGraph);
    }

    if (FPCGMetadataAttribute<FSoftObjectPath>* GraphAttribute =
        Metadata->FindOrCreateAttribute<FSoftObjectPath>(GraphAttr, FSoftObjectPath(),
            /*bAllowsInterpolation=*/false, /*bOverrideParent=*/false,
            /*bOverwriteIfTypeMismatch=*/false))
    {
        GraphAttribute->SetValue(PCGInvalidEntryKey, GraphPath);
    }
}

#undef LOCTEXT_NAMESPACE