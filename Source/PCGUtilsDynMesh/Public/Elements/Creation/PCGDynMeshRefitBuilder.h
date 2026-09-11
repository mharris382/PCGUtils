// Copyright Max Harris
#pragma once
#include "CoreMinimal.h"
#include "Factories/PCGUtilsDynMeshBuilderFactory.h"
#include "Factories/PCGUtilsDynMeshFactoryProvider.h"
#include "Elements/Creation/PrimitiveBuilder/PCGUtilsPrimitiveFittingDetails.h"
#include "PCGDynMeshRefitBuilder.generated.h"

UCLASS()
class PCGUTILSDYNMESH_API UPCGDynMeshRefitBuilderData : public UPCGUtilsDynMeshBuilderFactoryData
{
	GENERATED_BODY()
  public:
	UPROPERTY() TObjectPtr<const UPCGUtilsDynMeshBuilderFactoryData> Source;
	UPROPERTY() TObjectPtr<const UPCGUtilsDynMeshBuilderFactoryData> Target;
	UPROPERTY() FPCGUtilsFittingDetails Fitting;
	UPROPERTY() bool bRetarget = false;
	UPROPERTY() bool bUseTargetFrame = true;

  protected:
	virtual TSharedPtr<FPCGUtilsDynMeshBuilderOperation> CreateOperationInternal() const override;
	virtual void AddToCrc(FArchiveCrc32 &Ar, bool bFullDataCrc) const override;
};

/** Whole-result placement: selections are carried along, never used to fit only part of a recipe. */
UCLASS(BlueprintType, meta = (Keywords = "DynMesh Builder fitting alignment compound placement"))
class PCGUTILSDYNMESH_API UPCGDynMeshRefitBuilderSettings : public UPCGUtilsDynMeshFactoryProviderSettings
{
	GENERATED_BODY()
  public:
	UPCGDynMeshRefitBuilderSettings();
#if WITH_EDITOR
	virtual FName GetDefaultNodeName() const override
	{
		return TEXT("RefitBuilder");
	}
	virtual FText GetDefaultNodeTitle() const override
	{
		return NSLOCTEXT("BuilderRefit", "Title", "Builder | Refit");
	}
	virtual FText GetNodeTooltipText() const override
	{
		return NSLOCTEXT("BuilderRefit", "Tip",
		                 "Fits the completed Builder as one object. Target is an optional reference Builder, never "
		                 "included in the output. Whole-object operation; active selections travel with the geometry.");
	}
#endif
	/** Fits completed geometry. Retarget uses only orientation and padding; the source recipe retains its own fit and
	 * alignment. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Fitting",
	          meta = (PCG_Overridable, EditCondition = "!bRetarget", EditConditionHides))
	FPCGUtilsFittingDetails Fitting;
	/** Reorders the reference frame without changing its physical volume before reevaluating the source recipe. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Target",
	          meta = (PCG_Overridable, EditCondition = "bRetarget", EditConditionHides))
	FPCGUtilsFittingOrientation TargetOrientation;
	/** Insets the target minimum in its original axes before orientation. Positive is inward, negative outward. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Target",
	          meta = (PCG_Overridable, EditCondition = "bRetarget", EditConditionHides))
	FVector TargetPaddingMin = FVector::ZeroVector;
	/** Insets the target maximum in its original axes before orientation. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Target",
	          meta = (PCG_Overridable, EditCondition = "bRetarget", EditConditionHides))
	FVector TargetPaddingMax = FVector::ZeroVector;
	/** Measure Target in its own rigid Builder frame. Disable to keep incoming target axes (for example parent up). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Target", meta = (PCG_Overridable))
	bool bUseTargetFrame = true;
	UPROPERTY() bool bRetarget = false;
	virtual FName GetMainOutputPin() const override
	{
		return TEXT("Builder");
	}
	virtual FName GetOutputSourcePin() const override
	{
		return TEXT("Builder");
	}
	virtual UPCGUtilsDynMeshFactoryData *CreateFactory(FPCGContext *Context,
	                                                   UPCGUtilsDynMeshFactoryData *Existing = nullptr) const override;

  protected:
	virtual TArray<FPCGPinProperties> InputPinProperties() const override;
	virtual const FPCGDataTypeBaseId &GetFactoryTypeId() const override
	{
		return FPCGUtilsDynMeshBuilderFactoryDataTypeInfo::AsId();
	}
};

UCLASS(BlueprintType, meta = (Keywords = "DynMesh Builder fitting reference bounds recipe orientation"))
class PCGUTILSDYNMESH_API UPCGDynMeshRetargetBuilderSettings : public UPCGDynMeshRefitBuilderSettings
{
	GENERATED_BODY()
  public:
	UPCGDynMeshRetargetBuilderSettings()
	{
		bRetarget = true;
	}
#if WITH_EDITOR
	virtual FName GetDefaultNodeName() const override
	{
		return TEXT("RetargetBuilder");
	}
	virtual FText GetDefaultNodeTitle() const override
	{
		return NSLOCTEXT("BuilderRefit", "RetargetTitle", "Builder | Retarget");
	}
	virtual FText GetNodeTooltipText() const override
	{
		return NSLOCTEXT("BuilderRefit", "RetargetTip",
		                 "Reevaluates the source recipe against the optional Target Builder's geometry bounds. Source "
		                 "leaf fitting is retained. Target is evaluated in the incoming context and is not included in "
		                 "the output. Original seed attributes remain available.");
	}
#endif
};
