#pragma once

#include "Factories/Factory.h"
#include "PCGUtilsGraphBatchFactory.generated.h"

UCLASS(hidecategories = Object)
class UPCGUtilsGraphBatchFactory : public UFactory
{
	GENERATED_BODY()

public:
	UPCGUtilsGraphBatchFactory(const FObjectInitializer& ObjectInitializer);

	virtual UObject* FactoryCreateNew(
		UClass* InClass,
		UObject* InParent,
		FName InName,
		EObjectFlags Flags,
		UObject* Context,
		FFeedbackContext* Warn) override;
};
