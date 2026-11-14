// Copyright Max Harris

#pragma once

#include "CoreMinimal.h"
#include "Modifiers/PCGUtilsComponentModifier.h"
#include "PCGUtilsMirrorModifier.generated.h"

struct FPCGUtilsModifierPath;

/**
 * Blender's Mirror modifier for PCG components: adds a reflected copy across a plane, per enabled axis, applied
 * X then Y then Z. The plane passes through Mirror Plane's origin with its normal along Mirror Plane's own axis;
 * all of it is in the component's frame.
 *
 * Paths can be fused. Draw half a shape as an OPEN path with its ends on the plane: each end within Merge
 * Distance of the plane is joined to its mirror twin, and when both ends join the result is a closed path even
 * though the component's own path is open. A closed path with Fuse on gives the union of the two shapes.
 */
UCLASS(BlueprintType, meta=(DisplayName="Mirror"))
class PCGUTILS_API UPCGUtilsMirrorModifier : public UPCGUtilsComponentModifier
{
	GENERATED_BODY()

public:
	/** Mirror across the plane whose normal is the mirror plane's X axis. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Mirror", meta=(DisplayName="X"))
	bool bMirrorX = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Mirror", meta=(DisplayName="Y"))
	bool bMirrorY = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Mirror", meta=(DisplayName="Z"))
	bool bMirrorZ = false;

	/** Where the mirror planes are, in component space (Blender's Mirror Object). Identity mirrors across the component's own planes. The scene gizmo edits it. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Mirror")
	FTransform MirrorPlane = FTransform::Identity;

	/**
	 * Paths only. Open path: join each end within Merge Distance of the plane to its twin; both ends joined gives
	 * a closed path. Closed path: the union of the original and its mirror. Off: two separate paths.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Mirror")
	bool bFuse = true;

	/** Markers: drop a mirrored point that lands within Merge Distance of its original (a point on the plane). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Mirror")
	bool bMerge = true;

	/** How close to the plane a path end (or a marker) must be to be joined (or merged). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Mirror", meta=(ClampMin="0", Units="cm"))
	double MergeDistance = 10.0;

	/** Spacing used to trace curved paths before a closed-path union, in cm. The union result is a linear path. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, AdvancedDisplay, Category="Mirror", meta=(ClampMin="0.01", Units="cm"))
	double UnionTolerance = 0.5;

	/** Write which side of each plane a result is on. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Output", meta=(InlineEditConditionToggle))
	bool bWriteMirrorSide = false;

	/**
	 * Optional. int32 attribute: bit 0 set when reflected across X, bit 1 across Y, bit 2 across Z; 0 = original.
	 * Markers: per point. Paths: on @Data of each unfused path (a fused path mixes sides and gets none).
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Output", meta=(EditCondition="bWriteMirrorSide"))
	FName MirrorSideAttributeName = TEXT("MirrorSide");

	//~ UPCGUtilsComponentModifier
	virtual EPCGUtilsModifierTarget GetSupportedTargets() const override { return EPCGUtilsModifierTarget::Marker | EPCGUtilsModifierTarget::Path; }
	virtual void ModifyPoints(const FPCGUtilsModifierContext& Context, const UPCGBasePointData* In, TArray<UPCGBasePointData*>& Out) const override;
	virtual void ModifyPath(const FPCGUtilsModifierContext& Context, const UPCGSplineData* In, TArray<UPCGSplineData*>& Out) const override;

	/** The axes in use, X Y Z order (0, 1, 2). */
	TArray<int32> GetActiveAxes() const;

	/** Component-space reflection across one plane (row-vector convention). */
	FMatrix GetReflection(int32 Axis) const;

	/** Signed distance of a component-space point from one plane. */
	double PlaneDistance(int32 Axis, const FVector& Point) const;

	/** The plane's normal in component space. */
	FVector PlaneNormal(int32 Axis) const;

	/** A path reflected by one plane, reversed so it winds the same way as the original. */
	static FPCGUtilsModifierPath ReflectReversed(const FPCGUtilsModifierPath& Path, const FMatrix& Reflection);

private:
	void MirrorPathOnce(const FPCGUtilsModifierContext& Context, int32 Axis, const FPCGUtilsModifierPath& Path, int32 Side, TArray<TPair<FPCGUtilsModifierPath, int32>>& Out) const;
	bool FuseOpen(const FPCGUtilsModifierContext& Context, int32 Axis, const FPCGUtilsModifierPath& Path, TArray<TPair<FPCGUtilsModifierPath, int32>>& Out) const;
	bool UnionClosed(const FPCGUtilsModifierContext& Context, int32 Axis, const FPCGUtilsModifierPath& Path, TArray<TPair<FPCGUtilsModifierPath, int32>>& Out) const;
};
