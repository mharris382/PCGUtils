// Copyright Max Harris

#pragma once

#include "CoreMinimal.h"
#include "Async/PCGAsyncLoadingContext.h"
#include "Elements/PCGDynamicMeshBaseElement.h"
#include "GeometryScript/GeometryScriptTypes.h"
#include "Metadata/PCGAttributePropertySelector.h"
#include "PCGContext.h"
#include "PCGUtilsSettingsCategories.h"

#include "PCGUtilsDynMeshNodeColor.h"
#include "PCGDynMeshFromStaticMesh.generated.h"

class UMaterialInterface;

/**
 * Makes one DynMesh per mesh named on its input: the engine's Mesh To Dynamic Mesh, reading its meshes from a pin
 * rather than a setting.
 *
 * Mesh Attribute names a static or skeletal mesh on every element of the input (each attribute set entry, each
 * point), or once on @Data. Each mesh named becomes its own DynMesh, which keeps the input's tags and @Data and,
 * with Copy Element Attributes on, takes the attributes of the entry that named it as @Data. A list of assets thus
 * comes out as meshes that still carry their names and paths, ready for processing and Save Static Mesh.
 *
 * With All LODs, each mesh gives one DynMesh per LOD, marked with @Data.LOD and @Data.SourceMesh.
 *
 * DynMesh | From Static Mesh Points is the other way in: it places meshes by point transforms and appends each
 * point data's into one DynMesh.
 */
UCLASS(BlueprintType, ClassGroup = (Procedural), Category = "PCGUtils|DynMesh",
	meta = (Keywords = "DynMesh mesh static skeletal asset to from convert load list batch multiple"))
class PCGUTILSDYNMESH_API UPCGDynMeshFromStaticMeshSettings : public UPCGDynamicMeshBaseSettings
{
	GENERATED_BODY()

public:
#if WITH_EDITOR
	virtual EPCGSettingsType GetType() const override
	{
		return PCGUtilsSettingsCategories::AsSettingsType(PCGUtilsSettingsCategories::EValue::DynMeshCreation);
	}
	virtual FName GetDefaultNodeName() const override { return TEXT("DynMeshFromStaticMesh"); }
	virtual FText GetDefaultNodeTitle() const override;
	virtual FText GetNodeTooltipText() const override;
	virtual FLinearColor GetNodeTitleColor() const override { return PCGUtilsDynMeshNodeColor::Title; }
	virtual bool CanDynamicallyTrackKeys() const override { return true; }
#endif

	virtual TArray<FPCGPinProperties> InputPinProperties() const override;
	virtual TArray<FPCGPinProperties> OutputPinProperties() const override;

	/**
	 * Required. The attribute naming the mesh: a soft object path or a string, to a static or skeletal mesh. Read
	 * on every element of the domain it selects, each element giving one DynMesh; select @Data.<Name> for one mesh
	 * per input. An element whose value is empty gives no mesh.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Settings", meta = (PCG_Overridable))
	FPCGAttributePropertyInputSelector MeshAttribute;

	/** Copies the input's @Data attributes onto each DynMesh made from it. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Settings", meta = (PCG_Overridable))
	bool bCopyDataAttributes = true;

	/**
	 * Writes the attributes of the element that named the mesh (the attribute set entry or the point, Mesh
	 * Attribute included) to the DynMesh's @Data under their own names. They replace @Data attributes of the same
	 * name. Does nothing when Mesh Attribute selects @Data.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Settings", meta = (PCG_Overridable))
	bool bCopyElementAttributes = true;

	/** Stores the mesh's materials on its DynMesh, as the engine node does. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Settings", meta = (PCG_Overridable))
	bool bExtractMaterials = true;

	/** Replaces the extracted materials. Needs as many materials as each mesh has; a mesh with another count is skipped. */
	UPROPERTY(EditAnywhere, Category = "Settings", meta = (PCG_Overridable, EditCondition = "bExtractMaterials", EditConditionHides))
	TArray<TSoftObjectPtr<UMaterialInterface>> OverrideMaterials;

	/** LOD type to read from each mesh. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Settings|LODSettings", meta = (PCG_Overridable))
	EGeometryScriptLODType RequestedLODType = EGeometryScriptLODType::MaxAvailable;

	/** The LOD read from each mesh. Unused with All LODs. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Settings|LODSettings", meta = (PCG_Overridable, EditCondition = "!bReadAllLODs"))
	int32 RequestedLODIndex = 0;

	/**
	 * Makes one DynMesh per LOD of each mesh, LOD 0 first, instead of reading Requested LOD Index only. Source
	 * Model reads each stored source LOD; Render Data each render LOD; Max Available a source LOD where the asset
	 * stores one and the render LOD where it was generated (auto LODs have no source mesh). Pair with LOD Attribute
	 * and Source Mesh Attribute so Paint Vertex Color and Save Static Mesh can tell the LODs apart.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Settings|LODSettings", meta = (PCG_Overridable, DisplayName = "All LODs"))
	bool bReadAllLODs = false;

	/** Write each DynMesh's LOD number to @Data. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Settings|LODSettings", meta = (PCG_Overridable, InlineEditConditionToggle))
	bool bWriteLODAttribute = true;

	/**
	 * Optional output. @Data attribute (int32) receiving the LOD the DynMesh was read from: 0 for the most detailed.
	 * Paint Vertex Color (Paint LODs From Highest Detail) and Save Static Mesh (Save LODs From Attribute) read it.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Settings|LODSettings", meta = (PCG_Overridable, EditCondition = "bWriteLODAttribute"))
	FName LODAttribute = TEXT("LOD");

	/** Write the mesh asset each DynMesh was read from to @Data. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Settings|LODSettings", meta = (PCG_Overridable, InlineEditConditionToggle))
	bool bWriteSourceMeshAttribute = true;

	/**
	 * Optional output. @Data attribute (soft object path) receiving the mesh asset the DynMesh was read from, so the
	 * LODs of one mesh share a value. Paint Vertex Color's LOD Source Attribute reads it.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Settings|LODSettings", meta = (PCG_Overridable, EditCondition = "bWriteSourceMeshAttribute"))
	FName SourceMeshAttribute = TEXT("SourceMesh");

	/** Loads the meshes before running instead of in the background. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Settings|Debug")
	bool bSynchronousLoad = false;

protected:
	virtual FPCGElementPtr CreateElement() const override;
};

struct FPCGDynMeshFromStaticMeshContext : public FPCGContext, public IPCGAsyncLoadingContext {};

class FPCGDynMeshFromStaticMeshElement : public IPCGDynamicMeshBaseElementWithCustomContext<FPCGDynMeshFromStaticMeshContext>
{
public:
	/** Loading runs on the game thread; the conversion can run anywhere, as in the engine node. */
	virtual bool CanExecuteOnlyOnMainThread(FPCGContext* Context) const override;

protected:
	virtual bool PrepareDataInternal(FPCGContext* Context) const override;
	virtual bool ExecuteInternal(FPCGContext* Context) const override;
};
