// Copyright Max Harris

#pragma once

#include "CoreMinimal.h"
#include "PCGElement.h"
#include "PCGSettings.h"
#include "PCGUtilsSettingsCategories.h"

#include "PCGDynMeshFromStaticMeshPoints.generated.h"

/**
 * Turns points that each name a static mesh into DynMeshes: one DynMesh per input point data.
 *
 * Every point places the static mesh it names by its own transform, and the meshes of one point data are appended
 * into one DynMesh. That DynMesh keeps the point data's tags and its @Data attributes, so whatever identified the
 * points as a set (a tag turned into an attribute, say) still identifies the mesh made from them.
 *
 * This is the step between Get Static Mesh Data, which gives one point data per mesh component, and anything that
 * works on DynMeshes. Engine Append Meshes From Points is close but appends everything into one mesh that takes
 * its tags from the mesh it appends to, which loses what each point data carried.
 *
 * The source mesh is read where it exists (in the editor), which is closed where the render mesh is split along
 * hard edges and UV seams. In a cooked game the render mesh is read instead.
 */
UCLASS(BlueprintType, ClassGroup=(Procedural), Category="PCGUtils|DynMesh",
	meta=(Keywords="DynMesh mesh static mesh points from to convert component get static mesh data append"))
class PCGUTILSDYNMESH_API UPCGDynMeshFromStaticMeshPointsSettings : public UPCGSettings
{
	GENERATED_BODY()

public:
#if WITH_EDITOR
	virtual EPCGSettingsType GetType() const override
	{
		return PCGUtilsSettingsCategories::AsSettingsType(PCGUtilsSettingsCategories::EValue::DynMeshCreation);
	}
	virtual FName GetDefaultNodeName() const override { return TEXT("DynMeshFromStaticMeshPoints"); }
	virtual FText GetDefaultNodeTitle() const override;
	virtual FText GetNodeTooltipText() const override;
	virtual FLinearColor GetNodeTitleColor() const override { return FLinearColor(0.413f, 0.25f, 1.0f, 1.0f); }
#endif

	/**
	 * Attribute holding the static mesh each point places, as a soft object path or a string. Required. Read from
	 * each point, or from @Data when the points do not have it. Written as Mesh by Get Static Mesh Data.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Mesh", meta=(PCG_Overridable))
	FName MeshAttributeName = TEXT("Mesh");

	/** Points are world-space and DynMesh data is in the PCG target actor's local space by convention: convert each point's transform into that space. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Mesh", meta=(PCG_Overridable))
	bool bConvertToLocalSpace = true;

protected:
	virtual TArray<FPCGPinProperties> InputPinProperties() const override;
	virtual TArray<FPCGPinProperties> OutputPinProperties() const override;
	virtual FPCGElementPtr CreateElement() const override;
};

class PCGUTILSDYNMESH_API FPCGDynMeshFromStaticMeshPointsElement : public IPCGElement
{
public:
	/** Loading meshes and resolving the target actor both need the game thread. */
	virtual bool CanExecuteOnlyOnMainThread(FPCGContext* Context) const override { return true; }
	virtual bool IsCacheable(const UPCGSettings* InSettings) const override { return false; }

protected:
	virtual bool ExecuteInternal(FPCGContext* Context) const override;
};
