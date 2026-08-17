#pragma once

#include "CoreMinimal.h"
#include "PCGSettings.h"

#include "PCGDynMeshActorSpaceTransform.generated.h"

UENUM()
enum class EPCGDynMeshActorSpaceTransformDirection : uint8
{
	ToWorld,
	ToLocal
};

/** Transforms Dynamic Mesh data between actor-local and world space. Exposed only through ToWorld and ToLocal aliases. */
UCLASS(BlueprintType, ClassGroup=(Procedural), Category="PCGUtils|Dynamic Mesh")
class PCGUTILSDYNMESH_API UPCGDynMeshActorSpaceTransformSettings : public UPCGSettings
{
	GENERATED_BODY()

public:
	virtual TArray<FPCGPinProperties> InputPinProperties() const override;
	virtual TArray<FPCGPinProperties> OutputPinProperties() const override;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Settings", meta=(PCG_Overridable))
	bool bIgnoreRotation = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Settings", meta=(PCG_Overridable))
	bool bIgnoreScale = false;

	EPCGDynMeshActorSpaceTransformDirection GetDirection() const { return Direction; }

#if WITH_EDITOR
	virtual FName GetDefaultNodeName() const override;
	virtual FText GetDefaultNodeTitle() const override;
	virtual FText GetNodeTooltipText() const override;
	virtual FLinearColor GetNodeTitleColor() const override { return FLinearColor(0.413f, 0.25f ,1.0f, 1.0f);	}
	virtual TArray<FPCGPreConfiguredSettingsInfo> GetPreconfiguredInfo() const override;
	virtual bool OnlyExposePreconfiguredSettings() const override { return true; }
	virtual bool GroupPreconfiguredSettings() const override { return false; }
	virtual void ApplyPreconfiguredSettings(const FPCGPreConfiguredSettingsInfo& PreconfiguredInfo) override;
#endif

protected:
	virtual FPCGElementPtr CreateElement() const override;

private:
	UPROPERTY()
	EPCGDynMeshActorSpaceTransformDirection Direction = EPCGDynMeshActorSpaceTransformDirection::ToWorld;
};

class PCGUTILSDYNMESH_API FPCGDynMeshActorSpaceTransformElement : public IPCGElement
{
public:
	virtual bool CanExecuteOnlyOnMainThread(FPCGContext* Context) const override { return true; }

protected:
	virtual bool ExecuteInternal(FPCGContext* Context) const override;
};
