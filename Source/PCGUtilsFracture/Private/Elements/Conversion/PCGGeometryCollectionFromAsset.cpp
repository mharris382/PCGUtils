// Copyright Max Harris

#include "Elements/Conversion/PCGGeometryCollectionFromAsset.h"

#include "Data/PCGGeometryCollectionData.h"
#include "Data/PCGUtilsGeometryCollectionRevisionPublisher.h"
#include "FunctionLibraries/PCGUtilsGeometryCollectionHelpers.h"
#include "GeometryCollection/GeometryCollection.h"
#include "GeometryCollection/GeometryCollectionObject.h"
#include "PCGPin.h"
#include "PCGUtilsFracture.h"
#include "Utils/PCGLogErrors.h"

#if WITH_EDITOR
#include "Helpers/PCGDynamicTrackingHelpers.h"
#endif

#define LOCTEXT_NAMESPACE "PCGGeometryCollectionFromAsset"

#if WITH_EDITOR
FText UPCGGeometryCollectionFromAssetSettings::GetDefaultNodeTitle() const
{
	return LOCTEXT("Title", "GC | From Asset");
}

FText UPCGGeometryCollectionFromAssetSettings::GetNodeTooltipText() const
{
	return LOCTEXT("Tooltip",
		"Imports an existing Geometry Collection asset as transient, mutable GC data - the counterpart of Mesh "
		"To Dynamic Mesh. The asset is immutable and is never modified: its collection is deep-copied on the way "
		"in, so fracture, prune and selection downstream all work on a private copy that lives and dies inside "
		"one graph execution. Use it to carry hand-authored fracture work into PCG, then drive further levels, "
		"pruning or piece scattering from the graph.");
}

FString UPCGGeometryCollectionFromAssetSettings::GetAdditionalTitleInformation() const
{
	return Asset.IsNull() ? FString() : Asset.GetAssetName();
}

void UPCGGeometryCollectionFromAssetSettings::GetStaticTrackedKeys(
	FPCGSelectionKeyToSettingsMap& OutKeysToSettings,
	TArray<TObjectPtr<const UPCGGraph>>& OutVisitedGraphs) const
{
	// An overridden asset is not static - it is tracked per execution in ExecuteInternal instead.
	if (Asset.IsNull() ||
		IsPropertyOverriddenByPin(GET_MEMBER_NAME_CHECKED(UPCGGeometryCollectionFromAssetSettings, Asset)))
	{
		return;
	}

	FPCGSelectionKey Key = FPCGSelectionKey::CreateFromPath(Asset.ToSoftObjectPath());
	OutKeysToSettings.FindOrAdd(MoveTemp(Key)).Emplace(this, /*bCulling=*/false);
}
#endif // WITH_EDITOR

TArray<FPCGPinProperties> UPCGGeometryCollectionFromAssetSettings::InputPinProperties() const
{
	// A source element: it has nothing to operate on, only an asset to read.
	return {};
}

TArray<FPCGPinProperties> UPCGGeometryCollectionFromAssetSettings::OutputPinProperties() const
{
	TArray<FPCGPinProperties> Pins;
	Pins.Add(FPCGPinProperties(
		PCGGeometryCollectionFromAssetConstants::CollectionOutputPin,
		FPCGGeometryCollectionDataTypeInfo::AsId(),
		/*bAllowMultipleConnections=*/true, /*bAllowMultipleData=*/true));
	return Pins;
}

FPCGElementPtr UPCGGeometryCollectionFromAssetSettings::CreateElement() const
{
	return MakeShared<FPCGGeometryCollectionFromAssetElement>();
}

bool FPCGGeometryCollectionFromAssetElement::CanExecuteOnlyOnMainThread(FPCGContext* Context) const
{
	// Without a context we cannot tell which phase this is, so assume the one that needs the game thread.
	return !Context || Context->CurrentPhase == EPCGExecutionPhase::PrepareData;
}

FPCGContext* FPCGGeometryCollectionFromAssetElement::CreateContext()
{
	return new FPCGGeometryCollectionFromAssetContext();
}

bool FPCGGeometryCollectionFromAssetElement::PrepareDataInternal(FPCGContext* InContext) const
{
	TRACE_CPUPROFILER_EVENT_SCOPE(FPCGGeometryCollectionFromAssetElement::PrepareData);

	FPCGGeometryCollectionFromAssetContext* Context =
		static_cast<FPCGGeometryCollectionFromAssetContext*>(InContext);
	check(Context);

	const UPCGGeometryCollectionFromAssetSettings* Settings =
		Context->GetInputSettings<UPCGGeometryCollectionFromAssetSettings>();
	check(Settings);

	if (Context->WasLoadRequested() || Settings->Asset.IsNull())
	{
		return true;
	}

	// Asynchronous by default, so a cold asset does not stall the graph's whole execution.
	return Context->RequestResourceLoad(
		Context, {Settings->Asset.ToSoftObjectPath()}, !Settings->bSynchronousLoad);
}

bool FPCGGeometryCollectionFromAssetElement::ExecuteInternal(FPCGContext* Context) const
{
	TRACE_CPUPROFILER_EVENT_SCOPE(FPCGGeometryCollectionFromAssetElement::Execute);
	check(Context);

	const UPCGGeometryCollectionFromAssetSettings* Settings =
		Context->GetInputSettings<UPCGGeometryCollectionFromAssetSettings>();
	check(Settings);

	if (Settings->Asset.IsNull())
	{
		PCGLog::LogErrorOnGraph(
			LOCTEXT("NoAsset", "GC | From Asset has no Geometry Collection asset selected."), Context);
		return true;
	}

	const UGeometryCollection* Asset = Settings->Asset.Get();
	if (!Asset)
	{
		PCGLog::LogErrorOnGraph(FText::Format(
			LOCTEXT("AssetFailedToLoad",
				"Geometry Collection asset '{0}' failed to load."),
			FText::FromString(Settings->Asset.ToSoftObjectPath().ToString())), Context);
		return true;
	}

#if WITH_EDITOR
	if (Context->IsValueOverriden(GET_MEMBER_NAME_CHECKED(UPCGGeometryCollectionFromAssetSettings, Asset)))
	{
		FPCGDynamicTrackingHelper::AddSingleDynamicTrackingKey(
			Context, FPCGSelectionKey::CreateFromPath(Settings->Asset.ToSoftObjectPath()), /*bIsCulled=*/false);
	}
#endif

	const TSharedPtr<FGeometryCollection, ESPMode::ThreadSafe> SourceCollection = Asset->GetGeometryCollection();
	if (!SourceCollection.IsValid() ||
		SourceCollection->NumElements(FGeometryCollection::TransformGroup) == 0)
	{
		PCGLog::LogErrorOnGraph(FText::Format(
			LOCTEXT("EmptyAsset",
				"Geometry Collection asset '{0}' holds no bones, so there is nothing to import."),
			FText::FromString(Asset->GetName())), Context);
		return true;
	}

	// The whole immutability contract in one call. CopyTo fills the freshly-constructed collection's external
	// TManagedArray members in place rather than orphaning them, which is why this rather than an assignment -
	// the same reasoning as UPCGGeometryCollectionData::CreateMutableCopy. The asset keeps its own collection.
	TSharedRef<FGeometryCollection> Collection = MakeShared<FGeometryCollection>();
	SourceCollection->CopyTo(&Collection.Get());

	// This collection did not come from this module, so what it carries cannot be assumed. The fracture backend
	// guards on these attributes silently and just returns INDEX_NONE, so naming the gap at the import is the
	// difference between an actionable message and a mystery at the far end of the graph.
	TArray<FString> MissingAttributes;
	if (!PCGUtilsGeometryCollectionHelpers::ValidateFractureRequirements(*Collection, MissingAttributes))
	{
		PCGLog::LogErrorOnGraph(FText::Format(
			LOCTEXT("MalformedAsset",
				"Geometry Collection asset '{0}' is missing the attribute(s) fracture requires: {1}. The asset "
				"cannot be used as a fracture source."),
			FText::FromString(Asset->GetName()),
			FText::FromString(FString::Join(MissingAttributes, TEXT(", ")))), Context);
		return true;
	}

	TArray<TObjectPtr<UMaterialInterface>> Materials;
	if (Settings->bExtractMaterials)
	{
		Materials = Asset->Materials;
	}

	// A new lineage, not a revision: nothing upstream of this node produced a collection, so there is no
	// earlier state for a selection to be validated against. Publishing is also what gives the imported
	// collection its Level attribute, per-bone ids and material sections - the guarantees every downstream
	// selector relies on and which an arbitrary asset may well not already satisfy.
	FPCGUtilsGeometryCollectionPublishOptions PublishOptions;
	PublishOptions.bCompactHiddenGeometry = !Settings->bKeepHiddenGeometry;

	UPCGGeometryCollectionData* OutputData = PCGUtilsGeometryCollectionRevisionPublisher::PublishNewLineage(
		Context, Collection, MoveTemp(Materials), PublishOptions);
	if (!OutputData)
	{
		return true;
	}

	FPCGTaggedData& Output = Context->OutputData.TaggedData.Emplace_GetRef();
	Output.Data = OutputData;
	Output.Pin = PCGGeometryCollectionFromAssetConstants::CollectionOutputPin;

	UE_LOG(LogPCGUtilsFracture, Verbose, TEXT("GC From Asset: '%s' -> %s"),
		*Asset->GetName(), *PCGUtilsGeometryCollectionHelpers::DescribeCollection(*Collection));
	return true;
}

#undef LOCTEXT_NAMESPACE
