// Copyright Max Harris
// Factory architecture adapted from PCGExtendedToolkit, Copyright 2026 Timothe Lapetite and contributors (MIT).

#pragma once

#include "CoreMinimal.h"
#include "PCGSettings.h"

#include "PCGUtilsFactoryProvider.generated.h"

class UPCGUtilsFactoryData;

/**
 * The one base of every node that emits a PCGUtils factory on a single output pin. A derived class supplies the
 * pin name, the pin type and the factory; the shared element prepares it, captures every input as a data
 * dependency and emits it.
 *
 * Domain bases (UPCGUtilsDynMeshFactoryProviderSettings, UPCGUtilsGeometryCollectionFactoryProviderSettings)
 * derive from this, set their palette category and title colour, and expose a CreateFactory typed to their own
 * factory root.
 */
UCLASS(Abstract, BlueprintType, ClassGroup=(Procedural), Category="PCGUtils|Core")
class PCGUTILSCORE_API UPCGUtilsFactoryProviderSettings : public UPCGSettings
{
	GENERATED_BODY()

	friend class FPCGUtilsFactoryProviderElement;

public:
	virtual FName GetMainOutputPin() const { return NAME_None; }

	/** Decorators may preserve the primary input's tagged-data envelope while replacing its immutable recipe. */
	virtual FName GetOutputSourcePin() const { return NAME_None; }

	/** Whether the factory must be created on the game thread, e.g. because it loads assets. Off by default. */
	virtual bool RequiresMainThread(FPCGContext* InContext) const { return false; }

protected:
	/** Creates the factory this node emits. Null (already logged, or nothing to emit) emits nothing. */
	virtual UPCGUtilsFactoryData* CreateFactoryBase(FPCGContext* InContext) const
		PURE_VIRTUAL(UPCGUtilsFactoryProviderSettings::CreateFactoryBase, return nullptr;);

	/** The type of the output pin. */
	virtual const FPCGDataTypeBaseId& GetFactoryTypeId() const
		PURE_VIRTUAL(UPCGUtilsFactoryProviderSettings::GetFactoryTypeId, return FPCGDataTypeInfo::AsId(););

	virtual TArray<FPCGPinProperties> InputPinProperties() const override;
	virtual TArray<FPCGPinProperties> OutputPinProperties() const override;
	virtual FPCGElementPtr CreateElement() const override;
};

class PCGUTILSCORE_API FPCGUtilsFactoryProviderElement final : public IPCGElement
{
protected:
	virtual bool ExecuteInternal(FPCGContext* Context) const override;

public:
	virtual bool CanExecuteOnlyOnMainThread(FPCGContext* Context) const override;
	virtual void DisabledPassThroughData(FPCGContext* Context) const override;
};
