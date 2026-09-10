// Copyright Max Harris
#pragma once

#include "Elements/PCGUtilsFractureElementBase.h"
#include "PCGGeometryCollectionDataflow.generated.h"

class UDataflow;
struct FDataflowNode;

UENUM()
enum class EPCGUtilsDataflowInputType : uint8 { GeometryCollection UMETA(DisplayName="GC"), Points };

USTRUCT(BlueprintType)
struct PCGUTILSFRACTURE_API FPCGUtilsDataflowInput
{
	GENERATED_BODY()
	UPROPERTY(EditAnywhere, Category="Input")
	FName Name = TEXT("GC");
	UPROPERTY(EditAnywhere, Category="Input")
	EPCGUtilsDataflowInputType Type = EPCGUtilsDataflowInputType::GeometryCollection;
};

UENUM()
enum class EPCGUtilsDataflowParameterType : uint8 { Unsupported, Bool, Int32, Int64, Float, Double, Name, String };

/** Asset variables are discovered by Refresh Interface. Missing metadata attributes retain the inline/default value. */
USTRUCT(BlueprintType)
struct PCGUTILSFRACTURE_API FPCGUtilsDataflowParameter
{
	GENERATED_BODY()
	UPROPERTY(VisibleAnywhere, Category="Parameter")
	FName Name;
	UPROPERTY(VisibleAnywhere, Category="Parameter")
	EPCGUtilsDataflowParameterType Type = EPCGUtilsDataflowParameterType::Unsupported;
	/** Unsupported types remain visible, but use the Dataflow asset default. */
	UPROPERTY(VisibleAnywhere, Category="Parameter")
	FString Status;
	UPROPERTY(EditAnywhere, Category="Parameter", meta=(EditCondition="Type != EPCGUtilsDataflowParameterType::Unsupported"))
	FName AttributeName;
	UPROPERTY(EditAnywhere, Category="Parameter", meta=(EditCondition="Type != EPCGUtilsDataflowParameterType::Unsupported"))
	bool bOverride = false;
	UPROPERTY(EditAnywhere, Category="Parameter", meta=(EditCondition="Type == EPCGUtilsDataflowParameterType::Bool", EditConditionHides))
	bool BoolValue = false;
	UPROPERTY(EditAnywhere, Category="Parameter", meta=(EditCondition="Type == EPCGUtilsDataflowParameterType::Int32 || Type == EPCGUtilsDataflowParameterType::Int64", EditConditionHides))
	int64 IntValue = 0;
	UPROPERTY(EditAnywhere, Category="Parameter", meta=(EditCondition="Type == EPCGUtilsDataflowParameterType::Float || Type == EPCGUtilsDataflowParameterType::Double", EditConditionHides))
	double RealValue = 0;
	UPROPERTY(EditAnywhere, Category="Parameter", meta=(EditCondition="Type == EPCGUtilsDataflowParameterType::Name || Type == EPCGUtilsDataflowParameterType::String", EditConditionHides))
	FString StringValue;
};

/** Experimental, whole-collection boundary. No selection contract survives arbitrary Dataflow processing. */
UCLASS(BlueprintType, ClassGroup=(Procedural), meta=(Keywords="Dataflow Geometry Collection editor processor"))
class PCGUTILSFRACTURE_API UPCGGeometryCollectionDataflowSettings : public UPCGUtilsFractureElementBaseSettings
{
	GENERATED_BODY()
public:
#if WITH_EDITORONLY_DATA
	/** Editor-only reference: the settings remain loadable in packaged graphs. */
	UPROPERTY(EditAnywhere, Category="Dataflow", meta=(AllowedClasses="/Script/DataflowEngine.Dataflow"))
	TSoftObjectPtr<UObject> DataflowAsset;
#endif
	UPROPERTY(EditAnywhere, Category="Interface", meta=(TitleProperty="Name"))
	TArray<FPCGUtilsDataflowInput> Inputs = { FPCGUtilsDataflowInput() };
	/** Each name binds to a PCG Output GC bridge node in the Dataflow asset. */
	UPROPERTY(EditAnywhere, Category="Interface")
	TArray<FName> Outputs = { FName(TEXT("GC")) };
	UPROPERTY(EditAnywhere, Category="Parameters", meta=(TitleProperty="Name"))
	TArray<FPCGUtilsDataflowParameter> Parameters;
	/** Converts PCG world-space points into the collection's local space. */
	UPROPERTY(EditAnywhere, Category="Interface", meta=(PCG_Overridable))
	bool bPointsInCollectionSpace = true;
	UFUNCTION(CallInEditor, Category="Dataflow")
	void RefreshInterface();
	virtual void PostInitProperties() override;
	virtual void BeginDestroy() override;
#if WITH_EDITOR
	virtual FText GetDefaultNodeTitle() const override;
	virtual FText GetNodeTooltipText() const override;
	virtual void PostEditChangeProperty(FPropertyChangedEvent& Event) override;
	virtual EPCGChangeType GetChangeTypeForProperty(const FName& PropertyName) const override;
	virtual void GetStaticTrackedKeys(FPCGSelectionKeyToSettingsMap& OutKeysToSettings, TArray<TObjectPtr<const UPCGGraph>>& OutVisitedGraphs) const override;
	void OnDataflowVariablesChanged(const UDataflow* Asset, FName Variable);
	void OnDataflowNodeInvalidated(FDataflowNode& Node);
#endif
	virtual TArray<FPCGPinProperties> InputPinProperties() const override;
	virtual TArray<FPCGPinProperties> OutputPinProperties() const override;
protected:
	virtual FPCGElementPtr CreateElement() const override;
};

class PCGUTILSFRACTURE_API FPCGGeometryCollectionDataflowElement : public IPCGElement
{
public:
	virtual bool IsCacheable(const UPCGSettings*) const override { return false; }
	virtual bool CanExecuteOnlyOnMainThread(FPCGContext*) const override { return true; }
protected:
	virtual bool ExecuteInternal(FPCGContext* Context) const override;
};

namespace PCGUtilsDataflow
{
	/** Returns zero for invalid N:N/N:1 cardinalities; an input-free graph executes once. */
	PCGUTILSFRACTURE_API int32 GetBatchCount(TConstArrayView<int32> Counts);
}
