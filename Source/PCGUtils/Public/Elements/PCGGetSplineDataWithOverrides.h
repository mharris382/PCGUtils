#pragma once

#include "PCGSettings.h"
#include "Elements/PCGDataFromActor.h"
#include "Elements/PCGTypedGetter.h"
#include "OverrideGraphs.h"

#include "PCGGetSplineDataWithOverrides.generated.h"

UENUM(BlueprintType, meta = (Bitflags, UseEnumValuesAsMaskValuesInEditor = "true"))
enum class EPCGSplineOverrideGraphUsageFlags : uint8
{
    NONE = 0,
    PreProcessSpline = 1 << 0,
    PostSpawn = 1 << 1,
    PreBake = 1 << 2,
    PostBake = 1 << 3
};
ENUM_CLASS_FLAGS(EPCGSplineOverrideGraphUsageFlags);

/**
 * Extends GetSplineData to detect UPCGSplineComponents and stamp any active
 * override graphs into the @Data domain as attributes, enabling priority-based
 * override resolution downstream without per-point attribute overhead.
 * Use OverrideGraphUsageFlags to limit which overrides are stamped — only stamp
 * what this node's downstream graphs actually consume.
 */
UCLASS(BlueprintType, ClassGroup = (Procedural))
class PCGUTILS_API UPCGGetSplineDataWithOverridesSettings : public UPCGGetSplineSettings
{
    GENERATED_BODY()

public:
    UPCGGetSplineDataWithOverridesSettings();

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Settings", meta = (Bitmask, BitmaskEnum = "/Script/PCGUtils.EPCGSplineOverrideGraphUsageFlags"))
    EPCGSplineOverrideGraphUsageFlags OverrideGraphUsageFlags =
        EPCGSplineOverrideGraphUsageFlags::PreProcessSpline |
        EPCGSplineOverrideGraphUsageFlags::PreBake |
        EPCGSplineOverrideGraphUsageFlags::PostSpawn;

#if WITH_EDITOR
    virtual FName GetDefaultNodeName() const override
    {
        return FName(TEXT("PCGUtils|GetSplineData"));
    }
    virtual FText GetDefaultNodeTitle() const override
    {
        return NSLOCTEXT("PCGUtils|GetSplineData", "NodeTitle", "Get Spline Data With Overrides");
    }
    virtual FText GetNodeTooltipText() const override;
#endif

protected:
    virtual FPCGElementPtr CreateElement() const override;
};

class PCGUTILS_API FPCGGetSplineDataWithOverridesElement : public FPCGDataFromActorElement
{
protected:
    virtual void ProcessActor(
        FPCGContext* Context,
        const UPCGDataFromActorSettings* Settings,
        AActor* FoundActor) const override;

private:
    static void StampOverrideToMetadata(
        UPCGMetadata* Metadata,
        const FString& AttributePrefix,
        bool bUseGraph,
        const FSoftObjectPath& GraphPath);
};