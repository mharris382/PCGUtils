// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "PCGContext.h"
#include "Async/PCGAsyncLoadingContext.h"
#include "Elements/PCGDynamicMeshBaseElement.h"

#include "GeometryScript/GeometryScriptTypes.h"

#include "PCGAppendMeshesFromPointsC.generated.h"

UENUM(Blueprintable)
enum class EPCGAppendMeshesFromPointsCMode : uint8
{
	SingleStaticMesh UMETA(ToolTip="Mesh taken from the node settings"),
	StaticMeshFromAttribute UMETA(ToolTip="Mesh taken from attributes on the points"),
	DynamicMesh UMETA(ToolTip="Mesh taken from another dynamic mesh")
};

/**
* Append meshes at the points transforms. Mesh can be a single static mesh, multiple meshes coming from the points or another dynamic mesh.
*/
UCLASS(MinimalAPI, BlueprintType, ClassGroup = (Procedural))
class UPCGAppendMeshesFromPointsCSettings : public UPCGDynamicMeshBaseSettings
{
	GENERATED_BODY()

public:
	//~Begin UPCGSettings interface
#if WITH_EDITOR
	virtual FName GetDefaultNodeName() const override;
	virtual FText GetDefaultNodeTitle() const override;
	virtual FText GetNodeTooltipText() const override;
	virtual void GetStaticTrackedKeys(FPCGSelectionKeyToSettingsMap& OutKeysToSettings, TArray<TObjectPtr<const UPCGGraph>>& OutVisitedGraphs) const override;
	virtual bool CanDynamicallyTrackKeys() const override { return Mode != EPCGAppendMeshesFromPointsCMode::DynamicMesh; };
#endif

protected:
	virtual TArray<FPCGPinProperties> InputPinProperties() const override;
	virtual TArray<FPCGPinProperties> OutputPinProperties() const override;
	virtual FPCGElementPtr CreateElement() const override;
#if WITH_EDITOR
	virtual EPCGChangeType GetChangeTypeForProperty(const FName& InPropertyName) const override;
#endif // WITH_EDITOR
	//~End UPCGSettings interface

public:
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category="Settings")
	EPCGAppendMeshesFromPointsCMode Mode;
	
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category="Settings", meta = (EditCondition = "Mode==EPCGAppendMeshesFromPointsCMode::SingleStaticMesh", EditConditionHides, PCG_Overridable))
	TSoftObjectPtr<UStaticMesh> StaticMesh;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category="Settings", meta = (EditCondition = "Mode==EPCGAppendMeshesFromPointsCMode::StaticMeshFromAttribute", EditConditionHides, PCG_Overridable))
	FPCGAttributePropertyInputSelector MeshAttribute;


	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings", meta = (PCG_Overridable, Tooltip="Point Attribute that will be copied into the dynamic mesh vertex color"))
	FPCGAttributePropertyInputSelector ColorAttribute;



	/** Allows to extract materials from the static mesh and set them in the resulting append. */
    UPROPERTY(BlueprintReadWrite, EditAnywhere, Category="Settings", meta = (EditCondition = "Mode!=EPCGAppendMeshesFromPointsCMode::DynamicMesh", EditConditionHides, PCG_Overridable))
    bool bExtractMaterials = true;
	
	/** LOD type to use when creating DynamicMesh from specified StaticMesh. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|LODSettings", meta = (EditCondition = "Mode!=EPCGAppendMeshesFromPointsCMode::DynamicMesh", EditConditionHides, PCG_Overridable))
	EGeometryScriptLODType RequestedLODType = EGeometryScriptLODType::RenderData;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|LODSettings", meta = (EditCondition = "Mode!=EPCGAppendMeshesFromPointsCMode::DynamicMesh", EditConditionHides, PCG_Overridable))
	int32 RequestedLODIndex = 0;
	
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Debug", meta = (EditCondition = "Mode!=EPCGAppendMeshesFromPointsCMode::DynamicMesh", EditConditionHides))
	bool bSynchronousLoad = false;
};

struct FPCGAppendMeshesFromPointsCContext : public FPCGContext, public IPCGAsyncLoadingContext
{
	bool bPrepareDataSucceeded = false;
	TMap<FSoftObjectPath, TArray<int32>> MeshToPointIndicesMapping;
};

class FPCGAppendMeshesFromPointsCElement : public IPCGDynamicMeshBaseElement
{
public:
	virtual bool CanExecuteOnlyOnMainThread(FPCGContext* Context) const override;
protected:
	virtual FPCGContext* CreateContext() override;
	virtual bool PrepareDataInternal(FPCGContext* InContext) const override;
	virtual bool ExecuteInternal(FPCGContext* InContext) const override;
};

