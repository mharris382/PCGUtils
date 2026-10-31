// Copyright Max Harris

#pragma once

#include "CoreMinimal.h"
#include "Elements/PCGDataFromActor.h"
#include "PCGUtilsSettingsCategories.h"

#include "PCGUtilsDynMeshNodeColor.h"
#include "PCGGetStaticMeshBrushData.generated.h"

UENUM(BlueprintType)
enum class EPCGUtilsStaticMeshBrushSource : uint8
{
	/** Per actor: its combined mesh when it has a Static Mesh Brush Combiner Component, otherwise each brush on its own. Never both. */
	Automatic,
	/** Only combined meshes. An actor without a Static Mesh Brush Combiner Component is skipped with a warning. */
	Combined,
	/** Each Static Mesh Brush Component on its own, whether or not the actor has a combiner. */
	Individual
};

/**
 * Reads Static Mesh Brush Components from the selected actors as DynMesh data, using meshes that were prepared and
 * cached outside the graph (LOD choice, baked scale, simplification, and for a combiner, self union). The graph
 * pays only for a copy and a transform, not for the processing.
 *
 * Combined meshes carry the Combined Tag, meaning they are already one self-unioned solid.
 */
UCLASS(BlueprintType, ClassGroup=(Procedural), Category="PCGUtils|DynMesh",
	meta=(Keywords="DynMesh mesh static brush actor get data cached combined combiner union decimate simplify"))
class PCGUTILSDYNMESH_API UPCGGetStaticMeshBrushDataSettings : public UPCGDataFromActorSettings
{
	GENERATED_BODY()

public:
	UPCGGetStaticMeshBrushDataSettings();

#if WITH_EDITOR
	virtual EPCGSettingsType GetType() const override
	{
		return PCGUtilsSettingsCategories::AsSettingsType(PCGUtilsSettingsCategories::EValue::DynMeshCreation);
	}
	virtual FName GetDefaultNodeName() const override { return TEXT("GetStaticMeshBrushData"); }
	virtual FText GetDefaultNodeTitle() const override;
	virtual FText GetNodeTooltipText() const override;
	virtual FLinearColor GetNodeTitleColor() const override { return PCGUtilsDynMeshNodeColor::Title; }
#endif

	//~Begin UPCGDataFromActorSettings interface
	virtual EPCGDataType GetDataFilter() const override { return EPCGDataType::DynamicMesh; }

protected:
#if WITH_EDITOR
	virtual bool DisplayModeSettings() const override { return false; }
#endif
	//~End UPCGDataFromActorSettings interface

	virtual TArray<FPCGPinProperties> OutputPinProperties() const override;
	virtual FPCGElementPtr CreateElement() const override;

public:
	/**
	 * Which meshes to read from each actor. Automatic reads an actor's combined mesh when it has a Static Mesh Brush
	 * Combiner Component and its separate brushes otherwise. The component filter below applies to separate brushes
	 * only; a combined mesh always holds every brush on its actor.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Settings", meta=(PCG_Overridable))
	EPCGUtilsStaticMeshBrushSource Source = EPCGUtilsStaticMeshBrushSource::Automatic;

	/**
	 * Converts each mesh from world space into the PCG target actor's local space before output. Enable this when
	 * the result will be used with other DynMesh data, which is target-actor-local by convention.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Output", meta=(PCG_Overridable))
	bool bConvertWorldToActorLocal = true;

	/**
	 * Data tag added to every combined mesh. It means the mesh is already one self-unioned solid, so the graph can
	 * skip DynMesh | Self Union for it. Separate brushes never carry it. None leaves combined meshes untagged.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Output", meta=(PCG_Overridable))
	FName CombinedTag = TEXT("Combined");
};

class PCGUTILSDYNMESH_API FPCGGetStaticMeshBrushDataElement : public FPCGDataFromActorElement
{
protected:
	virtual void ProcessActors(FPCGContext* Context, const UPCGDataFromActorSettings* Settings, const TArray<AActor*>& FoundActors, TArray<FPCGTaskId>& OutDynamicDependencies) const override;
	virtual void ProcessActors(FPCGContext* Context, const UPCGDataFromActorSettings* Settings, const TArray<AActor*>& FoundActors) const override;
	virtual void ProcessActor(FPCGContext* Context, const UPCGDataFromActorSettings* Settings, AActor* FoundActor) const override;
};
