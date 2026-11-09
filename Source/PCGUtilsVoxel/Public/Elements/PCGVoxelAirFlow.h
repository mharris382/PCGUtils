// Copyright Max Harris

#pragma once

#include "CoreMinimal.h"
#include "Details/PCGUtilsInputShorthands.h"
#include "Elements/PCGVoxelCellProperties.h"
#include "Elements/PCGVoxelSettingsBase.h"
#include "PCGElement.h"

#include "PCGVoxelAirFlow.generated.h"

namespace PCGVoxelAirFlowPins
{
	const FName Flow = TEXT("Flow");
	const FName Cells = TEXT("Cells");
}

/**
 * Wind blowing past a voxel set. The air around the set is cut into air cells (one or more voxels each) and a smooth
 * flow is solved that goes around the solid cells instead of through them: it speeds up over tops and round corners,
 * slows in front of walls, and is weaker in the wind shadow behind solids. Optional turbulence adds swirls.
 *
 * Out on Flow: one point per air cell, a voxel set of its own (its Voxel Size and grid transform are written to @Data,
 * so every Voxel node reads it), with the flow vector and optional speed, shelter and distance to the solids. Out on
 * Cells: the input with how hard the wind hits each cell and the flow along its surface.
 *
 * The flow is "potential flow" plus a wind shadow: no flow ever enters a solid cell and air is never created or lost,
 * but it is not a full fluid simulation (no vortex shedding, no time). It is meant for art: foliage sway, particle
 * drift, streaks, snow and moss placement, flags.
 */
UCLASS(BlueprintType, ClassGroup=(Procedural), Category="PCGUtils|Voxel",
	meta=(Keywords="voxel voxels air flow wind field vector velocity breeze drift shelter wake turbulence curl"))
class PCGUTILSVOXEL_API UPCGVoxelAirFlowSettings : public UPCGVoxelSettingsBase
{
	GENERATED_BODY()

public:
	UPCGVoxelAirFlowSettings();

#if WITH_EDITOR
	virtual FName GetDefaultNodeName() const override { return TEXT("VoxelAirFlow"); }
	virtual FText GetDefaultNodeTitle() const override;
	virtual FText GetNodeTooltipText() const override;
#endif
	virtual bool UseSeed() const override { return true; }

	// ---- Wind ----

	/**
	 * The wind far from the solids: its direction, with its length as the speed (any unit; the flow comes out in the
	 * same unit). Must not be zero. @Data mode reads a Vector (default @Data.Wind) from each set.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Wind", meta=(PCG_Overridable))
	FPCGUtilsInputShorthandVector Wind;

	/** The axes Wind is given in. World (default) or the set's grid axes. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Wind", meta=(PCG_Overridable))
	EPCGVoxelDirectionSpace WindSpace = EPCGVoxelDirectionSpace::World;

	/**
	 * How far behind a solid its wind shadow reaches, in air cells. Air in the shadow starts with less wind (Wake
	 * Strength), and the solve then sends that air over and around instead. 0 turns shadows off.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Wind", meta=(PCG_Overridable, ClampMin="0"))
	double WakeLength = 6.0;

	/** How much wind is taken away right behind a solid (0 none, 1 all), fading to none at Wake Length. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Wind", meta=(PCG_Overridable, ClampMin="0", ClampMax="1"))
	double WakeStrength = 0.8;

	/** Swirl strength, as a fraction of the wind speed. 0 (default) is no turbulence. Seeded by the node seed. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Wind", meta=(PCG_Overridable, ClampMin="0"))
	double TurbulenceStrength = 0.0;

	/** Rough size of one swirl, in world units (grid space). Must be greater than 0 when Turbulence Strength is on. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Wind", meta=(PCG_Overridable, ClampMin="0", EditCondition="TurbulenceStrength > 0"))
	double TurbulenceSize = 800.0;

	// ---- Air ----

	/**
	 * How many voxels wide an air cell is along each axis (1 to 16). Larger is coarser and much cheaper: memory and
	 * time fall with the cube of this.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Air", meta=(PCG_Overridable, ClampMin="1", ClampMax="16"))
	int32 AirCellVoxels = 1;

	/**
	 * Air Cell Voxels above 1 only. An air cell is solid when more than this fraction of its voxels are filled. 0
	 * (default): any filled voxel makes it solid, so thin walls still block the wind.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Air", meta=(PCG_Overridable, ClampMin="0", ClampMax="0.999"))
	double SolidFraction = 0.0;

	/** How many air cells of air surround the set's bounds on each side. Past them the air is free (open). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Air", meta=(PCG_Overridable, ClampMin="0"))
	int32 Padding = 4;

	/**
	 * On: the air stops at the bottom of the lowest cells and that floor is a wall, so wind cannot pass under the set
	 * (terrain, anything standing on the ground). Off (default): air all round (floating shapes).
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Air", meta=(PCG_Overridable))
	bool bGroundAtBottom = false;

	/** The most air cells one set may use. Each costs about 60 bytes while the node runs. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Air", meta=(PCG_Overridable, ClampMin="1"))
	int32 MaxAirCells = 4000000;

	// ---- Solve ----

	/** Blur passes over the starting wind (shadows and turbulence), before the solve. Softens shadow edges. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Solve", meta=(PCG_Overridable, ClampMin="0"))
	int32 SmoothPasses = 2;

	/** Most solver steps. More gets closer to the exact flow on large sets; the solve stops early once Tolerance is met. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Solve", meta=(PCG_Overridable, ClampMin="0"))
	int32 MaxIterations = 300;

	/** The solve stops when the leftover error falls below this fraction of where it started. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Solve", meta=(PCG_Overridable, ClampMin="0"))
	double Tolerance = 0.001;

	// ---- Flow output ----

	/** Only air cells at most this many air cells from a solid come out on Flow. 0 (default): every air cell. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Flow Output", meta=(PCG_Overridable, ClampMin="0"))
	int32 MaxDistance = 0;

	/** The axes Flow and Surface Flow vectors are written in. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Flow Output", meta=(PCG_Overridable))
	EPCGVoxelDirectionSpace OutputSpace = EPCGVoxelDirectionSpace::World;

	/** Flow points: the air velocity (Vector), in Wind's unit. Always written. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Flow Output", meta=(PCG_Overridable))
	FName FlowAttribute = TEXT("Flow");

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Flow Output", meta=(PCG_Overridable, InlineEditConditionToggle))
	bool bWriteSpeed = true;

	/** Flow points: the air speed (Double), the length of Flow. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Flow Output", meta=(PCG_Overridable, EditCondition="bWriteSpeed"))
	FName SpeedAttribute = TEXT("FlowSpeed");

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Flow Output", meta=(PCG_Overridable, InlineEditConditionToggle))
	bool bWriteShelter = true;

	/** Flow points: how deep in a wind shadow the cell is (Double, 0 open to 1 right behind a solid or sealed in). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Flow Output", meta=(PCG_Overridable, EditCondition="bWriteShelter"))
	FName ShelterAttribute = TEXT("Shelter");

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Flow Output", meta=(PCG_Overridable, InlineEditConditionToggle))
	bool bWriteSolidDistance = false;

	/** Flow points: steps to the nearest solid air cell through face neighbours (Integer32, 1 = touching). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Flow Output", meta=(PCG_Overridable, EditCondition="bWriteSolidDistance"))
	FName SolidDistanceAttribute = TEXT("SolidDistance");

	// ---- Cells output ----

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Cells Output", meta=(PCG_Overridable, InlineEditConditionToggle))
	bool bWriteExposure = true;

	/**
	 * Cells points: how hard the wind hits the point's open sides (Double, 0 to 1), for its most hit side: how square
	 * the side faces the wind, times how open the air outside it is (1 - Shelter). 1 for a wall square on to the open
	 * wind, 0 for downwind, edge-on and fully sheltered sides. Uses the wind shadow, so Wake Length 0 gives facing only.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Cells Output", meta=(PCG_Overridable, EditCondition="bWriteExposure"))
	FName ExposureAttribute = TEXT("WindExposure");

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Cells Output", meta=(PCG_Overridable, InlineEditConditionToggle))
	bool bWriteSurfaceFlow = true;

	/** Cells points: the mean air velocity just outside the point's open sides (Vector), in Output Space. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Cells Output", meta=(PCG_Overridable, EditCondition="bWriteSurfaceFlow"))
	FName SurfaceFlowAttribute = TEXT("SurfaceFlow");

protected:
	virtual TArray<FPCGPinProperties> InputPinProperties() const override;
	virtual TArray<FPCGPinProperties> OutputPinProperties() const override;
	virtual FPCGElementPtr CreateElement() const override;
};

class PCGUTILSVOXEL_API FPCGVoxelAirFlowElement : public IPCGElement
{
protected:
	virtual bool ExecuteInternal(FPCGContext* Context) const override;
};
