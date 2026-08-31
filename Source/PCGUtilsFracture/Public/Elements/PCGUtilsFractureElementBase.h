// Copyright Max Harris

#pragma once

#include "CoreMinimal.h"
#include "PCGSettings.h"

#include "PCGUtilsFractureElementBase.generated.h"

class UPCGUtilsGCFactoryData;

/**
 * Shared settings base for every PCGUtilsFracture node.
 *
 * Its whole job is `GetType()`. A UPCGSettings subclass that forgets to override it silently lands in the
 * `Generic` palette bucket - nothing fails to compile, so the mistake is easy to miss. Deriving every element in
 * this module from here makes that impossible. `EPCGSettingsType::DynamicMesh` is the closest available bucket;
 * there is no GeometryCollection value in the engine enum.
 */
UCLASS(Abstract, BlueprintType, ClassGroup=(Procedural), Category="PCGUtils|Fracture")
class PCGUTILSFRACTURE_API UPCGUtilsFractureElementBaseSettings : public UPCGSettings
{
	GENERATED_BODY()

public:
#if WITH_EDITOR
	virtual EPCGSettingsType GetType() const override { return EPCGSettingsType::DynamicMesh; }
	virtual FLinearColor GetNodeTitleColor() const override;
#endif
};

/**
 * Provider base for nodes that emit a fracture-domain factory (Fracture or GC Selection) on a single output
 * pin. Mirrors UPCGUtilsDynMeshFactoryProviderSettings: the derived class supplies the pin name, the type id
 * and a CreateFactory override, and the shared element handles emission and data-dependency capture.
 */
UCLASS(Abstract, BlueprintType, ClassGroup=(Procedural), Category="PCGUtils|Fracture|Providers")
class PCGUTILSFRACTURE_API UPCGUtilsGCFactoryProviderSettings : public UPCGUtilsFractureElementBaseSettings
{
	GENERATED_BODY()

	friend class FPCGUtilsGCFactoryProviderElement;

public:
	virtual FName GetMainOutputPin() const;
	virtual UPCGUtilsGCFactoryData* CreateFactory(
		FPCGContext* InContext, UPCGUtilsGCFactoryData* InFactory = nullptr) const;

protected:
	virtual const FPCGDataTypeBaseId& GetFactoryTypeId() const;
	virtual TArray<FPCGPinProperties> InputPinProperties() const override;
	virtual TArray<FPCGPinProperties> OutputPinProperties() const override;
	virtual FPCGElementPtr CreateElement() const override;
};

class PCGUTILSFRACTURE_API FPCGUtilsGCFactoryProviderElement final : public IPCGElement
{
protected:
	virtual bool ExecuteInternal(FPCGContext* Context) const override;

public:
	virtual void DisabledPassThroughData(FPCGContext* Context) const override;
};
