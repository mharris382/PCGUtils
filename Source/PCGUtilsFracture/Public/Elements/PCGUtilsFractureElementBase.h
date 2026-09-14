// Copyright Max Harris

#pragma once

#include "CoreMinimal.h"
#include "PCGSettings.h"
#include "PCGUtilsSettingsCategories.h"

#include "PCGUtilsFractureElementBase.generated.h"

class UPCGUtilsGeometryCollectionFactoryData;

/**
 * Shared settings base for every PCGUtilsFracture node.
 *
 * Its main job is `GetType()`. PCGUtilsCore registers real GC categories in EPCGSettingsType at startup, and
 * this base maps each concrete class from its module-relative source folder. Deriving every element in this
 * module from here prevents nodes from silently falling into Generic or the engine's Dynamic Mesh bucket.
 */
UCLASS(Abstract, BlueprintType, ClassGroup=(Procedural), Category="PCGUtils|Fracture")
class PCGUTILSFRACTURE_API UPCGUtilsFractureElementBaseSettings : public UPCGSettings
{
	GENERATED_BODY()

public:
#if WITH_EDITOR
	virtual EPCGSettingsType GetType() const override
	{
		return PCGUtilsSettingsCategories::GeometryCollectionCategoryForClass(GetClass());
	}
	virtual FLinearColor GetNodeTitleColor() const override;
#endif
};

/**
 * Provider base for nodes that emit a fracture-domain factory (Fracture or GC Selection) on a single output
 * pin. Mirrors UPCGUtilsDynMeshFactoryProviderSettings: the derived class supplies the pin name, the type id
 * and a CreateFactory override, and the shared element handles emission and data-dependency capture.
 */
UCLASS(Abstract, BlueprintType, ClassGroup=(Procedural), Category="PCGUtils|Fracture|Providers")
class PCGUTILSFRACTURE_API UPCGUtilsGeometryCollectionFactoryProviderSettings : public UPCGUtilsFractureElementBaseSettings
{
	GENERATED_BODY()

	friend class FPCGUtilsGeometryCollectionFactoryProviderElement;

public:
	virtual FName GetMainOutputPin() const;
	virtual UPCGUtilsGeometryCollectionFactoryData* CreateFactory(
		FPCGContext* InContext, UPCGUtilsGeometryCollectionFactoryData* InFactory = nullptr) const;

	/** Whether CreateFactory must run on the game thread - e.g. because it loads assets. Off by default. */
	virtual bool RequiresMainThread(FPCGContext* InContext) const { return false; }

protected:
	virtual const FPCGDataTypeBaseId& GetFactoryTypeId() const;
	virtual TArray<FPCGPinProperties> InputPinProperties() const override;
	virtual TArray<FPCGPinProperties> OutputPinProperties() const override;
	virtual FPCGElementPtr CreateElement() const override;
};

class PCGUTILSFRACTURE_API FPCGUtilsGeometryCollectionFactoryProviderElement final : public IPCGElement
{
protected:
	virtual bool ExecuteInternal(FPCGContext* Context) const override;

public:
	virtual bool CanExecuteOnlyOnMainThread(FPCGContext* Context) const override;
	virtual void DisabledPassThroughData(FPCGContext* Context) const override;
};
