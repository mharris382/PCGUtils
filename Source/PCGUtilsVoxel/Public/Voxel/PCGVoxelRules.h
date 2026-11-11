// Copyright Max Harris

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "StructUtils/InstancedStruct.h"

#include "PCGVoxelRules.generated.h"

/*
 * Voxel rule tiles: which surface shape a mesh fits, and how far it may turn to be reused. Three layers, all plain
 * structs so they work wherever a struct can live (node settings, graph parameters, data assets):
 *   rule (one condition) -> compound rule (all must pass, under one shared rotation) -> mapping (ordered entries, each
 *   a compound rule and a mesh index). See Docs/PCGUtilsVoxel.md section 4j.
 *
 * Every rule is written in the mesh's own frame: +Z is the mesh's up; side layouts start at +Y (a wall mesh is open
 * on +Y, X along the wall). Open-face masks use the Cell Properties bits: +X 1, -X 2, +Y 4, -Y 8, +Z 16, -Z 32.
 */

/** One face neighbour of a cell, as a rule wants it. */
UENUM(BlueprintType)
enum class EPCGVoxelFaceState : uint8
{
	Any,
	/** The neighbour on this side is empty: the face is open (on the surface). */
	Empty UMETA(DisplayName="Empty (open)"),
	/** The neighbour on this side is filled: the face is closed. */
	Filled UMETA(DisplayName="Filled (closed)"),
};

/** Which of the four side faces (+Y, +X, -Y, -X in the mesh's frame) are open. */
UENUM(BlueprintType)
enum class EPCGVoxelSideLayout : uint8
{
	/** Sides do not matter. */
	Any,
	/** No side open. */
	None,
	/** +Y open only (a wall: the mesh faces +Y). */
	One,
	/** +Y and +X open (an outside vertical edge). */
	TwoAdjacent,
	/** +Y and -Y open (thin along Y). */
	TwoOpposite,
	/** All but -X open (the end of a strip). */
	Three,
	/** All four open (a post). */
	Four,
	/** Each side set on its own below. */
	Custom,
};

/** Which axes a Thin rule looks at, in the mesh's frame. */
UENUM(BlueprintType)
enum class EPCGVoxelThinAxis : uint8
{
	Any,
	Z UMETA(DisplayName="Up / Down (Z)"),
	X,
	Y,
	/** X or Y. */
	Sides UMETA(DisplayName="Either Side (X or Y)"),
};

/** The rotations a compound rule may use to fit a cell. */
UENUM(BlueprintType)
enum class EPCGVoxelRuleTurns : uint8
{
	/** As drawn. */
	Exact,
	/** The 4 quarter turns about up: floors stay floors. */
	AboutUp UMETA(DisplayName="About Up (4)"),
	/** About up, and upside down too: ceilings and floors share a mesh. */
	AboutUpAndFlip UMETA(DisplayName="About Up + Flip (8)"),
	/** All 24 cube rotations: an edge mesh also fits vertical edges. */
	Any UMETA(DisplayName="Any (24)"),
};

/** Which of the rotations still tied after Prefer Up a cell gets. */
UENUM(BlueprintType)
enum class EPCGVoxelRuleSpin : uint8
{
	/** The first in a fixed order (identity, then the yaws, then the flips, then the rest). */
	Fixed,
	/** One of the tied rotations per cell, from the node seed and the cell's seed. */
	Random,
};

/** Base of every rule: one condition on a cell, seen in the mesh's frame. Use a derived type. */
USTRUCT(BlueprintType, meta=(Hidden))
struct PCGUTILSVOXEL_API FPCGVoxelRule
{
	GENERATED_BODY()

	virtual ~FPCGVoxelRule() = default;

	/** Passes when the condition fails instead. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Rule")
	bool bInvert = false;

	/** The condition on the open-face mask, already in the mesh's frame (Invert not applied). */
	virtual bool Matches(uint8 OpenMask) const { return true; }

	/** The rule's tokens in the text form, without braces or '!'. */
	virtual FString ToText() const { return FString(); }

	bool Test(uint8 OpenMask) const { return Matches(OpenMask) != bInvert; }
};

/** Which faces of the cell are open (empty neighbour) or closed, in the mesh's frame. */
USTRUCT(BlueprintType, meta=(DisplayName="Faces"))
struct PCGUTILSVOXEL_API FPCGVoxelFacesRule : public FPCGVoxelRule
{
	GENERATED_BODY()

	/** The face on the mesh's +Z (its top). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Rule", meta=(DisplayName="Up (+Z)"))
	EPCGVoxelFaceState Up = EPCGVoxelFaceState::Any;

	/** The face on the mesh's -Z (its bottom). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Rule", meta=(DisplayName="Down (-Z)"))
	EPCGVoxelFaceState Down = EPCGVoxelFaceState::Any;

	/** The four side faces as a layout (One = +Y open, Two Adjacent = +Y and +X, Two Opposite = +Y and -Y, Three = all but -X), or Custom. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Rule")
	EPCGVoxelSideLayout Sides = EPCGVoxelSideLayout::Any;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Rule", meta=(DisplayName="+Y", EditCondition="Sides == EPCGVoxelSideLayout::Custom", EditConditionHides))
	EPCGVoxelFaceState PosY = EPCGVoxelFaceState::Any;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Rule", meta=(DisplayName="+X", EditCondition="Sides == EPCGVoxelSideLayout::Custom", EditConditionHides))
	EPCGVoxelFaceState PosX = EPCGVoxelFaceState::Any;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Rule", meta=(DisplayName="-Y", EditCondition="Sides == EPCGVoxelSideLayout::Custom", EditConditionHides))
	EPCGVoxelFaceState NegY = EPCGVoxelFaceState::Any;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Rule", meta=(DisplayName="-X", EditCondition="Sides == EPCGVoxelSideLayout::Custom", EditConditionHides))
	EPCGVoxelFaceState NegX = EPCGVoxelFaceState::Any;

	/** Faces that must be open and faces that must be closed, as masks. */
	void GetMasks(uint8& OutOpen, uint8& OutClosed) const;

	virtual bool Matches(uint8 OpenMask) const override;
	virtual FString ToText() const override;
};

/** Both faces of one axis open: the cell is one cell thick there. */
USTRUCT(BlueprintType, meta=(DisplayName="Thin"))
struct PCGUTILSVOXEL_API FPCGVoxelThinRule : public FPCGVoxelRule
{
	GENERATED_BODY()

	/** The axis (in the mesh's frame) that must be open on both sides; Any or Either Side passes when one of them is. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Rule")
	EPCGVoxelThinAxis Axis = EPCGVoxelThinAxis::Any;

	virtual bool Matches(uint8 OpenMask) const override;
	virtual FString ToText() const override;
};

/** Rules that must all pass under one shared rotation, and how the rotation is chosen. */
USTRUCT(BlueprintType)
struct PCGUTILSVOXEL_API FPCGVoxelCompoundRule
{
	GENERATED_BODY()

	/** All must pass (each with its own Invert). None: matches every cell. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Rule", meta=(BaseStruct="/Script/PCGUtilsVoxel.PCGVoxelRule", ExcludeBaseStruct))
	TArray<FInstancedStruct> Rules;

	/** The rotations that may be used to fit the rules to a cell. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Rule")
	EPCGVoxelRuleTurns Turns = EPCGVoxelRuleTurns::AboutUp;

	/** Of the rotations that fit, keep those whose mesh up is nearest world up (walls: +Y out, Z up, X along the wall). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Rule")
	bool bPreferUp = true;

	/** Which of the rotations still tied a cell gets. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Rule")
	EPCGVoxelRuleSpin Spin = EPCGVoxelRuleSpin::Fixed;

	/** True when every rule passes on this mesh-frame mask. Rules with no type pass (validation reports them). */
	bool Test(uint8 OpenMask) const;

	/** The text form: "<rules> | <settings>". */
	FString ToText() const;
};

/** One entry of a mapping: a compound rule and the mesh index it writes. */
USTRUCT(BlueprintType)
struct PCGUTILSVOXEL_API FPCGVoxelRuleEntry
{
	GENERATED_BODY()

	/** For reading the mapping and the text form (one word). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Rule")
	FName Name;

	/** Off: the entry is skipped. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Rule")
	bool bEnabled = true;

	/** Written to the Mesh Index attribute of the cells this entry matches (0 or more). Several entries may share one. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Rule", meta=(ClampMin="0"))
	int32 MeshIndex = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Rule")
	FPCGVoxelCompoundRule Rule;

	/** The text form: "<index> <name>: <compound>". */
	FString ToText() const;
};

/** Ordered entries: tried top to bottom, the first match wins. */
USTRUCT(BlueprintType)
struct PCGUTILSVOXEL_API FPCGVoxelRuleMapping
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Rules", meta=(TitleProperty="Name"))
	TArray<FPCGVoxelRuleEntry> Entries;

	/** One entry per line. */
	FString ToText() const;
};

/** A rule mapping to share between graphs. Rules work without one (inline on a node, in a graph parameter). */
UCLASS(BlueprintType)
class PCGUTILSVOXEL_API UPCGVoxelRuleMappingAsset : public UDataAsset
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Rules")
	FPCGVoxelRuleMapping Mapping;
};

namespace PCGVoxelRules
{
	/** A cube rotation from the mesh's frame to the grid's: the grid directions of the mesh's X, Y and Z. */
	struct FRotation
	{
		FIntVector X = FIntVector(1, 0, 0);
		FIntVector Y = FIntVector(0, 1, 0);
		FIntVector Z = FIntVector(0, 0, 1);

		FIntVector Apply(const FIntVector& V) const { return X * V.X + Y * V.Y + Z * V.Z; }
		FQuat ToQuat() const;
	};

	/** The 24 rotations in fixed order: identity, the yaws, the flipped yaws, then the rest. */
	PCGUTILSVOXEL_API const TArray<FRotation>& Rotations();

	/** How many of Rotations() a Turns setting allows (1, 4, 8 or 24); they are always the first ones. */
	PCGUTILSVOXEL_API int32 NumTurns(EPCGVoxelRuleTurns Turns);

	/** A grid open-face mask seen in the mesh's frame of Rotation: mesh face f is open when the grid face it lands on is. */
	PCGUTILSVOXEL_API uint8 ToMeshFrame(const FRotation& Rotation, uint8 GridMask);

	/** Face index (mask bit) of an axis direction, or INDEX_NONE. */
	PCGUTILSVOXEL_API int32 FaceOf(const FIntVector& Direction);

	/** The matches of one mapping for all 64 open-face masks. */
	struct FTable
	{
		/** Entry index per grid mask, or INDEX_NONE. */
		int32 Entry[64];
		/** The rotations (indices into Rotations()) still tied after Prefer Up, in fixed order. */
		TArray<uint8> Tied[64];
	};

	PCGUTILSVOXEL_API void BuildTable(const FPCGVoxelRuleMapping& Mapping, FTable& OutTable);

	/** Parses the text form. On failure returns false with OutError naming the line and the token. */
	PCGUTILSVOXEL_API bool ParseMapping(const FString& Text, FPCGVoxelRuleMapping& OutMapping, FString& OutError);
	PCGUTILSVOXEL_API bool ParseCompound(const FString& Text, FPCGVoxelCompoundRule& OutRule, FString& OutError);
}
