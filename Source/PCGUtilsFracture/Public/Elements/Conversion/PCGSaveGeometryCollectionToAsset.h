// Copyright Max Harris

#pragma once

#include "CoreMinimal.h"
#include "Elements/PCGUtilsFractureElementBase.h"
#include "GeometryCollection/GeometryCollectionObject.h"
#include "PCGAssetExporter.h"

#include "PCGSaveGeometryCollectionToAsset.generated.h"

namespace PCGSaveGeometryCollectionToAssetConstants
{
	inline const FName CollectionInputPin = TEXT("GC");
	inline const FName AssetPathOutputPin = TEXT("AssetPath");
	inline const FName AssetPathAttribute = TEXT("AssetPath");
}

/**
 * Geometry Collection settings that must be chosen when transient PCG GC data becomes a persistent asset.
 *
 * These deliberately mirror the asset properties that affect rendering and cooked Chaos simulation data. They
 * do not include component-only runtime behaviour such as removal-on-sleep or per-instance collision responses.
 */
USTRUCT(BlueprintType)
struct PCGUTILSFRACTURE_API FPCGGeometryCollectionAssetBakeSettings
{
	GENERATED_BODY()

	FPCGGeometryCollectionAssetBakeSettings();

	/** Applies these settings and invalidates the asset's derived render/simulation data. */
	void ApplyTo(UGeometryCollection& Asset) const;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Rendering")
	bool bEnableNanite = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Rendering",
		meta=(EditCondition="bEnableNanite", EditConditionHides))
	bool bEnableNaniteFallback = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Rendering",
		meta=(EditCondition="bEnableNanite", EditConditionHides))
	int32 NaniteMinimumResidencyInKB = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Rendering")
	bool bUseFullPrecisionUVs = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Rendering")
	bool bConvertVertexColorsToSRGB = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Rendering")
	bool bSupportRayTracing = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Cooking")
	bool bStripSourceDataOnCook = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Cooking")
	bool bStripRenderDataOnCook = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Clustering")
	bool bEnableClustering = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Clustering")
	int32 ClusterGroupIndex = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Clustering", meta=(ClampMin="0"))
	int32 MaxClusterLevel = 100;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Clustering")
	EClusterConnectionTypeEnum ClusterConnectionType =
		EClusterConnectionTypeEnum::Chaos_MinimalSpanningSubsetDelaunayTriangulation;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Clustering", meta=(ClampMin="0.0"))
	float ConnectionGraphBoundsFilteringMargin = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Damage")
	EDamageModelTypeEnum DamageModel = EDamageModelTypeEnum::Chaos_Damage_Model_UserDefined_Damage_Threshold;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Damage",
		meta=(EditCondition="DamageModel == EDamageModelTypeEnum::Chaos_Damage_Model_UserDefined_Damage_Threshold"))
	TArray<float> DamageThreshold = {500000.0f, 50000.0f, 5000.0f};

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Damage",
		meta=(EditCondition="DamageModel == EDamageModelTypeEnum::Chaos_Damage_Model_UserDefined_Damage_Threshold"))
	bool bUseSizeSpecificDamageThreshold = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Damage",
		meta=(EditCondition="DamageModel == EDamageModelTypeEnum::Chaos_Damage_Model_UserDefined_Damage_Threshold"))
	bool bUseMaterialDamageModifiers = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Damage", AdvancedDisplay)
	bool bPerClusterOnlyDamageThreshold = false;

	/** Treat Mass as density in kg/m^3; otherwise it is the collection's total mass in kg. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Mass")
	bool bMassAsDensity = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Mass", meta=(ClampMin="0.0"))
	float Mass = 2500.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Mass", meta=(ClampMin="0.0"))
	float MinimumMassClamp = 0.1f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Collision", AdvancedDisplay)
	bool bOptimizeConvexes = true;

	/** Size-dependent collision shapes and, optionally, size-dependent damage thresholds. */
	UPROPERTY(EditAnywhere, Category="Collision")
	TArray<FGeometryCollectionSizeSpecificData> SizeSpecificData;
};

/** Saves transient PCG GC data as a persistent Geometry Collection asset. */
UCLASS(BlueprintType, ClassGroup=(Procedural), Category="PCGUtils|Fracture",
	meta=(Keywords="GC Geometry Collection Asset Save Write Export Bake Chaos Fracture"))
class PCGUTILSFRACTURE_API UPCGSaveGeometryCollectionToAssetSettings
	: public UPCGUtilsFractureElementBaseSettings
{
	GENERATED_BODY()

public:
#if WITH_EDITOR
	virtual FName GetDefaultNodeName() const override { return TEXT("SaveGeometryCollectionToAsset"); }
	virtual FText GetDefaultNodeTitle() const override;
	virtual FText GetNodeTooltipText() const override;
#endif

	/** Standard PCG asset name, path, save-dialog, and save-on-export controls. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Asset",
		meta=(ShowOnlyInnerProperties, PCG_Overridable))
	FPCGAssetExporterParameters ExportParams;

	/** Copy the material slots carried by the GC data into the asset. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Asset", meta=(PCG_Overridable))
	bool bExportMaterials = true;

	/** Rendering, collision, mass, clustering, and damage settings baked onto the GC asset. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Asset",
		meta=(ShowOnlyInnerProperties, PCG_Overridable))
	FPCGGeometryCollectionAssetBakeSettings BakeSettings;

protected:
	virtual TArray<FPCGPinProperties> InputPinProperties() const override;
	virtual TArray<FPCGPinProperties> OutputPinProperties() const override;
	virtual FPCGElementPtr CreateElement() const override;
};

class PCGUTILSFRACTURE_API FPCGSaveGeometryCollectionToAssetElement final : public IPCGElement
{
public:
	virtual bool CanExecuteOnlyOnMainThread(FPCGContext* Context) const override { return true; }

protected:
	/** Asset writes are external state and must never disappear behind a PCG cache hit. */
	virtual bool IsCacheable(const UPCGSettings* InSettings) const override { return false; }
	virtual bool ExecuteInternal(FPCGContext* Context) const override;
};
