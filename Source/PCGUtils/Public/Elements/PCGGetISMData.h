// Copyright Max Harris

#pragma once

#include "CoreMinimal.h"
#include "Elements/PCGDataFromActor.h"
#include "PCGUtilsSettingsCategories.h"

#include "PCGGetISMData.generated.h"

class UInstancedStaticMeshComponent;
class UPCGBasePointData;

/**
 * Reads the instances of instanced static mesh components (ISM and HISM) back into PCG. Each component becomes one
 * point data set with one point per instance, in world space. The usual actor and component filters apply, so
 * "ISM components tagged X" is the Component Selector's tag filter.
 */
UCLASS(BlueprintType, ClassGroup=(Procedural), Category="PCGUtils|Actor Data",
	meta=(Keywords="ISM HISM instanced static mesh instances read back get actor component points"))
class PCGUTILS_API UPCGGetISMDataSettings : public UPCGDataFromActorSettings
{
	GENERATED_BODY()

public:
	UPCGGetISMDataSettings();

#if WITH_EDITOR
	virtual FName GetDefaultNodeName() const override { return FName(TEXT("GetISMData")); }
	virtual FText GetDefaultNodeTitle() const override;
	virtual FText GetNodeTooltipText() const override;
	virtual EPCGSettingsType GetType() const override { return PCGUtilsSettingsCategories::AsSettingsType(PCGUtilsSettingsCategories::EValue::Points); }
#endif

	virtual EPCGDataType GetDataFilter() const override { return EPCGDataType::Point; }

	/** Skip components tagged "PCG Generated Debug Component" (the ones PCG spawns for debug display). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Filters", meta=(PCG_Overridable))
	bool bSkipPCGDebugComponents = true;

	/** Write the component's static mesh (Soft Object Path) under Mesh Attribute Name. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Attributes", meta=(PCG_Overridable, InlineEditConditionToggle))
	bool bOutputMesh = true;

	/**
	 * Optional. Soft Object Path to the component's static mesh. A plain name ("Mesh") writes it on every point;
	 * "@Data.Mesh" writes it once for the data set.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Attributes", meta=(PCG_Overridable, EditCondition="bOutputMesh"))
	FName MeshAttributeName = TEXT("Mesh");

	/** Write the component's material for Material Slot under Material Attribute Name. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Attributes", meta=(PCG_Overridable, InlineEditConditionToggle))
	bool bOutputMaterial = false;

	/**
	 * Optional. Soft Object Path to the material the component renders in Material Slot (its override if it has
	 * one, otherwise the mesh's). "@Data.Material" writes it once for the data set; a plain name writes it on every point.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Attributes", meta=(PCG_Overridable, EditCondition="bOutputMaterial"))
	FName MaterialAttributeName = TEXT("@Data.Material");

	/** The material slot read for the material attribute. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Attributes", meta=(PCG_Overridable, EditCondition="bOutputMaterial", EditConditionHides, ClampMin="0", UIMin="0"))
	int32 MaterialSlot = 0;

	/** Give each point the static mesh's local bounding box. Off, points keep the default unit bounds. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Points", meta=(PCG_Overridable))
	bool bBoundsFromMesh = true;

protected:
	virtual TArray<FPCGPinProperties> OutputPinProperties() const override;
	virtual FPCGElementPtr CreateElement() const override;

#if WITH_EDITOR
	virtual bool DisplayModeSettings() const override { return false; }
#endif
};

class PCGUTILS_API FPCGGetISMDataElement : public FPCGDataFromActorElement
{
public:
	/** Builds the point data for one component. Returns null (and logs why) when the component yields nothing. */
	static UPCGBasePointData* MakeInstancePoints(FPCGContext* Context, const UPCGGetISMDataSettings& Settings,
		const UInstancedStaticMeshComponent& Component);

protected:
	virtual void ProcessActor(FPCGContext* Context, const UPCGDataFromActorSettings* Settings, AActor* FoundActor) const override;
};
