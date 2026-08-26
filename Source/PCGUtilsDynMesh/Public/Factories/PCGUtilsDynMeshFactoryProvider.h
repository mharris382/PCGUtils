// Copyright Max Harris
// Factory architecture adapted from PCGExtendedToolkit, Copyright 2026 Timothe Lapetite and contributors (MIT).

#pragma once

#include "CoreMinimal.h"
#include "PCGSettings.h"

#include "PCGUtilsDynMeshFactoryProvider.generated.h"

class UPCGUtilsDynMeshFactoryData;

UCLASS(Abstract, BlueprintType, ClassGroup=(Procedural), Category="PCGUtils|DynMesh|Providers")
class PCGUTILSDYNMESH_API UPCGUtilsDynMeshFactoryProviderSettings : public UPCGSettings
{
	GENERATED_BODY()

	friend class FPCGUtilsDynMeshFactoryProviderElement;

public:
#if WITH_EDITOR
	virtual EPCGSettingsType GetType() const override { return EPCGSettingsType::DynamicMesh; }
	virtual FLinearColor GetNodeTitleColor() const override { return FLinearColor(0.36f, 0.18f, 0.72f, 1.0f); }
#endif

	virtual FName GetMainOutputPin() const;
	virtual UPCGUtilsDynMeshFactoryData* CreateFactory(
		FPCGContext* InContext, UPCGUtilsDynMeshFactoryData* InFactory = nullptr) const;

protected:
	virtual void ApplyDeprecationBeforeUpdatePins(
		UPCGNode* InOutNode, TArray<TObjectPtr<UPCGPin>>& InputPins,
		TArray<TObjectPtr<UPCGPin>>& OutputPins) override;
	virtual const FPCGDataTypeBaseId& GetFactoryTypeId() const;
	virtual TArray<FPCGPinProperties> InputPinProperties() const override;
	virtual TArray<FPCGPinProperties> OutputPinProperties() const override;
	virtual FPCGElementPtr CreateElement() const override;
};

class PCGUTILSDYNMESH_API FPCGUtilsDynMeshFactoryProviderElement final : public IPCGElement
{
protected:
	virtual bool ExecuteInternal(FPCGContext* Context) const override;

public:
	virtual void DisabledPassThroughData(FPCGContext* Context) const override;
};
