// Copyright Max Harris

#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "PCGContext.h"
#include "PCGUtilsComponentModifier.generated.h"

class UPCGBasePointData;
class UPCGSplineData;
class UPCGSpatialData;
class UPCGUtilsComponentModifier;

/** The kinds of component data a modifier can change. */
UENUM(BlueprintType, meta=(Bitflags, UseEnumValuesAsMaskValuesInEditor="true"))
enum class EPCGUtilsModifierTarget : uint8
{
	None   = 0 UMETA(Hidden),
	/** Point data from a marker or any point provider. */
	Marker = 1 << 0,
	/** A path: spline data from a spline, shape path or any path provider. */
	Path   = 1 << 1,
	/** Volume data. No modifier implements this yet. */
	Volume = 1 << 2,
};
ENUM_CLASS_FLAGS(EPCGUtilsModifierTarget);

/** Something a modifier wants the designer to see. Never blocks the result: the modifier still produces output. */
struct PCGUTILS_API FPCGUtilsModifierWarning
{
	TWeakObjectPtr<const UPCGUtilsComponentModifier> Modifier;
	FText Message;
	/** Where to draw the warning in the viewport, when it is about one place. */
	TOptional<FVector> WorldLocation;
};

/**
 * What a modifier is given besides its input data.
 *
 * Modifier space is the component's own frame (as Blender modifiers work in the object's local space):
 * Array offsets, mirror planes and circle centres are all expressed in it.
 */
struct PCGUTILS_API FPCGUtilsModifierContext
{
	/** Null outside a PCG execution (editor previews); data is then created with plain NewObject. */
	FPCGContext* PCGContext = nullptr;
	/** The component that owns the stack. */
	const UObject* Owner = nullptr;
	/** Modifier space to world. */
	FTransform ModifierToWorld = FTransform::Identity;
	/** Optional sink for warnings. */
	TArray<FPCGUtilsModifierWarning>* Warnings = nullptr;

	template <typename T>
	T* NewData() const { return FPCGContext::NewObject_AnyThread<T>(PCGContext); }

	void Warn(const UPCGUtilsComponentModifier* Modifier, const FText& Message, TOptional<FVector> WorldLocation = {}) const;
};

/**
 * One entry in a component's modifier stack. A modifier changes what PCG reads from the component; it never
 * changes the component itself.
 *
 * Subclasses declare which kinds of data they handle (GetSupportedTargets) and override the matching Modify
 * function. Each takes one dataset and appends zero or more results, because a modifier may turn one path into
 * several (Array, unfused Mirror). The output must be initialised from the input so its metadata and @Data
 * survive; copies take their source's metadata entry as parent.
 *
 * An editor widget for a modifier is registered against its class in PCGUtilsEditor; the modifier itself knows
 * nothing about it.
 */
UCLASS(Abstract, EditInlineNew, DefaultToInstanced, CollapseCategories, BlueprintType)
class PCGUTILS_API UPCGUtilsComponentModifier : public UObject
{
	GENERATED_BODY()

public:
	/** Off: the stack skips this modifier, as if it were not there. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Modifier")
	bool bEnabled = true;

#if WITH_EDITORONLY_DATA
	/** Draw this modifier's part of the preview while the component is selected. */
	UPROPERTY(EditAnywhere, Category="Modifier")
	bool bShowVisualization = true;
#endif

	virtual EPCGUtilsModifierTarget GetSupportedTargets() const { return EPCGUtilsModifierTarget::None; }
	bool SupportsTarget(EPCGUtilsModifierTarget Target) const { return EnumHasAnyFlags(GetSupportedTargets(), Target); }

	/** Short name for the stack header, such as "Array". */
	virtual FText GetModifierDisplayName() const;

	/** Point data in modifier terms: append results to Out. The default passes the input through. */
	virtual void ModifyPoints(const FPCGUtilsModifierContext& Context, const UPCGBasePointData* In, TArray<UPCGBasePointData*>& Out) const;

	/** A path as spline data: append results to Out. The default passes the input through. */
	virtual void ModifyPath(const FPCGUtilsModifierContext& Context, const UPCGSplineData* In, TArray<UPCGSplineData*>& Out) const;

	/** Volume data: append results to Out. The default passes the input through. */
	virtual void ModifyVolume(const FPCGUtilsModifierContext& Context, const UPCGSpatialData* In, TArray<UPCGSpatialData*>& Out) const;
};
