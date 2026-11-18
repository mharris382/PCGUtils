// Copyright Max Harris

#pragma once

#include "CoreMinimal.h"
#include "Details/PCGUtilsInputShorthands.h"
#include "Details/PCGUtilsPointShorthands.h"
#include "Elements/Creation/PrimitiveBuilder/PCGUtilsPrimitiveFittingDetails.h"
#include "Elements/PCGVoxelSettingsBase.h"
#include "PCGElement.h"

#include "PCGVoxelBuildNavGrid.generated.h"

namespace PCGVoxelBuildNavGridPins
{
	inline const FName VoxelGrid = TEXT("VoxelGrid");
	inline const FName Bounds = TEXT("Bounds");
	inline const FName Vtx = TEXT("Vtx");
	inline const FName Edges = TEXT("Edges");
}

/** How the agent using a nav grid moves. */
UENUM(BlueprintType)
enum class EPCGVoxelNavAgentType : uint8
{
	/** Moves freely through empty space, keeping Surface Clearance from filled cells. */
	Flying,
	/** Walks on the surface of filled cells. Planned; not built yet. */
	Surface,
};

/** Which empty space a flying nav grid fills when no Bounds limit it further. */
UENUM(BlueprintType)
enum class EPCGVoxelNavFlyingSpace : uint8
{
	/** Every empty cell in the set's bounds grown by Padding cells on each side (or in Bounds, when connected). */
	PaddedBox UMETA(DisplayName="Padded Box"),
	/** Only empty space no path through empty cells leads out of: a sealed interior. */
	EnclosedOnly UMETA(DisplayName="Enclosed Only"),
	/** Only empty space joined to the outside. */
	OutsideOnly UMETA(DisplayName="Outside Only"),
};

/**
 * Builds a navigation graph from a voxel set, as a PCGEx cluster: Vtx (one point per vertex) and Edges (one point per
 * connection), paired by the PCGEx tags and attributes, so every PCGEx cluster node (pathfinding, refinement, flood
 * fill) reads it with no bridge node. Each connected island is its own Edges data, all sharing the input's Vtx data.
 *
 * Flying: the agent fits in every empty cell where its box (Agent Size) keeps Surface Clearance from every filled cell.
 * Uncompressed, each such cell is a vertex with edges to its face neighbours (and, with diagonals, to edge and corner
 * neighbours when no corner is cut). Compressed (default), those cells are merged into boxes: one vertex at each box's
 * centre, portal vertices on the faces boxes share, hub cubes round each centre (Hub Cubes), and every edge a
 * straight line inside one box.
 */
UCLASS(BlueprintType, ClassGroup=(Procedural), Category="PCGUtils|Voxel",
	meta=(Keywords="voxel voxels nav navigation grid graph cluster pcgex vtx edges flying fly agent path pathfinding air"))
class PCGUTILSVOXEL_API UPCGVoxelBuildNavGridSettings : public UPCGVoxelSettingsBase
{
	GENERATED_BODY()

public:
#if WITH_EDITOR
	virtual FName GetDefaultNodeName() const override { return TEXT("VoxelBuildNavGrid"); }
	virtual FText GetDefaultNodeTitle() const override;
	virtual FText GetNodeTooltipText() const override;
#endif

	/** How the agent moves: Flying through empty space, or Surface (walking on filled cells; planned, not built yet). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Settings", meta=(PCG_Overridable))
	EPCGVoxelNavAgentType AgentNavType = EPCGVoxelNavAgentType::Flying;

	/**
	 * The agent's size in cells along the grid's X, Y and Z (each greater than 0). Each axis is rounded up to whole
	 * cells; the agent needs that many empty cells, and a vertex sits at the middle of them (an even count puts it on a
	 * cell boundary). Constant, or one value per input from @Data (Vector, default attribute AgentSize).
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Settings", meta=(PCG_Overridable))
	FPCGUtilsInputShorthandVector AgentSize = FPCGUtilsInputShorthandVector(FVector(1.0, 1.0, 2.0), TEXT("AgentSize"));

	/** Treat thin gaps in the set (slots and holes narrower than 2 x Gap Radius + 1 cells) as filled: the agent never enters them. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Settings", meta=(PCG_Overridable))
	bool bIgnoreGaps = true;

	/** Gaps narrower than 2 x this + 1 cells count as filled (1: one-cell slots). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Settings", meta=(PCG_Overridable, EditCondition="bIgnoreGaps", ClampMin=1, ClampMax=16, UIMin=1, UIMax=8))
	int32 GapRadius = 1;

	/** Also connect edge and corner neighbours (up to 26 per vertex), where every cell the move passes beside is a vertex too, so no corner is cut. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Settings", meta=(PCG_Overridable))
	bool bEnableDiagonals = false;

	/**
	 * Merge the cells the agent fits in into boxes instead of one vertex per cell: each box gets a centre vertex, the
	 * faces boxes share get portal vertices (one per Compression Size tile), and every edge is a straight line inside
	 * one box, so it never leaves free space. Two boxes with the same face meet centre to centre with no portal.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Compression", meta=(PCG_Overridable))
	bool bEnableCompression = true;

	/**
	 * Compression: the largest box side, in cells, near surfaces (every cell when aerial collapse is off), and the
	 * widest portal tile on a shared face. Rounded to whole cells, 1 or greater. Constant, or one value per input from
	 * @Data (Double, default attribute CompressionSize).
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Compression", meta=(PCG_Overridable, EditCondition="bEnableCompression"))
	FPCGUtilsInputShorthandDoubleAbs CompressionSize = FPCGUtilsInputShorthandDoubleAbs(4.0, TEXT("CompressionSize"));

	/**
	 * Compression: nested cubes round the centre of each box two or more portals open into, so paths through a box bend
	 * round its middle instead of all meeting at one centre vertex. Cube c of N sits (c + 1) / (N + 1) of the way from
	 * the centre to the box's extent (one cube: halfway). Its corners join the centre (the innermost cube) or the cube
	 * inside it, its sides join along the lattice, and each portal joins its centre and the outermost cube's nearest
	 * point (every point tied for nearest). 0: portals join only the centre. Each cube adds up to 8 vertices and about 20
	 * edges per box at 1 subdivision; keep it low.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Compression", meta=(PCG_Overridable, EditCondition="bEnableCompression", ClampMin=0, UIMin=0, UIMax=3))
	int32 HubCubes = 1;

	/**
	 * Compression: steps along each side of a hub cube's surface (1: just its corners). A cube of n steps has
	 * 6 n^2 + 2 points and more edges; it gives portals a closer point to join.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Compression", meta=(PCG_Overridable, EditCondition="bEnableCompression && HubCubes > 0", ClampMin=1, UIMin=1, UIMax=4))
	int32 HubSubdivisions = 1;

	/**
	 * Flying, compression: cells at least Aerial Cell Distance from every filled cell are aerial; merge them into boxes
	 * up to Aerial Cell Collapse Size instead of Compression Size. Aerial and near cells never share a box.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Flying", meta=(PCG_Overridable, EditCondition="bEnableCompression"))
	bool bCollapseAerialCells = true;

	/**
	 * Flying: how far, in cells, a cell's box lies from every filled cell's box (Euclidean, box to box) to count as
	 * aerial. 0: every empty cell is aerial.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Flying", meta=(PCG_Overridable, ClampMin=0, UIMin=0, UIMax=16))
	int32 AerialCellDistance = 2;

	/** Flying, aerial collapse: the largest box of aerial cells, in cells along X, Y and Z (each 1 or greater). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Flying", meta=(PCG_Overridable, EditCondition="bEnableCompression && bCollapseAerialCells"))
	FIntVector AerialCellCollapseSize = FIntVector(16);

	/** Flying: which empty space to fill. Bounds, when connected, limits it further. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Flying", meta=(PCG_Overridable))
	EPCGVoxelNavFlyingSpace FlyingSpace = EPCGVoxelNavFlyingSpace::PaddedBox;

	/** Flying: cells of empty space added round the set's bounds on every side (no Bounds connected). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Flying", meta=(PCG_Overridable, ClampMin=0, UIMin=0, UIMax=16))
	int32 Padding = 2;

	/**
	 * Flying: how far, in cells, the agent's box keeps from each filled cell's box (0: it may touch; 1: never in a cell
	 * next to one, diagonals included). Per filled point (an attribute gives hazardous cells a wider berth), or constant.
	 * Optional attribute: Double, 0 or greater, default name SurfaceClearance.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Flying", meta=(PCG_Overridable))
	FPCGUtilsPointShorthandDoubleAbs SurfaceClearance = FPCGUtilsPointShorthandDoubleAbs(1.0, TEXT("SurfaceClearance"));

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Outputs", meta=(PCG_Overridable, InlineEditConditionToggle))
	bool bOutputSurfaceDistance = true;

	/** Optional output on Vtx, Double: distance in world units from the vertex's cell centre to the nearest filled cell's box. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Outputs", meta=(PCG_Overridable, EditCondition="bOutputSurfaceDistance"))
	FName SurfaceDistanceAttribute = TEXT("SurfaceDistance");

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Outputs", meta=(PCG_Overridable, InlineEditConditionToggle))
	bool bOutputNearestVoxelIndex = false;

	/**
	 * Optional output on Vtx, Integer32: index in the input of the point filling the nearest filled cell (a chunk's own
	 * point for a chunked set); -1 when the set is empty. Gaps Ignore Gaps fills are not counted as filled here.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Outputs", meta=(PCG_Overridable, EditCondition="bOutputNearestVoxelIndex"))
	FName NearestVoxelIndexAttribute = TEXT("NearestVoxelIndex");

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Outputs", meta=(PCG_Overridable, InlineEditConditionToggle))
	bool bOutputIsAerial = true;

	/**
	 * Optional output on Vtx, Boolean: true for a vertex in aerial space (Aerial Cell Distance from every filled cell).
	 * A box centre takes its box's cells; a portal is aerial when the boxes on both sides are.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Outputs", meta=(PCG_Overridable, EditCondition="bOutputIsAerial"))
	FName IsAerialAttribute = TEXT("IsAerial");

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Outputs", meta=(PCG_Overridable, InlineEditConditionToggle))
	bool bOutputIsPortal = true;

	/**
	 * Optional output on Vtx, Boolean: true on a portal vertex (on a face two boxes share). Portals keep paths in free
	 * space between boxes; downstream they can be dropped from a found path to smooth it.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Outputs", meta=(PCG_Overridable, EditCondition="bOutputIsPortal"))
	FName IsPortalAttribute = TEXT("IsPortal");

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Outputs", meta=(PCG_Overridable, InlineEditConditionToggle))
	bool bOutputChunkSize = true;

	/**
	 * Optional output on Vtx, Vector: the size in cells of the box a centre vertex stands for ((1, 1, 1) uncompressed);
	 * (0, 0, 0) on portals and hub cube points, which tells centres from the rest.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Outputs", meta=(PCG_Overridable, EditCondition="bOutputChunkSize"))
	FName ChunkSizeAttribute = TEXT("ChunkSize");

	/** When two filled cells are equally near, the one lying further along the first axis wins, then the second, then the third. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Outputs", meta=(PCG_Overridable, EditCondition="bOutputNearestVoxelIndex"))
	EPCGUtilsAxisOrder NearestVoxelAxisPriority = EPCGUtilsAxisOrder::XYZ;

	/** The node stops with an error when the box of cells it works over has more cells than this. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Settings", meta=(PCG_Overridable, ClampMin="1"))
	int64 MaxCells = 64 * 1024 * 1024;

protected:
	virtual TArray<FPCGPinProperties> InputPinProperties() const override;
	virtual TArray<FPCGPinProperties> OutputPinProperties() const override;
	virtual FPCGElementPtr CreateElement() const override;
};

class PCGUTILSVOXEL_API FPCGVoxelBuildNavGridElement : public IPCGElement
{
protected:
	virtual bool ExecuteInternal(FPCGContext* Context) const override;
};
