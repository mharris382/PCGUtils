// Copyright Max Harris

#pragma once

#include "CoreMinimal.h"
#include "Elements/Creation/PrimitiveBuilder/PCGUtilsPrimitiveFittingDetails.h"
#include "Factories/PCGUtilsDynMeshFactoryProvider.h"
#include "Factories/PCGUtilsDynMeshPrimitiveFactory.h"

#include "PCGPrimitiveBuilderFactory.generated.h"

class UPCGCreatePrimitiveSettingsBase;

/** Leaf Primitive Builder data: one Geometry Script primitive plus how it fits into a seed's bounds. */
UCLASS(BlueprintType, ClassGroup=(Procedural), Category="PCGUtils|DynMesh|Creation")
class PCGUTILSDYNMESH_API UPCGPrimitiveBuilderFactoryData : public UPCGUtilsDynMeshPrimitiveFactoryData
{
	GENERATED_BODY()

public:
	UPROPERTY()
	TObjectPtr<const UPCGCreatePrimitiveSettingsBase> Primitive;

	UPROPERTY()
	FPCGUtilsFittingDetails Fitting;

protected:
	virtual TSharedPtr<FPCGUtilsDynMeshPrimitiveOperation> CreateOperationInternal() const override;
	virtual void AddToCrc(FArchiveCrc32& Ar, bool bFullDataCrc) const override;
};

/** Authors a reusable Primitive Builder: an inline primitive type plus its Fitting settings. */
UCLASS(BlueprintType, ClassGroup=(Procedural), Category="PCGUtils|DynMesh|Creation")
class PCGUTILSDYNMESH_API UPCGPrimitiveBuilderFactoryProviderSettings : public UPCGUtilsDynMeshFactoryProviderSettings
{
	GENERATED_BODY()

public:
	UPCGPrimitiveBuilderFactoryProviderSettings(const FObjectInitializer& ObjectInitializer);

#if WITH_EDITOR
	virtual FName GetDefaultNodeName() const override { return TEXT("PrimitiveBuilder"); }
	virtual FText GetDefaultNodeTitle() const override;
	virtual FText GetNodeTooltipText() const override;
#endif

	/** The primitive type to generate. Pick a type in the dropdown to reveal its Geometry Script options. */
	UPROPERTY(EditAnywhere, Instanced, BlueprintReadWrite, Category = "Primitive")
	TObjectPtr<UPCGCreatePrimitiveSettingsBase> Primitive;

	/** How this primitive fits, aligns, and offsets into each seed's bounds. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Fitting", meta = (ShowOnlyInnerProperties))
	FPCGUtilsFittingDetails Fitting;

	virtual FName GetMainOutputPin() const override;
	virtual UPCGUtilsDynMeshFactoryData* CreateFactory(
		FPCGContext* InContext, UPCGUtilsDynMeshFactoryData* InFactory = nullptr) const override;

protected:
	virtual const FPCGDataTypeBaseId& GetFactoryTypeId() const override;
};
