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
	const FName Sources = TEXT("Sources");
}

/** How a flow source's effect fades from its centre to its Radius. */
UENUM(BlueprintType)
enum class EPCGVoxelFlowFalloff : uint8
{
	/** Full at the centre, easing to 0 at the Radius with no hard edge: (1 - (d/r)^2)^2. */
	Smooth,
	/** Straight line from full at the centre to 0 at the Radius. */
	Linear,
	/** Full everywhere inside the Radius, nothing outside. */
	Constant,
};

/** The axes a flow source's Force is given in. */
UENUM(BlueprintType)
enum class EPCGVoxelFlowSourceSpace : uint8
{
	/** World axes: every source pushes along its Force as written. */
	World,
	/** The source point's own axes: Force (1, 0, 0) pushes along the point's forward, so turning the point aims it. */
	Point,
};

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
	 * same unit). May be zero only when the Sources pin has data (sources alone drive the air). @Data mode reads a
	 * Vector (default @Data.Wind) from each set.
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

	/** Swirl strength, as a fraction of the wind speed (or of the strongest source when there is no wind). 0 (default) is no turbulence. Seeded by the node seed. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Wind", meta=(PCG_Overridable, ClampMin="0"))
	double TurbulenceStrength = 0.0;

	/** Rough size of one swirl, in world units (grid space). Must be greater than 0 when Turbulence Strength is on. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Wind", meta=(PCG_Overridable, ClampMin="0", EditCondition="TurbulenceStrength > 0"))
	double TurbulenceSize = 800.0;

	// ---- Sources ----

	/**
	 * Sources pin. Optional point attribute (Vector): a push along this vector (fan, jet), in Force Space, with its
	 * length as the speed it adds at the centre. The solve keeps the push but sends the air round in a loop, so a jet
	 * also pulls air in behind it.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Sources", meta=(PCG_Overridable))
	FName ForceAttribute = TEXT("FlowForce");

	/** The axes Force Attribute is in: World, or each source point's own rotation (turn the point to aim it). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Sources", meta=(PCG_Overridable))
	EPCGVoxelFlowSourceSpace ForceSpace = EPCGVoxelFlowSourceSpace::World;

	/**
	 * Sources pin. Optional point attribute (Double): air made (positive, a vent) or taken away (negative, suction)
	 * at the source, as the outward speed the air would have at the Radius with nothing in the way. Unlike Force this
	 * is fed into the solve itself, so the air really leaves (or arrives) through the open edges of the air box.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Sources", meta=(PCG_Overridable))
	FName OutflowAttribute = TEXT("FlowOutflow");

	/**
	 * Sources pin. Optional point attribute (Double): swirl speed about the source point's up axis (its local Z), turning
	 * counter-clockwise seen from above for positive values. Tilt the point to tilt the vortex.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Sources", meta=(PCG_Overridable))
	FName SwirlAttribute = TEXT("FlowSwirl");

	/**
	 * On (default): a source's Radius is its point's largest scaled half-size (the box you see). Off: Radius
	 * Attribute is read instead.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Sources", meta=(PCG_Overridable))
	bool bRadiusFromBounds = true;

	/** Sources pin, Radius From Bounds off only. Point attribute (Double, greater than 0): how far each source reaches. Required then. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Sources", meta=(PCG_Overridable, EditCondition="!bRadiusFromBounds"))
	FName RadiusAttribute = TEXT("FlowRadius");

	/** How each source fades from its centre to its Radius. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Sources", meta=(PCG_Overridable))
	EPCGVoxelFlowFalloff SourceFalloff = EPCGVoxelFlowFalloff::Smooth;

	/** Multiplies every source's Force, Outflow and Swirl. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Sources", meta=(PCG_Overridable))
	double SourceStrength = 1.0;

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
	 * Cells points: how hard the air hits the point's open sides (Double, 0 to 1), for its most hit side: the starting
	 * air (wind less its shadow, plus source pushes, before the solve bends it) blowing into that side, over the
	 * reference speed (the wind speed, or the strongest source when there is no wind). 1 for a wall square on to the
	 * open wind, 0 for downwind, edge-on and fully sheltered sides.
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
