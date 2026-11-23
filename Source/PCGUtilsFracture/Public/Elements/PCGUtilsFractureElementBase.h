// Copyright Max Harris

#pragma once

#include "CoreMinimal.h"
#include "Factories/PCGUtilsFactoryProvider.h"
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
 * Provider base of the fracture domain (Fracture or GC Selection on a single output pin): the shared provider
 * (UPCGUtilsFactoryProviderSettings) with the GC palette category and title colour, and a CreateFactory typed to
 * the GC factory root. The category and colour repeat UPCGUtilsFractureElementBaseSettings because a provider
 * derives from the shared provider base, not from that class.
 */
UCLASS(Abstract, BlueprintType, ClassGroup=(Procedural), Category="PCGUtils|Fracture|Providers")
class PCGUTILSFRACTURE_API UPCGUtilsGeometryCollectionFactoryProviderSettings : public UPCGUtilsFactoryProviderSettings
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

	virtual UPCGUtilsGeometryCollectionFactoryData* CreateFactory(
		FPCGContext* InContext, UPCGUtilsGeometryCollectionFactoryData* InFactory = nullptr) const;

protected:
	virtual UPCGUtilsFactoryData* CreateFactoryBase(FPCGContext* InContext) const override;
	virtual const FPCGDataTypeBaseId& GetFactoryTypeId() const override;
};

/** The GC providers run the shared provider element. */
using FPCGUtilsGeometryCollectionFactoryProviderElement = FPCGUtilsFactoryProviderElement;
