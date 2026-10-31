// Copyright Max Harris

#pragma once

#include "CoreMinimal.h"
#include "Details/PCGUtilsInputShorthands.h"
#include "Elements/PCGDynamicMeshBaseElement.h"
#include "GeometryScript/MeshAssetFunctions.h"
#include "PCGUtilsSettingsCategories.h"

#include "PCGUtilsDynMeshNodeColor.h"
#include "PCGDynMeshSaveStaticMesh.generated.h"

namespace PCGDynMeshSaveStaticMeshConstants
{
	inline const FName OutputLabel = TEXT("OutAssetPath");
}

/**
 * Saves every DynMesh on its input as a Static Mesh asset, in one run. The engine's Save Dynamic Mesh To Asset with
 * the same options, except that it saves only the first mesh it is given.
 *
 * Each mesh's asset is named by Asset Name and Asset Path. Either can be a constant or read from each mesh's @Data,
 * so a set of meshes can name themselves (Get Asset Save Parameters in Input Data mode writes both). A constant
 * Asset Name given more than one mesh gets the mesh's index on the pin as a suffix: Rock_0, Rock_1, ...
 *
 * With Save LODs From Attribute, meshes naming the same asset are written as its LODs, numbered by @Data.LOD, so
 * DynMesh | From Static Mesh with All LODs can round-trip a whole LOD chain.
 *
 * Editor only. Selection support (the usual DynMesh process contract) does not apply: a mesh is saved whole, and a
 * DynMesh Selection is not a mesh asset can be made of.
 */
UCLASS(BlueprintType, ClassGroup = (Procedural), Category = "PCGUtils|DynMesh",
	meta = (Keywords = "DynMesh mesh save static asset export bake write batch multiple"))
class PCGUTILSDYNMESH_API UPCGDynMeshSaveStaticMeshSettings : public UPCGDynamicMeshBaseSettings
{
	GENERATED_BODY()

public:
	UPCGDynMeshSaveStaticMeshSettings();

#if WITH_EDITOR
	virtual EPCGSettingsType GetType() const override
	{
		return PCGUtilsSettingsCategories::AsSettingsType(PCGUtilsSettingsCategories::EValue::DynMeshConversion);
	}
	virtual FName GetDefaultNodeName() const override { return TEXT("DynMeshSaveStaticMesh"); }
	virtual FText GetDefaultNodeTitle() const override;
	virtual FText GetNodeTooltipText() const override;
	virtual FLinearColor GetNodeTitleColor() const override { return PCGUtilsDynMeshNodeColor::Title; }

	/** Gives the shorthand members their plain override labels (AssetName, AssetNameAttribute, ...). */
	virtual TArray<FPCGSettingsOverridableParam> GatherOverridableParams() const override;
#endif

	virtual TArray<FPCGPinProperties> InputPinProperties() const override;
	virtual TArray<FPCGPinProperties> OutputPinProperties() const override;

	/**
	 * Name of the asset to save each mesh as. In @Data mode, a required String or Name @Data attribute on each mesh
	 * (default AssetName); a mesh without it is not saved, the constant is not a fallback. A constant name given
	 * more than one mesh gets "_<index>" appended, the mesh's index on the In pin.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Asset", meta = (PCG_Overridable))
	FPCGUtilsInputShorthandString AssetName;

	/**
	 * Content folder to save each mesh's asset in, such as /Game/Meshes. In @Data mode, a required String or Name
	 * @Data attribute on each mesh (default AssetPath); a mesh without it is not saved.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Asset", meta = (PCG_Overridable))
	FPCGUtilsInputShorthandString AssetPath;

	/**
	 * Opens the editor's Save Asset dialog, as the engine node does. Only with a single mesh on the pin: given more,
	 * the node warns and saves them all without asking. Off by default, unlike the engine node.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Asset", meta = (PCG_Overridable))
	bool bOpenSaveDialog = false;

	/** Saves each package to disk once its mesh is written. Off leaves the assets modified but unsaved. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Asset", meta = (PCG_Overridable))
	bool bSaveOnExportEnded = true;

	/**
	 * Takes priority over Copy Mesh To Asset Options' Replace Materials. On: a mesh that carries materials gives
	 * them to its asset. Off: Copy Mesh To Asset Options decides.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mesh", meta = (PCG_Overridable, DisplayName = "Export Materials From DynMesh"))
	bool bExportMaterialsFromDynMesh = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mesh", meta = (PCG_Overridable))
	FGeometryScriptCopyMeshToAssetOptions CopyMeshToAssetOptions;

	/** The LOD every mesh is written to. Unused with Save LODs From Attribute, where each mesh's @Data decides. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mesh", meta = (PCG_Overridable, EditCondition = "!bSaveLODsFromAttribute"))
	FGeometryScriptMeshWriteLOD MeshWriteLOD;

	/**
	 * Meshes that would save to the same asset become its LODs, each written to the LOD its LOD Attribute names,
	 * instead of being an error. A constant Asset Name then gets no index suffix, so a constant name and path save
	 * every input as one asset's LODs. LODs the asset already had beyond the highest one written are removed.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LODs", meta = (PCG_Overridable, DisplayName = "Save LODs From Attribute"))
	bool bSaveLODsFromAttribute = false;

	/**
	 * Required with Save LODs From Attribute. @Data attribute (int32 or int64) holding the LOD each mesh is written
	 * to. One asset's LODs must run 0, 1, 2... with no gap and no repeat. DynMesh | From Static Mesh writes it.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LODs", meta = (PCG_Overridable, EditCondition = "bSaveLODsFromAttribute"))
	FName LODAttribute = TEXT("LOD");

	/**
	 * With Save LODs From Attribute: a mesh whose High Quality Source Attribute is true is written to its asset's
	 * Hi-Res Source Model (the high-detail mesh Nanite builds from) instead of a LOD, and its LOD Attribute is not
	 * read. So DynMesh | Generate LODs with Output High Quality Source Mesh saves source and LODs in one call. One
	 * source per asset, and the asset still needs a LOD 0. An asset given no source keeps the Hi-Res Source it had.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LODs", meta = (PCG_Overridable, DisplayName = "Save High Quality Source From Attribute",
		EditCondition = "bSaveLODsFromAttribute"))
	bool bSaveHighQualitySourceFromAttribute = true;

	/**
	 * Optional per mesh, with Save High Quality Source From Attribute. @Data attribute (bool) marking the mesh to write
	 * as its asset's Hi-Res Source. A mesh without it is a LOD; one with it but of another type is an error. DynMesh |
	 * Generate LODs writes it.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LODs", meta = (PCG_Overridable,
		EditCondition = "bSaveLODsFromAttribute && bSaveHighQualitySourceFromAttribute"))
	FName HighQualitySourceAttribute = TEXT("IsHighQualitySource");

	/**
	 * Soft object path attribute written on each OutAssetPath attribute set, naming the asset saved. Always written,
	 * as the engine node does: it is what the output is for. Default AssetPath, the engine node's name.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Output", meta = (PCG_Overridable))
	FName OutputAssetAttributeName = TEXT("AssetPath");

protected:
	virtual FPCGElementPtr CreateElement() const override;
};

class FPCGDynMeshSaveStaticMeshElement : public IPCGElement
{
public:
	/** Creating, writing and saving assets must happen on the game thread. */
	virtual bool CanExecuteOnlyOnMainThread(FPCGContext* Context) const override { return true; }

protected:
	/** Writes assets, which PCG's data flow does not represent: a cache hit would skip the save. */
	virtual bool IsCacheable(const UPCGSettings* InSettings) const override { return false; }
	virtual bool ExecuteInternal(FPCGContext* Context) const override;
};
