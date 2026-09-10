// Copyright Max Harris
#pragma once
#if WITH_EDITOR
#include "Dataflow/DataflowObjectInterface.h"
#include "Data/PCGGeometryCollectionData.h"

class FPCGUtilsDataflowContext : public UE::Dataflow::FEngineContext
{
public:
	DATAFLOW_CONTEXT_INTERNAL(UE::Dataflow::FEngineContext, FPCGUtilsDataflowContext);
	explicit FPCGUtilsDataflowContext(UObject* InOwner) : Super(InOwner) {}
	TMap<FName, const UPCGGeometryCollectionData*> Collections;
	TMap<FName, TArray<FTransform>> Points;
};
#endif
