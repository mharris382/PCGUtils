// Copyright Max Harris

#pragma once

#include "CoreMinimal.h"
#include "PCGSettings.h"
#include "PCGUtilsSettingsCategories.h"
#include "PCGUtilsVoxel.h"
#include "Voxel/PCGVoxelGrid.h"

#include "PCGVoxelSettingsBase.generated.h"

/**
 * Base of every Voxel | node: the palette category, the title color, and how the node reads a voxel set (Grid).
 * Nodes deriving straight from UPCGSettings must override GetType() or they land under Generic; this does it once.
 */
UCLASS(Abstract)
class PCGUTILSVOXEL_API UPCGVoxelSettingsBase : public UPCGSettings
{
	GENERATED_BODY()

public:
#if WITH_EDITOR
	virtual EPCGSettingsType GetType() const override
	{
		return PCGUtilsSettingsCategories::AsSettingsType(PCGUtilsSettingsCategories::EValue::Voxel);
	}
	virtual FLinearColor GetNodeTitleColor() const override { return PCGUtilsVoxelNodeColor::Title; }
	virtual TArray<FPCGSettingsOverridableParam> GatherOverridableParams() const override;
#endif

	/** How the voxel set is read: cell size, grid placement, chunks, and what to do with points that break the contract. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Grid", meta=(PCG_Overridable))
	FPCGVoxelGridReadSettings Grid;
};
