// Copyright Max Harris

#pragma once

#include "CoreMinimal.h"
#include "Factories/PCGUtilsDynMeshFactoryProvider.h"
#include "Factories/PCGUtilsDynMeshPainterFactory.h"

#include "PCGDynMeshRandomValueByIslandPainter.generated.h"

/**
 * Immutable configuration for a Painter that assigns one deterministic random scalar to each connected vertex
 * component ("mesh island") of the canonical target mesh.
 */
UCLASS(BlueprintType, ClassGroup=(Procedural), Category="PCGUtils|DynMesh|Painters")
class PCGUTILSPAINTER_API UPCGDynMeshRandomValueByIslandPainterFactoryData
	: public UPCGUtilsDynMeshPainterFactoryData
{
	GENERATED_BODY()

public:
	UPROPERTY()
	int32 Seed = 0;

	UPROPERTY()
	float MinValue = 0.0f;

	UPROPERTY()
	float MaxValue = 1.0f;

protected:
	virtual TSharedPtr<FPCGUtilsDynMeshPainterOperation> CreateOperationInternal() const override;
	virtual void AddToCrc(FArchiveCrc32& Ar, bool bFullDataCrc) const override;
};

/**
 * A topology-dependent Painter: during preparation it finds the connected vertex components of the whole
 * canonical Dynamic Mesh, gives each one a deterministic random value hashed from the seed and a per-island
 * key, and returns that value per vertex during evaluation.
 *
 * Islands are the connected components of the canonical mesh's base topology, so UV / normal / color-overlay
 * seams and material boundaries never split one. Works on Dynamic Mesh and Static Mesh Component targets alike
 * (through the canonical LOD0 mesh); lower Static Mesh LODs receive the transferred colours, never a fresh
 * island pass.
 */
UCLASS(BlueprintType, ClassGroup=(Procedural), Category="PCGUtils|DynMesh|Painters",
	meta=(Keywords="random island connected component mesh piece per-island noise mask painter scalar DynMesh"))
class PCGUTILSPAINTER_API UPCGDynMeshRandomValueByIslandPainterProviderSettings
	: public UPCGUtilsDynMeshFactoryProviderSettings
{
	GENERATED_BODY()

public:
#if WITH_EDITOR
	virtual FName GetDefaultNodeName() const override { return TEXT("DynMeshRandomValueByMeshIsland"); }
	virtual FText GetDefaultNodeTitle() const override;
	virtual FText GetNodeTooltipText() const override;
#endif

	/** The inherited `Seed` (Settings category) is the determinism control: same Seed + topology + vertex IDs -> same per-island values. */
	virtual bool UseSeed() const override { return true; }

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Painter", meta=(PCG_Overridable))
	float MinValue = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Painter", meta=(PCG_Overridable))
	float MaxValue = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Painter", AdvancedDisplay, meta=(PCG_Overridable))
	int32 Priority = 0;

	virtual FName GetMainOutputPin() const override;
	virtual UPCGUtilsDynMeshFactoryData* CreateFactory(
		FPCGContext* InContext, UPCGUtilsDynMeshFactoryData* InFactory = nullptr) const override;

protected:
	virtual const FPCGDataTypeBaseId& GetFactoryTypeId() const override;
};
