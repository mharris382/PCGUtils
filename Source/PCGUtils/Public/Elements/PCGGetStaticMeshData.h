#pragma once

#include "CoreMinimal.h"
#include "Engine/EngineTypes.h"
#include "Data/PCGUtilsComponentData.h"
#include "Elements/PCGDataFromActor.h"

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

/** Collects static mesh components or individual instanced mesh instances as point data. */
UCLASS(BlueprintType, ClassGroup=(Procedural), Category="PCGUtils|Actor Data")
class PCGUTILS_API UPCGGetStaticMeshDataSettings : public UPCGDataFromActorSettings
{
	GENERATED_BODY()

public:
	UPCGGetStaticMeshDataSettings();

#if WITH_EDITOR
	virtual FName GetDefaultNodeName() const override { return FName(TEXT("PCGUtils|GetStaticMeshData")); }
	virtual FText GetDefaultNodeTitle() const override;
	virtual FText GetNodeTooltipText() const override;
#endif

	virtual EPCGDataType GetDataFilter() const override { return EPCGDataType::Point; }

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
#endif
};

class PCGUTILS_API FPCGGetStaticMeshDataElement : public FPCGDataFromActorElement
{
protected:
	virtual void ProcessActor(
		FPCGContext* Context,
		const UPCGDataFromActorSettings* Settings,
		AActor* FoundActor) const override;
};
