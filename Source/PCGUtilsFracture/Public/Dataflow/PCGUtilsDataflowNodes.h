// Copyright Max Harris
#pragma once

#include "Dataflow/DataflowNode.h"
#include "GeometryCollection/ManagedArrayCollection.h"
#include "PCGUtilsDataflowNodes.generated.h"

class UMaterialInterface;

USTRUCT(meta=(DataflowExperimental))
struct PCGUTILSFRACTURE_API FPCGUtilsDataflowCollectionInputNode : public FDataflowNode
{
	GENERATED_USTRUCT_BODY()
	DATAFLOW_NODE_DEFINE_INTERNAL(FPCGUtilsDataflowCollectionInputNode, "PCG Input GC", "PCGUtils", "Collection")
public:
	UPROPERTY(EditAnywhere, Category="PCG")
	FName PinName = TEXT("GC");
	UPROPERTY(meta=(DataflowOutput))
	FManagedArrayCollection Collection;
	UPROPERTY(meta=(DataflowOutput))
	TArray<TObjectPtr<UMaterialInterface>> Materials;
	FPCGUtilsDataflowCollectionInputNode(const UE::Dataflow::FNodeParameters& Params, FGuid Guid = FGuid::NewGuid());
	virtual void Evaluate(UE::Dataflow::FContext& Context, const FDataflowOutput* Out) const override;
};

USTRUCT(meta=(DataflowExperimental))
struct PCGUTILSFRACTURE_API FPCGUtilsDataflowPointsInputNode : public FDataflowNode
{
	GENERATED_USTRUCT_BODY()
	DATAFLOW_NODE_DEFINE_INTERNAL(FPCGUtilsDataflowPointsInputNode, "PCG Input Points", "PCGUtils", "Sites Positions Transforms")
public:
	UPROPERTY(EditAnywhere, Category="PCG")
	FName PinName = TEXT("Sites");
	UPROPERTY(meta=(DataflowOutput))
	TArray<FVector> Positions;
	UPROPERTY(meta=(DataflowOutput))
	TArray<FTransform> Transforms;
	FPCGUtilsDataflowPointsInputNode(const UE::Dataflow::FNodeParameters& Params, FGuid Guid = FGuid::NewGuid());
	virtual void Evaluate(UE::Dataflow::FContext& Context, const FDataflowOutput* Out) const override;
};

/** A passthrough endpoint, deliberately not an asset-writing terminal. Wire Materials alongside Collection. */
USTRUCT(meta=(DataflowExperimental))
struct PCGUTILSFRACTURE_API FPCGUtilsDataflowCollectionOutputNode : public FDataflowNode
{
	GENERATED_USTRUCT_BODY()
	DATAFLOW_NODE_DEFINE_INTERNAL(FPCGUtilsDataflowCollectionOutputNode, "PCG Output GC", "PCGUtils", "Collection")
public:
	UPROPERTY(EditAnywhere, Category="PCG")
	FName PinName = TEXT("GC");
	UPROPERTY(meta=(DataflowInput, DataflowOutput, DataflowPassthrough="Collection"))
	FManagedArrayCollection Collection;
	UPROPERTY(meta=(DataflowInput, DataflowOutput, DataflowPassthrough="Materials"))
	TArray<TObjectPtr<UMaterialInterface>> Materials;
	FPCGUtilsDataflowCollectionOutputNode(const UE::Dataflow::FNodeParameters& Params, FGuid Guid = FGuid::NewGuid());
	virtual void Evaluate(UE::Dataflow::FContext& Context, const FDataflowOutput* Out) const override;
};
