// Copyright Max Harris

#pragma once

#include "CoreMinimal.h"
#include "PCGSettings.h"
#include "PCGElement.h"
#include "Metadata/PCGAttributePropertySelector.h"

#include "PCGPathNeighborAttributes.generated.h"

/** Whether each input path loops back on itself, which decides what the first and last points read. */
UENUM(BlueprintType)
enum class EPCGUtilsPathClosedMode : uint8
{
	/** Read the Data-domain bool named by Closed Attribute Name. A path without that attribute is open (the PCGEx convention). */
	FromAttribute UMETA(DisplayName = "From Attribute"),
	/** Treat every input as open: reads past either end follow End Mode. */
	Open,
	/** Treat every input as closed: reads past either end wrap around. */
	Closed
};

/** What an open path's end points read when the neighbor they ask for does not exist. */
UENUM(BlueprintType)
enum class EPCGUtilsPathNeighborEndMode : uint8
{
	/** Read the nearest point that exists. With Offset +1 or -1 that is the point itself. */
	Clamp,
	/** Read the point's own value, whatever the offset. A "current != next" comparison then never fires at an end. */
	OwnValue UMETA(DisplayName = "Own Value"),
	/** Leave the output at its default: the source attribute's default value, or zero for a point property. */
	AttributeDefault UMETA(DisplayName = "Attribute Default")
};

/** One neighbor read: the value of Source at (point index + Offset), written onto each point. */
USTRUCT(BlueprintType)
struct PCGUTILS_API FPCGUtilsPathNeighborAttributeEntry
{
	GENERATED_BODY()

	FPCGUtilsPathNeighborAttributeEntry();

	/**
	 * Required. The attribute or point property to read from the neighbor. Any type. Must have one value per point
	 * (an @Data attribute is rejected unless the path has a single point). Examples: 'BarrierGeneration', '$Position'.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Settings)
	FPCGAttributePropertyInputSelector Source;

	/** Which neighbor to read, in points along the path. +1 is the next point, -1 the previous one. 0 copies the attribute unchanged. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Settings)
	int32 Offset = 1;

	/**
	 * Required. Output point-domain attribute receiving the neighbor's value, with the same type as Source.
	 * Created if missing, replaced if it exists with another type. May equal Source to shift that attribute in place.
	 * A '$Property' name writes a point property instead.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Settings)
	FName OutputAttributeName = TEXT("NextValue");

	/** Optional. Also write a bool saying whether the neighbor existed (false only on open-path ends). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Settings)
	bool bOutputHasNeighbor = false;

	/**
	 * Optional output point-domain bool, written when Output Has Neighbor is on. True when the point at index + Offset
	 * exists, or the path is closed; false where End Mode had to supply the value.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Settings, meta = (EditCondition = "bOutputHasNeighbor", EditConditionHides))
	FName HasNeighborAttributeName = TEXT("HasNextValue");
};

namespace PCGUtilsPathNeighbor
{
	/** Source index read by point Index, or INDEX_NONE when End Mode Attribute Default leaves it unwritten. bOutHasNeighbor is false when End Mode supplied the value. */
	PCGUTILS_API int32 ResolveIndex(int32 Index, int32 NumPoints, int32 Offset, bool bClosed, EPCGUtilsPathNeighborEndMode EndMode, bool& bOutHasNeighbor);
}

/**
 * Copies attribute values from neighboring points along each path onto every point: the next point's
 * value, the previous point's, or any offset. Closed paths wrap; open-path ends follow End Mode.
 *
 * Typical use: write NextBarrierGeneration from BarrierGeneration (Offset +1), then split the path wherever the
 * two differ, so every piece shares one value.
 */
UCLASS(BlueprintType, ClassGroup = (Procedural), Category = "PCGUtils", meta = (Keywords = "path next previous prev neighbor neighbour adjacent shift offset copy attribute split change compare closed loop PCGEx"))
class PCGUTILS_API UPCGUtilsPathNeighborAttributesSettings : public UPCGSettings
{
	GENERATED_BODY()

public:
	//~Begin UPCGSettings interface
#if WITH_EDITOR
	virtual FName GetDefaultNodeName() const override { return FName(TEXT("PathNeighborAttributes")); }
	virtual FText GetDefaultNodeTitle() const override;
	virtual FText GetNodeTooltipText() const override;
	virtual FString GetAdditionalTitleInformation() const override;
	virtual EPCGSettingsType GetType() const override;
#endif
	virtual TArray<FPCGPinProperties> InputPinProperties() const override;
	virtual TArray<FPCGPinProperties> OutputPinProperties() const override;

protected:
	virtual FPCGElementPtr CreateElement() const override;
	//~End UPCGSettings interface

public:
	/** The reads to perform, in order. A later entry sees the outputs of earlier ones. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Settings, meta = (TitleProperty = "{OutputAttributeName} ({Offset})"))
	TArray<FPCGUtilsPathNeighborAttributeEntry> Attributes = { FPCGUtilsPathNeighborAttributeEntry() };

	/** How to tell whether a path is closed. Closed paths wrap around; open ones follow End Mode at their ends. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Path, meta = (PCG_Overridable))
	EPCGUtilsPathClosedMode ClosedMode = EPCGUtilsPathClosedMode::FromAttribute;

	/**
	 * Optional input. Data-domain bool marking a closed path (@Data.IsClosed in PCGEx). Read when Closed Mode is
	 * From Attribute; a missing attribute means open, an attribute of another type is an error.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Path, meta = (PCG_Overridable, EditCondition = "ClosedMode == EPCGUtilsPathClosedMode::FromAttribute", EditConditionHides))
	FName ClosedAttributeName = TEXT("IsClosed");

	/** What an open path's end points read when the neighbor does not exist. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Path, meta = (PCG_Overridable))
	EPCGUtilsPathNeighborEndMode EndMode = EPCGUtilsPathNeighborEndMode::OwnValue;
};

class PCGUTILS_API FPCGUtilsPathNeighborAttributesElement : public IPCGElement
{
protected:
	virtual bool ExecuteInternal(FPCGContext* Context) const override;
};
