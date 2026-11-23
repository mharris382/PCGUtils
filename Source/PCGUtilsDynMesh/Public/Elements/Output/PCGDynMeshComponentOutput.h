// Copyright Max Harris
#pragma once

#include "PCGSettings.h"
#include "PCGElement.h"
#include "PCGUtilsSettingsCategories.h"
#include "PCGUtilsDynMeshNodeColor.h"
#include "Resources/PCGDynamicMeshManagedComponent.h"
#include "PCGDynMeshComponentOutput.generated.h"

class AActor;

/** Component output is a side effect, not a mesh process: the input data remains unmodified. */
UCLASS(Abstract)
class PCGUTILSDYNMESH_API UPCGDynMeshComponentOutputSettings : public UPCGSettings
{
	GENERATED_BODY()
public:
	/** Input vertices are in their PCG target actor's local space. Includes translation, rotation and scale.
	 * Disable only for meshes explicitly converted to world space. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Settings", meta=(PCG_Overridable))
	bool bInputIsActorLocal = true;

	/** Optional actor to own the generated components. Does not change the input mesh's coordinate frame. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Settings", meta=(PCG_Overridable))
	TSoftObjectPtr<AActor> TargetActor;

	virtual bool IsDebugOutput() const { return false; }
#if WITH_EDITOR
	virtual EPCGSettingsType GetType() const override { return PCGUtilsSettingsCategories::AsSettingsType(PCGUtilsSettingsCategories::EValue::DynMesh); }
	virtual FLinearColor GetNodeTitleColor() const override { return PCGUtilsDynMeshNodeColor::Title; }
#endif
protected:
	virtual TArray<FPCGPinProperties> InputPinProperties() const override;
	virtual TArray<FPCGPinProperties> OutputPinProperties() const override;
	virtual FPCGElementPtr CreateElement() const override;
};

/** Preview-only display for PCGUtils full actor-local DynMeshes. */
UCLASS(BlueprintType, ClassGroup=(Procedural), Category="PCGUtils|DynMesh", meta=(Keywords="DynMesh debug display preview actor local rotation scale"))
class PCGUTILSDYNMESH_API UPCGDynMeshDebugSettings : public UPCGDynMeshComponentOutputSettings
{
	GENERATED_BODY()
public:
	virtual bool IsDebugOutput() const override { return true; }
#if WITH_EDITOR
	virtual FName GetDefaultNodeName() const override { return TEXT("DynMeshDebug"); }
	virtual FText GetDefaultNodeTitle() const override { return NSLOCTEXT("PCGUtils", "DynMeshDebugTitle", "DynMesh | Debug"); }
	virtual FText GetNodeTooltipText() const override { return NSLOCTEXT("PCGUtils", "DynMeshDebugTooltip", "Preview PCGUtils DynMeshes using their full actor transform, including rotation and scale. Unreal's standard DynMesh Debug uses a translation-only frame. No components are created in cooked games."); }
#endif
};

/** Managed component output for PCGUtils full actor-local DynMeshes. */
UCLASS(BlueprintType, ClassGroup=(Procedural), Category="PCGUtils|DynMesh", meta=(Keywords="DynMesh spawn component output actor local rotation scale"))
class PCGUTILSDYNMESH_API UPCGDynMeshSpawnSettings : public UPCGDynMeshComponentOutputSettings
{
	GENERATED_BODY()
#if WITH_EDITOR
public:
	virtual FName GetDefaultNodeName() const override { return TEXT("DynMeshSpawn"); }
	virtual FText GetDefaultNodeTitle() const override { return NSLOCTEXT("PCGUtils", "DynMeshSpawnTitle", "DynMesh | Spawn"); }
	virtual FText GetNodeTooltipText() const override { return NSLOCTEXT("PCGUtils", "DynMeshSpawnTooltip", "Spawn managed DynMesh components using each input's full actor transform, including rotation and scale. Output data is passed through unchanged."); }
#endif
};

class PCGUTILSDYNMESH_API FPCGDynMeshComponentOutputElement : public IPCGElement
{
public:
	virtual bool CanExecuteOnlyOnMainThread(FPCGContext*) const override { return true; }
	virtual bool IsCacheable(const UPCGSettings*) const override { return false; }
protected:
	virtual bool ExecuteInternal(FPCGContext* Context) const override;
};

/** Separate resource identity prevents Debug and Spawn (or two output nodes) sharing one component. */
UCLASS()
class PCGUTILSDYNMESH_API UPCGDynMeshOutputManagedComponent : public UPCGDynamicMeshManagedComponent
{
	GENERATED_BODY()
public:
	UPROPERTY(Transient)
	TObjectPtr<const UPCGSettingsInterface> OutputSettings;
	UPROPERTY(Transient)
	int32 InputIndex = INDEX_NONE;
	UPROPERTY(Transient)
	TWeakObjectPtr<AActor> FrameActor;
	UPROPERTY(Transient)
	FTransform MeshToWorld = FTransform::Identity;
	virtual void MarkAsReused() override;
};
