// Copyright Max Harris
#include "Dataflow/PCGUtilsDataflowNodes.h"
#include "Materials/MaterialInterface.h"
#if WITH_EDITOR
#include "Dataflow/PCGUtilsDataflowContext.h"
#include "GeometryCollection/GeometryCollection.h"
#endif

FPCGUtilsDataflowCollectionInputNode::FPCGUtilsDataflowCollectionInputNode(const UE::Dataflow::FNodeParameters& Params, FGuid Guid)
	: FDataflowNode(Params, Guid)
{
	RegisterOutputConnection(&Collection);
	RegisterOutputConnection(&Materials);
}

void FPCGUtilsDataflowCollectionInputNode::Evaluate(UE::Dataflow::FContext& Context, const FDataflowOutput* Out) const
{
#if WITH_EDITOR
	if (const auto* Bridge = Context.AsType<FPCGUtilsDataflowContext>())
	{
		if (const auto* const* Data = Bridge->Collections.Find(PinName))
		{
			if (Out->IsA(&Collection)) SetValue(Context, FManagedArrayCollection((*Data)->GetCollection()), &Collection);
			if (Out->IsA(&Materials)) SetValue(Context, (*Data)->GetMaterials(), &Materials);
			return;
		}
	}
#endif
	Context.Error(FString::Printf(TEXT("PCG input '%s' requires a bound PCG processor execution."), *PinName.ToString()), this, Out);
}

FPCGUtilsDataflowPointsInputNode::FPCGUtilsDataflowPointsInputNode(const UE::Dataflow::FNodeParameters& Params, FGuid Guid)
	: FDataflowNode(Params, Guid)
{
	RegisterOutputConnection(&Positions);
	RegisterOutputConnection(&Transforms);
}

void FPCGUtilsDataflowPointsInputNode::Evaluate(UE::Dataflow::FContext& Context, const FDataflowOutput* Out) const
{
#if WITH_EDITOR
	if (const auto* Bridge = Context.AsType<FPCGUtilsDataflowContext>())
	{
		if (const auto* Values = Bridge->Points.Find(PinName))
		{
			if (Out->IsA(&Transforms)) SetValue(Context, *Values, &Transforms);
			if (Out->IsA(&Positions))
			{
				TArray<FVector> Result;
				for (const FTransform& Transform : *Values) Result.Add(Transform.GetLocation());
				SetValue(Context, MoveTemp(Result), &Positions);
			}
			return;
		}
	}
#endif
	Context.Error(FString::Printf(TEXT("PCG input '%s' requires a bound PCG processor execution."), *PinName.ToString()), this, Out);
}

FPCGUtilsDataflowCollectionOutputNode::FPCGUtilsDataflowCollectionOutputNode(const UE::Dataflow::FNodeParameters& Params, FGuid Guid)
	: FDataflowNode(Params, Guid)
{
	RegisterInputConnection(&Collection);
	RegisterOutputConnection(&Collection).SetPassthroughInput(&Collection);
	RegisterInputConnection(&Materials);
	RegisterOutputConnection(&Materials).SetPassthroughInput(&Materials);
}

void FPCGUtilsDataflowCollectionOutputNode::Evaluate(UE::Dataflow::FContext& Context, const FDataflowOutput* Out) const
{
	if (Out->IsA(&Collection)) ForwardInput(Context, &Collection, &Collection);
	if (Out->IsA(&Materials)) ForwardInput(Context, &Materials, &Materials);
}
