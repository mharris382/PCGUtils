// Copyright Max Harris

#include "Elements/Conversion/PCGSaveGeometryCollectionToAsset.h"

#include "Data/PCGGeometryCollectionData.h"
#include "Metadata/PCGMetadata.h"
#include "PCGAssetExporterUtils.h"
#include "PCGContext.h"
#include "PCGParamData.h"
#include "PCGPin.h"
#include "Utils/PCGLogErrors.h"

#define LOCTEXT_NAMESPACE "PCGSaveGeometryCollectionToAsset"

FPCGGeometryCollectionAssetBakeSettings::FPCGGeometryCollectionAssetBakeSettings()
{
	// A UGeometryCollection always has one catch-all entry. Mirroring that here keeps a newly baked asset's
	// collision behaviour identical to a collection created through the Content Browser.
	SizeSpecificData.AddDefaulted();
}

void FPCGGeometryCollectionAssetBakeSettings::ApplyTo(UGeometryCollection& Asset) const
{
	Asset.EnableNanite = bEnableNanite;
	Asset.bEnableNaniteFallback = bEnableNanite && bEnableNaniteFallback;
	Asset.NaniteMinimumResidencyInKB = static_cast<uint32>(FMath::Max(0, NaniteMinimumResidencyInKB));
	Asset.bUseFullPrecisionUVs = bUseFullPrecisionUVs;
	Asset.bConvertVertexColorsToSRGB = bConvertVertexColorsToSRGB;
	Asset.bSupportRayTracing = bSupportRayTracing;
	Asset.bStripOnCook = bStripSourceDataOnCook;
	Asset.bStripRenderDataOnCook = bStripRenderDataOnCook;

	Asset.EnableClustering = bEnableClustering;
	Asset.ClusterGroupIndex = ClusterGroupIndex;
	Asset.MaxClusterLevel = FMath::Max(0, MaxClusterLevel);
	Asset.ClusterConnectionType = ClusterConnectionType;
	Asset.ConnectionGraphBoundsFilteringMargin = FMath::Max(0.0f, ConnectionGraphBoundsFilteringMargin);

	Asset.DamageModel = DamageModel;
	Asset.DamageThreshold = DamageThreshold;
	Asset.bUseSizeSpecificDamageThreshold = bUseSizeSpecificDamageThreshold;
	Asset.bUseMaterialDamageModifiers = bUseMaterialDamageModifiers;
	Asset.PerClusterOnlyDamageThreshold = bPerClusterOnlyDamageThreshold;

	Asset.bDensityFromPhysicsMaterial = false;
	Asset.bMassAsDensity = bMassAsDensity;
	Asset.Mass = FMath::Max(0.0f, Mass);
	Asset.MinimumMassClamp = FMath::Max(0.0f, MinimumMassClamp);
	Asset.bOptimizeConvexes = bOptimizeConvexes;
	Asset.SizeSpecificData = SizeSpecificData;
	if (Asset.SizeSpecificData.IsEmpty())
	{
		Asset.SizeSpecificData.Add(UGeometryCollection::GeometryCollectionSizeSpecificDataDefaults());
	}

	// ResetFrom already invalidates after replacing the collection. Invalidate once more after applying all
	// simulation-affecting properties so the DDC key and render state describe this exact baked configuration.
	Asset.InvalidateCollection();
}

#if WITH_EDITOR
FText UPCGSaveGeometryCollectionToAssetSettings::GetDefaultNodeTitle() const
{
	return LOCTEXT("Title", "GC | Save Asset");
}

FText UPCGSaveGeometryCollectionToAssetSettings::GetNodeTooltipText() const
{
	return LOCTEXT("Tooltip",
		"Saves the first incoming GC as a Geometry Collection asset using PCG's standard asset exporter path. "
		"Materials and the GC-specific rendering, collision, mass, clustering, and damage configuration are "
		"baked onto the asset. Editor-only and never cached.");
}
#endif

TArray<FPCGPinProperties> UPCGSaveGeometryCollectionToAssetSettings::InputPinProperties() const
{
	TArray<FPCGPinProperties> Pins;
	Pins.Emplace_GetRef(
		PCGSaveGeometryCollectionToAssetConstants::CollectionInputPin,
		FPCGGeometryCollectionDataTypeInfo::AsId(),
		/*bAllowMultipleConnections=*/false,
		/*bAllowMultipleData=*/false).SetRequiredPin();
	return Pins;
}

TArray<FPCGPinProperties> UPCGSaveGeometryCollectionToAssetSettings::OutputPinProperties() const
{
	TArray<FPCGPinProperties> Pins;
	Pins.Emplace(
		PCGSaveGeometryCollectionToAssetConstants::AssetPathOutputPin,
		EPCGDataType::Param,
		/*bAllowMultipleConnections=*/true,
		/*bAllowMultipleData=*/false);
	return Pins;
}

FPCGElementPtr UPCGSaveGeometryCollectionToAssetSettings::CreateElement() const
{
	return MakeShared<FPCGSaveGeometryCollectionToAssetElement>();
}

bool FPCGSaveGeometryCollectionToAssetElement::ExecuteInternal(FPCGContext* Context) const
{
	TRACE_CPUPROFILER_EVENT_SCOPE(FPCGSaveGeometryCollectionToAssetElement::Execute);
	check(Context);

#if WITH_EDITOR
	const UPCGSaveGeometryCollectionToAssetSettings* Settings =
		Context->GetInputSettings<UPCGSaveGeometryCollectionToAssetSettings>();
	check(Settings);

	const TArray<FPCGTaggedData> Inputs = Context->InputData.GetInputsByPin(
		PCGSaveGeometryCollectionToAssetConstants::CollectionInputPin);
	if (Inputs.IsEmpty())
	{
		return true;
	}

	const UPCGGeometryCollectionData* CollectionData = Cast<const UPCGGeometryCollectionData>(Inputs[0].Data);
	if (!CollectionData || !CollectionData->HasCollection())
	{
		PCGLog::LogErrorOnGraph(
			LOCTEXT("MissingCollection", "GC | Save Asset requires valid GC data on its GC pin."), Context);
		return true;
	}

	UObject* WrittenObject = nullptr;
	UPackage* Package = UPCGAssetExporterUtils::CreateAsset<UGeometryCollection>(
		Settings->ExportParams,
		[CollectionData, Context, Settings, &WrittenObject](const FString&, UObject* Object)
		{
			UGeometryCollection* Asset = CastChecked<UGeometryCollection>(Object);
			TArray<TObjectPtr<UMaterialInterface>> Materials;
			if (Settings->bExportMaterials)
			{
				Materials = CollectionData->GetMaterials();
			}

			Asset->ResetFrom(CollectionData->GetCollection(), Materials, /*bHasInternalMaterials=*/false);
			Settings->BakeSettings.ApplyTo(*Asset);
			Asset->RebuildRenderData();
			Asset->CreateSimulationDataIfNeeded();
			Asset->PropagateTransformUpdateToComponents();
			WrittenObject = Asset;
			return true;
		},
		Context);

	if (!Package || !WrittenObject)
	{
		return true;
	}

	UPCGParamData* OutputData = FPCGContext::NewObject_AnyThread<UPCGParamData>(Context);
	check(OutputData && OutputData->Metadata);
	OutputData->Metadata->CreateAttribute<FSoftObjectPath>(
		PCGSaveGeometryCollectionToAssetConstants::AssetPathAttribute,
		FSoftObjectPath(WrittenObject),
		/*bAllowsInterpolation=*/false,
		/*bOverrideParent=*/false);
	OutputData->Metadata->AddEntry();

	FPCGTaggedData& Output = Context->OutputData.TaggedData.Emplace_GetRef();
	Output.Data = OutputData;
	Output.Pin = PCGSaveGeometryCollectionToAssetConstants::AssetPathOutputPin;
	Output.Tags = Inputs[0].Tags;
#else
	PCGLog::LogWarningOnGraph(
		LOCTEXT("CannotExportInNonEditor", "GC | Save Asset can only write assets in an editor build."), Context);
#endif

	return true;
}

#undef LOCTEXT_NAMESPACE
