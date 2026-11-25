#pragma once

#include "CoreMinimal.h"
#include "Engine/EngineTypes.h"
#include "Data/PCGUtilsComponentData.h"
#include "Elements/PCGDataFromActor.h"
#include "GeometryScript/MeshBooleanFunctions.h"

#include "PCGGetStaticMeshData.generated.h"

UENUM(BlueprintType)
enum class EPCGUtilsStaticMeshSource : uint8
{
	StaticMeshComponents UMETA(DisplayName="Static Mesh Components"),
	InstancedStaticMeshComponents UMETA(DisplayName="Instanced Static Mesh Components"),
	All UMETA(DisplayName="All Meshes")
};

UENUM(BlueprintType)
enum class EPCGUtilsMeshCollisionRequirement : uint8
{
	AnyEnabled UMETA(DisplayName="Any Collision"),
	Query UMETA(DisplayName="Query Collision"),
	Physics UMETA(DisplayName="Physics Collision"),
	QueryAndPhysics UMETA(DisplayName="Query and Physics Collision")
};

/** What Get Static Mesh Data puts on its output pin. */
UENUM(BlueprintType)
enum class EPCGUtilsStaticMeshOutput : uint8
{
	/** One point data per mesh component: one point per component, or one per instance for an instanced component. */
	Points,
	/** The meshes themselves as DynMesh data, placed where they are in the level. Same result as feeding the points to DynMesh | From Points. */
	DynMesh UMETA(DisplayName="DynMesh")
};

/** How the DynMesh output of Get Static Mesh Data groups the meshes it found. A mesh is one component, or one instance of an instanced component. */
UENUM(BlueprintType)
enum class EPCGUtilsStaticMeshMergeBehavior : uint8
{
	/** One DynMesh per mesh. */
	None,
	/** One DynMesh holding every mesh. */
	All,
	/** One DynMesh per island of meshes whose oriented bounding boxes overlap, directly or through other meshes. */
	ByOverlapping UMETA(DisplayName="By Overlapping")
};

class UStaticMesh;
class UStaticMeshComponent;

/** One mesh found by Get Static Mesh Data: a component, or one instance of an instanced component. */
struct FPCGUtilsStaticMeshPlacement
{
	UStaticMesh* StaticMesh = nullptr;
	UStaticMeshComponent* Component = nullptr;
	FTransform WorldTransform = FTransform::Identity;
};

/** Collects static mesh components or individual instanced mesh instances as point data, or as DynMesh data. */
UCLASS(BlueprintType, ClassGroup=(Procedural), Category="PCGUtils|Actor Data")
class PCGUTILSDYNMESH_API UPCGGetStaticMeshDataSettings : public UPCGDataFromActorSettings
{
	GENERATED_BODY()

public:
	UPCGGetStaticMeshDataSettings();

#if WITH_EDITOR
	virtual FName GetDefaultNodeName() const override { return FName(TEXT("PCGUtils|GetStaticMeshData")); }
	virtual FText GetDefaultNodeTitle() const override;
	virtual FText GetNodeTooltipText() const override;
#endif

	virtual EPCGDataType GetDataFilter() const override
	{
		return OutputMode == EPCGUtilsStaticMeshOutput::DynMesh ? EPCGDataType::DynamicMesh : EPCGDataType::Point;
	}

	/**
	 * Points (default) gives one point data per mesh component. DynMesh gives the meshes themselves, grouped by
	 * Merge Behavior. Not overridable: it decides the type of the output pin.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Settings")
	EPCGUtilsStaticMeshOutput OutputMode = EPCGUtilsStaticMeshOutput::Points;

	/** DynMesh output only. Which meshes end up in the same DynMesh. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="DynMesh Output", meta=(PCG_Overridable,
		EditCondition="OutputMode == EPCGUtilsStaticMeshOutput::DynMesh", EditConditionHides))
	EPCGUtilsStaticMeshMergeBehavior MergeBehavior = EPCGUtilsStaticMeshMergeBehavior::None;

	/**
	 * By Overlapping only. Grows every mesh's bounding box by this distance (cm) on each side before testing, so
	 * meshes that nearly touch still count as one island. Negative values require a deeper overlap.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="DynMesh Output", meta=(PCG_Overridable,
		EditCondition="OutputMode == EPCGUtilsStaticMeshOutput::DynMesh && MergeBehavior == EPCGUtilsStaticMeshMergeBehavior::ByOverlapping", EditConditionHides))
	double OverlapPadding = 0.0;

	/**
	 * DynMesh output only. Unions each output DynMesh with itself, so meshes that pass through each other become
	 * one solid. The result is kept for the editor session and reused while the group's meshes, their placement
	 * relative to each other and these options stay the same; reimporting or editing one of the Static Meshes
	 * rebuilds only the groups that use it.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="DynMesh Output", meta=(PCG_Overridable,
		EditCondition="OutputMode == EPCGUtilsStaticMeshOutput::DynMesh", EditConditionHides))
	bool bSelfUnion = false;

	/** Geometry Script self-union options, as on DynMesh | Self Union. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="DynMesh Output", meta=(PCG_Overridable,
		EditCondition="OutputMode == EPCGUtilsStaticMeshOutput::DynMesh && bSelfUnion", EditConditionHides))
	FGeometryScriptMeshSelfUnionOptions SelfUnionOptions;

	/** DynMesh output only. The meshes are found in world space and DynMesh data is in the PCG target actor's local space by convention: convert each DynMesh into that space. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="DynMesh Output", meta=(PCG_Overridable,
		EditCondition="OutputMode == EPCGUtilsStaticMeshOutput::DynMesh", EditConditionHides))
	bool bConvertToLocalSpace = true;

	/** Static mesh mode excludes ISMs; instanced mode emits one point per instance. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Settings", meta=(PCG_Overridable))
	EPCGUtilsStaticMeshSource MeshSource = EPCGUtilsStaticMeshSource::StaticMeshComponents;

	/** Excludes components tagged "PCG Generated Debug Component". */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Filters", meta=(PCG_Overridable))
	bool bFilterPCGDebugComponents = true;

	/** Requires the component to be visible and not hidden in game. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Filters", meta=(PCG_Overridable))
	bool bFilterHiddenComponents = false;

	/** Enables collision matching for both static and instanced mesh components. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Filters|Collision", meta=(PCG_Overridable))
	bool bFilterByCollision = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Filters|Collision", meta=(PCG_Overridable, EditCondition="bFilterByCollision", EditConditionHides))
	EPCGUtilsMeshCollisionRequirement CollisionRequirement = EPCGUtilsMeshCollisionRequirement::AnyEnabled;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Filters|Collision", meta=(PCG_Overridable, EditCondition="bFilterByCollision", EditConditionHides))
	bool bMatchCollisionProfile = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Filters|Collision", meta=(PCG_Overridable, EditCondition="bFilterByCollision && bMatchCollisionProfile", EditConditionHides))
	FName CollisionProfileName = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Filters|Collision", meta=(PCG_Overridable, EditCondition="bFilterByCollision", EditConditionHides))
	bool bMatchObjectType = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Filters|Collision", meta=(PCG_Overridable, EditCondition="bFilterByCollision && bMatchObjectType", EditConditionHides))
	TEnumAsByte<ECollisionChannel> ObjectType = ECC_WorldStatic;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Filters|Collision", meta=(PCG_Overridable, EditCondition="bFilterByCollision", EditConditionHides))
	bool bMatchTraceResponse = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Filters|Collision", meta=(PCG_Overridable, EditCondition="bFilterByCollision && bMatchTraceResponse", EditConditionHides))
	TEnumAsByte<ECollisionChannel> TraceChannel = ECC_Visibility;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Filters|Collision", meta=(PCG_Overridable, EditCondition="bFilterByCollision && bMatchTraceResponse", EditConditionHides))
	TEnumAsByte<ECollisionResponse> TraceResponse = ECR_Block;

	/**
	 * Attribute that receives the Static Mesh asset, as a soft object path. Optional: None writes nothing. On points
	 * it is what DynMesh | From Points reads. On a DynMesh it is written to @Data when every mesh in it is the same
	 * asset. Material attributes below are written to points only; a DynMesh carries its materials itself.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Settings")
	FName MeshOutputAttributeName = FName(TEXT("Mesh"));

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Settings", meta=(InlineEditConditionToggle))
	bool bExtractMeshMaterials = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Settings", meta=(EditCondition="bExtractMeshMaterials", ClampMin="1", UIMin="1", ToolTip="Maximum number of mesh materials to extract. When greater than one, a zero-based number is appended to the material attribute name. When set to one, the attribute name is used as-is."))
	int32 MaxMaterialCount = 1;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Settings", meta=(EditCondition="bExtractMeshMaterials", ToolTip="Base attribute name for extracted materials. When Max Material Count is greater than one, a zero-based number is appended to this name. When set to one, this name is used as-is."))
	FName MaterialOutputAttributeName = FName(TEXT("Material"));

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Settings", meta = (ShowOnlyInnerProperties))
	FGetComponentDataSettings ComponentSettings;

protected:
	virtual TArray<FPCGPinProperties> OutputPinProperties() const override;
	virtual FPCGElementPtr CreateElement() const override;

#if WITH_EDITOR
	virtual bool DisplayModeSettings() const override { return false; }
	virtual EPCGChangeType GetChangeTypeForProperty(FPropertyChangedEvent& PropertyChangedEvent) const override;
#endif
};

class PCGUTILSDYNMESH_API FPCGGetStaticMeshDataElement : public FPCGDataFromActorElement
{
public:
	/** Every mesh on FoundActor that passes the node's filters, in component order then instance order. */
	static void GatherPlacements(const FPCGContext* Context, const UPCGGetStaticMeshDataSettings* Settings, AActor* FoundActor,
		TArray<FPCGUtilsStaticMeshPlacement>& OutPlacements);

protected:
	virtual void ProcessActors(FPCGContext* Context, const UPCGDataFromActorSettings* Settings, const TArray<AActor*>& FoundActors,
		TArray<FPCGTaskId>& OutDynamicDependencies) const override;
	virtual void ProcessActor(
		FPCGContext* Context,
		const UPCGDataFromActorSettings* Settings,
		AActor* FoundActor) const override;
};
