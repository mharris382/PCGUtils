// Copyright Max Harris
// Factory architecture adapted from PCGExtendedToolkit, Copyright 2026 Timothe Lapetite and contributors (MIT).

#pragma once

#include "CoreMinimal.h"
#include "Factories/PCGUtilsFactoryProvider.h"
#include "PCGUtilsDynMeshNodeColor.h"

#include "PCGUtilsDynMeshFactoryProvider.generated.h"

class UPCGUtilsDynMeshFactoryData;

/**
 * Provider base of the DynMesh domain (selectors, builders, painters): the shared provider with the DynMesh palette
 * category and title colour, and a CreateFactory typed to the DynMesh factory root.
 */
UCLASS(Abstract, BlueprintType, ClassGroup=(Procedural), Category="PCGUtils|Core")
class PCGUTILSCORE_API UPCGUtilsDynMeshFactoryProviderSettings : public UPCGUtilsFactoryProviderSettings
{
	GENERATED_BODY()

public:
#if WITH_EDITOR
	virtual EPCGSettingsType GetType() const override { return EPCGSettingsType::DynamicMesh; }
	virtual FLinearColor GetNodeTitleColor() const override { return PCGUtilsDynMeshNodeColor::Title; }
#endif

	virtual UPCGUtilsDynMeshFactoryData* CreateFactory(
		FPCGContext* InContext, UPCGUtilsDynMeshFactoryData* InFactory = nullptr) const;

protected:
	virtual UPCGUtilsFactoryData* CreateFactoryBase(FPCGContext* InContext) const override;
	virtual const FPCGDataTypeBaseId& GetFactoryTypeId() const override;
};

/** The DynMesh providers run the shared provider element. */
using FPCGUtilsDynMeshFactoryProviderElement = FPCGUtilsFactoryProviderElement;
