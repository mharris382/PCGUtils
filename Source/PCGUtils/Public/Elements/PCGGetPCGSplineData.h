#pragma once

#include "PCGSettings.h"
#include "Data/PCGUtilsComponentData.h"
#include "Elements/PCGDataFromActor.h"
#include "Elements/PCGTypedGetter.h"

#include "PCGGetPCGSplineData.generated.h"

class UPCGSplineComponent;
class UPCGSplineData;

UCLASS(BlueprintType, ClassGroup = (Procedural), Category="PCGUtils|Paths")
class PCGUTILS_API UPCGGetPCGSplineDataSettings : public UPCGGetSplineSettings
{
	GENERATED_BODY()

public:
	UPCGGetPCGSplineDataSettings();

#if WITH_EDITOR
	virtual FName GetDefaultNodeName() const override { return FName(TEXT("PCGUtils|GetPCGSplineData")); }
	virtual FText GetDefaultNodeTitle() const override;
	virtual FText GetNodeTooltipText() const override;
#endif

protected:
	virtual FPCGElementPtr CreateElement() const override;

public:
	/** Bool attribute produced on the @Data domain indicating whether each spline is closed. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Settings", meta = (
		PCG_Overridable,
		ToolTip = "Name of the required output Bool attribute on the @Data domain. Its value is true when the source spline is closed."))
	FName IsClosedAttributeName = TEXT("IsClosed");

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Settings", meta = (ShowOnlyInnerProperties))
	FGetComponentDataSettings ComponentSettings;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Settings", meta = (ShowOnlyInnerProperties))
	FGetPathElementSettingsConfiguration PathSettings;
};

class PCGUTILS_API FPCGGetPCGSplineDataElement : public FPCGDataFromActorElement
{
protected:
	virtual void ProcessActor(
		FPCGContext* Context,
		const UPCGDataFromActorSettings* Settings,
		AActor* FoundActor) const override;

	/** Extension point for specialized getters. The generic node continues to accept every UPCGSplineComponent. */
	virtual bool ShouldProcessSplineComponent(const UPCGSplineComponent* Component) const;

	/** Called after the generic spline, path, and component metadata has been written. */
	virtual void WriteAdditionalSplineMetadata(
		FPCGContext* Context,
		const UPCGGetPCGSplineDataSettings* Settings,
		const UPCGSplineComponent* Component,
		UPCGSplineData* SplineData) const;
};
