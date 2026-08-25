#include "Factories/PCGUtilsGraphBatchFactory.h"

#include "Data/PCGUtilsGraphBatch.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(PCGUtilsGraphBatchFactory)

UPCGUtilsGraphBatchFactory::UPCGUtilsGraphBatchFactory(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	bCreateNew = true;
	bEditAfterNew = true;
	SupportedClass = UPCGUtilsGraphBatch::StaticClass();
}

UObject* UPCGUtilsGraphBatchFactory::FactoryCreateNew(
	UClass* InClass,
	UObject* InParent,
	FName InName,
	EObjectFlags Flags,
	UObject* Context,
	FFeedbackContext* Warn)
{
	return NewObject<UPCGUtilsGraphBatch>(InParent, InClass, InName, Flags);
}
