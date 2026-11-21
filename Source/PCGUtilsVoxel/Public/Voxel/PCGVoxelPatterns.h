// Copyright Max Harris

#pragma once

#include "CoreMinimal.h"
#include "Voxel/PCGVoxelGrid.h"
#include "Voxel/PCGVoxelPointIO.h"
#include "Voxel/PCGVoxelRules.h"

#include "PCGVoxelPatterns.generated.h"

/*
 * Voxel patterns: a small authored shape that is looked for all over a voxel set and, where it fits, claims cells as
 * oriented chunks (one point per chunk, for a mesh to be fitted to downstream). See Docs/PCGUtilsVoxel.md section 4p.
 *
 * A pattern is a plain struct (node settings, graph parameters, data assets). It has three parts, all written in the
 * pattern's own frame, which is also the frame of the mesh it places (+Z up, +Y forward, as rule tiles):
 *   Probes  what the grid around the origin cell must look like (Filled / Empty / Unclaimed). Read only: a probed
 *           cell is never taken by the pattern. A cell another pattern claimed still reads as Filled, but not as
 *           Unclaimed: that is how a pattern asks for plain wall beside it. Solid Face asks one side of a cell to
 *           be whole, reading an earlier chunk's Open Sides.
 *   Boxes   the chunks the pattern makes where it fits: each a box of cells, the mesh index it writes and how its mesh
 *           is turned inside the pattern.
 *   Turns   the rotations the whole pattern (probes and boxes together) may take to fit.
 *
 * The origin cell (0,0,0) is always a filled cell of the set: patterns are tried at every filled cell.
 *
 * Patterns run in order. Each one claims the cells of its boxes; a claimed cell cannot be claimed again, by a later
 * pattern or by a later fit of the same one. Claiming does not empty a cell: probes always read the set's occupancy.
 */

/** One signed grid direction. */
UENUM(BlueprintType)
enum class EPCGVoxelPatternDirection : uint8
{
	PosX UMETA(DisplayName="+X"),
	NegX UMETA(DisplayName="-X"),
	PosY UMETA(DisplayName="+Y"),
	NegY UMETA(DisplayName="-Y"),
	PosZ UMETA(DisplayName="+Z"),
	NegZ UMETA(DisplayName="-Z"),
};

/** What a probed cell must be. */
UENUM(BlueprintType)
enum class EPCGVoxelProbeState : uint8
{
	/** The cell must be empty. */
	Empty,
	/** The cell must be filled (a cell of the set; with Probes See Added Cells, also a cell an earlier pattern added). */
	Filled,
	/**
	 * The cell must be a cell of the set that no earlier pattern has taken: still a plain cube. A cell an earlier
	 * pattern claimed or added does not count; cells this same pattern takes do (its fits never block each other).
	 * Use it for the wall a corner needs beside it, so a corner is not made against another pattern's chunk.
	 */
	Unclaimed,
	/**
	 * The cell must be filled and whole on the side named by the probe's Face: a plain cube, or a cell of a chunk an
	 * earlier pattern made whose box does not list that side in Open Sides. A corner chunk is still solid on its two
	 * back sides and reads as air on its two cut sides. Use it for what a new piece rests against.
	 */
	SolidFace,
};

/** What a pattern does with the cells of its boxes. */
UENUM(BlueprintType)
enum class EPCGVoxelPatternOperation : uint8
{
	/** The boxes cover filled cells: those cells become the pattern's chunks. Every cell of every box must be filled and not claimed yet. */
	Claim,
	/** The boxes cover empty cells: new cells are added there (an inside corner filled in). Every cell of every box must be empty and not added yet. */
	Add,
};

/** One cell the pattern looks at, relative to its origin cell. */
USTRUCT(BlueprintType)
struct PCGUTILSVOXEL_API FPCGVoxelPatternProbe
{
	GENERATED_BODY()

	/** The cell, in cells from the origin cell, in the pattern's frame. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Probe")
	FIntVector Offset = FIntVector(0, 1, 0);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Probe")
	EPCGVoxelProbeState State = EPCGVoxelProbeState::Empty;

	/**
	 * How many cells in a row the probe covers, starting at Offset and stepping along Step. 1: just the one cell.
	 * A corner that needs three cells of wall behind it is one probe with Count 3.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Probe", meta=(ClampMin="1"))
	int32 Count = 1;

	/** The direction the row runs from Offset, in the pattern's frame. Only matters when Count is more than 1. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Probe", meta=(EditCondition="Count > 1"))
	EPCGVoxelPatternDirection Step = EPCGVoxelPatternDirection::PosX;

	/** Solid Face only: the side of the probed cell that must be whole, in the pattern's frame (usually the side facing the box that rests on it). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Probe", meta=(EditCondition="State == EPCGVoxelProbeState::SolidFace"))
	EPCGVoxelPatternDirection Face = EPCGVoxelPatternDirection::PosX;
};

/** One chunk a pattern makes: a box of cells and the mesh that goes in it. */
USTRUCT(BlueprintType)
struct PCGUTILSVOXEL_API FPCGVoxelPatternBox
{
	GENERATED_BODY()

	/** The box's lowest cell, in cells from the origin cell, in the pattern's frame. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Box")
	FIntVector First = FIntVector::ZeroValue;

	/** How many cells the box covers along the pattern's X, Y and Z. Each at least 1. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Box")
	FIntVector Span = FIntVector(1);

	/** Written to the Mesh Index attribute of this chunk's point (0 or more), for a mesh collection downstream to pick by. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Box", meta=(ClampMin="0"))
	int32 MeshIndex = 0;

	/** Where the mesh's +Y points, in the pattern's frame. With Mesh Up this turns one mesh differently in each box of a pattern. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Box")
	EPCGVoxelPatternDirection MeshForward = EPCGVoxelPatternDirection::PosY;

	/** Where the mesh's +Z points, in the pattern's frame. Must be at a right angle to Mesh Forward. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Box")
	EPCGVoxelPatternDirection MeshUp = EPCGVoxelPatternDirection::PosZ;

	/**
	 * The sides of this chunk its mesh cuts away, in the pattern's frame (an edge wedge: its two sloped sides). Later
	 * patterns' Solid Face probes read these sides as air and every other side as solid. None: the chunk is a whole
	 * cube to later patterns.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Box", meta=(Bitmask, BitmaskEnum="/Script/PCGUtilsVoxel.EPCGVoxelPatternDirection"))
	int32 OpenSides = 0;
};

/** A pattern: what to look for, what chunks to make there, and how far the result may be joined into longer chunks. */
USTRUCT(BlueprintType)
struct PCGUTILSVOXEL_API FPCGVoxelPattern
{
	GENERATED_BODY()

	FPCGVoxelPattern();

	/** For reading graphs, errors and the Pattern Name attribute. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Pattern")
	FName Name;

	/** Given to the points this pattern makes (Apply Patterns: Set Point Color), to see which pattern made what. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Pattern")
	FLinearColor DebugColor = FLinearColor(1.0f, 0.5f, 0.0f);

	/** Claim (default): the boxes take filled cells. Add: the boxes are new cells in empty space. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Pattern")
	EPCGVoxelPatternOperation Operation = EPCGVoxelPatternOperation::Claim;

	/**
	 * Cells around the origin that must be filled, empty or unclaimed for the pattern to fit. The origin cell is always filled.
	 * Cells not listed do not matter. None: the pattern fits wherever its boxes do.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Pattern")
	TArray<FPCGVoxelPatternProbe> Probes;

	/** The chunks made where the pattern fits: all of them or none. At least one; boxes of one pattern may not overlap. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Pattern")
	TArray<FPCGVoxelPatternBox> Boxes;

	/** The rotations the pattern may take to fit; tried in a fixed order (as drawn first), probes and boxes turning together. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Pattern")
	EPCGVoxelRuleTurns Turns = EPCGVoxelRuleTurns::AboutUp;

	/**
	 * Axes of the pattern's frame along which neighbouring fits are joined into one longer chunk (same turn, same box,
	 * lined up end to end): an edge pattern joined along X gives one long edge per wall. None: every fit keeps its own
	 * chunks. The chunking is the shared voxel compression.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Pattern", meta=(Bitmask, BitmaskEnum="/Script/PCGUtilsVoxel.EPCGVoxelAxes"))
	int32 JoinAlong = 0;

	/** The longest joined chunk, in cells, along a Join Along axis. 0: no limit. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Pattern", meta=(ClampMin="0"))
	int32 MaxJoinedSpan = 0;

	/** On: probes also read cells added by earlier patterns as filled. Off (default): probes read the input set only. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Pattern")
	bool bProbesSeeAddedCells = false;
};

/** A pattern to share between graphs. Patterns work without one (inline on a Voxel | Pattern node). */
UCLASS(BlueprintType)
class PCGUTILSVOXEL_API UPCGVoxelPatternAsset : public UDataAsset
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Pattern")
	FPCGVoxelPattern Pattern;
};

namespace PCGVoxelPatterns
{
	using FRotation = PCGVoxelRules::FRotation;

	PCGUTILSVOXEL_API FIntVector DirectionVector(EPCGVoxelPatternDirection Direction);

	/** Applies B, then A. */
	PCGUTILSVOXEL_API FRotation Compose(const FRotation& A, const FRotation& B);

	/** A box's mesh turn inside its pattern. False when Mesh Forward and Mesh Up are not at a right angle. */
	PCGUTILSVOXEL_API bool BoxRotation(const FPCGVoxelPatternBox& Box, FRotation& OutRotation);

	/**
	 * Checks one pattern. Returns false with OutError naming the exact failed invariant (which box or probe, its value
	 * and what is allowed). The error does not name the pattern; the caller does.
	 */
	PCGUTILSVOXEL_API bool Validate(const FPCGVoxelPattern& Pattern, FText& OutError);

	/** One chunk of the result. */
	struct FChunk
	{
		FIntVector First = FIntVector::ZeroValue;
		/** In grid axes, whatever way the mesh is turned. */
		FIntVector Span = FIntVector(1);
		/** Index into the patterns given to Apply. */
		int32 Pattern = INDEX_NONE;
		int32 MeshIndex = 0;
		/** The input point the chunk copies its values from: the point of its first cell (Claim) or of the fit's origin cell (Add). */
		int32 SourcePoint = INDEX_NONE;
		/** The mesh's axes in grid directions. */
		FRotation MeshToGrid;
		bool bAdded = false;
	};

	/**
	 * Optional limits, for selectors (not built yet; this is where one plugs in). Unset: no limit.
	 * Neither changes what probes read: a cell that may not be edited still counts as filled.
	 */
	struct FLimits
	{
		/** May this pattern be tried with its origin on this (filled) cell? */
		TFunction<bool(int32 Pattern, const FIntVector& OriginCell)> CanStart;
		/** May this pattern claim (or add) this cell? Every cell of every box must pass. */
		TFunction<bool(int32 Pattern, const FIntVector& Cell)> CanEdit;
	};

	struct FResult
	{
		/** Every chunk made, in pattern order, then by first cell (Z, Y, X). */
		TArray<FChunk> Chunks;
		/** Per entry of Grid.Cells: the cell is in a chunk. */
		TBitArray<> Claimed;
		/** Cells added by Add patterns. */
		TSet<FIntVector> Added;
		/** How many fits each pattern made (before joining). */
		TArray<int32> NumFits;
	};

	/**
	 * Runs the patterns over Grid, in the order given. Each pattern is tried at every filled cell (Z, then Y, then X)
	 * under each of its turns in fixed order, and takes the cells of its boxes wherever it fits; then its chunks are
	 * joined along its Join Along axes. Cells an Add pattern makes become visible (to patterns that ask) once that
	 * pattern has finished, so the order cells are visited in never changes what an Add pattern sees.
	 * Patterns must have passed Validate.
	 */
	PCGUTILSVOXEL_API void Apply(const FPCGVoxelGrid& Grid, TConstArrayView<const FPCGVoxelPattern*> Patterns, const FLimits& Limits, FResult& OutResult);

	/**
	 * The cells no chunk took, as output boxes: an input point with no claimed cell stays whole (a chunk stays a chunk);
	 * a point that lost some cells gives its other cells one by one.
	 */
	PCGUTILSVOXEL_API void Remainder(const FPCGVoxelGrid& Grid, const FResult& Result, TArray<FPCGVoxelCellOut>& OutCells);

	/**
	 * A grid-axis box (Min and Max about the chunk centre, in grid axes, world units) seen in the frame of a mesh turned
	 * by MeshToGrid: the point-local bounds of a point with that rotation and scale 1.
	 */
	PCGUTILSVOXEL_API void ToMeshFrameBounds(const FRotation& MeshToGrid, const FVector& GridMin, const FVector& GridMax, FVector& OutMin, FVector& OutMax);

	/**
	 * Starting points for common shapes, written for SM_Voxel_Edge (/PCGUtils/Assets/Meshes): a wedge along its Y whose
	 * cut runs across its +X / +Z edge, so its -X and -Z faces are whole. All three use mesh index 0.
	 */
	namespace Presets
	{
		/** An outside edge along the top of a wall: +X and +Z open. Joined along Y. */
		PCGUTILSVOXEL_API FPCGVoxelPattern TopEdge();
		/**
		 * An outside upright corner (+X and +Y open), Size cells on each side, the edge mesh stood on end. Joined along Z.
		 * WallCells: how many unclaimed cells of wall must run on from the corner along both walls (toward -X and -Y,
		 * over the corner's full width). 0: no wall needed, so a wall's end and a lone block also count as corners.
		 */
		PCGUTILSVOXEL_API FPCGVoxelPattern UprightCorner(int32 Size = 1, int32 WallCells = 1);
		/**
		 * Fills an inside upright corner: adds a cell in the empty corner between two walls. Joined along Z.
		 * WallCells (at least 1): how many cells of each wall beside the corner must show a whole side toward the fill
		 * (plain cells, or the solid back of an earlier chunk; never a chunk's cut side). The cell in the bend is free.
		 */
		PCGUTILSVOXEL_API FPCGVoxelPattern InsideCornerFill(int32 WallCells = 1);
	}
}
