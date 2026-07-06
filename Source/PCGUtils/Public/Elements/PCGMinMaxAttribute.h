// // Fill out your copyright notice in the Description page of Project Settings.
//
// #pragma once
//
// #include "CoreMinimal.h"
// #include "PCGSettings.h"
// #include "PCGMinMaxAttribute.generated.h"
//
// UENUM(BlueprintType)
// enum class EPCGMinMaxMode : uint8
// {
// 	RandomBetweenRanges UMETA(DisplayName = "Random between Ranges", ToolTip = "Randomly evaluates two ranges and using the time input attribute to lerp between"),
// 	RandomBetweenCurves UMETA(DisplayName = "Random between Curves", ToolTip = "Uses time to sample on two curves and then randomly interpolates between them"),
// };
// /**
//  * 
//  */
// UCLASS(BlueprintType, ClassGroup = (Procedural))
// class PCGUTILS_API UPCGMinMaxAttribute : public UPCGSettings
// {
// 	GENERATED_BODY()
// 	
// public:
// #if WITH_EDITOR
// 	virtual FName GetDefaultNodeName() const override { return FName(TEXT("MinMaxAttribute")); }
// 	virtual FText GetDefaultNodeTitle() const override { return NSLOCTEXT("PCGMinMaxAttributeSettings", "NodeTitle", "Min Max Attribute"); }
// 	virtual EPCGSettingsType GetType() const override { return EPCGSettingsType::PointOps; }
// 	
// //	virtual void CreateKernels(FPCGGPUCompilationContext& InOutContext, UObject* InObjectOuter, TArray<UPCGComputeKernel*>& OutKernels, TArray<FPCGKernelEdge>& OutEdges) const override;
// #endif
// 	
// 	
// 	
// 	protected:
// 	virtual TArray<FPCGPinProperties> InputPinProperties() const override { return Super::DefaultPointInputPinProperties(); }
// 	virtual TArray<FPCGPinProperties> OutputPinProperties() const override { return Super::DefaultPointOutputPinProperties(); }
// 	virtual FPCGElementPtr CreateElement() const override;
// 	
// public:
// 	
// 	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable))
// 	EPCGMinMaxMode Mode = EPCGMinMaxMode::RandomBetweenRanges;
// 	
// 	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable))
// 	FPCGAttributePropertyInputSelector TargetAttribute;
// 	
// 	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable))
// 	FPCGAttributePropertyInputSelector TimeAttribute;
// 	
// 	/** Target property/attribute related properties */
// 	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable))
// 	FPCGAttributePropertyInputSelector OutputAttribute;
// };
// class FPCGMinMaxAttribute : public IPCGElement
// {
// protected:
// 	virtual bool ExecuteInternal(FPCGContext* Context) const override;
// 	virtual EPCGElementExecutionLoopMode ExecutionLoopMode(const UPCGSettings* Settings) const override { return EPCGElementExecutionLoopMode::SinglePrimaryPin; }
// 	virtual bool SupportsBasePointDataInputs(FPCGContext* InContext) const override { return true; }
// };
