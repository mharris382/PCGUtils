// Copyright Max Harris

#pragma once

#include "CoreMinimal.h"
#include "Components/SplineComponent.h"
#include "Modifiers/PCGUtilsComponentModifier.h"
#include "UObject/Interface.h"
#include "PCGUtilsModifierStack.generated.h"

class UPCGBasePointData;
class UPCGSplineData;

/**
 * A component's modifiers, applied top to bottom. Each modifier receives every result of the one above it.
 * The Get nodes run the stack on the data they read from the component, so PCG sees the modified result.
 */
USTRUCT(BlueprintType)
struct PCGUTILS_API FPCGUtilsModifierStack
{
	GENERATED_BODY()

	/** Applied in order, top first. A modifier that does not handle this component's kind of data is skipped, with a warning. */
	UPROPERTY(EditAnywhere, Instanced, BlueprintReadWrite, Category="Modifiers")
	TArray<TObjectPtr<UPCGUtilsComponentModifier>> Modifiers;

	/** True when at least one enabled modifier handles this kind of data. */
	bool HasActiveModifiers(EPCGUtilsModifierTarget Target) const;

	/** Runs the stack on point data. With no active modifiers the result is just In. */
	TArray<UPCGBasePointData*> ApplyToPoints(const FPCGUtilsModifierContext& Context, UPCGBasePointData* In) const;

	/** Runs the stack on a path. With no active modifiers the result is just In. */
	TArray<UPCGSplineData*> ApplyToPath(const FPCGUtilsModifierContext& Context, UPCGSplineData* In) const;
};

UINTERFACE(MinimalAPI, meta=(CannotImplementInterfaceInBlueprint))
class UPCGUtilsModifierStackOwner : public UInterface
{
	GENERATED_BODY()
};

/** A component (or actor) whose PCG data is passed through a modifier stack. */
class PCGUTILS_API IPCGUtilsModifierStackOwner
{
	GENERATED_BODY()

public:
	virtual const FPCGUtilsModifierStack* GetModifierStack() const = 0;
	virtual FPCGUtilsModifierStack* GetMutableModifierStack() = 0;

	/** Modifier space to world. Defaults to the component's transform, or the actor's. */
	virtual FTransform GetModifierSpaceToWorld() const;

	/** The stack of Object when it is a stack owner, else null. */
	static const FPCGUtilsModifierStack* FindStack(const UObject* Object);

	/** A context for Object's stack, or an empty one (identity space) when it is not a stack owner. */
	static FPCGUtilsModifierContext MakeContext(const UObject* Object, FPCGContext* PCGContext, TArray<FPCGUtilsModifierWarning>* Warnings);
};

/** A path in modifier space, the form path modifiers do their maths in. */
struct PCGUTILS_API FPCGUtilsModifierPath
{
	/** Control points in modifier space. InputKey is ignored on write (points are renumbered). */
	TArray<FSplinePoint> Points;
	/** For each point, the source control point it came from (for metadata), or INDEX_NONE. */
	TArray<int32> SourceIndices;
	bool bClosed = false;

	int32 Num() const { return Points.Num(); }
	void Add(const FSplinePoint& Point, int32 SourceIndex) { Points.Add(Point); SourceIndices.Add(SourceIndex); }
};

/** Shared helpers for writing modifiers. */
struct PCGUTILS_API FPCGUtilsModifierHelpers
{
	/** In's control points in modifier space. */
	static FPCGUtilsModifierPath ReadPath(const FPCGUtilsModifierContext& Context, const UPCGSplineData* In);

	/**
	 * New spline data holding Path, initialised from Source so @Data and tags-relevant metadata survive.
	 * Control points that came from a source point get a metadata entry parented to it.
	 */
	static UPCGSplineData* WritePath(const FPCGUtilsModifierContext& Context, const UPCGSplineData* Source, const FPCGUtilsModifierPath& Path);

	/**
	 * New point data initialised from Source, with one point per entry of SourceIndices, each a copy of that source
	 * point (all native properties). Every point gets a fresh metadata entry parented to its source's, so a
	 * modifier can then write per-copy attributes. Transforms are left as copied; set them afterwards.
	 */
	static UPCGBasePointData* NewPointsFrom(const FPCGUtilsModifierContext& Context, const UPCGBasePointData* Source, const TArray<int32>& SourceIndices);

	/** Applies a modifier-space matrix to a path's control point (location, tangents, rotation, scale). */
	static FSplinePoint TransformSplinePoint(const FSplinePoint& Point, const FMatrix& Matrix);

	/** Writes an int32 attribute on the elements domain, one value per point, when Name is not None. */
	static void WritePointInt(UPCGBasePointData* Data, FName Name, const TArray<int32>& Values);

	/** Writes an int32 attribute on @Data when Name is not None. */
	static void WriteDataInt(UPCGData* Data, FName Name, int32 Value);

	//~ For Get nodes

	/**
	 * Spline data from path points. bIsLocalSpace: the points are relative to LocalToWorld. Linear paths get linear
	 * control points; curved ones keep their tangents (or auto tangents where none are given).
	 */
	static UPCGSplineData* MakeSpline(FPCGContext* PCGContext, const TArray<struct FPCGPathPoint>& Points, bool bIsLocalSpace,
		const FTransform& LocalToWorld, bool bIsLinearPath, bool bIsClosed);

	/** A spline's control points as world-space path points (transform, tangents). */
	static TArray<struct FPCGPathPoint> SplineToPathPoints(const UPCGSplineData* Spline);

	/** True when every control point is linear. */
	static bool IsLinearSpline(const UPCGSplineData* Spline);

	/** Copies every @Data attribute of From onto To (replacing same-named ones). For carrying modifier attributes across a data-type conversion. */
	static void CopyDataDomain(const UPCGData* From, UPCGData* To);

	/** Logs a stack's warnings as node warnings, prefixed with the owner's name. */
	static void LogWarningsOnGraph(FPCGContext* PCGContext, const UObject* Owner, const TArray<FPCGUtilsModifierWarning>& Warnings);

	/**
	 * Runs Owner's stack on point data and logs its warnings. Returns {In} when Owner has no stack or an empty one.
	 */
	static TArray<UPCGBasePointData*> ApplyOwnerStackToPoints(FPCGContext* PCGContext, const UObject* Owner, UPCGBasePointData* In);

	/** Runs Owner's stack on a path and logs its warnings. Returns {In} when Owner has no stack or an empty one. */
	static TArray<UPCGSplineData*> ApplyOwnerStackToPath(FPCGContext* PCGContext, const UObject* Owner, UPCGSplineData* In);

	/** True when Owner has a stack with at least one modifier in it. */
	static bool HasStack(const UObject* Owner);
};
