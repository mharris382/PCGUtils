#include "Elements/PCGGetSplineDataWithOverrides.h"

#include "OverrideGraphs.h"
#include "Components/PCGSplineComponent.h"
#include "Data/PCGSplineData.h"
#include "Metadata/PCGMetadata.h"
#include "Metadata/Accessors/PCGAttributeAccessorHelpers.h"

#include "GameFramework/Actor.h"

DEFINE_LOG_CATEGORY_STATIC(LogPCGGetSplineDataWithOverrides, Log, All);


#define LOCTEXT_NAMESPACE "PCGGetSplineDataWithOverridesElement"

namespace PCGGetSplineDataWithOverridesAttributes
{
    // Attribute name constants — format is "StructPropertyName.FieldName"
    // matching the property-name contract consumed by PCG override resolver graphs.

    static const FName PreBakeGraph_UseGraph    = FName("bUsePreBakeGraph");
    static const FName PreBakeGraph_Graph       = FName("PreBakeGraph");

    static const FName PostBakeGraph_UseGraph   = FName("bUsePostBakeGraph");
    static const FName PostBakeGraph_Graph      = FName("PostBakeGraph");

    static const FName PreProcessSplineGraph_UseGraph  = FName("bUsePreProcessSplineGraph");
    static const FName PreProcessSplineGraph_Graph     = FName("PreProcessSplineGraph");

    static const FName PostSpawnGraph_UseGraph  = FName("bUsePostSpawnGraph");
    static const FName PostSpawnGraph_Graph     = FName("PostSpawnGraph");
}

UPCGGetSplineDataWithOverridesSettings::UPCGGetSplineDataWithOverridesSettings()
{
	Mode = EPCGGetDataFromActorMode::ParseActorComponents;
}

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

    auto NameTagsToStringTags = [](const FName& InName) { return InName.ToString(); };
    TSet<FString> ActorTags;
    Algo::Transform(FoundActor->Tags, ActorTags, NameTagsToStringTags);

    // Build requested contract filter from settings bools.
    TArray<FName> RequestedContracts;
    if (OverrideSettings->bExtractPreProcessSpline) RequestedContracts.Add(TEXT("PreProcessSplineGraph"));
    if (OverrideSettings->bExtractPostSpawn)        RequestedContracts.Add(TEXT("PostSpawnGraph"));
    if (OverrideSettings->bExtractPreBake)          RequestedContracts.Add(TEXT("PreBakeGraph"));
    if (OverrideSettings->bExtractPostBake)         RequestedContracts.Add(TEXT("PostBakeGraph"));

    TInlineComponentArray<USplineComponent*, 4> Splines;
    FoundActor->GetComponents(Splines);

    for (USplineComponent* SplineComponent : Splines)
    {
        if (!SplineComponent->Implements<UPCGOverrideGraphProvider>())
        {
            UE_LOG(LogPCGGetSplineDataWithOverrides, Warning,
                TEXT("Actor '%s': spline component does not implement IPCGOverrideGraphProvider. "
                     "Override graphs will not be extracted."),
                *FoundActor->GetName());
            continue;
        }

        UPCGSplineData* SplineData = NewObject<UPCGSplineData>();
        SplineData->Initialize(SplineComponent);

        if (UPCGMetadata* Metadata = SplineData->Metadata)
        {
            TArray<FPCGNamedOverrideGraph> Entries =
                UPCGOverrideGraphLibrary::GetOverrideEntriesFromObject(
                    SplineComponent, RequestedContracts);

            UPCGOverrideGraphLibrary::StampOverrideEntriesToMetadata(Entries, Metadata);
        }

        FPCGTaggedData& TaggedData = Context->OutputData.TaggedData.Emplace_GetRef();
        TaggedData.Data = SplineData;
        Algo::Transform(SplineComponent->ComponentTags, TaggedData.Tags, NameTagsToStringTags);
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

    const FName UseGraphAttr  = FName(TEXT("bUse") + AttributePrefix );
    const FName GraphAttr     = FName(AttributePrefix);

    // FindOrCreate is safer than HasAttribute + CreateAttribute — avoids the
    // TOCTOU gap and handles the first-write-wins contract cleanly.
    // if (FPCGMetadataAttribute<bool>* UseGraphAttribute =
    //     Metadata->FindOrCreateAttribute<bool>(UseGraphAttr, false,
    //         /*bAllowsInterpolation=*/false, /*bOverrideParent=*/false,
    //         /*bOverwriteIfTypeMismatch=*/false))
    // {
    //     UseGraphAttribute->SetValue(PCGInvalidEntryKey, bUseGraph);
    // }

    if (FPCGMetadataAttribute<FSoftObjectPath>* GraphAttribute =
        Metadata->FindOrCreateAttribute<FSoftObjectPath>(GraphAttr, FSoftObjectPath(),
            /*bAllowsInterpolation=*/false, /*bOverrideParent=*/false,
            /*bOverwriteIfTypeMismatch=*/false))
    {
        GraphAttribute->SetValue(PCGInvalidEntryKey, GraphPath);
    }
}


#undef LOCTEXT_NAMESPACE